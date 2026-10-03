/**
 * @file state_set.hpp
 * @brief Monitoring semantics: the set of configurations consistent with the observations.
 * @ingroup kernel
 *
 * A Digital Twin that *monitors* a Physical Twin receives timestamped
 * observations ("event a happened at logical time t"). When the model is
 * nondeterministic (two transitions with the same label enabled at once) a
 * single observation may not determine the successor configuration. The kernel
 * never silently resolves such nondeterminism: the authoritative semantic state
 * of a running twin is a StateSet — the exact set of configurations of V_D that
 * are consistent with the whole timed observation history (a subset
 * construction). For event-deterministic models (reported by the compiler) the
 * set is always a singleton.
 *
 * Formally (proof/sections/06-kernel-isomorphism.tex, Prop. "monitor exactness"):
 * after observations (t1,a1)...(tk,ak), members() is exactly the set of
 * kernel images of IR configurations reachable by a run whose observable
 * timed trace is that sequence.
 */
#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/kernel/configuration.hpp"
#include "twin/kernel/model.hpp"

namespace twin::kernel {

/**
 * @brief A non-empty, canonically ordered set of configurations sharing one logical time.
 *
 * Invariants: non-empty; members sorted ascending and unique; all members have
 * the same `time`. The invariants are established by the factories and
 * preserved by every kernel operation.
 */
class StateSet {
public:
    /// @brief The singleton set {c}.
    [[nodiscard]] static StateSet of(Configuration c);
    /// @brief Canonicalise @p members (sort, de-duplicate); fails if empty or times differ.
    [[nodiscard]] static Result<StateSet> of(std::vector<Configuration> members);

    /// @brief Members in canonical order.
    [[nodiscard]] const std::vector<Configuration>& members() const noexcept { return members_; }
    /// @brief The common logical time of all members.
    [[nodiscard]] Ticks time() const noexcept { return members_.front().time; }
    /// @brief True iff exactly one configuration is possible.
    [[nodiscard]] bool is_singleton() const noexcept { return members_.size() == 1; }

    /// @brief Member-wise equality.
    friend bool operator==(const StateSet&, const StateSet&) = default;

private:
    explicit StateSet(std::vector<Configuration> members) : members_(std::move(members)) {}
    std::vector<Configuration> members_;
};

/// @brief What an observation refers to: an action label or one specific transition.
struct Selector {
    /// @brief Observe "some transition labelled `label` fired".
    [[nodiscard]] static Selector label(LabelId label) { return Selector{label, false}; }
    /// @brief Observe "exactly this transition fired" (e.g. an internal tau step chosen by the twin).
    [[nodiscard]] static Selector transition(ir::TransitionIndex t) { return Selector{t, true}; }

    std::uint32_t id{0};      ///< LabelId or TransitionIndex.
    bool by_transition{false};  ///< Interpretation of @c id.
};

/// @brief One way the observation was explained: member @c from took @c transition to member @c to.
struct Branch {
    std::uint32_t from{0};                 ///< Index into the pre-observation (delayed) member list.
    ir::TransitionIndex transition{0};     ///< Transition taken.
    std::uint32_t to{0};                   ///< Index into the post-observation member list.
};

/// @brief Result of a monitored step.
struct ObservationOutcome {
    StateSet before;                     ///< Possible configurations before the step.
    Ticks delay{0};                      ///< Logical time elapsed before the discrete step.
    std::vector<Configuration> delayed;  ///< Members after the delay that survived it.
    StateSet after;                      ///< Possible configurations after the step.
    std::vector<Branch> branches;        ///< All explanations of the observation.
};

/**
 * @brief Let logical time advance to @p to (pure delay observation).
 *
 * Members whose invariant forbids the delay are inconsistent with the
 * observation "no event until @p to" and are dropped.
 * @return TimeRegression if @p to precedes the current time;
 *         IncompatibleObservation if no member admits the delay.
 */
[[nodiscard]] Result<StateSet> advance_to(const Model& model, const StateSet& states, Ticks to);

/**
 * @brief Monitored timed step: delay to @p at, then the discrete step selected by @p selector.
 *
 * Every member that admits the delay and has a matching enabled transition
 * contributes all its matching successors (nondeterminism is preserved).
 * @return TimeRegression, or IncompatibleObservation if no member explains the
 *         observation (the observation is rejected and the state is unchanged).
 */
[[nodiscard]] Result<ObservationOutcome> observe(const Model& model, const StateSet& states,
                                                 Ticks at, Selector selector);

}  // namespace twin::kernel
