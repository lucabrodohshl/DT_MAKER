/**
 * @file instance.hpp
 * @brief KernelInstance: a stateful, transactional execution of one model.
 * @ingroup kernel
 *
 * KernelInstance offers the operational API of the semantic kernel:
 *
 * | Capability               | Method                                  |
 * |--------------------------|-----------------------------------------|
 * | initialize(model)        | KernelInstance::initialize()            |
 * | current_state()          | current_state()                         |
 * | advance_time(delta)      | advance_time(), advance_to()            |
 * | enabled_transitions()    | enabled_transitions()                   |
 * | apply_event(event)       | apply_event(), apply_transition()       |
 * | evaluate_propositions()  | evaluate_propositions()                 |
 * | successors(state)        | successors(), predict()                 |
 * | simulate(event_sequence) | simulate()                              |
 * | clone_state()            | clone_state()                           |
 * | restore_state(snapshot)  | restore_state()                         |
 *
 * Every mutating method is **transactional**: it computes the new state with
 * the pure functions of semantics.hpp / state_set.hpp and commits it only on
 * success, so a refused step leaves the instance unchanged. Query methods are
 * const and operate on copies. The class holds no other state than the model
 * pointer and the current StateSet — nothing that could carry hidden semantics.
 */
#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "twin/kernel/explore.hpp"
#include "twin/kernel/state_set.hpp"

namespace twin::kernel {

/// @brief An outgoing transition of a possible configuration, with its delay window.
struct EnabledTransition {
    std::uint32_t member{0};             ///< Index into current_state().members().
    ir::TransitionIndex transition{0};   ///< The transition.
    DelayWindow window;                  ///< Admissible delays from now.
    bool enabled_now{false};             ///< True iff a zero delay is admissible.
};

/// @brief Proposition status over a (possibly non-singleton) state set.
struct PropositionStatus {
    std::vector<ir::PropositionIndex> certain;   ///< Hold in every possible configuration.
    std::vector<ir::PropositionIndex> possible;  ///< Hold in at least one possible configuration.
};

/// @brief A stateful, transactional kernel execution (see file documentation).
class KernelInstance {
public:
    /// @brief Start an execution in the initial configuration of @p model.
    [[nodiscard]] static Result<KernelInstance> initialize(std::shared_ptr<const Model> model);

    /// @brief The executed model.
    [[nodiscard]] const Model& model() const noexcept { return *model_; }
    /// @brief Shared pointer to the executed model.
    [[nodiscard]] const std::shared_ptr<const Model>& model_ptr() const noexcept { return model_; }
    /// @brief The authoritative set of possible configurations.
    [[nodiscard]] const StateSet& current_state() const noexcept { return state_; }

    /// @brief A snapshot (deep copy) of the current semantic state.
    [[nodiscard]] StateSet clone_state() const { return state_; }
    /// @brief Replace the state by a snapshot after validating every member.
    [[nodiscard]] Status restore_state(const StateSet& snapshot);

    /// @brief Let @p delta ticks elapse (transactional).
    [[nodiscard]] Result<StateSet> advance_time(Ticks delta);
    /// @brief Let logical time elapse until @p time (transactional).
    [[nodiscard]] Result<StateSet> advance_to(Ticks time);

    /// @brief Outgoing transitions of every possible configuration with their delay windows.
    [[nodiscard]] std::vector<EnabledTransition> enabled_transitions() const;

    /// @brief Observe action @p label at logical time @p at (transactional).
    [[nodiscard]] Result<ObservationOutcome> apply_event(std::string_view label, Ticks at);
    /// @brief Observe that transition @p transition fired at @p at (transactional).
    [[nodiscard]] Result<ObservationOutcome> apply_transition(ir::TransitionIndex transition, Ticks at);

    /// @brief Propositions holding now (certainly / possibly).
    [[nodiscard]] PropositionStatus evaluate_propositions() const;

    /// @brief Discrete successors of every possible configuration (per member).
    [[nodiscard]] std::vector<std::vector<DiscreteSuccessor>> successors() const;
    /// @brief Bounded exploration from every possible configuration (per member).
    [[nodiscard]] std::vector<ExplorationResult> predict(const ExplorationLimits& limits) const;
    /// @brief Simulate a candidate schedule on a copy of the current state.
    [[nodiscard]] Result<std::vector<ObservationOutcome>> simulate(
        std::span<const ScheduledObservation> schedule) const;

private:
    KernelInstance(std::shared_ptr<const Model> model, StateSet state)
        : model_(std::move(model)), state_(std::move(state)) {}

    std::shared_ptr<const Model> model_;
    StateSet state_;
};

}  // namespace twin::kernel
