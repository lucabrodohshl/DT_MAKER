/**
 * @file semantics.hpp
 * @brief The semantic kernel K_sem: pure functions defining the timed transition system.
 * @ingroup kernel
 *
 * These functions ARE the operational semantics of the Digital Twin. Each one
 * mirrors one rule of the IR semantics (proof/sections/03-ir.tex) and is
 * stated in the proof as a definition of the kernel transition system
 * (proof/sections/05-kernel-semantics.tex):
 *
 * | Function                 | Formal rule                                                        |
 * |--------------------------|--------------------------------------------------------------------|
 * | initial_configuration()  | c0 = (l0, 0, 0)  if 0 |= Inv(l0)                                   |
 * | delay()                  | (l,v,t) -d-> (l,v+d,t+d)  iff  v |= Inv(l) and v+d |= Inv(l)       |
 * | fire()                   | (l,v,t) -e-> (l',v[Y:=0],t)  iff e=(l,g,a,Y,l'), v |= g, v[Y:=0] |= Inv(l') |
 * | propositions()           | L(l,v,t) = { at(l) }                                               |
 *
 * Properties guaranteed by construction (and relied upon by the proof):
 *  - **Purity**: no function mutates its arguments or any global state; the
 *    result depends only on the arguments. Prediction can therefore never
 *    affect the live execution.
 *  - **Exactness**: all arithmetic is on int64 ticks; any operation that would
 *    leave [0, kMaxTicks] fails with ErrorCode::ArithmeticOverflow instead of
 *    wrapping. Within the horizon, results are exact.
 *  - **Refusal is explicit**: an inadmissible step returns an Error explaining
 *    which constraint failed; it never returns a "nearby" state.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/kernel/configuration.hpp"
#include "twin/kernel/model.hpp"

namespace twin::kernel {

/// @brief v |= G : every atom of the conjunction holds in @p c.
[[nodiscard]] bool satisfies(const Configuration& c, std::span<const TickConstraint> conjunction);

/// @brief The initial configuration (l0, 0, 0); fails if 0 does not satisfy Inv(l0).
[[nodiscard]] Result<Configuration> initial_configuration(const Model& model);

/**
 * @brief Delay transition: let @p delta ticks of logical time elapse.
 *
 * Defined iff delta >= 0, the source configuration satisfies Inv(l), and
 * v + delta satisfies Inv(l). Because invariants are conjunctions of atomic
 * clock constraints, the set of admissible delays is an interval containing 0;
 * checking both end points is therefore equivalent to checking every
 * intermediate point (proof: Lemma "convexity of delays").
 *
 * @return ErrorCode::InvariantViolation (with the violated atom), InvalidArgument
 *         for negative delays, ArithmeticOverflow beyond the horizon.
 */
[[nodiscard]] Result<Configuration> delay(const Model& model, const Configuration& c, Ticks delta);

/// @brief Reason a transition is not enabled (Enabled if it is).
enum class Enablement {
    Enabled,                ///< The transition can fire now.
    WrongLocation,          ///< The configuration is not in the transition's source location.
    GuardFalse,             ///< The guard does not hold.
    TargetInvariantFalse,   ///< The target invariant fails after the resets.
    NoSuchTransition        ///< The transition index is out of range.
};

/// @brief Check whether @p transition can fire in @p c without delay.
[[nodiscard]] Enablement enablement(const Model& model, const Configuration& c,
                                    ir::TransitionIndex transition);

/**
 * @brief Discrete transition: fire @p transition in @p c (no time elapses).
 * @return ErrorCode::TransitionNotEnabled with the reason, or InvalidArgument
 *         for an out-of-range transition index.
 */
[[nodiscard]] Result<Configuration> fire(const Model& model, const Configuration& c,
                                         ir::TransitionIndex transition);

/// @brief Transitions enabled in @p c (no delay), ascending index order.
[[nodiscard]] std::vector<ir::TransitionIndex> enabled_transitions(const Model& model,
                                                                   const Configuration& c);

/// @brief A discrete successor: the transition fired and the resulting configuration.
struct DiscreteSuccessor {
    ir::TransitionIndex transition{0};  ///< Transition fired.
    Configuration target;               ///< Resulting configuration.
};

/// @brief All discrete successors of @p c (nondeterminism is preserved, never resolved).
[[nodiscard]] std::vector<DiscreteSuccessor> discrete_successors(const Model& model,
                                                                 const Configuration& c);

/// @brief Timed step: delay by @p delta, then fire @p transition.
[[nodiscard]] Result<Configuration> timed_step(const Model& model, const Configuration& c,
                                               Ticks delta, ir::TransitionIndex transition);

/// @brief Labelling L(c): indices of the propositions that hold in @p c.
[[nodiscard]] std::vector<ir::PropositionIndex> propositions(const Model& model,
                                                             const Configuration& c);

/**
 * @brief Admissible delays of a timed step, as an inclusive tick interval.
 *
 * `latest == std::nullopt` means unbounded (up to the execution horizon).
 */
struct DelayWindow {
    Ticks earliest{0};             ///< Smallest admissible delay.
    std::optional<Ticks> latest;   ///< Largest admissible delay, or unbounded.
};

/**
 * @brief Exact set { d >= 0 | timed_step(c, d, e) is defined } for a valid @p c.
 *
 * Each atomic constraint evaluated after a delay d is either independent of d
 * or a half-line in d, so the set is an interval of the tick grid; this
 * function computes it exactly (proof: Lemma "enabling windows").
 * @return std::nullopt if no delay enables the transition.
 */
[[nodiscard]] std::optional<DelayWindow> enabling_window(const Model& model, const Configuration& c,
                                                         ir::TransitionIndex transition);

/**
 * @brief Largest admissible delay in the current location (the invariant's
 * deadline), or std::nullopt if no invariant bounds the delay (time may then
 * elapse up to the execution horizon). Precondition: @p c satisfies its
 * invariant (for an invalid configuration the result is 0).
 */
[[nodiscard]] std::optional<Ticks> max_delay(const Model& model, const Configuration& c);

/// @brief Where an atom constraining a transition's delay window comes from.
enum class WindowOrigin : std::uint8_t {
    SourceInvariant,  ///< Inv(l) must hold throughout the delay.
    Guard,            ///< The guard must hold after the delay.
    TargetInvariant   ///< Inv(l') must hold after the delay and the resets.
};

/**
 * @brief One atom's contribution to a delay window: the set of delays it alone admits.
 *
 * With d the delay, the atom evaluates (w_l + s_l d) - (w_r + s_r d) ~ b (s = 0 for the
 * reference clock and for clocks the transition resets). It therefore admits either every
 * delay or none (no dependence on d), or a half-line, or a single point (for ==).
 */
struct WindowFactor {
    WindowOrigin origin{WindowOrigin::Guard};  ///< Source of the atom.
    ir::ClockConstraint atom;                  ///< The atom in model units.
    Ticks value_now{0};                        ///< v(lhs) - v(rhs) at d = 0 (after resets for the target invariant).
    Ticks bound{0};                            ///< Scaled bound, in ticks.
    bool depends_on_delay{false};              ///< Whether the atom's value changes with d.
    std::optional<Ticks> min_delay;            ///< The atom requires d >= min_delay.
    std::optional<Ticks> max_delay;            ///< The atom requires d <= max_delay.
    bool never{false};                         ///< No delay satisfies the atom.
};

/// @brief A transition's delay window together with the atoms that determine it.
struct WindowExplanation {
    std::optional<DelayWindow> window;   ///< Exactly enabling_window() for the same arguments.
    std::vector<WindowFactor> factors;   ///< Every atom involved, in rule order.
    bool wrong_location{false};          ///< The configuration is not in the transition's source.
};

/**
 * @brief enabling_window() with its derivation: the window is the intersection of the
 * factors' delay sets (and of the execution horizon). Computed by the same arithmetic as
 * enabling_window(), so the explanation and the window always agree.
 */
[[nodiscard]] WindowExplanation explain_window(const Model& model, const Configuration& c,
                                               ir::TransitionIndex transition);

/// @brief Evaluation of one atom, for explanations in ledgers and APIs.
struct AtomEvaluation {
    ir::ClockConstraint atom;   ///< The atom in model units.
    Ticks lhs_minus_rhs{0};     ///< v(lhs) - v(rhs), in ticks.
    Ticks bound{0};             ///< Scaled bound, in ticks.
    bool holds{false};          ///< Whether the atom holds.
};

/// @brief Evaluate each atom of the guard of @p transition in @p c.
[[nodiscard]] std::vector<AtomEvaluation> explain_guard(const Model& model, const Configuration& c,
                                                        ir::TransitionIndex transition);

}  // namespace twin::kernel
