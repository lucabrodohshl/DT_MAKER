/**
 * @file explore.hpp
 * @brief Prediction: bounded exploration and simulation from (copies of) semantic state.
 * @ingroup kernel
 *
 * All functions take the starting state by const reference and return new
 * values: prediction is structurally incapable of modifying the authoritative
 * live state (there is no mutable access path). Planners and the runtime's
 * admissibility checks use these functions to ask "what may happen" and "would
 * this be allowed", while the live execution only changes through
 * twin::runtime::TwinSession, which calls the transition functions directly.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "twin/kernel/semantics.hpp"
#include "twin/kernel/state_set.hpp"

namespace twin::kernel {

/// @brief A transition that can occur after some delay, with its exact delay window.
struct TimedSuccessor {
    ir::TransitionIndex transition{0};  ///< The transition.
    DelayWindow window;                 ///< All admissible delays (exact).
    Configuration earliest;             ///< Successor when firing at window.earliest.
};

/**
 * @brief For each outgoing transition of a valid @p c that can fire after
 * some delay: its exact delay window and the successor at the earliest delay.
 */
[[nodiscard]] std::vector<TimedSuccessor> timed_successors(const Model& model,
                                                           const Configuration& c);

/// @brief Bounds of an exploration.
struct ExplorationLimits {
    std::uint32_t max_depth{4};      ///< Maximum number of discrete steps.
    std::optional<Ticks> horizon;    ///< Absolute logical time bound (inclusive), if any.
    std::uint32_t max_nodes{512};    ///< Hard bound on the number of nodes.
};

/// @brief A node of the exploration tree.
struct ExplorationNode {
    Configuration config;                ///< Configuration reached.
    std::optional<std::uint32_t> parent; ///< Parent node (none for the root).
    ir::TransitionIndex via{0};          ///< Transition from the parent (if parent).
    DelayWindow window;                  ///< Delay window of that transition at the parent.
    std::uint32_t depth{0};              ///< Number of discrete steps from the root.
};

/// @brief Result of a bounded exploration.
struct ExplorationResult {
    std::vector<ExplorationNode> nodes;  ///< nodes[0] is the root.
    bool truncated{false};               ///< True if a limit cut the exploration short.
};

/**
 * @brief Bounded breadth-first exploration of the time-abstract successor tree.
 *
 * Each edge is a transition together with its exact window of admissible
 * delays; the child configuration is the successor at the *earliest*
 * admissible delay (a canonical representative). The exploration is therefore
 * exact about *which* transitions can occur and *when*, and representative
 * (not exhaustive) about clock values reachable by later firing times.
 */
[[nodiscard]] ExplorationResult explore(const Model& model, const Configuration& root,
                                        const ExplorationLimits& limits);

/// @brief A root-to-node path of an exploration (a predicted semantic trajectory).
struct TrajectoryStep {
    ir::TransitionIndex transition{0};  ///< Transition taken.
    DelayWindow window;                 ///< Admissible delays for it.
    Configuration after;                ///< Configuration reached (earliest representative).
};

/// @brief Extract the path from the root to node @p node of @p result.
[[nodiscard]] std::vector<TrajectoryStep> trajectory_to(const ExplorationResult& result,
                                                        std::uint32_t node);

/// @brief A scheduled observation for simulation.
struct ScheduledObservation {
    Ticks at{0};        ///< Absolute logical time.
    Selector selector;  ///< What is observed.
};

/**
 * @brief Simulate a candidate observation sequence from a copy of @p start.
 *
 * Applies the monitoring semantics step by step. Fails at the first
 * inadmissible step with that step's error (context "step" = its index).
 */
[[nodiscard]] Result<std::vector<ObservationOutcome>> simulate(
    const Model& model, const StateSet& start, std::span<const ScheduledObservation> schedule);

}  // namespace twin::kernel
