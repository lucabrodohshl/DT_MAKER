/**
 * @file scenario.hpp
 * @brief Scenario definition of the Physical Twin world (ground truth, prior knowledge, events).
 * @ingroup world
 *
 * @defgroup world Physical Twin world (simulator + environment service)
 * @brief The simulated physical side. Owns the GROUND TRUTH; runs in its own process.
 *
 * A scenario separates what physically exists from what the Digital Twin is
 * told: `ground_truth` is the real building, `prior_knowledge` is the
 * (outdated, partial) floor plan the building-information service publishes
 * initially. The difference between the two — and the timeline of facility
 * events — is what the twin has to discover during the mission.
 *
 * Scenario files are JSON (`scenarios/NAME.json`); maps are rows of characters:
 * `.` free, `#` wall, `o` obstacle, `D` open door, `d` closed door,
 * `!` hazard, `?` unknown (prior knowledge only).
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/geo/grid.hpp"

namespace twin::world {

/// @brief Logical time base of the world (1 tick = 1 ms), equal to the DT model's.
inline constexpr TimeBase kWorldTime{1000};

/// @brief An inspection target.
struct TargetSpec {
    std::string id;     ///< "T1"
    std::string name;   ///< "Main electrical panel"
    geo::Cell cell;     ///< Location.
};

/// @brief Physical parameters of the drone.
struct DroneSpec {
    double max_speed_mps{1.2};         ///< Cruise speed limit (m/s).
    double max_accel_mps2{1.5};        ///< Acceleration limit (m/s^2).
    double climb_rate_mps{0.5};        ///< Climb and descent rate (m/s).
    double cruise_height_m{1.5};       ///< Matches the ontology (cruise_value axiom).
    double spin_up_s{1.0};             ///< Motor spin-up before the climb.
    double battery_capacity_wh{10.0};  ///< Matches the ontology (10 Wh pack).
    double battery_start_pct{100.0};   ///< Charge at take-off (%).
    double reserve_pct{20.0};          ///< Firmware reserve threshold.
    double sensor_range_m{3.0};        ///< Onboard lidar range.
    double proximity_range_m{1.0};     ///< Collision-avoidance brake distance.
    double scan_rate_deg_s{60.0};      ///< Gimbal sweep speed (360 deg = full inspection).
    double waypoint_tolerance_m{0.15}; ///< Acceptance radius (< ontology's 0.25 m tolerance).
    double hover_power_w{55.0};        ///< Power while hovering (W).
    double drag_coeff{8.0};            ///< Extra power per (m/s)^2 of speed (W).
};

/// @brief A timed change of the world.
struct ScenarioEvent {
    Ticks at{0};                       ///< When it happens.
    std::string kind;                  ///< "set_cells" (physical change) | "hazard" (declared no-fly area).
    std::vector<geo::Cell> cells;      ///< Affected cells.
    geo::Occupancy occupancy{geo::Occupancy::Free};  ///< New ground-truth occupancy.
    std::string description;           ///< Human-readable.
    bool notify{false};                ///< Publish as a facility notice immediately.
};

/// @brief A complete scenario.
struct Scenario {
    std::string name;  ///< Scenario name.
    std::string description;  ///< What the scenario shows.
    std::uint64_t seed{0};               ///< Seed (reproducibility metadata).
    geo::OccupancyGrid ground_truth;     ///< The real building (simulator and observer API only).
    geo::OccupancyGrid prior_knowledge;  ///< The facility plan initially published to the twin.
    geo::Cell home;                      ///< Take-off and landing pad.
    std::vector<TargetSpec> targets;     ///< Inspection targets, in mission order.
    DroneSpec drone;                     ///< Physical parameters of the drone.
    std::vector<ScenarioEvent> events;  ///< Sorted by time.
};

/// @brief Load and validate a scenario file.
[[nodiscard]] Result<Scenario> load_scenario(const std::filesystem::path& path);

/**
 * @brief @p base with a different mission (operator-defined inspection targets).
 *
 * @p mission is `{targets: [{id?, name, cell: {x, y}}], home?: {x, y}}`. Validation follows the
 * scenario file's rules (home and targets must be navigable in the real building), plus: every
 * cell lies inside the building, targets are 1-12 distinct cells other than the home pad, and
 * names are non-empty. Missing ids become T1, T2, ... in mission order.
 */
[[nodiscard]] Result<Scenario> with_mission(Scenario base, const json::Json& mission);

/// @brief Parse a scenario from JSON.
[[nodiscard]] Result<Scenario> scenario_from_json(const json::Json& j);

}  // namespace twin::world
