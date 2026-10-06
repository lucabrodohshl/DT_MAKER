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
 * **Planning episodes.** Every route is chosen in a planning episode:
 *  1. the untrusted planner proposes one candidate per planner profile
 *     (e.g. "balanced", "known-space", "wide-clearance") over the twin-KNOWN map;
 *  2. each candidate is checked for *geometric feasibility* by the independent
 *     validator (validate_route: dense sampling against the known map, energy);
 *  3. each geometrically feasible candidate is checked for *behavioural
 *     admissibility* by the semantic kernel: the plan's nominal event schedule
 *     (the decision event, then `waypoint_reached!` per intermediate waypoint
 *     and the arrival event, at estimated arrival times) is simulated on a
 *     copy of the committed state (TwinSession::simulate). A candidate whose
 *     schedule the model forbids — e.g. a leg that would outlast a location
 *     invariant — is inadmissible however good its geometry;
 *  4. the cheapest feasible and admissible candidate under a common objective
 *     is selected; the episode (all candidates and verdicts) is recorded as a
 *     ledger context record *before* the decision event is proposed, so the
 *     evidence shows why the twin decided as it did.
 * The estimated arrival times are an explicit modelling assumption
 * (ControllerConfig::eta_*); the kernel's verdict is exact for the checked schedule.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "twin/planner/planner.hpp"
#include "twin/runtime/plan_validator.hpp"
#include "twin/runtime/session.hpp"
#include "twin/runtime/world_model.hpp"

namespace twin::runtime {

/// @brief A named planner configuration producing one candidate per episode.
struct PlannerProfile {
    std::string name;               ///< "balanced", "known-space", ...
    planner::CostWeights weights;   ///< Objective weights of this profile.
    bool allow_unknown{true};       ///< May the route cross unknown space?
};

/// @brief The default profiles: balanced, known-space only, wide clearance.
[[nodiscard]] std::vector<PlannerProfile> default_profiles();

/// @brief Tuning of the controller.
struct ControllerConfig {
    Ticks planning_latency{1200};      ///< Logical duration of a planning episode (REPLANNING).
    double improvement_ratio{0.8};     ///< Replan if the new cost < ratio * remaining cost ...
    double improvement_min{2.0};       ///< ... and the gain exceeds this (cost units ~ metres).
    planner::CostWeights weights{};    ///< Common objective used to compare candidates.
    std::vector<PlannerProfile> profiles = default_profiles();  ///< One candidate per profile.
    ValidationPolicy validation{};     ///< Geometric validation policy.
    double eta_speed_mps{1.0};         ///< Assumed mean speed for arrival-time estimates.
    double eta_leg_overhead_s{0.6};    ///< Assumed per-leg acceleration/turn overhead.
    Ticks takeoff_eta{5000};           ///< Assumed time from start_mission! to takeoff_complete!.
    Ticks brake_patience{5000};        ///< Abandon a route if the collision brake holds this long.
    /// Logical time between selecting a replacement plan and proposing `plan_accepted!` (one
    /// co-simulation tick): observers see the evaluated candidates while the twin still waits.
    /// The kernel's admissibility check simulates the decision at exactly this later time.
    Ticks decision_delay{100};
};

/// @brief A route proposed by the planner and its fate.
struct PlanRecord {
    std::int64_t id{0};                 ///< Route id (sent to the drone).
    std::int64_t episode{0};            ///< Planning episode that produced it.
    std::string candidate;              ///< Candidate label in that episode ("A", "B", ...).
    std::string profile;                ///< Planner profile.
    std::string goal;                   ///< "target:<id>" or "home".
    std::string status;                 ///< candidate | active | invalidated | superseded | completed | rejected
    std::string reason;                 ///< Why it was made; why it ended.
    geo::Point start;                   ///< Where it starts.
    std::vector<geo::Point> waypoints;  ///< Route in metres.
    double cost{0.0};                   ///< Planner objective (its own profile).
    double objective{0.0};              ///< Common objective (comparable across candidates).
    double length_m{0.0};               ///< Length.
    double unknown_m{0.0};              ///< Through unknown space.
    double energy_wh{0.0};              ///< Energy estimate.
    std::size_t expanded{0};            ///< Planner effort.
    Ticks created_at{0};                ///< Logical time of creation.
    Ticks ended_at{-1};                 ///< Logical time it stopped being active (-1: active).
};

/// @brief Encode a plan record (integers only: millimetres, millimetre costs).
[[nodiscard]] json::Json to_json(const PlanRecord& plan);

/// @brief A candidate of a planning episode with its independent verdicts.
struct Candidate {
    std::string label;                         ///< "A", "B", ... (episode-local).
    std::string profile;                       ///< Planner profile that produced it.
    bool found{false};                         ///< The planner found a path.
    std::string failure;                       ///< Planner failure reason (if not found).
    std::optional<std::string> duplicate_of;   ///< Same polyline as an earlier candidate.
    PlanRecord plan;                           ///< Geometry and metrics.
    bool geometric_ok{false};                  ///< Independent geometric validation passed.
    std::vector<std::string> geometric_issues; ///< Validator findings.
    bool behaviour_checked{false};             ///< The kernel check was performed.
    bool behaviour_ok{false};                  ///< The kernel admits the nominal schedule.
    std::string behaviour_detail;              ///< Kernel verdict in words.
    json::Json schedule = json::Json::array(); ///< Checked schedule [{"label","at"}].
    bool selected{false};                      ///< Chosen in this episode.
};

/// @brief Encode a candidate (integers only).
[[nodiscard]] json::Json to_json(const Candidate& candidate);

/// @brief A planning episode: trigger, goal, candidates, verdicts, selection.
struct PlanningEpisode {
    std::int64_t id{0};                   ///< Episode number.
    Ticks at{0};                          ///< Logical time of the selection.
    std::string goal;                     ///< "target:<id>" or "home".
    std::string reason;                   ///< Why the episode ran.
    std::string decision;                 ///< Decision event the selection supports ("" if none).
    std::vector<Candidate> candidates;    ///< In profile order.
    std::optional<std::string> selected;  ///< Label of the selected candidate.
};

/// @brief Encode an episode (integers only; recorded verbatim as ledger context).
[[nodiscard]] json::Json to_json(const PlanningEpisode& episode);

/// @brief The mission controller (see file documentation).
class MissionController {
public:
    /// @brief Controller proposing through @p session and planning with @p planner (both must outlive it).
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
    /// @brief Planning episodes (chronological).
    [[nodiscard]] json::Json episodes() const;
    /// @brief The mission ended (landed or failed).
    [[nodiscard]] bool finished() const noexcept { return finished_; }

private:
    /// A scheduled decision event preceding the route (label, absolute time).
    using Prefix = std::vector<std::pair<std::string, Ticks>>;

    [[nodiscard]] bool enabled_at(std::string_view label, Ticks now) const;
    bool propose(std::string_view label, Ticks now, const std::string& reason, json::Json extra = json::Json::object());
    std::optional<PlanRecord> run_episode(geo::Point from, const std::string& goal, Ticks now, const std::string& reason,
                                          const Prefix& prefix, Ticks depart_at, const std::string& decision);
    void evaluate(Candidate& c, geo::Point from, Ticks depart_at, const Prefix& prefix) const;
    void plan_episode(Ticks now);
    void decide_pending(Ticks now);
    void check_path(Ticks now);
    void check_improvement(Ticks now);
    void check_brake(Ticks now);
    void head_for_next_goal(Ticks now, const std::string& reason);
    void return_home(Ticks now, const std::string& reason, bool needs_decision);
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
    [[nodiscard]] Ticks leg_eta(double length_m) const;

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
    std::vector<PlanningEpisode> episodes_;
    std::int64_t next_plan_id_{1};
    std::int64_t next_episode_id_{1};
    std::optional<Ticks> plan_wanted_since_;
    std::optional<Ticks> braked_since_;  ///< obstacle_detected! accepted, no path_clear! yet.
    /// A selected replacement plan waiting for its decision time (plan_accepted!).
    struct PendingDecision {
        PlanRecord plan;
        Ticks at{0};
    };
    std::optional<PendingDecision> pending_;
    bool map_dirty_{false};
    bool discoveries_{false};
    bool start_requested_{false};
    bool finished_{false};
    std::vector<json::Json> commands_;
    std::vector<json::Json> events_;
};

}  // namespace twin::runtime
