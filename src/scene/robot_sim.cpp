/**
 * @file robot_sim.cpp
 * @brief Mobile-robot simulation adapter: rasterisation and simulator scenario (see robot_sim.hpp).
 */
#include "twin/scene/robot_sim.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

#include "twin/core/logical_time.hpp"

namespace twin::scene {

namespace {

using json::Json;

/// Doubled integer coordinates: cell centres are exact on this grid.
struct P2 {
    std::int64_t x{0};
    std::int64_t y{0};
};

P2 doubled(const Vec& v) { return P2{2 * v.x, 2 * v.y}; }

P2 cell_centre2(const Rect& b, std::int64_t cell, std::int64_t i, std::int64_t j) {
    return P2{2 * b.x + (2 * i + 1) * cell, 2 * b.y + (2 * j + 1) * cell};
}

bool in_rect(const Rect& r, const P2& p) {
    return p.x >= 2 * r.x && p.x <= 2 * (r.x + r.w) && p.y >= 2 * r.y && p.y <= 2 * (r.y + r.h);
}

/// Exact point-in-polygon on doubled coordinates; points on an edge count as inside.
bool in_polygon(const std::vector<Vec>& poly, const P2& p) {
    const std::size_t n = poly.size();
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const P2 a = doubled(poly[j]);
        const P2 b = doubled(poly[i]);
        // On the segment a-b?
        const std::int64_t cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (cross == 0 && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
            p.y <= std::max(a.y, b.y)) {
            return true;
        }
        if ((a.y > p.y) != (b.y > p.y)) {
            // p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y), multiplied out with the sign of dy.
            const std::int64_t dy = b.y - a.y;
            const std::int64_t lhs = (p.x - a.x) * dy;
            const std::int64_t rhs = (p.y - a.y) * (b.x - a.x);
            if (dy > 0 ? lhs < rhs : lhs > rhs) inside = !inside;
        }
    }
    return inside;
}

/// Squared distance (doubled units) from p to the segment a-b.
double segment_distance2(const P2& a, const P2& b, const P2& p) {
    const double dx = static_cast<double>(b.x - a.x);
    const double dy = static_cast<double>(b.y - a.y);
    const double px = static_cast<double>(p.x - a.x);
    const double py = static_cast<double>(p.y - a.y);
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? (px * dx + py * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double ex = px - t * dx;
    const double ey = py - t * dy;
    return ex * ex + ey * ey;
}

bool is_point_kind(std::string_view k) { return k == "point" || k == "label" || k == "waypoint" || k == "node"; }

std::optional<geo::Occupancy> occupancy_of(const Object& o, std::vector<Finding>& findings) {
    const std::string& t = o.semantic_type;
    if (t == "floor" || t == "free") return geo::Occupancy::Free;
    if (t == "wall") return geo::Occupancy::Wall;
    if (t == "obstacle") return geo::Occupancy::Obstacle;
    if (t == "hazard") return geo::Occupancy::Hazard;
    if (t == "unknown") return geo::Occupancy::Unknown;
    if (t == "door") {
        const std::string state = o.properties.value("state", std::string("open"));
        if (state == "open") return geo::Occupancy::DoorOpen;
        if (state == "closed") return geo::Occupancy::DoorClosed;
        findings.push_back(Finding{"error", "TWS004", "Door '" + (o.name.empty() ? o.id : o.name) +
                                                          "' has state '" + state + "'; use \"open\" or \"closed\".",
                                   o.id, "properties.state"});
        return std::nullopt;
    }
    return std::nullopt;
}

std::string label_of(const Object& o) { return o.name.empty() ? o.id : o.name; }

Result<double> decimal(const Json& section, const char* key, double fallback) {
    if (!section.is_object() || !section.contains(key)) return fallback;
    const Json& v = section.at(key);
    if (v.is_number_integer()) return static_cast<double>(v.get<std::int64_t>());
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        char* end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0' && std::isfinite(d)) return d;
    }
    return make_error(ErrorCode::ValidationError, std::string("'") + key + "' must be a decimal string such as \"1.5\"")
        .with("member", key);
}

Json cell_json(const geo::Cell& c) { return Json::array({c.x, c.y}); }

}  // namespace

geo::Cell cell_of(const Vec& p, const Rect& b, std::int64_t cell) {
    const auto floor_div = [](std::int64_t a, std::int64_t d) { return a >= 0 ? a / d : -((-a + d - 1) / d); };
    return geo::Cell{static_cast<int>(floor_div(p.x - b.x, cell)), static_cast<int>(floor_div(p.y - b.y, cell))};
}

std::vector<geo::Cell> footprint(const Object& o, const Rect& b, std::int64_t cell) {
    std::vector<geo::Cell> out;
    if (cell <= 0) return out;
    const std::int64_t width = b.w / cell;
    const std::int64_t height = b.h / cell;
    const Geometry& g = o.geometry;
    if (is_point_kind(o.kind) && !(o.kind == "node" && g.rect)) {
        if (g.points.empty()) return out;
        const geo::Cell c = cell_of(g.points.front(), b, cell);
        if (c.x >= 0 && c.y >= 0 && c.x < width && c.y < height) out.push_back(c);
        return out;
    }
    const std::optional<Rect> bb = bounding_box(o);
    if (!bb) return out;
    const bool stroke = o.kind == "line" || o.kind == "polyline";
    const std::int64_t pad = stroke ? std::max(g.width, cell) : 0;
    const auto lo = [&](std::int64_t v, std::int64_t origin) { return std::max<std::int64_t>(0, (v - pad - origin) / cell - 1); };
    const std::int64_t i0 = lo(bb->x, b.x);
    const std::int64_t j0 = lo(bb->y, b.y);
    const std::int64_t i1 = std::min(width - 1, (bb->x + bb->w + pad - b.x) / cell + 1);
    const std::int64_t j1 = std::min(height - 1, (bb->y + bb->h + pad - b.y) / cell + 1);
    const double half = static_cast<double>(stroke ? std::max(g.width, cell) : 0);  // doubled half-width
    for (std::int64_t j = j0; j <= j1; ++j) {
        for (std::int64_t i = i0; i <= i1; ++i) {
            const P2 p = cell_centre2(b, cell, i, j);
            bool hit = false;
            if (stroke) {
                for (std::size_t k = 1; k < g.points.size() && !hit; ++k) {
                    hit = segment_distance2(doubled(g.points[k - 1]), doubled(g.points[k]), p) <= half * half + 1e-6;
                }
            } else if (g.rect && (o.kind == "rect" || o.kind == "image" || o.kind == "node" || g.points.size() < 3)) {
                hit = in_rect(*g.rect, p);
            } else if (g.points.size() >= 3) {
                hit = in_polygon(g.points, p);
            }
            if (hit) out.push_back(geo::Cell{static_cast<int>(i), static_cast<int>(j)});
        }
    }
    return out;
}

Result<Raster> rasterize(const World& world, std::int64_t cell) {
    if (world.mode != "spatial") {
        return make_error(ErrorCode::InvalidArgument, "only spatial worlds can be rasterised for the mobile-robot simulator")
            .with("mode", world.mode);
    }
    if (cell <= 0) return make_error(ErrorCode::InvalidArgument, "the cell size must be positive");
    Raster r;
    r.cell_mm = cell;
    const Rect& b = world.bounds;
    if (b.w <= 0 || b.h <= 0 || b.w % cell != 0 || b.h % cell != 0) {
        r.findings.push_back(Finding{"error", "TWS001",
                                     "The world bounds (" + std::to_string(b.w) + " x " + std::to_string(b.h) +
                                         " mm) must be a positive multiple of the cell size (" + std::to_string(cell) +
                                         " mm).",
                                     "", "bounds"});
        return r;
    }
    const int width = static_cast<int>(b.w / cell);
    const int height = static_cast<int>(b.h / cell);
    const double cell_m = static_cast<double>(cell) / 1000.0;
    r.ground_truth = geo::OccupancyGrid(width, height, cell_m, geo::Occupancy::Wall);
    r.knowledge = geo::OccupancyGrid(width, height, cell_m, geo::Occupancy::Wall);
    for (const Layer& layer : world.layers) {
        const bool truth = layer.role == LayerRole::Shared || layer.role == LayerRole::GroundTruth;
        const bool known = layer.role == LayerRole::Shared || layer.role == LayerRole::Knowledge;
        if (!truth && !known) continue;
        for (const Object& o : world.objects) {
            if (o.layer != layer.id) continue;
            const std::optional<geo::Occupancy> occ = occupancy_of(o, r.findings);
            if (!occ) continue;
            if (o.kind == "edge" || o.kind == "connector" || o.kind == "image") {
                r.findings.push_back(Finding{"warning", "TWS003",
                                             "'" + label_of(o) + "' has semantic type '" + o.semantic_type +
                                                 "' but a " + o.kind + " has no footprint; it does not change the map.",
                                             o.id, "kind"});
                continue;
            }
            for (const geo::Cell& c : footprint(o, b, cell)) {
                if (truth) r.ground_truth.set(c, *occ);
                if (known) r.knowledge.set(c, *occ);
            }
        }
    }
    int unknown = 0;
    std::string first;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (r.ground_truth.at(geo::Cell{x, y}) != geo::Occupancy::Unknown) continue;
            if (unknown++ == 0) first = std::to_string(x) + "," + std::to_string(y);
        }
    }
    if (unknown > 0) {
        r.findings.push_back(Finding{"error", "TWS002",
                                     std::to_string(unknown) +
                                         " ground-truth cell(s) are 'unknown' (first at cell " + first +
                                         "). Unknown areas belong on a knowledge layer: the physical world is never unknown.",
                                     "", "layers"});
    }
    return r;
}

Json to_json(const Raster& r) {
    Json gt = Json::array();
    Json kn = Json::array();
    for (const std::string& row : r.ground_truth.to_rows()) gt.push_back(row);
    for (const std::string& row : r.knowledge.to_rows()) kn.push_back(row);
    return Json{{"cellMm", r.cell_mm},
                {"width", r.ground_truth.width()},
                {"height", r.ground_truth.height()},
                {"legend", {{"?", "unknown"}, {".", "free"}, {"#", "wall"}, {"o", "obstacle"}, {"D", "door_open"}, {"d", "door_closed"}, {"!", "hazard"}}},
                {"groundTruth", gt},
                {"knowledge", kn},
                {"findings", to_json(r.findings)}};
}

Result<Json> simulator_scenario(const World& world, const Json& sim, std::string_view name, std::string_view description) {
    std::vector<Finding> problems;
    auto fail = [&](std::string code, std::string message, std::string object, std::string path) {
        problems.push_back(Finding{"error", std::move(code), std::move(message), std::move(object), std::move(path)});
    };
    if (!sim.is_object() || sim.value("kind", std::string()) != "mobile-robot") {
        return make_error(ErrorCode::InvalidArgument, "the simulation section is not of kind \"mobile-robot\"");
    }
    const std::int64_t cell = sim.value("cellSize", std::int64_t{500});
    Result<Raster> raster = rasterize(world, cell);
    if (!raster) return std::move(raster).error();
    for (const Finding& f : raster.value().findings) {
        if (f.severity == "error") problems.push_back(f);
    }
    const Raster& r = raster.value();
    const Rect& b = world.bounds;

    // Robot and observation model.
    const Json robot = sim.value("robot", Json::object());
    const Json obs = sim.value("observation", Json::object());
    Json drone = Json::object();
    const std::pair<const char*, std::pair<const char*, double>> robot_keys[] = {
        {"max_speed_mps", {"maxSpeed", 1.2}},          {"max_accel_mps2", {"maxAccel", 1.5}},
        {"battery_capacity_wh", {"batteryCapacityWh", 10.0}}, {"battery_start_pct", {"batteryStartPct", 100.0}},
        {"reserve_pct", {"reservePct", 20.0}}};
    for (const auto& [out_key, in] : robot_keys) {
        Result<double> v = decimal(robot, in.first, in.second);
        if (!v) fail("TWS010", v.error().message, "", std::string("simulation.robot.") + in.first);
        else drone[out_key] = v.value();
    }
    for (const auto& [out_key, in] : {std::pair<const char*, std::pair<const char*, double>>{"sensor_range_m", {"sensorRange", 3.0}},
                                      {"proximity_range_m", {"proximityRange", 1.0}}}) {
        Result<double> v = decimal(obs, in.first, in.second);
        if (!v) fail("TWS011", v.error().message, "", std::string("simulation.observation.") + in.first);
        else drone[out_key] = v.value();
    }
    const std::string noise = obs.value("noise", std::string("none"));
    if (noise != "none") {
        fail("TWS012",
             "Observation noise model '" + noise +
                 "' is not supported by this simulator version; only \"none\" is executed (no approximation is made).",
             "", "simulation.observation.noise");
    }
    Json observation{{"update_interval_ms", obs.value("updateIntervalMs", std::int64_t{0})},
                     {"observes", obs.value("observes", Json::array({"wall", "obstacle", "door"}))},
                     {"noise", noise}};

    // Mission: start pad and targets.
    const Json mission = sim.value("mission", Json::object());
    const Object* start = nullptr;
    if (mission.contains("start") && mission.at("start").is_string()) {
        start = world.object(mission.at("start").get<std::string>());
        if (start == nullptr) fail("TWS020", "The mission start refers to an unknown world object.", "", "simulation.mission.start");
    } else {
        for (const Object& o : world.objects) {
            if (o.semantic_type == "start") {
                start = &o;
                break;
            }
        }
        if (start == nullptr) fail("TWS020", "Place a 'start' marker in the world (where the robot takes off).", "", "simulation.mission.start");
    }
    std::vector<const Object*> targets;
    if (mission.contains("targets") && mission.at("targets").is_array()) {
        for (const Json& t : mission.at("targets")) {
            const Object* o = t.is_string() ? world.object(t.get<std::string>()) : nullptr;
            if (o == nullptr) fail("TWS021", "A mission target refers to an unknown world object.", t.is_string() ? t.get<std::string>() : "", "simulation.mission.targets");
            else targets.push_back(o);
        }
    } else {
        for (const Object& o : world.objects) {
            if (o.semantic_type == "target") targets.push_back(&o);
        }
    }
    if (targets.empty()) fail("TWS021", "Place at least one 'target' marker in the world.", "", "simulation.mission.targets");
    auto point_cell = [&](const Object& o, const std::string& what) -> std::optional<geo::Cell> {
        if (o.geometry.points.empty()) {
            fail("TWS022", what + " '" + label_of(o) + "' must be a point.", o.id, "geometry");
            return std::nullopt;
        }
        const geo::Cell c = cell_of(o.geometry.points.front(), b, cell);
        if (!r.ground_truth.contains(c)) {
            fail("TWS022", what + " '" + label_of(o) + "' lies outside the world.", o.id, "geometry");
            return std::nullopt;
        }
        if (!geo::is_navigable(r.ground_truth.at(c))) {
            fail("TWS023", what + " '" + label_of(o) + "' is on a " + std::string(geo::to_string(r.ground_truth.at(c))) +
                               " cell; it must be on free floor.",
                 o.id, "geometry");
        }
        return c;
    };
    Json home;
    if (start != nullptr) {
        if (auto c = point_cell(*start, "The start marker")) home = cell_json(*c);
    }
    Json target_list = Json::array();
    std::set<std::string> target_ids;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const Object& o = *targets[i];
        std::string id = o.properties.value("targetId", std::string());
        if (id.empty()) id = "T" + std::to_string(i + 1);
        if (!target_ids.insert(id).second) fail("TWS024", "Two targets share the id '" + id + "'.", o.id, "properties.targetId");
        if (auto c = point_cell(o, "Target")) {
            target_list.push_back(Json{{"id", id}, {"name", label_of(o)}, {"cell", cell_json(*c)}});
        }
    }

    // Timeline: world changes at logical times (e.g. a declared no-fly zone).
    Json events = Json::array();
    for (const Json& e : sim.value("timeline", Json::array())) {
        const std::string object_id = e.value("object", std::string());
        const Object* o = world.object(object_id);
        const std::string at = e.value("at", std::string());
        if (!parse_time(at, TimeBase{1000})) {
            fail("TWS030", "Timeline event '" + e.value("id", std::string()) + "' needs a time such as \"52\" or \"12.5\" (seconds).", object_id, "simulation.timeline.at");
            continue;
        }
        if (o == nullptr) {
            fail("TWS031", "Timeline event '" + e.value("id", std::string()) + "' refers to an unknown world object.", object_id, "simulation.timeline.object");
            continue;
        }
        const std::string kind = e.value("kind", std::string("hazard"));
        std::string occupancy = e.value("occupancy", std::string(kind == "hazard" ? "hazard" : "obstacle"));
        Json cells = Json::array();
        for (const geo::Cell& c : footprint(*o, b, cell)) cells.push_back(cell_json(c));
        if (cells.empty()) {
            fail("TWS032", "Timeline object '" + label_of(*o) + "' covers no cell.", o->id, "geometry");
            continue;
        }
        events.push_back(Json{{"at", at},
                              {"kind", kind == "hazard" ? "hazard" : "set_cells"},
                              {"occupancy", occupancy},
                              {"cells", cells},
                              {"description", e.value("description", label_of(*o))},
                              {"notify", e.value("notify", kind == "hazard")}});
    }

    if (!problems.empty()) {
        Error err = make_error(ErrorCode::ValidationError,
                               std::to_string(problems.size()) + " problem(s) prevent simulating this world: " + problems.front().message);
        for (const Finding& f : problems) err.with(f.code, f.message);
        return err;
    }
    Json gt = Json::array();
    Json kn = Json::array();
    for (const std::string& row : r.ground_truth.to_rows()) gt.push_back(row);
    for (const std::string& row : r.knowledge.to_rows()) kn.push_back(row);
    return Json{{"name", std::string(name)},
                {"description", std::string(description)},
                {"seed", sim.value("seed", std::int64_t{0})},
                {"cell_size_mm", cell},
                {"home", home},
                {"targets", target_list},
                {"drone", drone},
                {"observation", observation},
                {"events", events},
                {"ground_truth", gt},
                {"prior_knowledge", kn},
                {"generator", "twin::scene mobile-robot adapter (twin-world/1)"}};
}

}  // namespace twin::scene
