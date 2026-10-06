/**
 * @file zone_graph.hpp
 * @brief Internal: exact forward zone-graph exploration of a source automaton (property checks).
 *
 * Standard symbolic reachability: from the zero valuation of the initial
 * location, successors apply guard, resets, target invariant, time elapse and
 * the target invariant again; zones are k-extrapolated with per-clock maximal
 * constants (which must include the constants of the property being checked)
 * and an inclusion check prunes covered states. For diagonal-free automata this
 * preserves every clock constraint whose constant is at most k, so location
 * reachability and the satisfiability of such constraints are exact (dense time).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "dbm/constraints.h"
#include "twin/compiler/reader.hpp"

namespace twin::authoring::detail {

/// @brief One symbolic state: a location and a (time-elapsed, extrapolated) zone.
struct ZoneState {
    std::size_t location{0};   ///< Location index (document order).
    std::vector<raw_t> zone;   ///< Closed DBM of dimension clocks + 1.
    std::size_t parent{0};     ///< Predecessor state (itself for the initial state).
};

/// @brief The explored graph.
struct ZoneGraph {
    std::size_t dim{1};               ///< Clocks + 1.
    std::vector<ZoneState> states;    ///< In BFS order; states[0] is initial.
    bool complete{true};              ///< False if the state limit was reached.
};

/// @brief Explore @p model with maximal constants @p max (size dim, max[0] = 0), up to @p limit states.
[[nodiscard]] ZoneGraph explore(const compiler::SourceModel& model, const std::vector<std::int32_t>& max,
                                std::size_t limit);

/// @brief A clock constraint x_lhs - x_rhs ~ bound as DBM constraints (one, or two for ==).
[[nodiscard]] std::vector<constraint_t> to_dbm(const ir::ClockConstraint& c);

/// @brief Whether @p zone intersected with every constraint of @p conjunction is non-empty.
[[nodiscard]] bool satisfiable(std::vector<raw_t> zone, std::size_t dim, const std::vector<ir::ClockConstraint>& conjunction);

/// @brief Maximal constant per clock (index 1..n) over the model's guards and invariants.
[[nodiscard]] std::vector<std::int32_t> model_max_constants(const compiler::SourceModel& model);

/// @brief Whether the model uses a diagonal constraint anywhere.
[[nodiscard]] bool has_diagonal(const compiler::SourceModel& model);

}  // namespace twin::authoring::detail
