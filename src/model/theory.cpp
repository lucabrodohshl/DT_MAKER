// The domain-theory compiler: the formal ontology K and the interpretations I_D and
// I_P in the formats SemPTDTAlignmentICSE reads (04-compilation.md, section 4.4).
#include "theory.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "twin/model/compile.hpp"
#include "twin/model/expression.hpp"

namespace twin::model::detail {

std::string symbol_of(const Instance& owner, const Property& property) {
    return Resolved::qualify(owner, property.name);
}

namespace {

bool builtin_sort(const std::string& s) { return s == "Real" || s == "Int" || s == "Bool"; }

/// Conjunction of formulas, flattening nested conjunctions: (and a (and b c)) is written (and a b c).
std::string conj(const std::vector<std::string>& parts) {
    std::vector<std::string> flat;
    for (const std::string& p : parts) {
        for (std::string& c : expr::conjuncts(p)) flat.push_back(std::move(c));
    }
    if (flat.empty()) return {};
    if (flat.size() == 1) return flat.front();
    std::string s = "(and";
    for (const std::string& p : flat) s += " " + p;
    return s + ")";
}

std::string smt_number(const std::string& decimal) {
    return !decimal.empty() && decimal.front() == '-' ? "(- " + decimal.substr(1) + ")" : decimal;
}

class Theory {
public:
    Theory(const Resolved& r, const BehaviorOutput& b, std::vector<Diagnostic>& out) : r_(r), b_(b), out_(out) {}

    TheoryOutput run() {
        TheoryOutput result;
        declarations();
        interpretations();
        reference();
        result.ontology = ontology_text();
        result.dt_interpretation = interp_text("I_D: interpretation of V_D (" + r_.doc().root + ")", dt_entries_);
        if (r_.doc().reference) {
            result.pt_interpretation = interp_text("I_P: interpretation of the physical reference (" + r_.doc().reference->automaton.name + ")", pt_entries_);
        }
        result.trace = std::move(trace_);
        result.symbols = funs_.size() + rels_.size();
        result.axioms = axioms_.size();
        return result;
    }

private:
    struct Decl {
        std::string name;
        std::string sort;     // fun only
        std::string comment;
    };
    struct Axiom {
        std::string id;
        std::string formula;
        std::string comment;
    };
    struct Entry {
        std::string key;
        std::string formula;
    };

    void err(const char* code, const std::string& message, const std::string& path, const std::string& id) {
        out_.push_back({"error", code, message, "", path, id});
    }

    // ------------------------------------------------------------------ K1–K6

    void declarations() {
        for (const auto& inst : r_.instances()) {
            const std::string where = inst->dotted.empty() ? r_.doc().root : inst->dotted;
            for (const Property* p : inst->type->properties) {
                if (p->kind == PropertyKind::PerTwin || p->type == DataType::String) continue;
                const std::string sym = symbol_of(*inst, *p);
                const std::string desc = where + "." + p->name + " (" + std::string(to_string(p->kind)) + (p->unit.empty() ? "" : ", " + p->unit) + ")";
                trace_.push_back(trace_entry("symbol", sym, {{p->id, p->type == DataType::Boolean ? "K2.relation" : "K1.function"}}));
                if (p->type == DataType::Boolean) {
                    rels_.push_back({sym, "", desc});
                } else {
                    std::string sort = p->sort;
                    if (sort.empty()) sort = p->type == DataType::Enum ? "Real" : quantity_sort(p->unit);
                    if (!builtin_sort(sort) && std::find(sorts_.begin(), sorts_.end(), sort) == sorts_.end()) {
                        sorts_.push_back(sort);
                        sort_units_[sort] = p->unit;
                    }
                    funs_.push_back({sym, sort, desc});
                }
                if (p->type == DataType::Enum && !p->values.empty()) {
                    std::vector<std::string> alts;
                    for (std::size_t i = 0; i < p->values.size(); ++i) alts.push_back("(= " + sym + " " + std::to_string(i) + ")");
                    axiom(sym + "_domain", alts.size() == 1 ? alts.front() : "(or " + join(alts) + ")", "values of " + where + "." + p->name, p->id, "K3.enum");
                }
                if (p->kind == PropertyKind::Fixed) {
                    if (const auto v = r_.value(*inst, *p)) {
                        std::string f;
                        if (p->type == DataType::Boolean) {
                            f = *v == "true" ? sym : "(not " + sym + ")";
                        } else if (p->type == DataType::Enum) {
                            const auto it = std::find(p->values.begin(), p->values.end(), *v);
                            f = "(= " + sym + " " + std::to_string(it - p->values.begin()) + ")";
                        } else {
                            f = "(= " + sym + " " + smt_number(*v) + ")";
                        }
                        axiom(sym + "_value", f, where + "." + p->name + " = " + *v, p->id, "K4.value");
                    }
                } else if (p->kind == PropertyKind::Measured && (p->type == DataType::Real || p->type == DataType::Integer)) {
                    if (p->min) axiom(sym + "_min", "(>= " + sym + " " + smt_number(*p->min) + ")", "lowest possible value", p->id, "K5.range");
                    if (p->max) axiom(sym + "_max", "(<= " + sym + " " + smt_number(*p->max) + ")", "highest possible value", p->id, "K5.range");
                }
            }
        }
        for (const auto& inst : r_.instances()) {
            for (const Assumption* a : inst->type->assumptions) {
                const auto t = expr::condition(a->expr, r_.resolver(*inst));
                if (t.error) {
                    err(t.error->code.c_str(), "Assumption '" + a->name + "': " + t.error->message, "types[" + inst->type->type->name + "].assumptions[" + a->name + "]", a->id);
                    continue;
                }
                axiom(Resolved::qualify(*inst, "assume_" + a->name), t.smt, a->description, a->id, "K6.assumption");
            }
        }
    }

    void axiom(const std::string& id, const std::string& formula, const std::string& comment, const std::string& source, const char* rule) {
        axioms_.push_back({id, formula, comment});
        trace_.push_back(trace_entry("axiom", id, {{source, rule}}));
    }

    static std::string join(const std::vector<std::string>& v) {
        std::string s;
        for (const std::string& x : v) s += (s.empty() ? "" : " ") + x;
        return s;
    }

    // ------------------------------------------------------------------ meanings

    /// The meaning of a state of an instance (state meaning and its groups'), translated.
    std::vector<std::string> state_meaning(const Instance& inst, const State& s) {
        std::vector<std::string> parts;
        const Behavior* b = inst.type->behavior;
        const auto add = [&](const std::string& text, const std::string& path, const std::string& id) {
            if (text.empty()) return;
            const auto t = expr::condition(text, r_.resolver(inst));
            if (t.error) {
                err(t.error->code.c_str(), t.error->message, path, id);
                return;
            }
            parts.push_back(t.smt);
        };
        add(s.meaning, "types[" + inst.type->behavior_owner->name + "].behavior.states[" + s.name + "].meaning", s.id);
        if (b != nullptr) {
            for (const Group& g : b->groups) {
                if (std::find(g.states.begin(), g.states.end(), s.name) != g.states.end()) {
                    add(g.meaning, "types[" + inst.type->behavior_owner->name + "].behavior.groups[" + g.name + "].meaning", g.id);
                }
            }
        }
        return parts;
    }

    /// The interpretation of a channel; "" means no entry (invisible to the aligner).
    std::string channel_formula(const ChannelInfo& c) {
        if (c.direction != '!') return {};  // the .interp format interprets send labels only
        switch (c.kind) {
            case ChannelInfo::Kind::Event: {
                const Event& e = *c.event;
                if (e.origin == EventOrigin::Condition) return translate(e.when, *c.owner, "events[" + e.name + "].when", e.id);
                if (!e.meaning.empty()) return translate(e.meaning, *c.owner, "events[" + e.name + "].meaning", e.id);
                if (e.uninterpreted) return {};
                return fresh(c.name + "_reported", "event " + c.name + " reported", e.id, "K7.reported");
            }
            case ChannelInfo::Kind::Action: {
                const Action& a = *c.action;
                const std::string req = fresh(c.name + "_requested", "action " + c.name + " requested", a.id, "K7.requested");
                if (a.meaning.empty()) return req;
                const std::string m = translate(a.meaning, *c.owner, "actions[" + a.name + "].meaning", a.id);
                return m.empty() ? req : conj({req, m});
            }
            case ChannelInfo::Kind::Timeout:
                return fresh(c.name + "_elapsed", "timeout " + c.name + " elapsed", c.state->id, "K7.elapsed");
        }
        return {};
    }

    std::string translate(const std::string& text, const Instance& scope, const std::string& path, const std::string& id) {
        const auto t = expr::condition(text, r_.resolver(scope));
        if (t.error) {
            err(t.error->code.c_str(), t.error->message, "types[" + scope.type->type->name + "]." + path, id);
            return {};
        }
        return t.smt;
    }

    std::string fresh(const std::string& name, const std::string& comment, const std::string& source, const char* rule) {
        if (std::none_of(rels_.begin(), rels_.end(), [&](const Decl& d) { return d.name == name; })) {
            rels_.push_back({name, "", comment});
            trace_.push_back(trace_entry("symbol", name, {{source, rule}}));
        }
        return name;
    }

    // ------------------------------------------------------------------ I_D (I1–I6)

    void interpretations() {
        for (const LocationInfo& loc : b_.locations) {
            std::vector<std::string> parts;
            std::vector<Source> src;
            for (const auto& [inst, state] : loc.components) {
                const auto m = state_meaning(*inst, *state);
                parts.insert(parts.end(), m.begin(), m.end());
                src.push_back({state->id, "I1.location"});
            }
            if (parts.empty()) continue;  // no entry: the aligner reads the location as true
            dt_entries_.push_back({loc.name, conj(parts)});
            trace_.push_back(trace_entry("interpretation", loc.name, src));
        }
        for (const ChannelInfo& c : b_.channels) {
            const std::string f = channel_formula(c);
            if (f.empty()) continue;
            dt_entries_.push_back({c.name + "!", f});
            channel_formula_[c.name] = f;
            std::vector<Source> src;
            if (c.event != nullptr) src.push_back({c.event->id, c.event->origin == EventOrigin::Condition ? "I2.condition" : "I3.reported"});
            if (c.action != nullptr) src.push_back({c.action->id, "I4.action"});
            if (c.state != nullptr) src.push_back({c.state->id, "I5.timeout"});
            trace_.push_back(trace_entry("interpretation", c.name + "!", src));
        }
    }

    // ------------------------------------------------------------------ I_P (P1, P2)

    void reference() {
        if (!r_.doc().reference) return;
        const Reference& ref = *r_.doc().reference;
        const authoring::Model& pt = ref.automaton;
        for (const auto& [loc, _] : ref.states) {
            if (std::none_of(pt.locations.begin(), pt.locations.end(), [&](const auto& l) { return l.name == loc; })) {
                err("TML070", "The mapping names '" + loc + "', which is not a location of the physical reference.", "model.reference.states." + loc, "");
            }
        }
        for (const auto& [chan, _] : ref.labels) {
            if (std::none_of(pt.channels.begin(), pt.channels.end(), [&](const auto& c) { return c.name == chan; })) {
                err("TML070", "The mapping names '" + chan + "', which is not a channel of the physical reference.", "model.reference.labels." + chan, "");
            }
        }
        for (const authoring::LocationDecl& l : pt.locations) {
            const auto it = ref.states.find(l.name);
            if (it == ref.states.end()) {
                out_.push_back({"warning", "TML070", "Location '" + l.name + "' of the physical reference has no meaning; the aligner reads it as true.",
                                "Map it to a state of the model.", "model.reference.states." + l.name, ""});
                continue;
            }
            std::vector<std::string> parts;
            for (const std::string& path : it->second.states) {
                const auto found = find_state(path);
                if (!found.first) {
                    err("TML070", "'" + path + "' is not a state of the model.", "model.reference.states." + l.name, "");
                    continue;
                }
                const auto m = state_meaning(*found.first, *found.second);
                parts.insert(parts.end(), m.begin(), m.end());
            }
            if (it->second.states.empty() && !it->second.meaning.empty()) {
                const std::string f = translate(it->second.meaning, *r_.root(), "reference", "");
                if (!f.empty()) parts.push_back(f);
            }
            if (!parts.empty()) pt_entries_.push_back({l.name, conj(parts)});
        }
        for (const authoring::ChannelDecl& c : pt.channels) {
            const auto it = ref.labels.find(c.name);
            if (it == ref.labels.end()) {
                err("TML070", "Channel '" + c.name + "' of the physical reference is not mapped.", "model.reference.labels." + c.name, "");
                continue;
            }
            if (it->second.internal) continue;
            const auto m = r_.member(*r_.root(), it->second.trigger);
            if (!m || (m->event == nullptr && m->action == nullptr)) {
                err("TML070", "'" + it->second.trigger + "' is not an event or action of the model.", "model.reference.labels." + c.name, "");
                continue;
            }
            const std::string channel = Resolved::qualify(*m->owner, m->event != nullptr ? m->event->name : m->action->name);
            std::string f;
            if (const auto known = channel_formula_.find(channel); known != channel_formula_.end()) {
                f = known->second;
            } else {
                ChannelInfo info;
                info.name = channel;
                info.owner = m->owner;
                info.event = m->event;
                info.action = m->action;
                info.kind = m->event != nullptr ? ChannelInfo::Kind::Event : ChannelInfo::Kind::Action;
                f = channel_formula(info);
            }
            if (!f.empty()) pt_entries_.push_back({c.name + "!", f});
        }
    }

    std::pair<const Instance*, const State*> find_state(const std::string& path) const {
        const std::size_t dot = path.rfind('.');
        const Instance* inst = r_.part(*r_.root(), dot == std::string::npos ? std::string{} : path.substr(0, dot));
        const std::string name = dot == std::string::npos ? path : path.substr(dot + 1);
        if (inst == nullptr || inst->type->behavior == nullptr) return {nullptr, nullptr};
        for (const State& s : inst->type->behavior->states) {
            if (s.name == name) return {inst, &s};
        }
        return {nullptr, nullptr};
    }

    // ------------------------------------------------------------------ rendering

    std::string ontology_text() const {
        const Document& d = r_.doc();
        std::string out = "; Domain theory K of the model " + (d.label.empty() ? d.name : d.label) + " (" + d.name + ").\n";
        out += "; Generated by " + std::string(kCompilerVersion) + " from the twin model; change the model, not this file.\n";
        std::vector<std::string> imported_sorts;
        std::vector<std::string> imported_funs;
        std::vector<std::string> imported_rels;
        std::vector<std::string> imported_axioms;
        for (const std::string& line : d.declarations) {
            const std::string t = line.substr(line.find_first_not_of(" \t") == std::string::npos ? 0 : line.find_first_not_of(" \t"));
            if (t.rfind("sort", 0) == 0) imported_sorts.push_back(t);
            else if (t.rfind("fun", 0) == 0) imported_funs.push_back(t);
            else if (t.rfind("rel", 0) == 0) imported_rels.push_back(t);
            else imported_axioms.push_back(t);
        }
        out += "\n; === SORTS ===\n";
        for (const std::string& s : sorts_) {
            const auto u = sort_units_.find(s);
            out += "sort " + s + (u != sort_units_.end() && !u->second.empty() ? "  ; " + u->second : std::string{}) + "\n";
        }
        for (const std::string& s : imported_sorts) out += s + "\n";
        out += "\n; === FUNCTIONS ===\n";
        for (const Decl& f : funs_) out += "fun " + f.name + " : " + f.sort + "  ; " + f.comment + "\n";
        for (const std::string& s : imported_funs) out += s + "\n";
        out += "\n; === RELATIONS ===\n";
        for (const Decl& r : rels_) out += "rel " + r.name + " :  ; " + r.comment + "\n";
        for (const std::string& s : imported_rels) out += s + "\n";
        out += "\n; === AXIOMS ===\n";
        for (const Axiom& a : axioms_) out += "axiom " + a.id + " : " + a.formula + (a.comment.empty() ? std::string{} : "  ; " + a.comment) + "\n";
        for (const std::string& s : imported_axioms) out += s + "\n";
        return out;
    }

    static std::string interp_text(const std::string& title, const std::vector<Entry>& entries) {
        std::string out = "; " + title + ", generated by " + std::string(kCompilerVersion) + ".\n";
        out += "; Locations without an entry mean true; channels without an entry are internal for alignment.\n";
        out += "\n; --- locations ---\n";
        for (const Entry& e : entries) {
            if (e.key.back() != '!') out += e.key + " : " + e.formula + "\n";
        }
        out += "\n; --- events ---\n";
        for (const Entry& e : entries) {
            if (e.key.back() == '!') out += e.key + " : " + e.formula + "\n";
        }
        return out;
    }

    const Resolved& r_;
    const BehaviorOutput& b_;
    std::vector<Diagnostic>& out_;
    std::vector<std::string> sorts_;
    std::map<std::string, std::string> sort_units_;
    std::vector<Decl> funs_;
    std::vector<Decl> rels_;
    std::vector<Axiom> axioms_;
    std::vector<Entry> dt_entries_;
    std::vector<Entry> pt_entries_;
    std::map<std::string, std::string> channel_formula_;
    json::Json trace_ = json::Json::array();
};

}  // namespace

TheoryOutput compile_theory(const Resolved& model, const BehaviorOutput& behavior, std::vector<Diagnostic>& diagnostics) {
    return Theory(model, behavior, diagnostics).run();
}

}  // namespace twin::model::detail
