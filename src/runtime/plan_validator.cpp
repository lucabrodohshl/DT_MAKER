/**
 * @file plan_validator.cpp
 * @brief Dense-sampling route validation (independent of the planner's algorithm).
 */
#include "twin/runtime/plan_validator.hpp"

#include <cmath>
#include <set>

namespace twin::runtime {

PlanCheck validate_route(const geo::OccupancyGrid& map, geo::Point from, const std::vector<geo::Point>& waypoints,
                         double energy_remaining_wh, double home_distance_m, const ValidationPolicy& policy) {
    PlanCheck check;
    if (waypoints.empty()) {
        check.issues.emplace_back("empty route");
        return check;
    }
    std::set<geo::Cell> reported;
    geo::Point prev = from;
    for (const geo::Point& wp : waypoints) {
        const double seg = geo::distance(prev, wp);
        const int samples = std::max(1, static_cast<int>(std::ceil(seg / policy.sample_step_m)));
        geo::Cell last_cell = map.cell_of(prev);
        for (int i = 0; i <= samples; ++i) {
            const double f = static_cast<double>(i) / samples;
            const geo::Point p{prev.x + (wp.x - prev.x) * f, prev.y + (wp.y - prev.y) * f};
            const geo::Cell c = map.cell_of(p);
            const geo::Occupancy o = map.at(c);
            if (geo::is_blocking(o) && reported.insert(c).second) {
                check.issues.push_back("route crosses " + std::string(geo::to_string(o)) + " at cell [" +
                                       std::to_string(c.x) + "," + std::to_string(c.y) + "]");
            } else if (o == geo::Occupancy::Unknown && c != last_cell) {
                if (!policy.allow_unknown && reported.insert(c).second) {
                    check.issues.push_back("route crosses unknown space");
                }
                check.unknown_m += map.cell_size();
            }
            last_cell = c;
        }
        check.length_m += seg;
        prev = wp;
    }
    check.energy_wh = check.length_m * policy.wh_per_m;
    const double needed = check.energy_wh + home_distance_m * 1.3 * policy.wh_per_m + policy.reserve_wh;
    if (needed > energy_remaining_wh) {
        check.issues.push_back("insufficient energy: route + return + reserve needs " +
                               std::to_string(needed).substr(0, 5) + " Wh, " +
                               std::to_string(energy_remaining_wh).substr(0, 5) + " Wh remain");
    }
    check.valid = check.issues.empty();
    return check;
}

}  // namespace twin::runtime
