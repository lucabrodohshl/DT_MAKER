/**
 * @file layout.cpp
 * @brief twin-ta-layout/1 codec (presentation only; never hashed into evidence).
 */
#include "twin/authoring/layout.hpp"

#include <string>
#include <utility>

namespace twin::authoring {
namespace {

json::Json encode_point(const Point& p) { return json::Json{{"x", p.x}, {"y", p.y}}; }

Error with_path(Error e, const std::string& path) {
    e.with("path", path);
    return e;
}

Result<Point> decode_point(const json::Json& j, const std::string& path) {
    if (Status s = json::expect_keys(j, {"x", "y"}); !s) return with_path(std::move(s).error(), path);
    Result<std::int64_t> x = json::get_int(j, "x");
    if (!x) return with_path(std::move(x).error(), path);
    Result<std::int64_t> y = json::get_int(j, "y");
    if (!y) return with_path(std::move(y).error(), path);
    return Point{x.value(), y.value()};
}

}  // namespace

json::Json to_json(const Layout& layout) {
    json::Json locations = json::Json::object();
    for (const auto& [name, l] : layout.locations) {
        json::Json j = encode_point(l.position);
        if (l.label) j["label"] = encode_point(*l.label);
        locations[name] = std::move(j);
    }
    json::Json edges = json::Json::object();
    for (const auto& [id, e] : layout.edges) {
        json::Json nails = json::Json::array();
        for (const Point& p : e.nails) nails.push_back(encode_point(p));
        json::Json j{{"nails", nails}};
        if (e.label) j["label"] = encode_point(*e.label);
        edges[id] = std::move(j);
    }
    return json::Json{{"format", std::string(kLayoutFormat)}, {"locations", locations}, {"edges", edges}};
}

Result<Layout> layout_from_json(const json::Json& doc) {
    if (Status s = json::expect_keys(doc, {"format", "locations", "edges"}); !s) return std::move(s).error();
    if (!doc.at("format").is_string() || doc.at("format").get<std::string>() != kLayoutFormat) {
        return make_error(ErrorCode::ValidationError, "unsupported layout format (expected twin-ta-layout/1)");
    }
    if (!doc.at("locations").is_object() || !doc.at("edges").is_object()) {
        return make_error(ErrorCode::ValidationError, "locations and edges must be objects");
    }
    Layout out;
    for (const auto& [name, j] : doc.at("locations").items()) {
        const std::string path = "locations." + name;
        if (Status s = json::expect_keys(j, {"x", "y"}, {"label"}); !s) return with_path(std::move(s).error(), path);
        Result<std::int64_t> x = json::get_int(j, "x");
        if (!x) return with_path(std::move(x).error(), path);
        Result<std::int64_t> y = json::get_int(j, "y");
        if (!y) return with_path(std::move(y).error(), path);
        LocationLayout l{Point{x.value(), y.value()}, std::nullopt};
        if (j.contains("label")) {
            Result<Point> p = decode_point(j.at("label"), path + ".label");
            if (!p) return std::move(p).error();
            l.label = p.value();
        }
        out.locations.emplace(name, l);
    }
    for (const auto& [id, j] : doc.at("edges").items()) {
        const std::string path = "edges." + id;
        if (Status s = json::expect_keys(j, {"nails"}, {"label"}); !s) return with_path(std::move(s).error(), path);
        if (!j.at("nails").is_array()) {
            return with_path(make_error(ErrorCode::ValidationError, "nails must be an array"), path);
        }
        EdgeLayout e;
        for (std::size_t i = 0; i < j.at("nails").size(); ++i) {
            Result<Point> p = decode_point(j.at("nails")[i], path + ".nails[" + std::to_string(i) + "]");
            if (!p) return std::move(p).error();
            e.nails.push_back(p.value());
        }
        if (j.contains("label")) {
            Result<Point> p = decode_point(j.at("label"), path + ".label");
            if (!p) return std::move(p).error();
            e.label = p.value();
        }
        out.edges.emplace(id, std::move(e));
    }
    return out;
}

}  // namespace twin::authoring
