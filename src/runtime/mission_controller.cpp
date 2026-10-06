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

/// Finite value in milli-units for JSON (infinite costs encode as -1).
std::int64_t milli(double value) { return std::isfinite(value) ? mm(value) : -1; }

json::Json points_mm(const std::vector<geo::Point>& pts) {
    json::Json arr = json::Json::array();
    for (const geo::Point& p : pts) arr.push_back(json::Json::array({mm(p.x), mm(p.y)}));
    return arr;
}

bool same_polyline(const std::vector<geo::Point>& a, const std::vector<geo::Point>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (mm(a[i].x) != mm(b[i].x) || mm(a[i].y) != mm(b[i].y)) return false;
    }
    return true;
}

/// The arrival event of a route towards @p goal.
const char* arrival_label(const std::string& goal) { return goal == "home" ? "home_reached!" : "target_reached!"; }

/// Status of a mission target for APIs.
const char* target_status(bool done, bool unreachable, bool current) {
    if (done) return "inspected";
    if (unreachable) return "unreachable";
    return current ? "current" : "pending";
}

json::Json drone_command(const char* kind, const std::string& target = std::string()) {
    return json::Json{{"kind", kind}, {"waypoints_mm", json::Json::array()}, {"goal", ""}, {"route_id", 0},
                      {"target_id", target}};
}

}  // namespace

std::vector<PlannerProfile> default_profiles() {
    planner::CostWeights wide;
    wide.proximity = 4.0;
    wide.clearance_m = 1.5;
    return {PlannerProfile{"balanced", planner::CostWeights{}, true},
            PlannerProfile{"known-space", planner::CostWeights{}, false},
            PlannerProfile{"wide-clearance", wide, true}};
}

json::Json to_json(const PlanRecord& p) {
    return json::Json{{"id", p.id},
                      {"episode", p.episode},
                      {"candidate", p.candidate},
                      {"profile", p.profile},
                      {"goal", p.goal},
                      {"status", p.status},
                      {"reason", p.reason},
                      {"start_mm", json::Json::array({mm(p.start.x), mm(p.start.y)})},
                      {"waypoints_mm", points_mm(p.waypoints)},
                      {"cost_mm", milli(p.cost)},
                      {"objective_mm", milli(p.objective)},
                      {"length_mm", mm(p.length_m)},
                      {"unknown_mm", mm(p.unknown_m)},
                      {"energy_mwh", mm(p.energy_wh)},
                      {"expanded", static_cast<std::int64_t>(p.expanded)},
                      {"created_at", p.created_at},
                      {"ended_at", p.ended_at}};
}

json::Json to_json(const Candidate& c) {
    json::Json issues = json::Json::array();
    for (const std::string& i : c.geometric_issues) issues.push_back(i);
    return json::Json{{"label", c.label},
                      {"profile", c.profile},
                      {"found", c.found},
                      {"failure", c.failure},
                      {"duplicate_of", c.duplicate_of.value_or(std::string())},
                      {"plan", c.found ? to_json(c.plan) : json::Json::object()},
                      {"geometric", {{"ok", c.geometric_ok}, {"issues", issues}}},
                      {"behavioural",
                       {{"checked", c.behaviour_checked},
                        {"ok", c.behaviour_ok},
                        {"detail", c.behaviour_detail},
                        {"schedule", c.schedule}}},
                      {"selected", c.selected}};
}

json::Json to_json(const PlanningEpisode& e) {
    json::Json candidates = json::Json::array();
    for (const Candidate& c : e.candidates) candidates.push_back(to_json(c));
    return json::Json{{"id", e.id},
                      {"at", e.at},
                      {"goal", e.goal},
                      {"reason", e.reason},
                      {"decision", e.decision},
                      {"candidates", candidates},
                      {"selected", e.selected.value_or(std::string())}};
}

MissionController::MissionController(TwinSession& session, planner::PathPlanner& planner, ControllerConfig config)
    : session_(session), planner_(planner), config_(std::move(config)) {}

void MissionController::reset(Mission mission, const TwinWorldModel* world) {
    mission_ = std::move(mission);
    world_ = world;
    telemetry_ = json::Json::object();
    goal_.clear();
    done_.clear();
    unreachable_.clear();
    active_.reset();
    history_.clear();
    episodes_.clear();
    next_plan_id_ = 1;
    next_episode_id_ = 1;
    plan_wanted_since_.reset();
    braked_since_.reset();
    pending_.reset();
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

bool MissionController::propose(std::string_view label, Ticks now, const std::string& reason, json::Json extra) {
    json::Json payload = std::move(extra);
    payload["reason"] = reason;
    ledger::Input in{"mission-controller", ledger::InputKind::Label, std::string(label), now, payload};
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
    if (goal.starts_with("target:")) {
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

Ticks MissionController::leg_eta(double length_m) const {
    const double seconds = length_m / config_.eta_speed_mps + config_.eta_leg_overhead_s;
    return static_cast<Ticks>(std::llround(seconds * static_cast<double>(session_.model().time_base().ticks_per_unit)));
}

// ------------------------------------------------------------- planning episodes

void MissionController::evaluate(Candidate& c, geo::Point from, Ticks depart_at, const Prefix& prefix) const {
    const geo::OccupancyGrid& map = world_->map();
    // 1. Geometric feasibility: independent dense-sampling validation against the KNOWN map.
    const double home_m =
        c.plan.goal == "home" ? 0.0 : geo::distance(map.center(goal_cell(c.plan.goal)), map.center(mission_.home));
    const PlanCheck check = validate_route(map, from, c.plan.waypoints, energy_wh(), home_m, config_.validation);
    c.geometric_ok = check.valid;
    c.geometric_issues = check.issues;
    if (!c.geometric_ok) {
        c.behaviour_detail = "not checked: geometrically infeasible";
        return;
    }
    // 2. Behavioural admissibility: the kernel simulates the plan's nominal schedule on a copy.
    std::vector<kernel::ScheduledObservation> schedule;
    bool expressible = true;
    auto push = [&](const std::string& label, Ticks at) {
        c.schedule.push_back(json::Json{{"label", label}, {"at", at}});
        const std::optional<kernel::LabelId> id = session_.model().label_id(label);
        if (!id) {
            expressible = false;
            return;
        }
        schedule.push_back(kernel::ScheduledObservation{at, kernel::Selector::label(*id)});
    };
    for (const auto& [label, at] : prefix) push(label, at);
    Ticks t = depart_at;
    geo::Point prev = from;
    for (std::size_t i = 0; i < c.plan.waypoints.size(); ++i) {
        t += leg_eta(geo::distance(prev, c.plan.waypoints[i]));
        const bool last = i + 1 == c.plan.waypoints.size();
        push(last ? arrival_label(c.plan.goal) : "waypoint_reached!", t);
        prev = c.plan.waypoints[i];
    }
    c.behaviour_checked = true;
    if (!expressible) {
        c.behaviour_detail = "the model has no event for a step of this plan";
        return;
    }
    const TimeBase base = session_.model().time_base();
    Result<std::vector<kernel::ObservationOutcome>> sim = session_.simulate(schedule);
    if (sim) {
        c.behaviour_ok = true;
        c.behaviour_detail = "the kernel admits the nominal schedule: " + std::to_string(schedule.size()) +
                             " events, arrival at t=" + format_time(t, base);
        return;
    }
    const std::string_view step = sim.error().context_value("step");
    std::string at_step;
    if (!step.empty()) {
        const std::size_t k = std::stoul(std::string(step));
        if (k < c.schedule.size()) {
            at_step = " at step " + std::to_string(k + 1) + " (" + c.schedule[k].value("label", std::string()) +
                      " at t=" + format_time(c.schedule[k].value("at", Ticks{0}), base) + ")";
        }
    }
    c.behaviour_detail = "the kernel refuses the schedule" + at_step + ": " + sim.error().message;
}

std::optional<PlanRecord> MissionController::run_episode(geo::Point from, const std::string& goal, Ticks now,
                                                         const std::string& reason, const Prefix& prefix,
                                                         Ticks depart_at, const std::string& decision) {
    const geo::OccupancyGrid& map = world_->map();
    PlanningEpisode ep;
    ep.id = next_episode_id_++;
    ep.at = now;
    ep.goal = goal;
    ep.reason = reason;
    ep.decision = decision;
    char label = 'A';
    for (const PlannerProfile& profile : config_.profiles) {
        Candidate c;
        c.label = std::string(1, label++);
        c.profile = profile.name;
        const planner::PlanningProblem problem{&map, map.cell_of(from), goal_cell(goal), profile.weights,
                                               profile.allow_unknown};
        const planner::Plan p = planner_.plan(problem);
        c.found = p.found;
        c.failure = p.failure;
        if (p.found) {
            PlanRecord& r = c.plan;
            r.episode = ep.id;
            r.candidate = c.label;
            r.profile = profile.name;
            r.goal = goal;
            r.status = "candidate";
            r.reason = reason;
            r.start = from;
            r.waypoints = p.waypoints;
            r.cost = p.cost;
            r.objective = planner::route_cost(map, from, p.waypoints, config_.weights);
            r.length_m = p.length_m;
            r.unknown_m = p.unknown_m;
            r.energy_wh = p.energy_wh;
            r.expanded = p.expanded;
            r.created_at = now;
            for (const Candidate& earlier : ep.candidates) {
                if (earlier.found && same_polyline(earlier.plan.waypoints, p.waypoints)) {
                    c.duplicate_of = earlier.label;
                    r.id = earlier.plan.id;
                    c.geometric_ok = earlier.geometric_ok;
                    c.geometric_issues = earlier.geometric_issues;
                    c.behaviour_checked = earlier.behaviour_checked;
                    c.behaviour_ok = earlier.behaviour_ok;
                    c.behaviour_detail = "same route as candidate " + earlier.label;
                    c.schedule = earlier.schedule;
                    break;
                }
            }
            if (!c.duplicate_of) {
                r.id = next_plan_id_++;
                evaluate(c, from, depart_at, prefix);
            }
        }
        ep.candidates.push_back(std::move(c));
    }
    Candidate* best = nullptr;
    for (Candidate& c : ep.candidates) {
        if (!c.found || c.duplicate_of || !c.geometric_ok || !c.behaviour_ok) continue;
        if (best == nullptr || c.plan.objective < best->plan.objective) best = &c;
    }
    if (best != nullptr) {
        best->selected = true;
        ep.selected = best->label;
    }
    const json::Json encoded = to_json(ep);
    // Evidence first: the episode is chained into the ledger before any decision it supports.
    (void)session_.record_context("planning", now, encoded);
    event("planning", encoded);
    std::optional<PlanRecord> selected;
    if (best != nullptr) {
        selected = best->plan;
        selected->status = "proposed";
    }
    episodes_.push_back(std::move(ep));
    return selected;
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
    if (label == "obstacle_detected!") {
        braked_since_ = at;
    } else if (label == "obstacle_cleared!") {
        braked_since_.reset();
    }
    if (label == "takeoff_complete!") {
        if (active_) send_route(*active_);
    } else if (label == "target_reached!") {
        const std::string target = goal_.starts_with("target:") ? goal_.substr(7) : std::string();
        close_active("completed", "target reached", at);
        command(drone_command("inspect", target));
    } else if (label == "inspection_complete!") {
        if (goal_.starts_with("target:")) done_.insert(goal_);
        head_for_next_goal(at, "inspection of " + goal_.substr(7) + " complete");
    } else if (label == "home_reached!") {
        close_active("completed", "home reached", at);
        command(drone_command("land"));
    } else if (label == "landing_complete!") {
        finished_ = true;
        event("mission", json::Json{{"text", "Mission complete: landed at home"}, {"at", at}});
    } else if (label == "battery_low!") {
        for (const MissionTarget& t : mission_.targets) {
            if (!done_.contains("target:" + t.id)) unreachable_.insert("target:" + t.id);
        }
        close_active("superseded", "battery reserve reached", at);
        return_home(at, "battery reserve reached: return home", false);
    }
}

void MissionController::head_for_next_goal(Ticks now, const std::string& reason) {
    std::string goal = next_goal();
    while (goal != "home") {
        if (std::optional<PlanRecord> p =
                run_episode(position(), goal, now, reason + ": next target " + goal.substr(7), {}, now, "")) {
            activate(std::move(*p), now);
            send_route(*active_);
            return;
        }
        unreachable_.insert(goal);
        goal = next_goal();
    }
    return_home(now, unreachable_.empty() ? "all targets inspected" : "remaining targets unreachable", true);
}

void MissionController::return_home(Ticks now, const std::string& reason, bool needs_decision) {
    const Prefix prefix = needs_decision ? Prefix{{"return_requested!", now}} : Prefix{};
    std::optional<PlanRecord> home =
        run_episode(position(), "home", now, reason, prefix, now, needs_decision ? "return_requested!" : "");
    if (needs_decision) {
        if (!enabled_at("return_requested!", now)) return;
        json::Json extra = home ? json::Json{{"plan_id", home->id}, {"episode", home->episode}} : json::Json::object();
        if (!propose("return_requested!", now, reason, std::move(extra))) return;
    }
    if (home) {
        activate(std::move(*home), now);
        send_route(*active_);
    } else {
        goal_ = "home";
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
    if (finished_ || world_ == nullptr) return;
    if (enabled_at("mission_loaded!", now)) {
        command(drone_command("upload_mission"));
        propose("mission_loaded!", now,
                "mission '" + mission_.name + "' with " + std::to_string(mission_.targets.size()) + " targets received");
    }
    if (start_requested_ && enabled_at("start_mission!", now)) {
        const std::string goal = next_goal();
        const Prefix prefix{{"start_mission!", now}, {"takeoff_complete!", now + config_.takeoff_eta}};
        if (std::optional<PlanRecord> p = run_episode(world_->map().center(mission_.home), goal, now, "initial route",
                                                      prefix, now + config_.takeoff_eta, "start_mission!")) {
            const std::string why = "pre-flight checks passed; route " + std::to_string(p->id) + " (candidate " +
                                    p->candidate + ") to " + goal;
            if (propose("start_mission!", now, why, json::Json{{"plan_id", p->id}, {"episode", p->episode}})) {
                activate(std::move(*p), now);
                command(drone_command("arm_takeoff"));
            }
        }
        start_requested_ = false;
    }
    decide_pending(now);
    plan_episode(now);
    check_path(now);
    check_brake(now);
    check_improvement(now);
    if (enabled_at("replan_timeout!", now) && !enabled_at("plan_accepted!", now + 1)) {
        propose("replan_timeout!", now, "no admissible plan within the planning deadline");
        finished_ = true;
    }
}

void MissionController::decide_pending(Ticks now) {
    if (!pending_ || now < pending_->at) return;
    PendingDecision p = std::move(*pending_);
    pending_.reset();
    // The world may have changed since the selection: re-check the geometry on the current knowledge.
    ValidationPolicy geometric = config_.validation;
    geometric.reserve_wh = -std::numeric_limits<double>::infinity();
    const PlanCheck check = validate_route(world_->map(), position(), p.plan.waypoints, energy_wh(), 0.0, geometric);
    if (!check.valid || !enabled_at("plan_accepted!", now)) {
        event("planner", json::Json{{"text", "selected plan dropped before its decision: " +
                                                 (check.valid ? std::string("decision no longer enabled")
                                                              : check.issues.front())},
                                    {"at", now}});
        plan_wanted_since_ = now;  // plan again
        return;
    }
    const std::string why = "route " + std::to_string(p.plan.id) + " (candidate " + p.plan.candidate + ", episode " +
                            std::to_string(p.plan.episode) + ") selected";
    if (propose("plan_accepted!", now, why, json::Json{{"plan_id", p.plan.id}, {"episode", p.plan.episode}})) {
        activate(std::move(p.plan), now);
        send_route(*active_);
        plan_wanted_since_.reset();
    }
}

void MissionController::plan_episode(Ticks now) {
    if (pending_) return;  // a selection is waiting for its decision time
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
    const Ticks decide_at = now + config_.decision_delay;
    while (true) {
        const Prefix prefix{{"plan_accepted!", decide_at}};
        if (std::optional<PlanRecord> p = run_episode(position(), goal, now, "replanning towards " + goal, prefix,
                                                      decide_at, "plan_accepted!")) {
            pending_ = PendingDecision{std::move(*p), decide_at};
            return;
        }
        if (goal == "home") break;
        unreachable_.insert(goal);
        goal = next_goal();
    }
    // No admissible route to any remaining target; the way home is the only option.
    if (enabled_at("return_requested!", now)) {
        return_home(now, "remaining targets unreachable", true);
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
    if (enabled_at("path_invalidated!", now) &&
        propose("path_invalidated!", now, why, json::Json{{"plan_id", active_->id}})) {
        braked_since_.reset();
        close_active("invalidated", why, now);
        command(drone_command("hold"));
    }
}

void MissionController::check_brake(Ticks now) {
    if (!braked_since_ || now < *braked_since_ + config_.brake_patience) return;
    if (!active_ || !enabled_at("path_invalidated!", now)) return;
    const std::string why = "collision-avoidance brake held for " +
                            format_time(now - *braked_since_, session_.model().time_base()) +
                            " s: something the twin does not know blocks route " + std::to_string(active_->id);
    if (propose("path_invalidated!", now, why, json::Json{{"plan_id", active_->id}})) {
        braked_since_.reset();
        close_active("invalidated", why, now);
        command(drone_command("hold"));
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
    const planner::PlanningProblem problem{&map, map.cell_of(position()), goal_cell(goal_), config_.weights, true};
    const planner::Plan candidate = planner_.plan(problem);
    if (!candidate.found) return;
    const double gain = current - candidate.cost;
    if (candidate.cost < config_.improvement_ratio * current && gain > config_.improvement_min) {
        const std::string why = "better route found: cost " + std::to_string(static_cast<int>(current)) + " -> " +
                                std::to_string(static_cast<int>(candidate.cost));
        if (propose("replan_requested!", now, why, json::Json{{"plan_id", active_->id}})) {
            close_active("superseded", why, now);
            command(drone_command("hold"));
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
                                     {"status", target_status(done_.contains(g), unreachable_.contains(g), goal_ == g)}});
    }
    return json::Json{{"mission", mission_.name},
                      {"home", geo::to_json(mission_.home)},
                      {"goal", goal_},
                      {"targets", targets},
                      {"planning", plan_wanted_since_.has_value() || pending_.has_value()},
                      {"finished", finished_},
                      {"planner", planner_.name()},
                      {"episodes", static_cast<std::int64_t>(episodes_.size())}};
}

json::Json MissionController::plans() const {
    json::Json history = json::Json::array();
    for (const PlanRecord& p : history_) history.push_back(to_json(p));
    return json::Json{{"active", active_ ? to_json(*active_) : json::Json()}, {"history", history}};
}

json::Json MissionController::episodes() const {
    json::Json out = json::Json::array();
    for (const PlanningEpisode& e : episodes_) out.push_back(to_json(e));
    return out;
}

}  // namespace twin::runtime
