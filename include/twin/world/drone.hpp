/**
 * @file drone.hpp
 * @brief Deterministic 2-D indoor drone simulator (the Physical Twin).
 * @ingroup world
 *
 * Mission-level fidelity, not aerodynamics: bounded-acceleration kinematics
 * in the plane, a climb/descent profile, a battery drained by
 * P = P_hover + k * v^2, a 360-degree gimbal inspection, a lidar-like sensor
 * with line of sight, and a collision-avoidance brake that stops the drone in
 * front of anything solid (also things the Digital Twin does not know about).
 *
 * The simulator has its own flight-controller firmware modes and emits events
 * in the PT vocabulary of V_P (e.g. "wp_arrived!", "poi_arrived!"); the
 * Digital Twin's adapter translates them into DT labels via the verified
 * label equivalence E. Fully deterministic: identical command sequences and
 * step sizes yield identical trajectories.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "twin/geo/grid.hpp"
#include "twin/json/canonical.hpp"
#include "twin/world/scenario.hpp"

namespace twin::world {

/// @brief Flight-controller firmware modes (the PT's own state machine).
enum class FcMode { Boot, Idle, Arming, Climb, Auto, Hold, Brake, Survey, Descent, Disarmed };

/// @brief Name of a firmware mode ("FC_AUTO_WP", ...), as in V_P.
[[nodiscard]] const char* to_string(FcMode mode) noexcept;

/// @brief A command from the Digital Twin to the drone.
struct Command {
    enum class Kind { UploadMission, ArmTakeoff, FollowRoute, Hold, Inspect, Land };
    Kind kind{Kind::Hold};                ///< What to do.
    std::vector<geo::Point> waypoints;    ///< FollowRoute: route in metres.
    std::string goal;                     ///< FollowRoute: "target:<id>", "home" or "waypoint".
    std::int64_t route_id{0};             ///< FollowRoute: route version (echoed in telemetry).
    std::string target_id;                ///< Inspect: target being inspected.
};

/// @brief Encode / decode commands (integers and strings only; millimetres).
[[nodiscard]] json::Json to_json(const Command& command);
[[nodiscard]] Result<Command> command_from_json(const json::Json& j);

/// @brief An event emitted by the flight controller (PT vocabulary).
struct PtEvent {
    Ticks at{0};          ///< Logical time.
    std::string label;    ///< "altitude_reached!", "wp_arrived!", "poi_arrived!", ...
    json::Json detail = json::Json::object();  ///< Context (waypoint index, target, ...).
};

/// @brief Telemetry sample (integers only on the wire: mm, mm/s, permille, mWh).
struct Telemetry {
    Ticks at{0};
    geo::Point position;
    geo::Point velocity;
    double altitude_m{0.0};
    double battery_pct{100.0};
    double energy_wh{0.0};
    double heading_deg{0.0};
    double gimbal_deg{0.0};
    FcMode mode{FcMode::Boot};
    std::int64_t route_id{0};
    int waypoint_index{0};
    std::string goal;
};

/// @brief Encode telemetry with integer units.
[[nodiscard]] json::Json to_json(const Telemetry& t);

/**
 * @brief The drone simulator. Reads the ground truth for physics and sensing;
 * never writes it.
 */
class DroneSimulator {
public:
    DroneSimulator(const Scenario& scenario, const geo::OccupancyGrid* ground_truth);

    /// @brief Accept a command (InvalidArgument if not applicable in the current mode).
    [[nodiscard]] Status command(const Command& c);

    /// @brief Advance the physics by @p dt ticks; returns the events emitted.
    [[nodiscard]] std::vector<PtEvent> step(Ticks now_after, Ticks dt);

    /// @brief Current telemetry.
    [[nodiscard]] Telemetry telemetry(Ticks at) const;

    /// @brief Cells visible to the onboard sensor (line of sight, range), with
    /// their TRUE occupancy. Hazards are invisible to the sensor and omitted.
    [[nodiscard]] std::vector<geo::CellChange> sense() const;

    [[nodiscard]] FcMode mode() const noexcept { return mode_; }
    [[nodiscard]] geo::Point position() const noexcept { return pos_; }

private:
    void fly(Ticks at, double dt_s, std::vector<PtEvent>& events);
    bool blocked_ahead(geo::Point direction) const;
    void drain(double dt_s, double speed);

    const Scenario& scenario_;
    const geo::OccupancyGrid* truth_;
    DroneSpec spec_;
    FcMode mode_{FcMode::Boot};
    geo::Point pos_;
    geo::Point vel_;
    double altitude_{0.0};
    double energy_wh_{0.0};
    double heading_deg_{0.0};
    double gimbal_deg_{0.0};
    double mode_timer_s_{0.0};
    std::vector<geo::Point> route_;
    std::size_t next_wp_{0};
    std::string goal_;
    std::int64_t route_id_{0};
    std::string inspecting_;
    bool reserve_reported_{false};
    bool braked_for_obstacle_{false};
};

}  // namespace twin::world
