/**
 * @file model.cpp
 * @brief twin-ta/1 codec: strict decoding with located errors, canonical encoding.
 */
#include "twin/authoring/model.hpp"

#include <string>
#include <utility>

namespace twin::authoring {
namespace {

/// Prefix an error with the JSON path of the member being decoded.
Error at(Error e, const std::string& path) {
    e.with("path", path);
    return e;
}

Error invalid(const std::string& path, std::string message) {
    return make_error(ErrorCode::ValidationError, std::move(message)).with("path", path);
}

Result<std::string> string_member(const json::Json& o, std::string_view key, const std::string& path) {
    Result<std::string> s = json::get_string(o, key);
    if (!s) return at(std::move(s).error(), path);
    return s;
}

json::Json encode_bound(const Bound& b) {
    if (const auto* v = std::get_if<std::int64_t>(&b.value)) return *v;
    return std::get<std::string>(b.value);
}

json::Json encode_constraint(const Constraint& c) {
    json::Json out = json::Json::array();
    for (const Atom& a : c) {
        json::Json j{{"clock", a.clock}, {"op", std::string(ir::to_string(a.op))}, {"bound", encode_bound(a.bound)}};
        if (a.minus) j["minus"] = *a.minus;
        out.push_back(std::move(j));
    }
    return out;
}

Result<Constraint> decode_constraint(const json::Json& j, const std::string& path) {
    if (!j.is_array()) return invalid(path, "expected an array of atoms");
    Constraint out;
    for (std::size_t i = 0; i < j.size(); ++i) {
        const std::string p = path + "[" + std::to_string(i) + "]";
        const json::Json& a = j[i];
        if (Status s = json::expect_keys(a, {"clock", "op", "bound"}, {"minus"}); !s) return at(std::move(s).error(), p);
        Atom atom;
        Result<std::string> clock = string_member(a, "clock", p);
        if (!clock) return std::move(clock).error();
        atom.clock = std::move(clock).value();
        if (a.contains("minus")) {
            Result<std::string> minus = string_member(a, "minus", p);
            if (!minus) return std::move(minus).error();
            atom.minus = std::move(minus).value();
        }
        Result<std::string> op = string_member(a, "op", p);
        if (!op) return std::move(op).error();
        const std::optional<ir::Comparison> cmp = ir::parse_comparison(op.value());
        if (!cmp) return invalid(p + ".op", "unknown comparison '" + op.value() + "' (use <, <=, ==, >=, >)");
        atom.op = *cmp;
        const json::Json& b = a.at("bound");
        if (b.is_number_integer() || b.is_number_unsigned()) {
            Result<std::int64_t> v = json::get_int(a, "bound");
            if (!v) return at(std::move(v).error(), p);
            atom.bound = Bound{v.value()};
        } else if (b.is_string()) {
            atom.bound = Bound{b.get<std::string>()};
        } else {
            return invalid(p + ".bound", "a bound is an integer or the name of an integer constant");
        }
        out.push_back(std::move(atom));
    }
    return out;
}

/// Decode an array of {name, note} declarations (clocks, channels).
template <class Decl>
Result<std::vector<Decl>> decode_named(const json::Json& doc, std::string_view key) {
    const std::string path(key);
    Result<const json::Json*> arr = json::get_array(doc, key);
    if (!arr) return at(std::move(arr).error(), path);
    std::vector<Decl> out;
    for (std::size_t i = 0; i < arr.value()->size(); ++i) {
        const std::string p = path + "[" + std::to_string(i) + "]";
        const json::Json& d = (*arr.value())[i];
        if (Status s = json::expect_keys(d, {"name", "note"}); !s) return at(std::move(s).error(), p);
        Result<std::string> name = string_member(d, "name", p);
        if (!name) return std::move(name).error();
        Result<std::string> note = string_member(d, "note", p);
        if (!note) return std::move(note).error();
        out.push_back(Decl{std::move(name).value(), std::move(note).value()});
    }
    return out;
}

Result<ConstantDecl> decode_constant(const json::Json& c, const std::string& p) {
    if (Status s = json::expect_keys(c, {"name", "value", "note"}); !s) return at(std::move(s).error(), p);
    Result<std::string> name = string_member(c, "name", p);
    if (!name) return std::move(name).error();
    Result<std::int64_t> value = json::get_int(c, "value");
    if (!value) return at(std::move(value).error(), p);
    Result<std::string> note = string_member(c, "note", p);
    if (!note) return std::move(note).error();
    return ConstantDecl{std::move(name).value(), value.value(), std::move(note).value()};
}

Result<LocationDecl> decode_location(const json::Json& l, const std::string& p) {
    if (Status s = json::expect_keys(l, {"name", "initial", "invariant", "note"}); !s) return at(std::move(s).error(), p);
    LocationDecl loc;
    Result<std::string> name = string_member(l, "name", p);
    if (!name) return std::move(name).error();
    loc.name = std::move(name).value();
    Result<bool> initial = json::get_bool(l, "initial");
    if (!initial) return at(std::move(initial).error(), p);
    loc.initial = initial.value();
    Result<Constraint> inv = decode_constraint(l.at("invariant"), p + ".invariant");
    if (!inv) return std::move(inv).error();
    loc.invariant = std::move(inv).value();
    Result<std::string> note = string_member(l, "note", p);
    if (!note) return std::move(note).error();
    loc.note = std::move(note).value();
    return loc;
}

Result<std::optional<Sync>> decode_sync(const json::Json& sync, const std::string& p) {
    if (sync.is_null()) return std::optional<Sync>{};
    if (Status s = json::expect_keys(sync, {"channel", "direction"}); !s) return at(std::move(s).error(), p);
    Result<std::string> channel = string_member(sync, "channel", p);
    if (!channel) return std::move(channel).error();
    Result<std::string> direction = string_member(sync, "direction", p);
    if (!direction) return std::move(direction).error();
    if (direction.value() != "!" && direction.value() != "?") {
        return invalid(p + ".direction", R"(direction must be "!" or "?")");
    }
    return std::optional<Sync>{Sync{std::move(channel).value(), direction.value()[0]}};
}

Result<EdgeDecl> decode_edge(const json::Json& e, const std::string& p) {
    if (Status s = json::expect_keys(e, {"id", "source", "target", "sync", "guard", "resets", "note"}); !s) {
        return at(std::move(s).error(), p);
    }
    EdgeDecl edge;
    for (auto [key, field] : {std::pair{"id", &edge.id}, std::pair{"source", &edge.source},
                              std::pair{"target", &edge.target}, std::pair{"note", &edge.note}}) {
        Result<std::string> v = string_member(e, key, p);
        if (!v) return std::move(v).error();
        *field = std::move(v).value();
    }
    Result<std::optional<Sync>> sync = decode_sync(e.at("sync"), p + ".sync");
    if (!sync) return std::move(sync).error();
    edge.sync = std::move(sync).value();
    Result<Constraint> guard = decode_constraint(e.at("guard"), p + ".guard");
    if (!guard) return std::move(guard).error();
    edge.guard = std::move(guard).value();
    const json::Json& resets = e.at("resets");
    if (!resets.is_array()) return invalid(p + ".resets", "expected an array of clock names");
    for (const json::Json& r : resets) {
        if (!r.is_string()) return invalid(p + ".resets", "expected an array of clock names");
        edge.resets.push_back(r.get<std::string>());
    }
    return edge;
}

/// Decode the array @p key of @p doc element by element with @p decode.
template <class T, class Fn>
Result<std::vector<T>> decode_list(const json::Json& doc, std::string_view key, Fn decode) {
    Result<const json::Json*> arr = json::get_array(doc, key);
    if (!arr) return at(std::move(arr).error(), std::string(key));
    std::vector<T> out;
    for (std::size_t i = 0; i < arr.value()->size(); ++i) {
        Result<T> item = decode((*arr.value())[i], std::string(key) + "[" + std::to_string(i) + "]");
        if (!item) return std::move(item).error();
        out.push_back(std::move(item).value());
    }
    return out;
}

}  // namespace

std::string edge_label(const EdgeDecl& edge) {
    if (!edge.sync) return std::string(ir::kTauLabel);
    return edge.sync->channel + std::string(1, edge.sync->direction);
}

const LocationDecl* initial_location(const Model& model) noexcept {
    const LocationDecl* found = nullptr;
    for (const LocationDecl& l : model.locations) {
        if (!l.initial) continue;
        if (found != nullptr) return nullptr;
        found = &l;
    }
    return found;
}

json::Json to_json(const Model& m) {
    json::Json clocks = json::Json::array();
    for (const ClockDecl& c : m.clocks) clocks.push_back({{"name", c.name}, {"note", c.note}});
    json::Json constants = json::Json::array();
    for (const ConstantDecl& c : m.constants) {
        constants.push_back({{"name", c.name}, {"value", c.value}, {"note", c.note}});
    }
    json::Json channels = json::Json::array();
    for (const ChannelDecl& c : m.channels) channels.push_back({{"name", c.name}, {"note", c.note}});
    json::Json locations = json::Json::array();
    for (const LocationDecl& l : m.locations) {
        locations.push_back({{"name", l.name},
                             {"initial", l.initial},
                             {"invariant", encode_constraint(l.invariant)},
                             {"note", l.note}});
    }
    json::Json edges = json::Json::array();
    for (const EdgeDecl& e : m.edges) {
        json::Json sync = nullptr;
        if (e.sync) sync = json::Json{{"channel", e.sync->channel}, {"direction", std::string(1, e.sync->direction)}};
        edges.push_back({{"id", e.id},
                         {"source", e.source},
                         {"target", e.target},
                         {"sync", sync},
                         {"guard", encode_constraint(e.guard)},
                         {"resets", e.resets},
                         {"note", e.note}});
    }
    return json::Json{{"format", std::string(kModelFormat)},
                      {"name", m.name},
                      {"note", m.note},
                      {"clocks", clocks},
                      {"constants", constants},
                      {"channels", channels},
                      {"locations", locations},
                      {"edges", edges}};
}

Result<Model> model_from_json(const json::Json& doc) {
    if (Status s = json::expect_keys(doc, {"format", "name", "note", "clocks", "constants", "channels", "locations",
                                           "edges"});
        !s) {
        return at(std::move(s).error(), "$");
    }
    Result<std::string> format = string_member(doc, "format", "$");
    if (!format) return std::move(format).error();
    if (format.value() != kModelFormat) {
        return invalid("$.format", "unsupported model format '" + format.value() + "' (expected twin-ta/1)");
    }
    Model m;
    Result<std::string> name = string_member(doc, "name", "$");
    if (!name) return std::move(name).error();
    m.name = std::move(name).value();
    Result<std::string> note = string_member(doc, "note", "$");
    if (!note) return std::move(note).error();
    m.note = std::move(note).value();

    Result<std::vector<ClockDecl>> clocks = decode_named<ClockDecl>(doc, "clocks");
    if (!clocks) return std::move(clocks).error();
    m.clocks = std::move(clocks).value();
    Result<std::vector<ChannelDecl>> channels = decode_named<ChannelDecl>(doc, "channels");
    if (!channels) return std::move(channels).error();
    m.channels = std::move(channels).value();
    Result<std::vector<ConstantDecl>> constants = decode_list<ConstantDecl>(doc, "constants", decode_constant);
    if (!constants) return std::move(constants).error();
    m.constants = std::move(constants).value();
    Result<std::vector<LocationDecl>> locations = decode_list<LocationDecl>(doc, "locations", decode_location);
    if (!locations) return std::move(locations).error();
    m.locations = std::move(locations).value();
    Result<std::vector<EdgeDecl>> edges = decode_list<EdgeDecl>(doc, "edges", decode_edge);
    if (!edges) return std::move(edges).error();
    m.edges = std::move(edges).value();
    return m;
}

json::Json constraint_to_json(const Constraint& constraint) { return encode_constraint(constraint); }

Result<Constraint> constraint_from_json(const json::Json& atoms) { return decode_constraint(atoms, "atoms"); }

std::string content_sha256(const Model& model) {
    Result<std::string> h = json::canonical_sha256(to_json(model));
    // to_json produces only strings, integers, booleans and null: always canonical-safe.
    return h ? h.value() : std::string();
}

}  // namespace twin::authoring
