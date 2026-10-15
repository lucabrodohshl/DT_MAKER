#include "twin/model/document.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

#include "twin/core/sha256.hpp"

namespace twin::model {

using json::Json;

// --------------------------------------------------------------------------- diagnostics

Json to_json(const Diagnostic& d) {
    Json out = {{"severity", d.severity}, {"code", d.code}, {"message", d.message}, {"path", d.path}};
    if (!d.hint.empty()) out["hint"] = d.hint;
    if (!d.element.empty()) out["element"] = d.element;
    return out;
}

Json to_json(const std::vector<Diagnostic>& diagnostics) {
    Json out = Json::array();
    for (const Diagnostic& d : diagnostics) out.push_back(to_json(d));
    return out;
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == "error"; });
}

std::string_view to_string(PropertyKind kind) noexcept {
    switch (kind) {
        case PropertyKind::Measured: return "measured";
        case PropertyKind::Fixed: return "fixed";
        case PropertyKind::PerTwin: return "per-twin";
    }
    return "measured";
}

std::string_view to_string(DataType type) noexcept {
    switch (type) {
        case DataType::Real: return "real";
        case DataType::Integer: return "integer";
        case DataType::Boolean: return "boolean";
        case DataType::Enum: return "enum";
        case DataType::String: return "string";
    }
    return "real";
}

std::string_view to_string(Tone tone) noexcept {
    switch (tone) {
        case Tone::Normal: return "normal";
        case Tone::Warning: return "warning";
        case Tone::Critical: return "critical";
    }
    return "normal";
}

const AssetType* Document::find_type(std::string_view type_name) const noexcept {
    for (const AssetType& t : types) {
        if (t.name == type_name) return &t;
    }
    return nullptr;
}

const RelationshipType* Document::find_relationship_type(std::string_view type_name) const noexcept {
    for (const RelationshipType& r : relationship_types) {
        if (r.name == type_name) return &r;
    }
    return nullptr;
}

bool is_name(std::string_view text) noexcept {
    static constexpr std::array<std::string_view, 6> kKeywords{"and", "or", "not", "true", "false", "self"};
    if (text.empty()) return false;
    const auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!alpha(text.front())) return false;
    for (char c : text) {
        if (!alpha(c) && !digit(c) && c != '_') return false;
    }
    if (text.find("__") != std::string_view::npos) return false;
    return std::find(kKeywords.begin(), kKeywords.end(), text) == kKeywords.end();
}

// --------------------------------------------------------------------------- decoding

namespace {

/// Collects TML001 findings while reading.
struct Ctx {
    std::vector<Diagnostic> diagnostics;
    void error(const std::string& path, const std::string& message, const std::string& element = {}) {
        diagnostics.push_back({"error", "TML001", message, "", path, element});
    }
};

/// A JSON object being read: remembers the keys it consumed to report unknown ones.
class Obj {
public:
    Obj(const Json& j, std::string path, Ctx& ctx) : j_(j), path_(std::move(path)), ctx_(ctx) {
        if (!j_.is_object()) ctx_.error(path_, "Expected an object.");
    }
    Obj(const Obj&) = delete;
    Obj& operator=(const Obj&) = delete;
    Obj(Obj&&) = delete;
    Obj& operator=(Obj&&) = delete;
    ~Obj() = default;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] bool ok() const noexcept { return j_.is_object(); }

    const Json* get(std::string_view key) {
        seen_.insert(std::string(key));
        if (!j_.is_object()) return nullptr;
        const auto it = j_.find(std::string(key));
        if (it == j_.end() || it->is_null()) return nullptr;
        return &*it;
    }

    std::string str(std::string_view key, bool required = false, std::string fallback = {}) {
        const Json* v = get(key);
        if (v == nullptr) {
            if (required && j_.is_object()) ctx_.error(path_ + "." + std::string(key), "Missing required field.");
            return fallback;
        }
        if (!v->is_string()) {
            ctx_.error(path_ + "." + std::string(key), "Expected a string.");
            return fallback;
        }
        return v->get<std::string>();
    }

    /// A decimal written as a string or an integer number; floats are refused (exactness).
    std::optional<std::string> decimal(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr) return std::nullopt;
        if (v->is_string()) return v->get<std::string>();
        if (v->is_number_integer()) return std::to_string(v->get<std::int64_t>());
        if (v->is_boolean()) return v->get<bool>() ? std::string("true") : std::string("false");
        ctx_.error(path_ + "." + std::string(key), "Expected an exact decimal written as a string (floating-point numbers are not exact).");
        return std::nullopt;
    }

    bool boolean(std::string_view key, bool fallback = false) {
        const Json* v = get(key);
        if (v == nullptr) return fallback;
        if (!v->is_boolean()) {
            ctx_.error(path_ + "." + std::string(key), "Expected true or false.");
            return fallback;
        }
        return v->get<bool>();
    }

    std::int64_t integer(std::string_view key, std::int64_t fallback = 0) {
        const Json* v = get(key);
        if (v == nullptr) return fallback;
        if (!v->is_number_integer()) {
            ctx_.error(path_ + "." + std::string(key), "Expected an integer.");
            return fallback;
        }
        return v->get<std::int64_t>();
    }

    const Json* array(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr) return nullptr;
        if (!v->is_array()) {
            ctx_.error(path_ + "." + std::string(key), "Expected an array.");
            return nullptr;
        }
        return v;
    }

    const Json* object(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr) return nullptr;
        if (!v->is_object()) {
            ctx_.error(path_ + "." + std::string(key), "Expected an object.");
            return nullptr;
        }
        return v;
    }

    std::vector<std::string> strings(std::string_view key) {
        std::vector<std::string> out;
        const Json* a = array(key);
        if (a == nullptr) return out;
        for (const Json& e : *a) {
            if (e.is_string()) {
                out.push_back(e.get<std::string>());
            } else {
                ctx_.error(path_ + "." + std::string(key), "Expected an array of strings.");
            }
        }
        return out;
    }

    std::map<std::string, std::string> string_map(std::string_view key) {
        std::map<std::string, std::string> out;
        const Json* o = object(key);
        if (o == nullptr) return out;
        for (const auto& [k, v] : o->items()) {
            if (v.is_string()) {
                out[k] = v.get<std::string>();
            } else if (v.is_number_integer()) {
                out[k] = std::to_string(v.get<std::int64_t>());
            } else if (v.is_boolean()) {
                out[k] = v.get<bool>() ? "true" : "false";
            } else {
                ctx_.error(path_ + "." + std::string(key) + "." + k, "Expected an exact decimal or literal written as a string.");
            }
        }
        return out;
    }

    /// Report keys that were never read.
    void finish() {
        if (!j_.is_object()) return;
        for (const auto& [k, v] : j_.items()) {
            (void)v;
            if (!seen_.contains(k)) ctx_.error(path_ + "." + k, "Unknown field '" + k + "'.");
        }
    }

private:
    const Json& j_;
    std::string path_;
    Ctx& ctx_;
    std::set<std::string> seen_;
};

std::string key_of(const Json& e, std::size_t index) {
    if (e.is_object()) {
        if (const auto it = e.find("name"); it != e.end() && it->is_string()) return it->get<std::string>();
        if (const auto it = e.find("id"); it != e.end() && it->is_string()) return it->get<std::string>();
    }
    return std::to_string(index);
}

/// Iterate an optional array of objects with a path per element.
template <class F>
void each(Obj& parent, std::string_view key, const F& f) {
    const Json* a = parent.array(key);
    if (a == nullptr) return;
    std::size_t i = 0;
    for (const Json& e : *a) {
        f(e, parent.path() + "." + std::string(key) + "[" + key_of(e, i) + "]");
        ++i;
    }
}

std::optional<ir::Comparison> comparison(std::string_view text) { return ir::parse_comparison(text); }

authoring::Bound bound_of(const Json* v, const std::string& path, Ctx& ctx) {
    if (v == nullptr) {
        ctx.error(path, "Missing bound.");
        return authoring::Bound{std::int64_t{0}};
    }
    if (v->is_number_integer()) return authoring::Bound{v->get<std::int64_t>()};
    if (v->is_string()) return authoring::Bound{v->get<std::string>()};
    ctx.error(path, "A bound is an integer or the name of a constant.");
    return authoring::Bound{std::int64_t{0}};
}

TimeAtom atom_of(const Json& j, const std::string& path, Ctx& ctx) {
    Obj o(j, path, ctx);
    TimeAtom a;
    a.timer = o.str("timer", true);
    const std::string m = o.str("minus");
    if (!m.empty()) a.minus = m;
    const std::string op = o.str("op", true, "<=");
    if (const auto c = comparison(op)) {
        a.op = *c;
    } else {
        ctx.error(path + ".op", "Unknown comparison '" + op + "' (use <, <=, ==, >=, >).");
    }
    a.bound = bound_of(o.get("bound"), path + ".bound", ctx);
    o.finish();
    return a;
}

std::vector<TimeAtom> atoms_of(Obj& o, std::string_view key, Ctx& ctx) {
    std::vector<TimeAtom> out;
    const Json* a = o.array(key);
    if (a == nullptr) return out;
    std::size_t i = 0;
    for (const Json& e : *a) out.push_back(atom_of(e, o.path() + "." + std::string(key) + "[" + std::to_string(i++) + "]", ctx));
    return out;
}

Unexpected unexpected_of(Obj& o, Ctx& ctx) {
    const std::string u = o.str("unexpected", false, "nonconformance");
    if (u == "nonconformance") return Unexpected::Nonconformance;
    if (u == "ignore") return Unexpected::Ignore;
    ctx.error(o.path() + ".unexpected", "Expected 'nonconformance' or 'ignore'.");
    return Unexpected::Nonconformance;
}

char direction_of(Obj& o, Ctx& ctx) {
    const std::string d = o.str("direction", false, "!");
    if (d == "!" || d == "?") return d.front();
    ctx.error(o.path() + ".direction", "Expected '!' or '?'.");
    return '!';
}

Property property_of(const Json& j, const std::string& path, Ctx& ctx) {
    Obj o(j, path, ctx);
    Property p;
    p.id = o.str("id", true);
    p.name = o.str("name", true);
    p.label = o.str("label");
    p.description = o.str("description");
    const std::string kind = o.str("kind", false, "measured");
    if (kind == "measured") p.kind = PropertyKind::Measured;
    else if (kind == "fixed") p.kind = PropertyKind::Fixed;
    else if (kind == "per-twin") p.kind = PropertyKind::PerTwin;
    else ctx.error(path + ".kind", "Expected 'measured', 'fixed' or 'per-twin'.", p.id);
    const std::string type = o.str("type", false, "real");
    if (type == "real") p.type = DataType::Real;
    else if (type == "integer") p.type = DataType::Integer;
    else if (type == "boolean") p.type = DataType::Boolean;
    else if (type == "enum") p.type = DataType::Enum;
    else if (type == "string") p.type = DataType::String;
    else ctx.error(path + ".type", "Expected 'real', 'integer', 'boolean', 'enum' or 'string'.", p.id);
    p.unit = o.str("unit");
    p.values = o.strings("values");
    p.value = o.decimal("value");
    if (const Json* r = o.object("range")) {
        Obj ro(*r, path + ".range", ctx);
        p.min = ro.decimal("min");
        p.max = ro.decimal("max");
        ro.finish();
    }
    p.period = o.decimal("period");
    p.sort = o.str("sort");
    o.finish();
    return p;
}

Event event_of(const Json& j, const std::string& path, Ctx& ctx) {
    Obj o(j, path, ctx);
    Event e;
    e.id = o.str("id", true);
    e.name = o.str("name", true);
    e.label = o.str("label");
    e.description = o.str("description");
    const std::string origin = o.str("origin", false, "reported");
    if (origin == "reported") e.origin = EventOrigin::Reported;
    else if (origin == "condition") e.origin = EventOrigin::Condition;
    else ctx.error(path + ".origin", "Expected 'reported' or 'condition'.", e.id);
    e.when = o.str("when");
    e.rearm = o.str("rearm");
    e.meaning = o.str("meaning");
    e.unexpected = unexpected_of(o, ctx);
    e.uninterpreted = o.boolean("uninterpreted");
    e.direction = direction_of(o, ctx);
    o.finish();
    return e;
}

Action action_of(const Json& j, const std::string& path, Ctx& ctx) {
    Obj o(j, path, ctx);
    Action a;
    a.id = o.str("id", true);
    a.name = o.str("name", true);
    a.label = o.str("label");
    a.description = o.str("description");
    a.meaning = o.str("meaning");
    a.unexpected = unexpected_of(o, ctx);
    a.requestable = o.boolean("requestable", true);
    a.direction = direction_of(o, ctx);
    o.finish();
    return a;
}

Part part_of(const Json& j, const std::string& path, Ctx& ctx) {
    Obj o(j, path, ctx);
    Part p;
    p.id = o.str("id", true);
    p.name = o.str("name", true);
    p.label = o.str("label");
    p.description = o.str("description");
    p.type = o.str("type", true);
    p.external = o.boolean("external");
    p.values = o.string_map("values");
    o.finish();
    return p;
}

Trigger trigger_of(const Json* j, const std::string& path, Ctx& ctx) {
    Trigger t;
    if (j == nullptr) {
        ctx.error(path, "Missing trigger.");
        return t;
    }
    Obj o(*j, path, ctx);
    int kinds = 0;
    if (const Json* v = o.get("event")) {
        ++kinds;
        t.kind = TriggerKind::Event;
        if (v->is_string()) t.ref = v->get<std::string>();
        else ctx.error(path + ".event", "Expected an event path.");
    }
    if (const Json* v = o.get("action")) {
        ++kinds;
        t.kind = TriggerKind::Action;
        if (v->is_string()) t.ref = v->get<std::string>();
        else ctx.error(path + ".action", "Expected an action path.");
    }
    if (const Json* v = o.get("timeout")) {
        ++kinds;
        t.kind = TriggerKind::Timeout;
        t.after = bound_of(v, path + ".timeout", ctx);
    }
    if (const Json* v = o.get("internal")) {
        ++kinds;
        t.kind = TriggerKind::Internal;
        if (!v->is_boolean() || !v->get<bool>()) ctx.error(path + ".internal", "Expected true.");
    }
    t.timer = o.str("timer");
    if (kinds != 1) ctx.error(path, "A trigger has exactly one of 'event', 'action', 'timeout' or 'internal'.");
    o.finish();
    return t;
}

Behavior behavior_of(const Json& j, const std::string& path, Ctx& ctx) {
    Obj o(j, path, ctx);
    Behavior b;
    each(o, "timers", [&](const Json& e, const std::string& p) {
        Obj t(e, p, ctx);
        Timer timer;
        timer.id = t.str("id", true);
        timer.name = t.str("name", true);
        const std::string r = t.str("restart", false, "on-state-change");
        if (r == "on-state-change") timer.restart = Restart::OnStateChange;
        else if (r == "explicit") timer.restart = Restart::Explicit;
        else ctx.error(p + ".restart", "Expected 'on-state-change' or 'explicit'.", timer.id);
        timer.description = t.str("description");
        t.finish();
        b.timers.push_back(std::move(timer));
    });
    each(o, "constants", [&](const Json& e, const std::string& p) {
        Obj c(e, p, ctx);
        Constant k;
        k.id = c.str("id", true);
        k.name = c.str("name", true);
        k.value = c.integer("value");
        k.description = c.str("description");
        c.finish();
        b.constants.push_back(std::move(k));
    });
    each(o, "states", [&](const Json& e, const std::string& p) {
        Obj s(e, p, ctx);
        State st;
        st.id = s.str("id", true);
        st.name = s.str("name", true);
        st.label = s.str("label");
        st.description = s.str("description");
        st.initial = s.boolean("initial");
        st.meaning = s.str("meaning");
        st.limits = atoms_of(s, "limits", ctx);
        const std::string tone = s.str("tone", false, "normal");
        if (tone == "normal") st.tone = Tone::Normal;
        else if (tone == "warning") st.tone = Tone::Warning;
        else if (tone == "critical") st.tone = Tone::Critical;
        else ctx.error(p + ".tone", "Expected 'normal', 'warning' or 'critical'.", st.id);
        s.finish();
        b.states.push_back(std::move(st));
    });
    each(o, "groups", [&](const Json& e, const std::string& p) {
        Obj g(e, p, ctx);
        Group gr;
        gr.id = g.str("id", true);
        gr.name = g.str("name", true);
        gr.label = g.str("label");
        gr.states = g.strings("states");
        gr.meaning = g.str("meaning");
        g.finish();
        b.groups.push_back(std::move(gr));
    });
    each(o, "transitions", [&](const Json& e, const std::string& p) {
        Obj t(e, p, ctx);
        Transition tr;
        tr.id = t.str("id", true);
        tr.from = t.strings("from");
        tr.to = t.str("to", true);
        tr.trigger = trigger_of(t.get("trigger"), p + ".trigger", ctx);
        tr.window = atoms_of(t, "window", ctx);
        tr.restart = t.strings("restart");
        tr.description = t.str("description");
        t.finish();
        b.transitions.push_back(std::move(tr));
    });
    o.finish();
    return b;
}

AssetType type_of(const Json& j, const std::string& path, Ctx& ctx) {
    Obj o(j, path, ctx);
    AssetType t;
    t.id = o.str("id", true);
    t.name = o.str("name", true);
    t.label = o.str("label");
    t.description = o.str("description");
    t.extends = o.str("extends");
    t.values = o.string_map("values");
    each(o, "properties", [&](const Json& e, const std::string& p) { t.properties.push_back(property_of(e, p, ctx)); });
    each(o, "events", [&](const Json& e, const std::string& p) { t.events.push_back(event_of(e, p, ctx)); });
    each(o, "actions", [&](const Json& e, const std::string& p) { t.actions.push_back(action_of(e, p, ctx)); });
    each(o, "parts", [&](const Json& e, const std::string& p) { t.parts.push_back(part_of(e, p, ctx)); });
    each(o, "relationships", [&](const Json& e, const std::string& p) {
        Obj r(e, p, ctx);
        Relationship rel;
        rel.id = r.str("id", true);
        rel.type = r.str("type", true);
        rel.from = r.str("from", true);
        rel.to = r.str("to", true);
        r.finish();
        t.relationships.push_back(std::move(rel));
    });
    each(o, "links", [&](const Json& e, const std::string& p) {
        Obj l(e, p, ctx);
        Link link;
        link.id = l.str("id", true);
        link.from = l.str("from", true);
        link.to = l.str("to", true);
        l.finish();
        t.links.push_back(std::move(link));
    });
    each(o, "assumptions", [&](const Json& e, const std::string& p) {
        Obj a(e, p, ctx);
        Assumption as;
        as.id = a.str("id", true);
        as.name = a.str("name", true);
        as.expr = a.str("expr", true);
        as.description = a.str("description");
        a.finish();
        t.assumptions.push_back(std::move(as));
    });
    if (const Json* b = o.object("behavior")) t.behavior = behavior_of(*b, path + ".behavior", ctx);
    o.finish();
    return t;
}

}  // namespace

Decoded decode(const Json& j) {
    Ctx ctx;
    Decoded out;
    Document& d = out.document;
    Obj o(j, "model", ctx);
    if (!o.ok()) {
        out.diagnostics = std::move(ctx.diagnostics);
        return out;
    }
    const std::string format = o.str("format", true);
    if (!format.empty() && format != kFormat) ctx.error("model.format", "Expected format '" + std::string(kFormat) + "', got '" + format + "'.");
    d.name = o.str("name", true);
    d.label = o.str("label");
    d.description = o.str("description");
    if (const Json* t = o.object("time")) {
        Obj to(*t, "model.time", ctx);
        d.time_unit = to.str("unit", false, "s");
        d.ticks_per_unit = to.integer("ticksPerUnit", 1000);
        to.finish();
    }
    d.root = o.str("root", true);
    d.iri_base = o.str("iriBase");
    d.allow_nondeterminism = o.boolean("allowNondeterminism");
    each(o, "types", [&](const Json& e, const std::string& p) { d.types.push_back(type_of(e, p, ctx)); });
    each(o, "relationshipTypes", [&](const Json& e, const std::string& p) {
        Obj r(e, p, ctx);
        RelationshipType rt;
        rt.id = r.str("id", true);
        rt.name = r.str("name", true);
        rt.label = r.str("label");
        rt.inverse = r.str("inverse");
        rt.from = r.strings("from");
        rt.to = r.strings("to");
        rt.symmetric = r.boolean("symmetric");
        rt.description = r.str("description");
        r.finish();
        d.relationship_types.push_back(std::move(rt));
    });
    if (const Json* r = o.object("reference")) {
        Obj ro(*r, "model.reference", ctx);
        Reference ref;
        if (const Json* a = ro.object("automaton")) {
            auto m = authoring::model_from_json(*a);
            if (m) {
                ref.automaton = std::move(m).value();
            } else {
                ctx.error("model.reference.automaton", "Not a twin-ta/1 model: " + m.error().message);
            }
        } else {
            ctx.error("model.reference.automaton", "Missing the physical reference automaton.");
        }
        if (const Json* s = ro.object("states")) {
            for (const auto& [loc, v] : s->items()) {
                Obj so(v, "model.reference.states." + loc, ctx);
                ReferenceState rs;
                rs.states = so.strings("states");
                rs.meaning = so.str("meaning");
                so.finish();
                ref.states[loc] = std::move(rs);
            }
        }
        if (const Json* l = ro.object("labels")) {
            for (const auto& [chan, v] : l->items()) {
                Obj lo(v, "model.reference.labels." + chan, ctx);
                ReferenceLabel rl;
                rl.trigger = lo.str("trigger");
                rl.internal = lo.boolean("internal");
                lo.finish();
                ref.labels[chan] = std::move(rl);
            }
        }
        ro.finish();
        d.reference = std::move(ref);
    }
    each(o, "requirements", [&](const Json& e, const std::string& p) {
        Obj r(e, p, ctx);
        Requirement req;
        req.id = r.str("id", true);
        req.name = r.str("name", true);
        req.title = r.str("title");
        req.kind = r.str("kind", true);
        req.state = r.str("state");
        req.property = r.str("property");
        req.severity = r.str("severity", false, "warning");
        req.description = r.str("description");
        r.finish();
        d.requirements.push_back(std::move(req));
    });
    d.declarations = o.strings("declarations");
    if (const Json* l = o.object("layout")) d.layout = *l;
    o.finish();
    out.diagnostics = std::move(ctx.diagnostics);
    return out;
}

// --------------------------------------------------------------------------- encoding

namespace {

void put(Json& o, const char* key, const std::string& v) {
    if (!v.empty()) o[key] = v;
}

Json bound_json(const authoring::Bound& b) {
    if (const auto* i = std::get_if<std::int64_t>(&b.value)) return *i;
    return std::get<std::string>(b.value);
}

Json atom_json(const TimeAtom& a) {
    Json o = {{"timer", a.timer}, {"op", std::string(ir::to_string(a.op))}, {"bound", bound_json(a.bound)}};
    if (a.minus) o["minus"] = *a.minus;
    return o;
}

Json atoms_json(const std::vector<TimeAtom>& atoms) {
    Json a = Json::array();
    for (const TimeAtom& t : atoms) a.push_back(atom_json(t));
    return a;
}

Json property_json(const Property& p) {
    Json o = {{"id", p.id}, {"name", p.name}, {"kind", std::string(to_string(p.kind))}, {"type", std::string(to_string(p.type))}};
    put(o, "label", p.label);
    put(o, "description", p.description);
    put(o, "unit", p.unit);
    if (!p.values.empty()) o["values"] = p.values;
    if (p.value) o["value"] = *p.value;
    if (p.min || p.max) {
        Json r = Json::object();
        if (p.min) r["min"] = *p.min;
        if (p.max) r["max"] = *p.max;
        o["range"] = r;
    }
    if (p.period) o["period"] = *p.period;
    put(o, "sort", p.sort);
    return o;
}

Json event_json(const Event& e) {
    Json o = {{"id", e.id}, {"name", e.name}, {"origin", e.origin == EventOrigin::Condition ? "condition" : "reported"}};
    put(o, "label", e.label);
    put(o, "description", e.description);
    put(o, "when", e.when);
    put(o, "rearm", e.rearm);
    put(o, "meaning", e.meaning);
    if (e.unexpected == Unexpected::Ignore) o["unexpected"] = "ignore";
    if (e.uninterpreted) o["uninterpreted"] = true;
    if (e.direction != '!') o["direction"] = std::string(1, e.direction);
    return o;
}

Json action_json(const Action& a) {
    Json o = {{"id", a.id}, {"name", a.name}};
    put(o, "label", a.label);
    put(o, "description", a.description);
    put(o, "meaning", a.meaning);
    if (a.unexpected == Unexpected::Ignore) o["unexpected"] = "ignore";
    if (!a.requestable) o["requestable"] = false;
    if (a.direction != '!') o["direction"] = std::string(1, a.direction);
    return o;
}

Json behavior_json(const Behavior& b) {
    Json o = Json::object();
    if (!b.timers.empty()) {
        Json a = Json::array();
        for (const Timer& t : b.timers) {
            Json x = {{"id", t.id}, {"name", t.name}};
            if (t.restart == Restart::Explicit) x["restart"] = "explicit";
            put(x, "description", t.description);
            a.push_back(std::move(x));
        }
        o["timers"] = std::move(a);
    }
    if (!b.constants.empty()) {
        Json a = Json::array();
        for (const Constant& c : b.constants) {
            Json x = {{"id", c.id}, {"name", c.name}, {"value", c.value}};
            put(x, "description", c.description);
            a.push_back(std::move(x));
        }
        o["constants"] = std::move(a);
    }
    Json states = Json::array();
    for (const State& s : b.states) {
        Json x = {{"id", s.id}, {"name", s.name}};
        put(x, "label", s.label);
        put(x, "description", s.description);
        if (s.initial) x["initial"] = true;
        put(x, "meaning", s.meaning);
        if (!s.limits.empty()) x["limits"] = atoms_json(s.limits);
        if (s.tone != Tone::Normal) x["tone"] = std::string(to_string(s.tone));
        states.push_back(std::move(x));
    }
    o["states"] = std::move(states);
    if (!b.groups.empty()) {
        Json a = Json::array();
        for (const Group& g : b.groups) {
            Json x = {{"id", g.id}, {"name", g.name}, {"states", g.states}};
            put(x, "label", g.label);
            put(x, "meaning", g.meaning);
            a.push_back(std::move(x));
        }
        o["groups"] = std::move(a);
    }
    Json transitions = Json::array();
    for (const Transition& t : b.transitions) {
        Json trig = Json::object();
        switch (t.trigger.kind) {
            case TriggerKind::Event: trig["event"] = t.trigger.ref; break;
            case TriggerKind::Action: trig["action"] = t.trigger.ref; break;
            case TriggerKind::Timeout: trig["timeout"] = bound_json(t.trigger.after); break;
            case TriggerKind::Internal: trig["internal"] = true; break;
        }
        if (!t.trigger.timer.empty()) trig["timer"] = t.trigger.timer;
        Json x = {{"id", t.id}, {"from", t.from}, {"to", t.to}, {"trigger", trig}};
        if (!t.window.empty()) x["window"] = atoms_json(t.window);
        if (!t.restart.empty()) x["restart"] = t.restart;
        put(x, "description", t.description);
        transitions.push_back(std::move(x));
    }
    o["transitions"] = std::move(transitions);
    return o;
}

Json type_json(const AssetType& t) {
    Json o = {{"id", t.id}, {"name", t.name}};
    put(o, "label", t.label);
    put(o, "description", t.description);
    put(o, "extends", t.extends);
    if (!t.values.empty()) o["values"] = t.values;
    if (!t.properties.empty()) {
        Json a = Json::array();
        for (const Property& p : t.properties) a.push_back(property_json(p));
        o["properties"] = std::move(a);
    }
    if (!t.events.empty()) {
        Json a = Json::array();
        for (const Event& e : t.events) a.push_back(event_json(e));
        o["events"] = std::move(a);
    }
    if (!t.actions.empty()) {
        Json a = Json::array();
        for (const Action& x : t.actions) a.push_back(action_json(x));
        o["actions"] = std::move(a);
    }
    if (!t.parts.empty()) {
        Json a = Json::array();
        for (const Part& p : t.parts) {
            Json x = {{"id", p.id}, {"name", p.name}, {"type", p.type}};
            put(x, "label", p.label);
            put(x, "description", p.description);
            if (p.external) x["external"] = true;
            if (!p.values.empty()) x["values"] = p.values;
            a.push_back(std::move(x));
        }
        o["parts"] = std::move(a);
    }
    if (!t.relationships.empty()) {
        Json a = Json::array();
        for (const Relationship& r : t.relationships) a.push_back({{"id", r.id}, {"type", r.type}, {"from", r.from}, {"to", r.to}});
        o["relationships"] = std::move(a);
    }
    if (!t.links.empty()) {
        Json a = Json::array();
        for (const Link& l : t.links) a.push_back({{"id", l.id}, {"from", l.from}, {"to", l.to}});
        o["links"] = std::move(a);
    }
    if (!t.assumptions.empty()) {
        Json a = Json::array();
        for (const Assumption& x : t.assumptions) {
            Json y = {{"id", x.id}, {"name", x.name}, {"expr", x.expr}};
            put(y, "description", x.description);
            a.push_back(std::move(y));
        }
        o["assumptions"] = std::move(a);
    }
    if (t.behavior) o["behavior"] = behavior_json(*t.behavior);
    return o;
}

}  // namespace

Json encode(const Document& d) {
    Json o = {{"format", std::string(kFormat)},
              {"name", d.name},
              {"root", d.root},
              {"time", {{"unit", d.time_unit}, {"ticksPerUnit", d.ticks_per_unit}}}};
    put(o, "label", d.label);
    put(o, "description", d.description);
    put(o, "iriBase", d.iri_base);
    if (d.allow_nondeterminism) o["allowNondeterminism"] = true;
    Json types = Json::array();
    for (const AssetType& t : d.types) types.push_back(type_json(t));
    o["types"] = std::move(types);
    if (!d.relationship_types.empty()) {
        Json a = Json::array();
        for (const RelationshipType& r : d.relationship_types) {
            Json x = {{"id", r.id}, {"name", r.name}};
            put(x, "label", r.label);
            put(x, "inverse", r.inverse);
            if (!r.from.empty()) x["from"] = r.from;
            if (!r.to.empty()) x["to"] = r.to;
            if (r.symmetric) x["symmetric"] = true;
            put(x, "description", r.description);
            a.push_back(std::move(x));
        }
        o["relationshipTypes"] = std::move(a);
    }
    if (d.reference) {
        Json states = Json::object();
        for (const auto& [loc, s] : d.reference->states) {
            Json x = Json::object();
            if (!s.states.empty()) x["states"] = s.states;
            put(x, "meaning", s.meaning);
            states[loc] = std::move(x);
        }
        Json labels = Json::object();
        for (const auto& [chan, l] : d.reference->labels) {
            Json x = Json::object();
            put(x, "trigger", l.trigger);
            if (l.internal) x["internal"] = true;
            labels[chan] = std::move(x);
        }
        o["reference"] = {{"automaton", authoring::to_json(d.reference->automaton)}, {"states", states}, {"labels", labels}};
    }
    if (!d.requirements.empty()) {
        Json a = Json::array();
        for (const Requirement& r : d.requirements) {
            Json x = {{"id", r.id}, {"name", r.name}, {"kind", r.kind}, {"severity", r.severity}};
            put(x, "title", r.title);
            put(x, "state", r.state);
            put(x, "property", r.property);
            put(x, "description", r.description);
            a.push_back(std::move(x));
        }
        o["requirements"] = std::move(a);
    }
    if (!d.declarations.empty()) o["declarations"] = d.declarations;
    if (!d.layout.empty()) o["layout"] = d.layout;
    return o;
}

namespace {

/// Remove presentation-only members recursively (labels, descriptions, ids, layout, tones).
void strip(Json& j) {
    if (j.is_object()) {
        for (const char* k : {"label", "description", "id", "layout", "tone", "title"}) j.erase(k);
        for (auto& [k, v] : j.items()) {
            (void)k;
            strip(v);
        }
    } else if (j.is_array()) {
        for (Json& e : j) strip(e);
    }
}

}  // namespace

Json semantic_projection(const Document& document) {
    Json j = encode(document);
    // The reference automaton keeps its own notes out of the semantics as well.
    if (document.reference) {
        authoring::Model m = document.reference->automaton;
        m.note.clear();
        for (auto& c : m.clocks) c.note.clear();
        for (auto& c : m.constants) c.note.clear();
        for (auto& c : m.channels) c.note.clear();
        for (auto& l : m.locations) l.note.clear();
        for (auto& e : m.edges) e.note.clear();
        j["reference"]["automaton"] = authoring::to_json(m);
    }
    strip(j);
    return j;
}

std::string semantic_digest(const Document& document) {
    const auto dumped = json::canonical_dump(semantic_projection(document));
    return dumped ? twin::sha256_hex(dumped.value()) : std::string{};
}

}  // namespace twin::model
