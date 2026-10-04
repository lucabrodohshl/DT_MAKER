/**
 * @file mission_controller.hpp
 * @brief Mission controller: proposes decisions and routes — UNTRUSTED.
 * @ingroup runtime
 *
 * The controller turns mission goals (inspect targets, return home) into
 * proposals. It follows a strict rule that keeps the kernel the single
 * semantic authority:
 *
 *  - it never inspects or branches on the twin's location (no shadow state
 *    machine); it uses only the model's *event vocabulary*;
 *  - it proposes a decision event (e.g. `plan_accepted!`) only if the kernel
 *    reports that event enabled at the current logical time, and the kernel
 *    still decides when the event is submitted;
 *  - it reacts to events the kernel has *accepted* (e.g. after
 *    `target_reached!` it commands an inspection);
 *  - its own state is mission bookkeeping only: which targets are done or
 *    unreachable, the current goal, and the plans proposed so far.
 *
 * Planning episodes: whenever `plan_accepted!` is enabled the twin is waiting
 * for a plan; the controller plans (taking `planning_latency` of logical
 * time), validates the route independently (validate_route), and proposes
 * `plan_accepted!`. If no admissible route to the remaining targets exists it
 * makes a mission-level decision (`return_requested!`).
 */
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "twin/planner/planner.hpp"
#include "twin/runtime/plan_validator.hpp"
#include "twin/runtime/session.hpp"
#include "twin/runtime/world_model.hpp"

namespace twin::runtime {

/// @brief Tuning of the controller.
struct ControllerConfig {
    Ticks planning_latency{1200};      ///< Logical duration of a planning episode.
    double improvement_ratio{0.8};     ///< Replan if the new cost < ratio * remaining cost ...
    double improvement_min{2.0};       ///< ... and the gain exceeds this (cost units ~ metres).
    planner::CostWeights weights{};    ///< Planning objective.
    ValidationPolicy validation{};     ///< Route validation policy.
};

/// @brief A route proposed by the planner and its fate.
struct PlanRecord {
    std::int64_t id{0};                 ///< Route id (sent to the drone).
    std::string goal;                   ///< "target:<id>" or "home".
    std::string status;                 ///< active | invalidated | superseded | completed | rejected
    std::string reason;                 ///< Why it was made; why it ended.
    geo::Point start;                   ///< Where it starts.
    std::vector<geo::Point> waypoints;  ///< Route in metres.
    double cost{0.0};                   ///< Planner objective.
    double length_m{0.0};               ///< Length.
    double unknown_m{0.0};              ///< Through unknown space.
    double energy_wh{0.0};              ///< Energy estimate.
    std::size_t expanded{0};            ///< Planner effort.
    Ticks created_at{0};                ///< Logical time of creation.
    Ticks ended_at{-1};                 ///< Logical time it stopped being active (-1: active).
};

/// @brief Encode a plan record (integers only: millimetres, millimetre costs).
[[nodiscard]] json::Json to_json(const PlanRecord& plan);

/// @brief The mission controller (see file documentation).
class MissionController {
public:
    MissionController(TwinSession& session, planner::PathPlanner& planner, ControllerConfig config = {});

    /// @brief Start a mission with the twin's current world model.
    void reset(Mission mission, const TwinWorldModel* world);
    /// @brief Operator start request (or autostart).
    void request_start() { start_requested_ = true; }

    /// @brief Latest telemetry from the flight controller.
    void on_telemetry(const json::Json& telemetry) { telemetry_ = telemetry; }
    /// @brief A PT observation was accepted by the kernel.
    void on_accepted(const std::string& label, Ticks at);
    /// @brief The twin's world model changed.
    void on_map_changed(const std::vector<geo::CellChange>& changed);
    /// @brief Make the decisions due at logical time @p now.
    void decide(Ticks now);

    /// @brief Drone commands to send (in order), cleared on take.
    [[nodiscard]] std::vector<json::Json> take_commands();
    /// @brief Events for observers (plans, decisions), cleared on take.
    [[nodiscard]] std::vector<json::Json> take_events();

    /// @brief Mission progress for APIs.
    [[nodiscard]] json::Json status() const;
    /// @brief Active plan and history.
    [[nodiscard]] json::Json plans() const;
    /// @brief The mission ended (landed or failed).
    [[nodiscard]] bool finished() const noexcept { return finished_; }

private:
    [[nodiscard]] bool enabled_at(std::string_view label, Ticks now) const;
    bool propose(std::string_view label, Ticks now, const std::string& reason);
    std::optional<PlanRecord> make_plan(geo::Point from, const std::string& goal, Ticks now,
                                        const std::string& reason);
    void plan_episode(Ticks now);
    void check_path(Ticks now);
    void check_improvement(Ticks now);
    void head_for_next_goal(Ticks now, const std::string& reason);
    void activate(PlanRecord plan, Ticks now);
    void close_active(const std::string& status, const std::string& reason, Ticks now);
    void send_route(const PlanRecord& plan);
    void command(json::Json c) { commands_.push_back(std::move(c)); }
    void event(const std::string& type, json::Json payload);

    [[nodiscard]] geo::Cell goal_cell(const std::string& goal) const;
    [[nodiscard]] std::string next_goal() const;
    [[nodiscard]] geo::Point position() const;
    [[nodiscard]] double energy_wh() const;
    [[nodiscard]] std::vector<geo::Point> remaining_route() const;
    [[nodiscard]] bool flying_route() const;

    TwinSession& session_;
    planner::PathPlanner& planner_;
    ControllerConfig config_;
    Mission mission_;
    const TwinWorldModel* world_{nullptr};
    json::Json telemetry_ = json::Json::object();
    std::string goal_;
    std::set<std::string> done_;
    std::set<std::string> unreachable_;
    std::optional<PlanRecord> active_;
    std::vector<PlanRecord> history_;
    std::int64_t next_plan_id_{1};
    std::optional<Ticks> plan_wanted_since_;
    bool map_dirty_{false};
    bool discoveries_{false};
    bool start_requested_{false};
    bool finished_{false};
    Ticks last_now_{0};
    std::vector<json::Json> commands_;
    std::vector<json::Json> events_;
};

}  // namespace twin::runtime
