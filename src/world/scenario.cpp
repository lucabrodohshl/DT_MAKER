/**
 * @file scenario.cpp
 * @brief Scenario file parsing and validation.
 */
#include "twin/world/scenario.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace twin::world {
namespace {

using json::Json;

std::optional<geo::Occupancy> occupancy_named(const std::string& name) {
    for (geo::Occupancy o : {geo::Occupancy::Free, geo::Occupancy::Wall, geo::Occupancy::Obstacle,
                             geo::Occupancy::DoorOpen, geo::Occupancy::DoorClosed, geo::Occupancy::Hazard}) {
        if (geo::to_string(o) == name) return o;
    }
    return std::nullopt;
}

Result<std::vector<std::string>> rows(const Json& j, const char* key) {
    if (!j.contains(key) || !j.at(key).is_array()) {
        return make_error(ErrorCode::ValidationError, std::string("missing map '") + key + "'");
    }
    std::vector<std::string> out;
    for (const Json& r : j.at(key)) {
        if (!r.is_string()) return make_error(ErrorCode::ValidationError, "map rows must be strings");
        out.push_back(r.get<std::string>());
    }
    return out;
}

/// Cells from either "cells": [[x,y],...] or "rect": [x0,y0,x1,y1] (inclusive).
Result<std::vector<geo::Cell>> event_cells(const Json& e) {
    std::vector<geo::Cell> cells;
    if (e.contains("rect")) {
        const Json& r = e.at("rect");
        if (!r.is_array() || r.size() != 4) return make_error(ErrorCode::ValidationError, "rect is [x0,y0,x1,y1]");
        for (int y = r[1].get<int>(); y <= r[3].get<int>(); ++y) {
            for (int x = r[0].get<int>(); x <= r[2].get<int>(); ++x) cells.push_back(geo::Cell{x, y});
        }
    }
    if (e.contains("cells")) {
        for (const Json& c : e.at("cells")) {
            Result<geo::Cell> cell = geo::cell_from_json(c);
            if (!cell) return std::move(cell).error();
            cells.push_back(cell.value());
        }
    }
    if (cells.empty()) return make_error(ErrorCode::ValidationError, "event without cells");
    return cells;
}

double number_or(const Json& j, const char* key, double fallback) {
    return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>() : fallback;
}

}  // namespace

Result<Scenario> scenario_from_json(const Json& j) {
    Scenario s;
    s.name = j.value("name", std::string("unnamed"));
    s.description = j.value("description", std::string());
    s.seed = j.value("seed", std::uint64_t{0});
    const double cell_size = static_cast<double>(j.value("cell_size_mm", std::int64_t{500})) / 1000.0;
    Result<std::vector<std::string>> truth = rows(j, "ground_truth");
    Result<std::vector<std::string>> prior = rows(j, "prior_knowledge");
    if (!truth) return std::move(truth).error();
    if (!prior) return std::move(prior).error();
    Result<geo::OccupancyGrid> g = geo::OccupancyGrid::from_rows(truth.value(), cell_size);
    Result<geo::OccupancyGrid> p = geo::OccupancyGrid::from_rows(prior.value(), cell_size);
    if (!g) return std::move(g).error().with("map", "ground_truth");
    if (!p) return std::move(p).error().with("map", "prior_knowledge");
    s.ground_truth = std::move(g).value();
    s.prior_knowledge = std::move(p).value();
    if (s.ground_truth.width() != s.prior_knowledge.width() || s.ground_truth.height() != s.prior_knowledge.height()) {
        return make_error(ErrorCode::ValidationError, "ground truth and prior knowledge differ in size");
    }
    for (int y = 0; y < s.ground_truth.height(); ++y) {
        for (int x = 0; x < s.ground_truth.width(); ++x) {
            if (s.ground_truth.at(geo::Cell{x, y}) == geo::Occupancy::Unknown) {
                return make_error(ErrorCode::ValidationError, "the ground truth cannot contain unknown cells")
                    .with("cell", std::to_string(x) + "," + std::to_string(y));
            }
        }
    }
    Result<geo::Cell> home = geo::cell_from_json(j.value("home", Json()));
    if (!home) return std::move(home).error().with("member", "home");
    s.home = home.value();
    for (const Json& t : j.value("targets", Json::array())) {
        Result<geo::Cell> cell = geo::cell_from_json(t.value("cell", Json()));
        if (!cell) return std::move(cell).error().with("member", "targets");
        s.targets.push_back(TargetSpec{t.value("id", std::string()), t.value("name", std::string()), cell.value()});
    }
    if (s.targets.empty()) return make_error(ErrorCode::ValidationError, "a mission needs at least one target");
    for (const geo::Cell c : {s.home}) {
        if (!geo::is_navigable(s.ground_truth.at(c))) {
            return make_error(ErrorCode::ValidationError, "home must be a free cell");
        }
    }
    for (const TargetSpec& t : s.targets) {
        if (!geo::is_navigable(s.ground_truth.at(t.cell))) {
            return make_error(ErrorCode::ValidationError, "target cells must be free").with("target", t.id);
        }
    }
    const Json d = j.value("drone", Json::object());
    s.drone.max_speed_mps = number_or(d, "max_speed_mps", s.drone.max_speed_mps);
    s.drone.max_accel_mps2 = number_or(d, "max_accel_mps2", s.drone.max_accel_mps2);
    s.drone.battery_capacity_wh = number_or(d, "battery_capacity_wh", s.drone.battery_capacity_wh);
    s.drone.battery_start_pct = number_or(d, "battery_start_pct", s.drone.battery_start_pct);
    s.drone.reserve_pct = number_or(d, "reserve_pct", s.drone.reserve_pct);
    s.drone.sensor_range_m = number_or(d, "sensor_range_m", s.drone.sensor_range_m);
    for (const Json& e : j.value("events", Json::array())) {
        ScenarioEvent ev;
        Result<Ticks> at = parse_time(e.value("at", std::string()), kWorldTime);
        if (!at) return std::move(at).error().with("member", "events.at");
        ev.at = at.value();
        ev.kind = e.value("kind", std::string("set_cells"));
        Result<std::vector<geo::Cell>> cells = event_cells(e);
        if (!cells) return std::move(cells).error();
        ev.cells = std::move(cells).value();
        const std::optional<geo::Occupancy> occ =
            occupancy_named(e.value("occupancy", std::string(ev.kind == "hazard" ? "hazard" : "free")));
        if (!occ) return make_error(ErrorCode::ValidationError, "unknown occupancy in event");
        ev.occupancy = *occ;
        ev.description = e.value("description", std::string());
        ev.notify = e.value("notify", ev.kind == "hazard");
        s.events.push_back(std::move(ev));
    }
    std::stable_sort(s.events.begin(), s.events.end(),
                     [](const ScenarioEvent& a, const ScenarioEvent& b) { return a.at < b.at; });
    return s;
}

Result<Scenario> load_scenario(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return make_error(ErrorCode::IoError, "cannot read scenario").with("file", path.string());
    std::ostringstream buf;
    buf << in.rdbuf();
    Result<Json> j = json::parse(buf.str());
    if (!j) return std::move(j).error().with("file", path.string());
    return scenario_from_json(j.value());
}

}  // namespace twin::world
