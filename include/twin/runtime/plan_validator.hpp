/**
 * @file plan_validator.hpp
 * @brief Independent geometric validation of planner proposals.
 * @ingroup runtime
 *
 * The planner is untrusted. Before the runtime even asks the kernel whether
 * accepting a plan is behaviourally admissible, the route is re-checked here
 * with a deliberately different algorithm than the planner's grid search:
 * the polyline is sampled every few centimetres and every sample must lie in
 * a cell the twin does not know to be blocked. An energy check compares the
 * route (plus the way home) with the remaining battery.
 */
#pragma once

#include <string>
#include <vector>

#include "twin/geo/grid.hpp"

namespace twin::runtime {

/// @brief Validation policy.
struct ValidationPolicy {
    bool allow_unknown{true};      ///< Optimistic: unknown space may be crossed.
    double sample_step_m{0.05};    ///< Sampling resolution.
    double reserve_wh{2.0};        ///< Energy that must remain after the route and the way home.
    double wh_per_m{0.0154};       ///< Flight energy per metre (cruise).
};

/// @brief Outcome of a validation.
struct PlanCheck {
    bool valid{false};                 ///< No issue found.
    std::vector<std::string> issues;   ///< Human-readable problems.
    double length_m{0.0};              ///< Route length.
    double unknown_m{0.0};             ///< Length through unknown space.
    double energy_wh{0.0};             ///< Route energy estimate.
};

/**
 * @brief Validate the route @p waypoints flown from @p from against @p map.
 * @param map the twin-KNOWN map (never the ground truth).
 * @param from where the route starts (metres).
 * @param waypoints the route (metres).
 * @param energy_remaining_wh current battery energy.
 * @param home_distance_m straight-line distance from the route's end to home (energy check).
 * @param policy sampling resolution, reserve and energy model.
 */
[[nodiscard]] PlanCheck validate_route(const geo::OccupancyGrid& map, geo::Point from,
                                       const std::vector<geo::Point>& waypoints, double energy_remaining_wh,
                                       double home_distance_m, const ValidationPolicy& policy = {});

}  // namespace twin::runtime
