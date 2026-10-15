// Validation of a twin model (04-compilation.md, section 4.7): names, references,
// values, expressions, relationships, links and behaviour well-formedness. Checks that
// need the compiled machines (determinism, timeouts) live in behavior.cpp.
#include "validate.hpp"

#include <algorithm>
#include <set>

#include "twin/model/expression.hpp"

namespace twin::model::detail {

namespace {

class Checker {
public:
    Checker(const Document& d, const Resolved& r, std::vector<Diagnostic>& out) : d_(d), r_(r), out_(out) {}

    void run() {
        ids();
        if (!is_name(d_.name)) err("TML002", "The model name '" + d_.name + "' is not a valid name.", "Use letters, digits and single underscores, starting with a letter.", "model.name");
        std::set<std::string> type_names;
        for (const AssetType& t : d_.types) {
            const std::string p = "types[" + t.name + "]";
            name(t.name, p, t.id, "type");
            if (!type_names.insert(t.name).second) err("TML003", "Two types are named '" + t.name + "'.", "", p, t.id);
            if (!t.extends.empty() && d_.find_type(t.extends) == nullptr) err("TML004", "Unknown supertype '" + t.extends + "'.", "", p + ".extends", t.id);
            type(t, p);
        }
        std::set<std::string> rel_names;
        for (const RelationshipType& rt : d_.relationship_types) {
            const std::string p = "relationshipTypes[" + rt.name + "]";
            name(rt.name, p, rt.id, "relationship type");
            if (!rel_names.insert(rt.name).second) err("TML003", "Two relationship types are named '" + rt.name + "'.", "", p, rt.id);
            if (!rt.inverse.empty() && !is_name(rt.inverse)) err("TML002", "The inverse name '" + rt.inverse + "' is not a valid name.", "", p + ".inverse", rt.id);
            for (const auto& list : {rt.from, rt.to}) {
                for (const std::string& tn : list) {
                    if (d_.find_type(tn) == nullptr) err("TML004", "Unknown type '" + tn + "' in the allowed end types.", "", p, rt.id);
                }
            }
        }
        if (d_.ticks_per_unit <= 0 || !power_of_ten(d_.ticks_per_unit)) err("TML010", "Ticks per time unit must be a power of ten.", "", "model.time.ticksPerUnit");
        requirements();
    }

private:
    void err(const char* code, const std::string& message, const std::string& hint, const std::string& path, const std::string& element = {}) {
        out_.push_back({"error", code, message, hint, path, element});
    }
    void warn(const char* code, const std::string& message, const std::string& hint, const std::string& path, const std::string& element = {}) {
        out_.push_back({"warning", code, message, hint, path, element});
    }
    void info(const char* code, const std::string& message, const std::string& path, const std::string& element = {}) {
        out_.push_back({"info", code, message, "", path, element});
    }

    static bool power_of_ten(std::int64_t v) {
        while (v > 1 && v % 10 == 0) v /= 10;
        return v == 1;
    }

    void name(const std::string& n, const std::string& path, const std::string& id, const char* what) {
        if (!is_name(n)) {
            err("TML002", std::string("The ") + what + " name '" + n + "' is not valid.",
                "Use letters, digits and single underscores, starting with a letter; and, or, not, true, false and self are reserved.", path, id);
        }
    }

    void ids() {
        std::set<std::string> seen;
        const auto see = [&](const std::string& id, const std::string& path) {
            if (id.empty()) return;  // reported by decoding
            if (!seen.insert(id).second) err("TML003", "The element id '" + id + "' is used twice.", "Element ids must be unique in the model.", path, id);
        };
        for (const AssetType& t : d_.types) {
            const std::string p = "types[" + t.name + "]";
            see(t.id, p);
            for (const auto& x : t.properties) see(x.id, p + ".properties[" + x.name + "]");
            for (const auto& x : t.events) see(x.id, p + ".events[" + x.name + "]");
            for (const auto& x : t.actions) see(x.id, p + ".actions[" + x.name + "]");
            for (const auto& x : t.parts) see(x.id, p + ".parts[" + x.name + "]");
            for (const auto& x : t.relationships) see(x.id, p + ".relationships[" + x.id + "]");
            for (const auto& x : t.links) see(x.id, p + ".links[" + x.id + "]");
            for (const auto& x : t.assumptions) see(x.id, p + ".assumptions[" + x.name + "]");
            if (t.behavior) {
                for (const auto& x : t.behavior->timers) see(x.id, p + ".behavior.timers[" + x.name + "]");
                for (const auto& x : t.behavior->constants) see(x.id, p + ".behavior.constants[" + x.name + "]");
                for (const auto& x : t.behavior->states) see(x.id, p + ".behavior.states[" + x.name + "]");
                for (const auto& x : t.behavior->groups) see(x.id, p + ".behavior.groups[" + x.name + "]");
                for (const auto& x : t.behavior->transitions) see(x.id, p + ".behavior.transitions[" + x.id + "]");
            }
        }
        for (const auto& x : d_.relationship_types) see(x.id, "relationshipTypes[" + x.name + "]");
        for (const auto& x : d_.requirements) see(x.id, "requirements[" + x.name + "]");
    }

    // ------------------------------------------------------------------ values

    bool valid_value(const Property& p, const std::string& v, std::string& why) const {
        switch (p.type) {
            case DataType::Real:
                if (!expr::is_decimal(v)) {
                    why = "'" + v + "' is not an exact decimal.";
                    return false;
                }
                break;
            case DataType::Integer:
                if (!expr::is_decimal(v) || v.find('.') != std::string::npos) {
                    why = "'" + v + "' is not an integer.";
                    return false;
                }
                break;
            case DataType::Boolean:
                if (v != "true" && v != "false") {
                    why = "'" + v + "' is not true or false.";
                    return false;
                }
                break;
            case DataType::Enum:
                if (std::find(p.values.begin(), p.values.end(), v) == p.values.end()) {
                    why = "'" + v + "' is not one of the values of '" + p.name + "'.";
                    return false;
                }
                break;
            case DataType::String: break;
        }
        if ((p.type == DataType::Real || p.type == DataType::Integer) && expr::is_decimal(v)) {
            if (p.min && expr::is_decimal(*p.min) && compare_decimal(v, *p.min) < 0) {
                why = v + " is below the minimum " + *p.min + ".";
                return false;
            }
            if (p.max && expr::is_decimal(*p.max) && compare_decimal(v, *p.max) > 0) {
                why = v + " is above the maximum " + *p.max + ".";
                return false;
            }
        }
        return true;
    }

    void property(const Property& p, const std::string& path) {
        name(p.name, path, p.id, "property");
        if (p.type == DataType::Enum) {
            if (p.values.empty()) err("TML010", "Enumeration '" + p.name + "' has no values.", "List its values, e.g. OFF, AUTO, MANUAL.", path, p.id);
            std::set<std::string> lits;
            for (const std::string& v : p.values) {
                if (!is_name(v)) err("TML002", "The enumeration value '" + v + "' is not a valid name.", "", path + ".values", p.id);
                if (!lits.insert(v).second) err("TML003", "The enumeration value '" + v + "' appears twice.", "", path + ".values", p.id);
            }
        } else if (!p.values.empty()) {
            err("TML010", "Only enumerations have a list of values.", "", path + ".values", p.id);
        }
        for (const auto& [which, v] : {std::pair{"minimum", p.min}, std::pair{"maximum", p.max}}) {
            if (v && !expr::is_decimal(*v)) err("TML010", std::string("The ") + which + " '" + *v + "' is not an exact decimal.", "", path + ".range", p.id);
        }
        if ((p.min || p.max) && p.type != DataType::Real && p.type != DataType::Integer) err("TML010", "Only numbers have a range.", "", path + ".range", p.id);
        if (p.min && p.max && expr::is_decimal(*p.min) && expr::is_decimal(*p.max) && compare_decimal(*p.min, *p.max) > 0) {
            err("TML010", "The range minimum is above the maximum.", "", path + ".range", p.id);
        }
        if (p.period) {
            if (p.kind != PropertyKind::Measured) err("TML010", "Only measured properties have an update period.", "", path + ".period", p.id);
            if (!expr::is_decimal(*p.period) || p.period->front() == '-' || compare_decimal(*p.period, "0") <= 0) {
                err("TML010", "The update period must be a positive number of seconds.", "", path + ".period", p.id);
            }
        }
        if (p.kind == PropertyKind::Fixed) {
            if (!p.value) {
                err("TML010", "Fixed property '" + p.name + "' has no value.", "Give it a value, or make it measured.", path + ".value", p.id);
            } else {
                std::string why;
                if (!valid_value(p, *p.value, why)) err("TML010", "Value of '" + p.name + "': " + why, "", path + ".value", p.id);
            }
        } else if (p.value) {
            err("TML010", "Only fixed properties have a value in the model.", p.kind == PropertyKind::PerTwin ? "Per-twin values are entered when a twin is created." : "",
                path + ".value", p.id);
        }
    }

    void overrides(const std::map<std::string, std::string>& values, const EffectiveType& target, const std::string& path, const std::string& id) {
        for (const auto& [k, v] : values) {
            const Property* p = target.property(k);
            if (p == nullptr) {
                err("TML004", "'" + target.type->name + "' has no property '" + k + "'.", "", path + ".values." + k, id);
                continue;
            }
            if (p->kind != PropertyKind::Fixed) {
                err("TML010", "Only fixed properties take a value here ('" + k + "' is " + std::string(to_string(p->kind)) + ").", "", path + ".values." + k, id);
                continue;
            }
            std::string why;
            if (!valid_value(*p, v, why)) err("TML010", "Value of '" + k + "': " + why, "", path + ".values." + k, id);
        }
    }

    // ------------------------------------------------------------------ expressions

    void expression(const std::string& text, const EffectiveType& scope, const std::string& path, const std::string& id) {
        if (text.empty()) return;
        const auto t = expr::condition(text, r_.type_resolver(scope));
        if (t.error) {
            err(t.error->code.c_str(), t.error->message + (t.error->column > 0 ? " (column " + std::to_string(t.error->column) + ")" : std::string{}),
                t.error->code == "TML022" ? "Conditions can use the properties of this type and of its parts (part.property)." : "", path, id);
        }
    }

    // ------------------------------------------------------------------ types

    void type(const AssetType& t, const std::string& p) {
        const EffectiveType* e = r_.type(t.name);
        if (e == nullptr) return;
        std::set<std::string> members;
        const auto member = [&](const std::string& n, const std::string& path, const std::string& id) {
            if (!members.insert(n).second) err("TML003", "'" + t.name + "' has two members named '" + n + "' (including inherited ones).", "", path, id);
        };
        for (const Property* x : e->properties) member(x->name, p + ".properties[" + x->name + "]", x->id);
        for (const Event* x : e->events) member(x->name, p + ".events[" + x->name + "]", x->id);
        for (const Action* x : e->actions) member(x->name, p + ".actions[" + x->name + "]", x->id);
        for (const Part* x : e->parts) member(x->name, p + ".parts[" + x->name + "]", x->id);

        for (const Property& x : t.properties) property(x, p + ".properties[" + x.name + "]");
        // Overrides of inherited fixed values.
        if (!t.values.empty()) overrides(t.values, *e, p, t.id);

        for (const Event& x : t.events) {
            const std::string ep = p + ".events[" + x.name + "]";
            name(x.name, ep, x.id, "event");
            if (x.origin == EventOrigin::Condition) {
                if (x.when.empty()) err("TML030", "Condition event '" + x.name + "' has no condition.", "Write when it occurs, e.g. temperature > limit.", ep + ".when", x.id);
                if (!x.meaning.empty()) err("TML030", "A condition event means its condition; remove 'meaning'.", "", ep + ".meaning", x.id);
                expression(x.when, *e, ep + ".when", x.id);
                expression(x.rearm, *e, ep + ".rearm", x.id);
            } else {
                if (!x.when.empty() || !x.rearm.empty()) err("TML030", "A reported event has no condition; make it a condition event or move the condition to 'meaning'.", "", ep + ".when", x.id);
                expression(x.meaning, *e, ep + ".meaning", x.id);
            }
        }
        for (const Action& x : t.actions) {
            const std::string ap = p + ".actions[" + x.name + "]";
            name(x.name, ap, x.id, "action");
            expression(x.meaning, *e, ap + ".meaning", x.id);
        }
        for (const Part& x : t.parts) {
            const std::string pp = p + ".parts[" + x.name + "]";
            name(x.name, pp, x.id, "part");
            const EffectiveType* pt = r_.type(x.type);
            if (pt == nullptr) {
                err("TML004", "Unknown type '" + x.type + "' of part '" + x.name + "'.", "", pp + ".type", x.id);
                continue;
            }
            overrides(x.values, *pt, pp, x.id);
            if (x.external && pt->behavior != nullptr) {
                err("TML080", "External part '" + x.name + "' has a type with behaviour.", "An external part is a reference to an asset outside the twin; it has no behaviour here.",
                    pp, x.id);
            }
        }
        for (const Relationship& x : t.relationships) relationship(x, *e, p + ".relationships[" + x.id + "]");
        for (const Link& x : t.links) link(x, *e, p + ".links[" + x.id + "]");
        for (const Assumption& x : t.assumptions) {
            const std::string ap = p + ".assumptions[" + x.name + "]";
            name(x.name, ap, x.id, "assumption");
            expression(x.expr, *e, ap + ".expr", x.id);
        }
        if (t.behavior) behavior(t, *t.behavior, *e, p + ".behavior");
    }

    /// Type of the asset at a dotted part path from a type ("self" = the type).
    const EffectiveType* part_type(const EffectiveType& scope, const std::string& dotted) const {
        if (dotted.empty() || dotted == "self") return &scope;
        const EffectiveType* cur = &scope;
        std::size_t start = 0;
        for (;;) {
            const std::size_t dot = dotted.find('.', start);
            const std::string seg = dotted.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            const Part* p = cur->part(seg);
            if (p == nullptr) return nullptr;
            cur = r_.type(p->type);
            if (cur == nullptr) return nullptr;
            if (dot == std::string::npos) return cur;
            start = dot + 1;
        }
    }

    static bool is_a(const EffectiveType& t, const std::string& type_name) {
        return std::any_of(t.lineage.begin(), t.lineage.end(), [&](const AssetType* a) { return a->name == type_name; });
    }

    void relationship(const Relationship& x, const EffectiveType& scope, const std::string& path) {
        const RelationshipType* rt = d_.find_relationship_type(x.type);
        if (rt == nullptr) err("TML004", "Unknown relationship type '" + x.type + "'.", "Add it to the vocabulary.", path + ".type", x.id);
        const EffectiveType* from = part_type(scope, x.from);
        const EffectiveType* to = part_type(scope, x.to);
        if (from == nullptr) err("TML004", "Unknown asset '" + x.from + "'.", "", path + ".from", x.id);
        if (to == nullptr) err("TML004", "Unknown asset '" + x.to + "'.", "", path + ".to", x.id);
        if (rt == nullptr || from == nullptr || to == nullptr) return;
        const auto allowed = [&](const std::vector<std::string>& list, const EffectiveType& t) {
            return list.empty() || std::any_of(list.begin(), list.end(), [&](const std::string& n) { return is_a(t, n); });
        };
        if (!allowed(rt->from, *from)) err("TML040", "'" + x.type + "' cannot start at a " + from->type->name + ".", "", path, x.id);
        if (!allowed(rt->to, *to)) err("TML040", "'" + x.type + "' cannot end at a " + to->type->name + ".", "", path, x.id);
    }

    /// Resolve `[part.]*member` at type level.
    const EffectiveType* owner_of(const EffectiveType& scope, const std::string& dotted, std::string& member) const {
        const std::size_t dot = dotted.rfind('.');
        member = dot == std::string::npos ? dotted : dotted.substr(dot + 1);
        return part_type(scope, dot == std::string::npos ? std::string{} : dotted.substr(0, dot));
    }

    void link(const Link& x, const EffectiveType& scope, const std::string& path) {
        std::string m;
        const EffectiveType* from = owner_of(scope, x.from, m);
        if (from == nullptr || from->event(m) == nullptr) err("TML031", "'" + x.from + "' is not an event of this type or of its parts.", "", path + ".from", x.id);
        const EffectiveType* to = owner_of(scope, x.to, m);
        if (to == nullptr || to->action(m) == nullptr) err("TML031", "'" + x.to + "' is not an action of this type or of its parts.", "", path + ".to", x.id);
    }

    // ------------------------------------------------------------------ behaviour

    void atom(const TimeAtom& a, const Behavior& b, const std::string& path, const std::string& id, bool limit) {
        const auto timer_known = [&](const std::string& n) {
            return std::any_of(b.timers.begin(), b.timers.end(), [&](const Timer& t) { return t.name == n; });
        };
        if (!timer_known(a.timer)) err("TML004", "Unknown timer '" + a.timer + "'.", "Declare the timer in this behaviour.", path, id);
        if (a.minus) {
            if (!limit) err("TML052", "Differences between timers are allowed in state limits only.", "", path, id);
            else if (!timer_known(*a.minus)) err("TML004", "Unknown timer '" + *a.minus + "'.", "", path, id);
        }
        if (limit && a.op != ir::Comparison::Less && a.op != ir::Comparison::LessEqual) {
            err("TML052", "A state limit is an upper bound (< or <=).", "Lower bounds belong to transition windows.", path, id);
        }
        bound(a.bound, b, path, id);
    }

    void bound(const authoring::Bound& v, const Behavior& b, const std::string& path, const std::string& id) {
        if (const auto* i = std::get_if<std::int64_t>(&v.value)) {
            if (*i < 0) err("TML051", "A time bound cannot be negative.", "", path, id);
            return;
        }
        const std::string& n = std::get<std::string>(v.value);
        if (std::none_of(b.constants.begin(), b.constants.end(), [&](const Constant& c) { return c.name == n; })) {
            err("TML051", "Unknown constant '" + n + "' used as a time bound.", "", path, id);
        }
    }

    void behavior(const AssetType& t, const Behavior& b, const EffectiveType& e, const std::string& p) {
        if (e.lineage.size() > 1) {
            for (std::size_t i = 0; i + 1 < e.lineage.size(); ++i) {
                if (e.lineage[i]->behavior) {
                    err("TML050", "'" + t.name + "' declares a behaviour but its supertype '" + e.lineage[i]->name + "' already has one.",
                        "A subtype inherits the behaviour of its supertype unchanged.", p, t.id);
                }
            }
        }
        std::set<std::string> names;
        for (const Timer& x : b.timers) {
            name(x.name, p + ".timers[" + x.name + "]", x.id, "timer");
            if (!names.insert(x.name).second) err("TML003", "Two timers are named '" + x.name + "'.", "", p + ".timers[" + x.name + "]", x.id);
        }
        for (const Constant& x : b.constants) {
            name(x.name, p + ".constants[" + x.name + "]", x.id, "constant");
            if (!names.insert(x.name).second) err("TML003", "The name '" + x.name + "' is used twice in this behaviour.", "", p + ".constants[" + x.name + "]", x.id);
            if (x.value < 0) err("TML051", "A duration cannot be negative.", "", p + ".constants[" + x.name + "]", x.id);
        }
        std::set<std::string> states;
        int initials = 0;
        for (const State& s : b.states) {
            const std::string sp = p + ".states[" + s.name + "]";
            name(s.name, sp, s.id, "state");
            if (!states.insert(s.name).second) err("TML003", "Two states are named '" + s.name + "'.", "", sp, s.id);
            if (s.initial) ++initials;
            for (const TimeAtom& a : s.limits) atom(a, b, sp + ".limits", s.id, true);
            expression(s.meaning, e, sp + ".meaning", s.id);
        }
        if (initials != 1) {
            err("TML050", initials == 0 ? "The behaviour of '" + t.name + "' has no initial state." : "The behaviour of '" + t.name + "' has several initial states.",
                "Mark exactly one state as initial.", p + ".states", t.id);
        }
        std::set<std::string> groups;
        for (const Group& g : b.groups) {
            const std::string gp = p + ".groups[" + g.name + "]";
            name(g.name, gp, g.id, "group");
            if (states.contains(g.name) || !groups.insert(g.name).second) err("TML003", "The name '" + g.name + "' is already used by a state or group.", "", gp, g.id);
            for (const std::string& m : g.states) {
                if (!states.contains(m)) err("TML004", "Unknown state '" + m + "' in group '" + g.name + "'.", "", gp + ".states", g.id);
            }
            expression(g.meaning, e, gp + ".meaning", g.id);
        }
        for (const Transition& tr : b.transitions) {
            const std::string tp = p + ".transitions[" + tr.id + "]";
            if (tr.from.empty()) err("TML004", "A transition needs at least one source state.", "", tp + ".from", tr.id);
            for (const std::string& f : tr.from) {
                if (!states.contains(f) && !groups.contains(f)) err("TML004", "Unknown source state '" + f + "'.", "", tp + ".from", tr.id);
            }
            if (!states.contains(tr.to)) err("TML004", "Unknown target state '" + tr.to + "'.", "", tp + ".to", tr.id);
            switch (tr.trigger.kind) {
                case TriggerKind::Event: {
                    std::string m;
                    const EffectiveType* o = owner_of(e, tr.trigger.ref, m);
                    if (o == nullptr || o->event(m) == nullptr) err("TML004", "'" + tr.trigger.ref + "' is not an event of '" + t.name + "' or of its parts.", "", tp + ".trigger", tr.id);
                    break;
                }
                case TriggerKind::Action: {
                    std::string m;
                    const EffectiveType* o = owner_of(e, tr.trigger.ref, m);
                    if (o == nullptr || o->action(m) == nullptr) err("TML004", "'" + tr.trigger.ref + "' is not an action of '" + t.name + "' or of its parts.", "", tp + ".trigger", tr.id);
                    break;
                }
                case TriggerKind::Timeout:
                    bound(tr.trigger.after, b, tp + ".trigger.timeout", tr.id);
                    if (!tr.trigger.timer.empty() && std::none_of(b.timers.begin(), b.timers.end(), [&](const Timer& x) { return x.name == tr.trigger.timer; })) {
                        err("TML004", "Unknown timer '" + tr.trigger.timer + "'.", "", tp + ".trigger.timer", tr.id);
                    }
                    break;
                case TriggerKind::Internal: break;
            }
            for (const TimeAtom& a : tr.window) atom(a, b, tp + ".window", tr.id, false);
            for (const std::string& x : tr.restart) {
                if (std::none_of(b.timers.begin(), b.timers.end(), [&](const Timer& tm) { return tm.name == x; })) {
                    err("TML004", "Unknown timer '" + x + "' in restart.", "", tp + ".restart", tr.id);
                }
            }
        }
        unreachable(b, p);
    }

    void unreachable(const Behavior& b, const std::string& p) {
        const State* init = nullptr;
        for (const State& s : b.states) {
            if (s.initial) init = &s;
        }
        if (init == nullptr) return;
        std::set<std::string> seen{init->name};
        bool grew = true;
        while (grew) {
            grew = false;
            for (const Transition& tr : b.transitions) {
                bool from_seen = false;
                for (const std::string& f : tr.from) {
                    if (seen.contains(f)) from_seen = true;
                    for (const Group& g : b.groups) {
                        if (g.name == f && std::any_of(g.states.begin(), g.states.end(), [&](const std::string& m) { return seen.contains(m); })) from_seen = true;
                    }
                }
                if (from_seen && seen.insert(tr.to).second) grew = true;
            }
        }
        for (const State& s : b.states) {
            if (!seen.contains(s.name)) warn("TML055", "State '" + s.name + "' cannot be reached from the initial state.", "", p + ".states[" + s.name + "]", s.id);
        }
    }

    void requirements() {
        std::set<std::string> names;
        for (const Requirement& q : d_.requirements) {
            const std::string p = "requirements[" + q.name + "]";
            name(q.name, p, q.id, "requirement");
            if (!names.insert(q.name).second) err("TML003", "Two requirements are named '" + q.name + "'.", "", p, q.id);
            if (q.kind != "never" && q.kind != "property") err("TML001", "A requirement is of kind 'never' or 'property'.", "", p + ".kind", q.id);
            if (q.kind == "never" && q.state.empty()) err("TML004", "Name the state that must never be reached.", "", p + ".state", q.id);
            if (q.kind == "property" && q.property.empty()) err("TML004", "Write the property.", "", p + ".property", q.id);
            if (q.severity != "info" && q.severity != "warning" && q.severity != "critical") err("TML001", "Severity is info, warning or critical.", "", p + ".severity", q.id);
        }
    }

    const Document& d_;
    const Resolved& r_;
    std::vector<Diagnostic>& out_;
};

}  // namespace

int compare_decimal(const std::string& a, const std::string& b) {
    // Exact comparison of two decimals -?d+(.d+)?
    const bool na = !a.empty() && a.front() == '-';
    const bool nb = !b.empty() && b.front() == '-';
    const auto parts = [](const std::string& s, bool neg) {
        const std::string body = neg ? s.substr(1) : s;
        const std::size_t dot = body.find('.');
        std::string ip = dot == std::string::npos ? body : body.substr(0, dot);
        std::string fp = dot == std::string::npos ? std::string{} : body.substr(dot + 1);
        while (ip.size() > 1 && ip.front() == '0') ip.erase(0, 1);
        while (!fp.empty() && fp.back() == '0') fp.pop_back();
        return std::pair{ip, fp};
    };
    auto [ai, af] = parts(a, na);
    auto [bi, bf] = parts(b, nb);
    const bool za = ai == "0" && af.empty();
    const bool zb = bi == "0" && bf.empty();
    const int sa = za ? 0 : (na ? -1 : 1);
    const int sb = zb ? 0 : (nb ? -1 : 1);
    if (sa != sb) return sa < sb ? -1 : 1;
    if (sa == 0) return 0;
    int mag = 0;
    if (ai.size() != bi.size()) {
        mag = ai.size() < bi.size() ? -1 : 1;
    } else if (ai != bi) {
        mag = ai < bi ? -1 : 1;
    } else {
        const std::size_t n = std::max(af.size(), bf.size());
        af.resize(n, '0');
        bf.resize(n, '0');
        mag = af == bf ? 0 : (af < bf ? -1 : 1);
    }
    return sa < 0 ? -mag : mag;
}

void validate(const Document& document, const Resolved& resolved, std::vector<Diagnostic>& diagnostics) {
    Checker(document, resolved, diagnostics).run();
}

}  // namespace twin::model::detail
