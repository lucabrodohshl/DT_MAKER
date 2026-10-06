/**
 * @file planner.hpp
 * @brief Path planning over the twin's (partial) occupancy knowledge — UNTRUSTED.
 * @ingroup planner
 *
 * @defgroup planner Path planner (untrusted)
 * @brief Proposes routes; never decides behaviour.
 *
 * The planner is outside the trusted computing base. It *proposes* routes over
 * the twin-known map; the runtime independently validates each proposal's
 * geometry (runtime::PlanValidator) and asks the semantic kernel whether the
 * corresponding behavioural step (e.g. `plan_accepted!`) is admissible. A
 * wrong, slow or crashing planner can therefore delay a mission or make it
 * fail safely; it cannot make the twin execute behaviour its model forbids.
 *
 * Objective (understandable on purpose): the cost of a path is
 *
 *     sum over steps  length_m * (1 + w_prox * proximity + w_unknown * [unknown]
 *                                   + w_hazard * [near hazard])
 *
 * where proximity in [0,1] grows as the free-space clearance falls below
 * `clearance_m`. Walls, obstacles, closed doors and hazards are impassable;
 * unknown space is passable at a risk premium (optimistic planning, so that
 * the drone explores and the twin learns).
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "twin/geo/grid.hpp"

namespace twin::planner {

/// @brief Weights of the planning objective (non-negative).
struct CostWeights {
    double proximity{1.5};        ///< Penalty for flying close to obstacles (per metre at contact).
    double clearance_m{1.0};      ///< Clearance at and above which the proximity penalty is zero.
    double unknown{2.0};          ///< Risk premium per metre through unknown space.
    double hazard{6.0};           ///< Penalty per metre within `hazard_margin_m` of a hazard.
    double hazard_margin_m{1.0};  ///< Width of the hazard margin.
};

/// @brief Energy model used for estimates (same physics as the PT simulator).
struct EnergyModel {
    double hover_power_w{55.0};    ///< Power while hovering.
    double drag_coeff{8.0};        ///< Additional power per (m/s)^2.
    double cruise_speed_mps{1.2};  ///< Cruise speed.
    /// @brief Energy in Wh to fly @p length_m at cruise speed.
    [[nodiscard]] double flight_energy_wh(double length_m) const noexcept;
};

/// @brief A planning request.
struct PlanningProblem {
    const geo::OccupancyGrid* map{nullptr};  ///< Twin-known map (not ground truth).
    geo::Cell start;                         ///< Start cell (current position).
    geo::Cell goal;                          ///< Goal cell.
    CostWeights weights;                     ///< Objective weights.
    bool allow_unknown{true};                ///< May the path cross unknown cells?
};

/// @brief A proposed route.
struct Plan {
    bool found{false};                   ///< A path exists.
    std::string failure;                 ///< Reason when not found.
    std::vector<geo::Cell> cells;        ///< Grid path, start to goal.
    std::vector<geo::Point> waypoints;   ///< Simplified polyline in metres (start excluded).
    double cost{0.0};                    ///< Objective value.
    double length_m{0.0};                ///< Polyline length.
    double unknown_m{0.0};               ///< Length through unknown space.
    double energy_wh{0.0};               ///< Estimated flight energy.
    std::size_t expanded{0};             ///< Search nodes expanded.
};

/// @brief Interface of a path planner (alternative algorithms plug in here).
class PathPlanner {
public:
    virtual ~PathPlanner() = default;
    PathPlanner() = default;
    /// @brief Copyable (planners hold no state between calls).
    PathPlanner(const PathPlanner&) = default;
    /// @brief Copy assignment.
    PathPlanner& operator=(const PathPlanner&) = default;
    /// @brief Movable.
    PathPlanner(PathPlanner&&) = default;
    /// @brief Move assignment.
    PathPlanner& operator=(PathPlanner&&) = default;

    /// @brief Plan a route; never throws.
    [[nodiscard]] virtual Plan plan(const PlanningProblem& problem) = 0;
    /// @brief Algorithm name for reports ("A*").
    [[nodiscard]] virtual std::string name() const = 0;
};

/**
 * @brief Weighted A* (8-connected, no corner cutting) with octile heuristic,
 * followed by line-of-sight waypoint simplification. The heuristic is
 * admissible because every step multiplier is >= 1.
 */
class AStarPlanner final : public PathPlanner {
public:
    /// @brief Planner using @p energy for its energy estimates.
    explicit AStarPlanner(EnergyModel energy = {}) : energy_(energy) {}
    [[nodiscard]] Plan plan(const PlanningProblem& problem) override;
    [[nodiscard]] std::string name() const override { return "A*"; }

private:
    EnergyModel energy_;
};

/// @brief True iff the planner may pass through the cell under @p allow_unknown.
[[nodiscard]] bool passable(geo::Occupancy o, bool allow_unknown) noexcept;

/**
 * @brief Cost of following @p waypoints from @p from under the same objective
 * (used to compare the remaining current route with a new candidate).
 * Returns +infinity if the polyline crosses an impassable cell.
 */
[[nodiscard]] double route_cost(const geo::OccupancyGrid& map, geo::Point from,
                                const std::vector<geo::Point>& waypoints, const CostWeights& weights);

}  // namespace twin::planner
