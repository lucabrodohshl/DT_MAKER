/**
 * @file world.cpp
 * @brief twin-world/1 documents: decoding and structural validation (see world.hpp).
 */
#include "twin/scene/world.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace twin::scene {

namespace {

using json::Json;

Error bad(const std::string& path, const std::string& message) {
    return make_error(ErrorCode::ValidationError, message).with("path", path);
}

/// First floating-point number in a document (canonical documents carry integers only).
std::optional<std::string> first_float(const Json& j, const std::string& path) {
    if (j.is_number_float()) return path;
    if (j.is_object()) {
        for (const auto& [k, v] : j.items()) {
            if (auto p = first_float(v, path + "." + k)) return p;
        }
    } else if (j.is_array()) {
        for (std::size_t i = 0; i < j.size(); ++i) {
            if (auto p = first_float(j[i], path + "[" + std::to_string(i) + "]")) return p;
        }
    }
    return std::nullopt;
}

Result<std::int64_t> int_member(const Json& o, const char* key, const std::string& path, bool required = true,
                                std::int64_t fallback = 0) {
    if (!o.is_object() || !o.contains(key)) {
        if (!required) return fallback;
        return bad(path + "." + key, std::string("member '") + key + "' (integer) is required");
    }
    const Json& v = o.at(key);
    if (!v.is_number_integer()) return bad(path + "." + key, std::string("member '") + key + "' must be an integer");
    return v.get<std::int64_t>();
}

std::string string_member(const Json& o, const char* key) {
    if (!o.is_object() || !o.contains(key) || !o.at(key).is_string()) return {};
    return o.at(key).get<std::string>();
}

Result<Vec> vec_from(const Json& j, const std::string& path) {
    if (j.is_array() && j.size() == 2 && j[0].is_number_integer() && j[1].is_number_integer()) {
        return Vec{j[0].get<std::int64_t>(), j[1].get<std::int64_t>()};
    }
    if (j.is_object()) {
        auto x = int_member(j, "x", path);
        if (!x) return std::move(x).error();
        auto y = int_member(j, "y", path);
        if (!y) return std::move(y).error();
        return Vec{x.value(), y.value()};
    }
    return bad(path, "a point is [x, y] or {x, y} with integer coordinates");
}

Result<std::vector<Vec>> points_from(const Json& g, const std::string& path) {
    std::vector<Vec> out;
    if (!g.contains("points")) return out;
    const Json& ps = g.at("points");
    if (!ps.is_array()) return bad(path + ".points", "points must be an array");
    for (std::size_t i = 0; i < ps.size(); ++i) {
        auto v = vec_from(ps[i], path + ".points[" + std::to_string(i) + "]");
        if (!v) return std::move(v).error();
        out.push_back(v.value());
    }
    return out;
}

Result<std::optional<Rect>> rect_from(const Json& g, const std::string& path) {
    if (!g.contains("w") && !g.contains("h")) return std::optional<Rect>{};
    auto x = int_member(g, "x", path);
    if (!x) return std::move(x).error();
    auto y = int_member(g, "y", path);
    if (!y) return std::move(y).error();
    auto w = int_member(g, "w", path);
    if (!w) return std::move(w).error();
    auto h = int_member(g, "h", path);
    if (!h) return std::move(h).error();
    return std::optional<Rect>{Rect{x.value(), y.value(), w.value(), h.value()}};
}

bool is_point_kind(std::string_view k) { return k == "point" || k == "label" || k == "waypoint"; }
bool is_area_kind(std::string_view k) { return k == "polygon" || k == "region" || k == "zone"; }
bool is_link_kind(std::string_view k) { return k == "edge" || k == "connector"; }

Result<Geometry> geometry_from(const std::string& kind, const Json& g, const std::string& path) {
    Geometry out;
    if (!g.is_object()) return bad(path, "geometry must be an object");
    if (is_link_kind(kind)) {
        out.from = string_member(g, "from");
        out.to = string_member(g, "to");
        out.directed = g.value("directed", false);
        auto ps = points_from(g, path);
        if (!ps) return std::move(ps).error();
        out.points = std::move(ps).value();
        return out;
    }
    if (is_point_kind(kind) || kind == "node") {
        if (g.contains("x") || g.contains("y")) {
            auto v = vec_from(g, path);
            if (!v) return std::move(v).error();
            out.points.push_back(v.value());
        }
        if (kind == "node" && !out.points.empty() && (g.contains("w") || g.contains("h"))) {
            // A sized node: {x, y} is its centre, {w, h} its extent.
            auto w = int_member(g, "w", path);
            if (!w) return std::move(w).error();
            auto h = int_member(g, "h", path);
            if (!h) return std::move(h).error();
            const Vec c = out.points.front();
            out.rect = Rect{c.x - w.value() / 2, c.y - h.value() / 2, w.value(), h.value()};
        }
        return out;
    }
    if (kind == "rect" || kind == "image" || is_area_kind(kind)) {
        auto r = rect_from(g, path);
        if (!r) return std::move(r).error();
        out.rect = r.value();
        auto ps = points_from(g, path);
        if (!ps) return std::move(ps).error();
        out.points = std::move(ps).value();
        return out;
    }
    // line, polyline
    auto ps = points_from(g, path);
    if (!ps) return std::move(ps).error();
    out.points = std::move(ps).value();
    auto w = int_member(g, "width", path, false, 0);
    if (!w) return std::move(w).error();
    out.width = w.value();
    return out;
}

void add(std::vector<Finding>& out, const char* severity, const char* code, std::string message, std::string object,
         std::string path) {
    out.push_back(Finding{severity, code, std::move(message), std::move(object), std::move(path)});
}

bool inside(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

}  // namespace

std::string_view to_string(LayerRole role) noexcept {
    switch (role) {
        case LayerRole::Shared: return "shared";
        case LayerRole::GroundTruth: return "ground-truth";
        case LayerRole::Knowledge: return "knowledge";
        case LayerRole::Event: return "event";
        case LayerRole::Annotation: return "annotation";
        case LayerRole::Background: return "background";
    }
    return "shared";
}

std::optional<LayerRole> layer_role_from_string(std::string_view text) noexcept {
    for (LayerRole r : {LayerRole::Shared, LayerRole::GroundTruth, LayerRole::Knowledge, LayerRole::Event,
                        LayerRole::Annotation, LayerRole::Background}) {
        if (to_string(r) == text) return r;
    }
    return std::nullopt;
}

const Layer* World::layer(std::string_view id) const noexcept {
    for (const Layer& l : layers) {
        if (l.id == id) return &l;
    }
    return nullptr;
}

const Object* World::object(std::string_view id) const noexcept {
    for (const Object& o : objects) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

Json empty_world(std::int64_t width_mm, std::int64_t height_mm) {
    return Json{{"format", std::string(kWorldFormat)},
                {"mode", "spatial"},
                {"unit", "mm"},
                {"bounds", {{"x", 0}, {"y", 0}, {"w", width_mm}, {"h", height_mm}}},
                {"grid", {{"size", 500}, {"snap", true}}},
                {"layers", Json::array({Json{{"id", "site"}, {"name", "Site"}, {"role", "shared"}, {"visible", true}, {"locked", false}}})},
                {"objects", Json::array()}};
}

Result<World> world_from_json(const Json& d) {
    if (!d.is_object()) return bad("$", "a world document is a JSON object");
    if (d.value("format", std::string()) != kWorldFormat) {
        return bad("$.format", "format must be \"" + std::string(kWorldFormat) + "\"");
    }
    if (auto f = first_float(d, "$")) {
        return bad(*f, "world documents use integers (millimetres) or decimal strings, never floating-point numbers");
    }
    World w;
    w.mode = d.value("mode", std::string("spatial"));
    if (w.mode != "spatial" && w.mode != "topology" && w.mode != "diagram") {
        return bad("$.mode", "mode must be spatial, topology or diagram");
    }
    w.unit = d.value("unit", std::string(w.mode == "spatial" ? "mm" : "px"));
    if (d.contains("bounds")) {
        auto r = rect_from(d.at("bounds"), "$.bounds");
        if (!r) return std::move(r).error();
        if (r.value()) w.bounds = *r.value();
    }
    const Json layers = d.value("layers", Json::array());
    if (!layers.is_array()) return bad("$.layers", "layers must be an array");
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const Json& l = layers[i];
        const std::string path = "$.layers[" + std::to_string(i) + "]";
        if (!l.is_object()) return bad(path, "a layer is an object");
        Layer layer;
        layer.id = string_member(l, "id");
        layer.name = string_member(l, "name");
        const auto role = layer_role_from_string(l.value("role", std::string("shared")));
        if (!role) return bad(path + ".role", "role must be shared, ground-truth, knowledge, event, annotation or background");
        layer.role = *role;
        layer.visible = l.value("visible", true);
        layer.locked = l.value("locked", false);
        w.layers.push_back(std::move(layer));
    }
    const Json objects = d.value("objects", Json::array());
    if (!objects.is_array()) return bad("$.objects", "objects must be an array");
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const Json& o = objects[i];
        const std::string path = "$.objects[" + std::to_string(i) + "]";
        if (!o.is_object()) return bad(path, "an object is a JSON object");
        Object obj;
        obj.id = string_member(o, "id");
        obj.layer = string_member(o, "layer");
        obj.kind = string_member(o, "kind");
        obj.semantic_type = string_member(o, "semanticType");
        obj.name = string_member(o, "name");
        if (std::find(std::begin(kKinds), std::end(kKinds), obj.kind) == std::end(kKinds)) {
            return bad(path + ".kind", "unknown object kind '" + obj.kind + "'");
        }
        auto g = geometry_from(obj.kind, o.value("geometry", Json::object()), path + ".geometry");
        if (!g) return std::move(g).error();
        obj.geometry = std::move(g).value();
        if (o.contains("properties")) {
            if (!o.at("properties").is_object()) return bad(path + ".properties", "properties must be an object");
            obj.properties = o.at("properties");
        }
        if (o.contains("asset") && o.at("asset").is_string() && !o.at("asset").get<std::string>().empty()) {
            obj.asset = o.at("asset").get<std::string>();
        }
        for (const Json& t : o.value("tags", Json::array())) {
            if (t.is_string()) obj.tags.push_back(t.get<std::string>());
        }
        w.objects.push_back(std::move(obj));
    }
    return w;
}

std::optional<Rect> bounding_box(const Object& o) {
    if (o.geometry.rect && (o.kind == "rect" || o.kind == "image" || o.kind == "node" || o.geometry.points.empty())) {
        return o.geometry.rect;
    }
    if (o.geometry.points.empty()) return std::nullopt;
    std::int64_t x0 = o.geometry.points.front().x;
    std::int64_t y0 = o.geometry.points.front().y;
    std::int64_t x1 = x0;
    std::int64_t y1 = y0;
    for (const Vec& p : o.geometry.points) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    const std::int64_t half = o.geometry.width / 2;
    return Rect{x0 - half, y0 - half, x1 - x0 + 2 * half, y1 - y0 + 2 * half};
}

std::vector<Finding> validate(const World& w) {
    std::vector<Finding> out;
    if (w.bounds.w <= 0 || w.bounds.h <= 0) {
        add(out, "error", "TWW005", "The world needs bounds with a positive width and height.", "", "$.bounds");
    }
    std::set<std::string> ids;
    for (std::size_t i = 0; i < w.layers.size(); ++i) {
        const Layer& l = w.layers[i];
        const std::string path = "$.layers[" + std::to_string(i) + "]";
        if (l.id.empty()) add(out, "error", "TWW001", "A layer has no id.", "", path + ".id");
        else if (!ids.insert(l.id).second) add(out, "error", "TWW001", "Duplicate id '" + l.id + "'.", l.id, path + ".id");
    }
    std::map<std::string, const Object*> by_id;
    for (std::size_t i = 0; i < w.objects.size(); ++i) {
        const Object& o = w.objects[i];
        const std::string path = "$.objects[" + std::to_string(i) + "]";
        if (o.id.empty()) {
            add(out, "error", "TWW001", "An object has no id.", "", path + ".id");
            continue;
        }
        if (!ids.insert(o.id).second) add(out, "error", "TWW001", "Duplicate id '" + o.id + "'.", o.id, path + ".id");
        by_id.emplace(o.id, &o);
    }
    for (std::size_t i = 0; i < w.objects.size(); ++i) {
        const Object& o = w.objects[i];
        const std::string path = "$.objects[" + std::to_string(i) + "]";
        const std::string label = o.name.empty() ? o.id : o.name;
        const Layer* layer = w.layer(o.layer);
        if (layer == nullptr) {
            add(out, "error", "TWW002", "Object '" + label + "' is on unknown layer '" + o.layer + "'.", o.id, path + ".layer");
        }
        const Geometry& g = o.geometry;
        const auto geometry_error = [&](const std::string& why) {
            add(out, "error", "TWW003", "Object '" + label + "' (" + o.kind + "): " + why, o.id, path + ".geometry");
        };
        if (is_point_kind(o.kind)) {
            if (g.points.size() != 1) geometry_error("a " + o.kind + " needs exactly one position {x, y}.");
        } else if (o.kind == "line") {
            if (g.points.size() != 2) geometry_error("a line needs exactly two points.");
        } else if (o.kind == "polyline") {
            if (g.points.size() < 2) geometry_error("a polyline needs at least two points.");
        } else if (is_area_kind(o.kind)) {
            if (!g.rect && g.points.size() < 3) geometry_error("an area needs at least three points or a rectangle.");
        } else if (o.kind == "rect" || o.kind == "image") {
            if (!g.rect) geometry_error("a rectangle needs x, y, w and h.");
        } else if (o.kind == "node") {
            if (g.points.size() != 1) geometry_error("a node needs a position {x, y}.");
        } else if (is_link_kind(o.kind)) {
            for (const std::string* end : {&g.from, &g.to}) {
                if (end->empty() || by_id.count(*end) == 0) {
                    add(out, "error", "TWW004",
                        "Connection '" + label + "' refers to unknown object '" + *end + "'.", o.id, path + ".geometry");
                }
            }
        }
        if (g.rect && (g.rect->w <= 0 || g.rect->h <= 0)) geometry_error("width and height must be positive.");
        if (g.width < 0) geometry_error("the stroke width cannot be negative.");
        if (o.kind == "image" && layer != nullptr && layer->role != LayerRole::Background) {
            add(out, "error", "TWW008",
                "Image '" + label + "' must be on a background layer: imported images never become semantic geometry.",
                o.id, path + ".layer");
        }
        if (o.kind != "image" && layer != nullptr && layer->role == LayerRole::Background) {
            add(out, "warning", "TWW009",
                "Object '" + label + "' is on a background layer; background layers hold imported images only.", o.id,
                path + ".layer");
        }
        if (w.bounds.w > 0 && w.bounds.h > 0) {
            if (auto bb = bounding_box(o); bb && !inside(w.bounds, *bb)) {
                add(out, "warning", "TWW007", "Object '" + label + "' extends outside the world bounds.", o.id, path);
            }
        }
    }
    return out;
}

json::Json to_json(const std::vector<Finding>& findings) {
    Json out = Json::array();
    for (const Finding& f : findings) {
        out.push_back(Json{{"severity", f.severity}, {"code", f.code}, {"message", f.message}, {"object", f.object}, {"path", f.path}});
    }
    return out;
}

bool has_errors(const std::vector<Finding>& findings) noexcept {
    return std::any_of(findings.begin(), findings.end(), [](const Finding& f) { return f.severity == "error"; });
}

}  // namespace twin::scene
