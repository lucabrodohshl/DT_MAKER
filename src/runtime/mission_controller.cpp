/**
 * @file mission_controller.cpp
 * @brief Event-vocabulary-driven mission logic (no location names, no shadow state machine).
 */
#include "twin/runtime/mission_controller.hpp"

#include <cmath>
#include <limits>

namespace twin::runtime {
namespace {

std::int64_t mm(double metres) { return static_cast<std::int64_t>(std::llround(metres * 1000.0)); }

json::Json points_mm(const std::vector<geo::Point>& pts) {
    json::Json arr = json::Json::array();
    for (const geo::Point& p : pts) arr.push_back(json::Json::array({mm(p.x), mm(p.y)}));
    return arr;
}

}  // namespace

json::Json to_json(const PlanRecord& p) {
    return json::Json{{"id", p.id},
                      {"goal", p.goal},
                      {"status", p.status},
                      {"reason", p.reason},
                      {"start_mm", json::Json::array({mm(p.start.x), mm(p.start.y)})},
                      {"waypoints_mm", points_mm(p.waypoints)},
                      {"cost_mm", mm(p.cost)},
                      {"length_mm", mm(p.length_m)},
                      {"unknown_mm", mm(p.unknown_m)},
                      {"energy_mwh", mm(p.energy_wh)},
                      {"expanded", static_cast<std::int64_t>(p.expanded)},
                      {"created_at", p.created_at},
                      {"ended_at", p.ended_at}};
}

MissionController::MissionController(TwinSession& session, planner::PathPlanner& planner, ControllerConfig config)
    : session_(session), planner_(planner), config_(config) {}

void MissionController::reset(Mission mission, const TwinWorldModel* world) {
    mission_ = std::move(mission);
    world_ = world;
    telemetry_ = json::Json::object();
    goal_.clear();
    done_.clear();
    unreachable_.clear();
    active_.reset();
    history_.clear();
    next_plan_id_ = 1;
    plan_wanted_since_.reset();
    map_dirty_ = discoveries_ = start_requested_ = finished_ = false;
    commands_.clear();
    events_.clear();
}

// ------------------------------------------------------------------ kernel access

bool MissionController::enabled_at(std::string_view label, Ticks now) const {
    const std::optional<kernel::LabelId> id = session_.model().label_id(label);
    if (!id) return false;
    const Snapshot snap = session_.snapshot();
    if (snap.failed || snap.closed) return false;
    const Ticks delay = now - snap.state.time();
    if (delay < 0) return false;
    for (const EnabledInfo& e : snap.enabled) {
        if (session_.model().label_of(e.transition) != *id) continue;
        if (e.window.earliest <= delay && (!e.window.latest || delay <= *e.window.latest)) return true;
    }
    return false;
}

bool MissionController::propose(std::string_view label, Ticks now, const std::string& reason) {
    ledger::Input in{"mission-controller", ledger::InputKind::Label, std::string(label), now,
                     json::Json{{"reason", reason}}};
    Result<SubmitResult> r = session_.submit(in);
    const bool accepted = r.ok() && r.value().accepted;
    event("decision", json::Json{{"label", std::string(label)},
                                 {"accepted", accepted},
                                 {"reason", reason},
                                 {"at", now},
                                 {"ledger_seq", r.ok() ? static_cast<std::int64_t>(r.value().ledger_seq) : -1}});
    return accepted;
}

// ---------------------------------------------------------------------- helpers

void MissionController::event(const std::string& type, json::Json payload) {
    payload["type"] = type;
    events_.push_back(std::move(payload));
}

geo::Cell MissionController::goal_cell(const std::string& goal) const {
    if (goal.rfind("target:", 0) == 0) {
        const std::string id = goal.substr(7);
        for (const MissionTarget& t : mission_.targets) {
            if (t.id == id) return t.cell;
        }
    }
    return mission_.home;
}

std::string MissionController::next_goal() const {
    for (const MissionTarget& t : mission_.targets) {
        const std::string g = "target:" + t.id;
        if (!done_.contains(g) && !unreachable_.contains(g)) return g;
    }
    return "home";
}

geo::Point MissionController::position() const {
    if (telemetry_.contains("x_mm")) {
        return geo::Point{telemetry_.at("x_mm").get<double>() / 1000.0, telemetry_.at("y_mm").get<double>() / 1000.0};
    }
    return world_->map().center(mission_.home);
}

double MissionController::energy_wh() const {
    return telemetry_.contains("energy_mwh") ? telemetry_.at("energy_mwh").get<double>() / 1000.0
                                             : std::numeric_limits<double>::infinity();
}

bool MissionController::flying_route() const {
    const std::string mode = telemetry_.value("mode", std::string());
    return active_ && (mode == "FC_AUTO_WP" || mode == "FC_BRAKE_HOLD") &&
           telemetry_.value("route_id", std::int64_t{-1}) == active_->id;
}

std::vector<geo::Point> MissionController::remaining_route() const {
    if (!active_) return {};
    std::size_t from = 0;
    if (telemetry_.value("route_id", std::int64_t{-1}) == active_->id) {
        from = static_cast<std::size_t>(std::max<std::int64_t>(0, telemetry_.value("waypoint_index", std::int64_t{0})));
    }
    if (from >= active_->waypoints.size()) return {};
    return {active_->waypoints.begin() + static_cast<std::ptrdiff_t>(from), active_->waypoints.end()};
}

std::optional<PlanRecord> MissionController::make_plan(geo::Point from, const std::string& goal, Ticks now,
                                                       const std::string& reason) {
    const geo::OccupancyGrid& map = world_->map();
    planner::PlanningProblem problem{&map, map.cell_of(from), goal_cell(goal), config_.weights, true};
    planner::Plan p = planner_.plan(problem);
    PlanRecord rec;
    rec.id = next_plan_id_++;
    rec.goal = goal;
    rec.start = from;
    rec.created_at = now;
    rec.reason = reason;
    if (!p.found) {
        rec.status = "rejected";
        rec.reason = reason + " — planner: " + p.failure;
        history_.push_back(rec);
        event("plan", json::Json{{"plan", to_json(rec)}});
        return std::nullopt;
    }
    rec.waypoints = p.waypoints;
    rec.cost = p.cost;
    rec.length_m = p.length_m;
    rec.unknown_m = p.unknown_m;
    rec.energy_wh = p.energy_wh;
    rec.expanded = p.expanded;
    const double home_m = goal == "home" ? 0.0 : geo::distance(map.center(goal_cell(goal)), map.center(mission_.home));
    const PlanCheck check = validate_route(map, from, p.waypoints, energy_wh(), home_m, config_.validation);
    if (!check.valid) {
        rec.status = "rejected";
        rec.reason = reason + " — validator: " + check.issues.front();
        history_.push_back(rec);
        event("plan", json::Json{{"plan", to_json(rec)}});
        return std::nullopt;
    }
    rec.status = "proposed";
    return rec;
}

void MissionController::activate(PlanRecord plan, Ticks now) {
    plan.status = "active";
    plan.created_at = now;
    goal_ = plan.goal;
    active_ = std::move(plan);
    event("plan", json::Json{{"plan", to_json(*active_)}});
}

void MissionController::close_active(const std::string& status, const std::string& reason, Ticks now) {
    if (!active_) return;
    active_->status = status;
    active_->reason = active_->reason + "; " + reason;
    active_->ended_at = now;
    event("plan", json::Json{{"plan", to_json(*active_)}});
    history_.push_back(std::move(*active_));
    active_.reset();
}

void MissionController::send_route(const PlanRecord& plan) {
    command(json::Json{{"kind", "follow_route"},
                       {"waypoints_mm", points_mm(plan.waypoints)},
                       {"goal", plan.goal},
                       {"route_id", plan.id},
                       {"target_id", ""}});
}

// ------------------------------------------------------------------ reactions

void MissionController::on_accepted(const std::string& label, Ticks at) {
    if (label == "takeoff_complete!") {
        if (active_) send_route(*active_);
    } else if (label == "target_reached!") {
        const std::string target = goal_.rfind("target:", 0) == 0 ? goal_.substr(7) : std::string();
        close_active("completed", "target reached", at);
        command(json::Json{{"kind", "inspect"}, {"waypoints_mm", json::Json::array()}, {"goal", ""},
                           {"route_id", 0}, {"target_id", target}});
    } else if (label == "inspection_complete!") {
        if (goal_.rfind("target:", 0) == 0) done_.insert(goal_);
        head_for_next_goal(at, "inspection of " + goal_.substr(7) + " complete");
    } else if (label == "home_reached!") {
        close_active("completed", "home reached", at);
        command(json::Json{{"kind", "land"}, {"waypoints_mm", json::Json::array()}, {"goal", ""}, {"route_id", 0},
                           {"target_id", ""}});
    } else if (label == "landing_complete!") {
        finished_ = true;
        event("mission", json::Json{{"text", "Mission complete: landed at home"}, {"at", at}});
    } else if (label == "battery_low!") {
        for (const MissionTarget& t : mission_.targets) {
            if (!done_.contains("target:" + t.id)) unreachable_.insert("target:" + t.id);
        }
        close_active("superseded", "battery reserve reached", at);
        if (std::optional<PlanRecord> home = make_plan(position(), "home", at, "battery reserve: return home")) {
            activate(std::move(*home), at);
            send_route(*active_);
        }
    }
}

void MissionController::head_for_next_goal(Ticks now, const std::string& reason) {
    std::string goal = next_goal();
    while (goal != "home") {
        if (std::optional<PlanRecord> p = make_plan(position(), goal, now, reason + ": next target " + goal.substr(7))) {
            activate(std::move(*p), now);
            send_route(*active_);
            return;
        }
        unreachable_.insert(goal);
        goal = next_goal();
    }
    // Every target is done or unreachable: mission-level decision to return.
    const bool all_done = unreachable_.empty();
    if (enabled_at("return_requested!", now) &&
        propose("return_requested!", now, all_done ? "all targets inspected" : "remaining targets unreachable")) {
        if (std::optional<PlanRecord> home = make_plan(position(), "home", now, "return to home")) {
            activate(std::move(*home), now);
            send_route(*active_);
        } else {
            goal_ = "home";
        }
    }
}

void MissionController::on_map_changed(const std::vector<geo::CellChange>& changed) {
    if (changed.empty()) return;
    map_dirty_ = true;
    for (const geo::CellChange& c : changed) {
        if (geo::is_navigable(c.occupancy)) discoveries_ = true;
    }
}

// --------------------------------------------------------------------- decisions

void MissionController::decide(Ticks now) {
    last_now_ = now;
    if (finished_ || world_ == nullptr) return;
    if (enabled_at("mission_loaded!", now)) {
        command(json::Json{{"kind", "upload_mission"}, {"waypoints_mm", json::Json::array()}, {"goal", ""},
                           {"route_id", 0}, {"target_id", ""}});
        propose("mission_loaded!", now,
                "mission '" + mission_.name + "' with " + std::to_string(mission_.targets.size()) + " targets received");
    }
    if (start_requested_ && enabled_at("start_mission!", now)) {
        const std::string goal = next_goal();
        if (std::optional<PlanRecord> p = make_plan(world_->map().center(mission_.home), goal, now, "initial route")) {
            if (propose("start_mission!", now, "pre-flight checks passed; route " + std::to_string(p->id) + " to " + goal)) {
                activate(std::move(*p), now);
                command(json::Json{{"kind", "arm_takeoff"}, {"waypoints_mm", json::Json::array()}, {"goal", ""},
                                   {"route_id", 0}, {"target_id", ""}});
            }
        }
        start_requested_ = false;
    }
    plan_episode(now);
    check_path(now);
    check_improvement(now);
    if (enabled_at("replan_timeout!", now) && !enabled_at("plan_accepted!", now + 1)) {
        propose("replan_timeout!", now, "no admissible plan within the planning deadline");
        finished_ = true;
    }
}

void MissionController::plan_episode(Ticks now) {
    if (!enabled_at("plan_accepted!", now)) {
        plan_wanted_since_.reset();
        return;
    }
    if (!plan_wanted_since_) {
        plan_wanted_since_ = now;
        event("planner", json::Json{{"text", "planning started"}, {"at", now}, {"planner", planner_.name()}});
    }
    if (now < *plan_wanted_since_ + config_.planning_latency) return;
    std::string goal = goal_.empty() ? next_goal() : goal_;
    while (true) {
        if (std::optional<PlanRecord> p = make_plan(position(), goal, now, "replanning towards " + goal)) {
            const std::int64_t id = p->id;
            if (propose("plan_accepted!", now, "route " + std::to_string(id) + " validated")) {
                activate(std::move(*p), now);
                send_route(*active_);
                plan_wanted_since_.reset();
            }
            return;
        }
        if (goal == "home") break;
        unreachable_.insert(goal);
        goal = next_goal();
    }
    // No admissible route to any remaining target; the way home is the only option.
    if (enabled_at("return_requested!", now) && propose("return_requested!", now, "remaining targets unreachable")) {
        if (std::optional<PlanRecord> home = make_plan(position(), "home", now, "return to home")) {
            activate(std::move(*home), now);
            send_route(*active_);
        }
        plan_wanted_since_.reset();
    }
}

void MissionController::check_path(Ticks now) {
    if (!map_dirty_) return;
    map_dirty_ = false;
    if (!flying_route()) return;
    const std::vector<geo::Point> rest = remaining_route();
    if (rest.empty()) return;
    ValidationPolicy geometric = config_.validation;
    geometric.reserve_wh = -std::numeric_limits<double>::infinity();  // battery is the PT's reserve event
    const PlanCheck check = validate_route(world_->map(), position(), rest, energy_wh(), 0.0, geometric);
    if (check.valid) return;
    const std::string why = check.issues.front();
    if (enabled_at("path_invalidated!", now) && propose("path_invalidated!", now, why)) {
        close_active("invalidated", why, now);
        command(json::Json{{"kind", "hold"}, {"waypoints_mm", json::Json::array()}, {"goal", ""}, {"route_id", 0},
                           {"target_id", ""}});
    }
}

void MissionController::check_improvement(Ticks now) {
    if (!discoveries_) return;
    discoveries_ = false;
    if (!flying_route() || !enabled_at("replan_requested!", now)) return;
    const std::vector<geo::Point> rest = remaining_route();
    if (rest.empty()) return;
    const geo::OccupancyGrid& map = world_->map();
    const double current = planner::route_cost(map, position(), rest, config_.weights);
    planner::PlanningProblem problem{&map, map.cell_of(position()), goal_cell(goal_), config_.weights, true};
    const planner::Plan candidate = planner_.plan(problem);
    if (!candidate.found) return;
    const double gain = current - candidate.cost;
    if (candidate.cost < config_.improvement_ratio * current && gain > config_.improvement_min) {
        const std::string why = "better route found: cost " + std::to_string(static_cast<int>(current)) + " -> " +
                                std::to_string(static_cast<int>(candidate.cost));
        if (propose("replan_requested!", now, why)) {
            close_active("superseded", why, now);
            command(json::Json{{"kind", "hold"}, {"waypoints_mm", json::Json::array()}, {"goal", ""},
                               {"route_id", 0}, {"target_id", ""}});
        }
    }
}

// -------------------------------------------------------------------- outputs

std::vector<json::Json> MissionController::take_commands() {
    std::vector<json::Json> out;
    out.swap(commands_);
    return out;
}

std::vector<json::Json> MissionController::take_events() {
    std::vector<json::Json> out;
    out.swap(events_);
    return out;
}

json::Json MissionController::status() const {
    json::Json targets = json::Json::array();
    for (const MissionTarget& t : mission_.targets) {
        const std::string g = "target:" + t.id;
        targets.push_back(json::Json{{"id", t.id},
                                     {"name", t.name},
                                     {"cell", geo::to_json(t.cell)},
                                     {"status", done_.contains(g)          ? "inspected"
                                                : unreachable_.contains(g) ? "unreachable"
                                                : goal_ == g               ? "current"
                                                                           : "pending"}});
    }
    return json::Json{{"mission", mission_.name},
                      {"home", geo::to_json(mission_.home)},
                      {"goal", goal_},
                      {"targets", targets},
                      {"planning", plan_wanted_since_.has_value()},
                      {"finished", finished_},
                      {"planner", planner_.name()}};
}

json::Json MissionController::plans() const {
    json::Json history = json::Json::array();
    for (const PlanRecord& p : history_) history.push_back(to_json(p));
    return json::Json{{"active", active_ ? to_json(*active_) : json::Json()}, {"history", history}};
}

}  // namespace twin::runtime
