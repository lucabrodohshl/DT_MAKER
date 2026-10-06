/**
 * @file world.cpp
 * @brief Ground truth, scenario timeline, environment service and the World facade.
 */
#include "twin/world/world.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace twin::world {

// ---------------------------------------------------------------- environment

EnvironmentService::EnvironmentService(const Scenario& scenario) : published_(scenario.prior_knowledge) {}

void EnvironmentService::observe(Ticks at, const std::vector<geo::CellChange>& sensed) {
    geo::MapUpdate update;
    update.at = at;
    update.kind = "sensor_observation";
    std::map<geo::Occupancy, int> discovered;
    for (const geo::CellChange& c : sensed) {
        const geo::Occupancy known = published_.at(c.cell);
        // A declared hazard is facility knowledge; the lidar cannot clear it.
        if (known == geo::Occupancy::Hazard || known == c.occupancy) continue;
        published_.set(c.cell, c.occupancy);
        update.cells.push_back(c);
        if (known == geo::Occupancy::Unknown) {
            ++discovered[geo::Occupancy::Unknown];
        } else {
            ++discovered[c.occupancy];
        }
    }
    if (update.cells.empty()) return;
    std::string what;
    auto add = [&what](int n, const char* text) {
        if (n == 0) return;
        if (!what.empty()) what += ", ";
        what += std::to_string(n) + " " + text;
    };
    add(discovered[geo::Occupancy::Wall], "unexpected wall cell(s)");
    add(discovered[geo::Occupancy::DoorClosed], "closed door cell(s)");
    add(discovered[geo::Occupancy::Obstacle], "obstacle cell(s)");
    add(discovered[geo::Occupancy::DoorOpen], "open door cell(s)");
    add(discovered[geo::Occupancy::Free], "cell(s) found free");
    add(discovered[geo::Occupancy::Unknown], "previously unknown cell(s) mapped");
    update.description = "Onboard sensing: " + what;
    update.seq = head() + 1;
    updates_.push_back(std::move(update));
}

void EnvironmentService::notice(Ticks at, const std::string& description, const std::vector<geo::CellChange>& cells) {
    geo::MapUpdate update;
    update.at = at;
    update.kind = "facility_notice";
    update.description = description;
    for (const geo::CellChange& c : cells) {
        if (published_.at(c.cell) == c.occupancy) continue;
        published_.set(c.cell, c.occupancy);
        update.cells.push_back(c);
    }
    update.seq = head() + 1;
    updates_.push_back(std::move(update));
}

std::vector<geo::MapUpdate> EnvironmentService::updates_since(std::uint64_t since) const {
    std::vector<geo::MapUpdate> out;
    for (const geo::MapUpdate& u : updates_) {
        if (u.seq > since) out.push_back(u);
    }
    return out;
}

// ---------------------------------------------------------------------- world

World::World(Scenario scenario)
    : scenario_(std::move(scenario)), truth_(scenario_.ground_truth), drone_(scenario_, &truth_), env_(scenario_) {}

void World::apply_due_events(std::vector<std::string>& log) {
    while (next_event_ < scenario_.events.size() && scenario_.events[next_event_].at <= now_) {
        const ScenarioEvent& e = scenario_.events[next_event_++];
        std::vector<geo::CellChange> changes;
        for (const geo::Cell& c : e.cells) {
            truth_.set(c, e.occupancy);
            changes.push_back(geo::CellChange{c, e.occupancy});
        }
        ++truth_version_;
        if (e.notify) {
            env_.notice(now_, e.description, changes);
        }
        log.push_back(e.description);
    }
}

StepResult World::step(Ticks dt) {
    std::lock_guard lock(mutex_);
    StepResult r;
    now_ += dt;
    apply_due_events(r.facility_log);
    r.events = drone_.step(now_, dt);
    // Observation model: sensor sweeps at the configured interval report only the observed classes.
    const ObservationSpec& om = scenario_.observation;
    if (om.update_interval <= 0 || last_sense_ < 0 || now_ - last_sense_ >= om.update_interval) {
        last_sense_ = now_;
        std::vector<geo::CellChange> sensed = drone_.sense();
        std::erase_if(sensed, [&om](const geo::CellChange& c) {
            switch (c.occupancy) {
                case geo::Occupancy::Wall: return !om.walls;
                case geo::Occupancy::Obstacle: return !om.obstacles;
                case geo::Occupancy::DoorOpen:
                case geo::Occupancy::DoorClosed: return !om.doors;
                case geo::Occupancy::Free: return !om.free_space;
                default: return false;
            }
        });
        env_.observe(now_, sensed);
    }
    r.telemetry = drone_.telemetry(now_);
    return r;
}

Status World::command(const Command& c) {
    std::lock_guard lock(mutex_);
    return drone_.command(c);
}

Ticks World::now() const {
    std::lock_guard lock(mutex_);
    return now_;
}

Telemetry World::telemetry() const {
    std::lock_guard lock(mutex_);
    return drone_.telemetry(now_);
}

json::Json World::env_known_map() const {
    std::lock_guard lock(mutex_);
    return json::Json{{"map", geo::to_json(env_.published())}, {"seq", env_.head()}, {"at", now_}};
}

json::Json World::env_updates_since(std::uint64_t since) const {
    std::lock_guard lock(mutex_);
    json::Json arr = json::Json::array();
    for (const geo::MapUpdate& u : env_.updates_since(since)) arr.push_back(geo::to_json(u));
    return json::Json{{"updates", arr}, {"head", env_.head()}, {"at", now_}};
}

json::Json World::env_mission() const {
    std::lock_guard lock(mutex_);
    json::Json targets = json::Json::array();
    for (const TargetSpec& t : scenario_.targets) {
        targets.push_back(json::Json{{"id", t.id}, {"name", t.name}, {"cell", geo::to_json(t.cell)}});
    }
    return json::Json{{"name", scenario_.name},
                      {"home", geo::to_json(scenario_.home)},
                      {"targets", targets},
                      {"cruise_height_mm", static_cast<std::int64_t>(std::llround(scenario_.drone.cruise_height_m * 1000))},
                      {"battery_capacity_mwh",
                       static_cast<std::int64_t>(std::llround(scenario_.drone.battery_capacity_wh * 1000))},
                      {"reserve_permille", static_cast<std::int64_t>(std::llround(scenario_.drone.reserve_pct * 10))}};
}

json::Json World::env_hazards() const {
    std::lock_guard lock(mutex_);
    json::Json cells = json::Json::array();
    const geo::OccupancyGrid& g = env_.published();
    for (int y = 0; y < g.height(); ++y) {
        for (int x = 0; x < g.width(); ++x) {
            if (g.at(geo::Cell{x, y}) == geo::Occupancy::Hazard) cells.push_back(geo::to_json(geo::Cell{x, y}));
        }
    }
    return json::Json{{"hazard_cells", cells}};
}

json::Json World::observer_ground_truth() const {
    std::lock_guard lock(mutex_);
    return json::Json{{"map", geo::to_json(truth_)}, {"version", truth_version_}, {"at", now_}};
}

json::Json World::observer_state() const {
    std::lock_guard lock(mutex_);
    return json::Json{{"telemetry", to_json(drone_.telemetry(now_))}, {"truth_version", truth_version_}};
}

Status World::observer_inject(const std::vector<geo::Cell>& cells, geo::Occupancy occupancy,
                              const std::string& description, bool notify) {
    std::lock_guard lock(mutex_);
    std::vector<geo::CellChange> changes;
    for (const geo::Cell& c : cells) {
        if (!truth_.contains(c)) return make_error(ErrorCode::InvalidArgument, "cell outside the building");
        if (c == scenario_.home) return make_error(ErrorCode::InvalidArgument, "cannot block the home pad");
        truth_.set(c, occupancy);
        changes.push_back(geo::CellChange{c, occupancy});
    }
    ++truth_version_;
    if (notify) env_.notice(now_, description, changes);
    return ok_status();
}

json::Json World::scenario_info() const {
    std::lock_guard lock(mutex_);
    json::Json targets = json::Json::array();
    for (const TargetSpec& t : scenario_.targets) {
        targets.push_back(json::Json{{"id", t.id}, {"name", t.name}, {"cell", geo::to_json(t.cell)}});
    }
    json::Json timeline = json::Json::array();
    for (const ScenarioEvent& e : scenario_.events) {
        timeline.push_back(json::Json{{"at", e.at}, {"kind", e.kind}, {"description", e.description}});
    }
    return json::Json{{"name", scenario_.name},
                      {"description", scenario_.description},
                      {"home", geo::to_json(scenario_.home)},
                      {"targets", targets},
                      {"timeline", timeline},
                      {"observation",
                       {{"sensor_range_mm", static_cast<std::int64_t>(std::llround(scenario_.drone.sensor_range_m * 1000))},
                        {"proximity_range_mm", static_cast<std::int64_t>(std::llround(scenario_.drone.proximity_range_m * 1000))},
                        {"update_interval_ms", scenario_.observation.update_interval},
                        {"walls", scenario_.observation.walls},
                        {"obstacles", scenario_.observation.obstacles},
                        {"doors", scenario_.observation.doors}}}};
}

}  // namespace twin::world
