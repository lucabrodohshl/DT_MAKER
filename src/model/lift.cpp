// Lifting a timed automaton (and its ontology and interpretation) into the modelling
// language: the inverse of the behaviour and domain-theory compilers (03-metamodel.md 3.9).
#include "twin/model/lift.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

#include "resolve.hpp"
#include "twin/model/expression.hpp"

namespace twin::model {

namespace {

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    const std::size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

/// Split "text ; comment" at the first ';' outside parentheses.
std::pair<std::string, std::string> strip_comment(const std::string& line) {
    int depth = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '(') ++depth;
        if (line[i] == ')') --depth;
        if (line[i] == ';' && depth <= 0) return {trim(line.substr(0, i)), trim(line.substr(i + 1))};
    }
    return {trim(line), {}};
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        out.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

struct OntDecl {
    std::string kind;  // sort, fun, rel
    std::string name;
    std::vector<std::string> args;
    std::string ret;
    std::string comment;
    std::string raw;
};

struct OntAxiom {
    std::string id;
    std::string formula;
    std::string comment;
    std::string raw;
    bool used{false};
};

struct Ontology {
    std::vector<OntDecl> decls;
    std::vector<OntAxiom> axioms;
    std::vector<std::string> unknown;  // unparsed lines kept verbatim
};

Ontology parse_ontology(const std::string& text) {
    Ontology o;
    for (const std::string& line : lines(text)) {
        const auto [body, comment] = strip_comment(line);
        if (body.empty()) continue;
        const std::size_t sp = body.find_first_of(" \t");
        const std::string kw = body.substr(0, sp);
        const std::string rest = sp == std::string::npos ? std::string{} : trim(body.substr(sp));
        if (kw == "sort") {
            o.decls.push_back({"sort", rest, {}, {}, comment, line});
        } else if (kw == "fun" || kw == "rel" || kw == "axiom") {
            const std::size_t colon = rest.find(':');
            if (colon == std::string::npos) {
                o.unknown.push_back(line);
                continue;
            }
            const std::string name = trim(rest.substr(0, colon));
            const std::string sig = trim(rest.substr(colon + 1));
            if (kw == "axiom") {
                o.axioms.push_back({name, sig, comment, line, false});
                continue;
            }
            OntDecl d{kw, name, {}, {}, comment, line};
            std::string args = sig;
            if (kw == "fun") {
                const std::size_t arrow = sig.find("->");
                if (arrow != std::string::npos) {
                    args = trim(sig.substr(0, arrow));
                    d.ret = trim(sig.substr(arrow + 2));
                } else {
                    d.ret = sig;
                    args.clear();
                }
            }
            std::size_t p = 0;
            while (p < args.size()) {
                const std::size_t q = args.find_first_of(" \t", p);
                const std::string a = args.substr(p, q == std::string::npos ? std::string::npos : q - p);
                if (!a.empty()) d.args.push_back(a);
                if (q == std::string::npos) break;
                p = q + 1;
            }
            o.decls.push_back(std::move(d));
        } else {
            o.unknown.push_back(line);
        }
    }
    return o;
}

std::map<std::string, std::string> parse_interpretation(const std::string& text) {
    std::map<std::string, std::string> out;
    for (const std::string& line : lines(text)) {
        const auto [body, comment] = strip_comment(line);
        (void)comment;
        if (body.empty()) continue;
        const std::size_t colon = body.find(':');
        if (colon == std::string::npos) continue;
        out[trim(body.substr(0, colon))] = trim(body.substr(colon + 1));
    }
    return out;
}

/// The expression for an SMT formula: readable when inside the grammar and all symbols are names; raw otherwise.
std::string expression_of(const std::string& smt) {
    const auto n = expr::from_smt(smt);
    if (!n) return "smt:" + expr::normalize_smt(smt);
    bool names = true;
    const std::function<void(const expr::Node&)> walk = [&](const expr::Node& x) {
        if (x.kind == expr::Node::Kind::Path && !is_name(x.text)) names = false;
        for (const expr::Node& c : x.children) walk(c);
    };
    walk(*n);
    return names ? expr::print(*n) : "smt:" + expr::normalize_smt(smt);
}

/// (= sym value) or (= value sym) with a decimal value; returns the value.
std::optional<std::string> value_axiom(const std::string& formula, const std::string& sym) {
    const auto n = expr::from_smt(formula);
    if (!n || n->kind != expr::Node::Kind::Compare || n->text != "==") return std::nullopt;
    const auto number = [](const expr::Node& x) -> std::optional<std::string> {
        if (x.kind == expr::Node::Kind::Number) return x.text;
        if (x.kind == expr::Node::Kind::Neg && x.children.front().kind == expr::Node::Kind::Number) return "-" + x.children.front().text;
        return std::nullopt;
    };
    const expr::Node& a = n->children[0];
    const expr::Node& b = n->children[1];
    if (a.kind == expr::Node::Kind::Path && a.text == sym) return number(b);
    if (b.kind == expr::Node::Kind::Path && b.text == sym) return number(a);
    return std::nullopt;
}

/// (>= sym n) / (<= sym n): returns {">=" or "<=", n}.
std::optional<std::pair<std::string, std::string>> range_axiom(const std::string& formula, const std::string& sym) {
    const auto n = expr::from_smt(formula);
    if (!n || n->kind != expr::Node::Kind::Compare || (n->text != ">=" && n->text != "<=")) return std::nullopt;
    const expr::Node& a = n->children[0];
    const expr::Node& b = n->children[1];
    if (a.kind != expr::Node::Kind::Path || a.text != sym) return std::nullopt;
    if (b.kind == expr::Node::Kind::Number) return std::pair{n->text, b.text};
    if (b.kind == expr::Node::Kind::Neg && b.children.front().kind == expr::Node::Kind::Number) return std::pair{n->text, "-" + b.children.front().text};
    return std::nullopt;
}

TimeAtom time_atom(const authoring::Atom& a) {
    TimeAtom t;
    t.timer = a.clock;
    t.minus = a.minus;
    t.op = a.op;
    t.bound = a.bound;
    return t;
}

}  // namespace

Lifted lift(const LiftInput& input) {
    Lifted out;
    const authoring::Model& ta = input.automaton;
    const auto fail = [&](const std::string& message, const std::string& path) {
        out.diagnostics.push_back({"error", "TML002", message, "Rename it in the automaton before lifting.", path, ""});
    };
    if (!is_name(ta.name)) fail("The automaton name '" + ta.name + "' is not a valid type name.", "automaton.name");
    for (const auto& l : ta.locations) {
        if (!is_name(l.name)) fail("Location '" + l.name + "' is not a valid state name.", "automaton.locations");
    }
    for (const auto& c : ta.clocks) {
        if (!is_name(c.name)) fail("Clock '" + c.name + "' is not a valid timer name.", "automaton.clocks");
    }
    for (const auto& c : ta.channels) {
        if (!is_name(c.name)) fail("Channel '" + c.name + "' is not a valid event name.", "automaton.channels");
    }
    for (const auto& c : ta.constants) {
        if (!is_name(c.name)) fail("Constant '" + c.name + "' is not a valid name.", "automaton.constants");
    }
    if (has_errors(out.diagnostics)) return out;

    const auto interp = parse_interpretation(input.interpretation);
    const auto meaning_of = [&](const std::string& key) -> std::optional<std::string> {
        const auto it = interp.find(key);
        if (it == interp.end()) return std::nullopt;
        return it->second;
    };

    Document d;
    d.name = input.name.empty() ? "lifted" : input.name;
    d.label = input.label;
    d.root = ta.name;
    d.description = ta.note;
    AssetType root;
    root.id = "t_" + ta.name;
    root.name = ta.name;

    // ------------------------------------------------------------------ channels -> events / actions
    std::map<std::string, char> direction;
    std::map<std::string, int> uses;
    for (const auto& e : ta.edges) {
        if (!e.sync) continue;
        const auto [it, fresh] = direction.emplace(e.sync->channel, e.sync->direction);
        if (!fresh && it->second != e.sync->direction) {
            out.diagnostics.push_back({"error", "TML003", "Channel '" + e.sync->channel + "' is used both as '!' and '?'.",
                                       "An event or action has one direction in the language.", "automaton.edges", ""});
        }
        ++uses[e.sync->channel];
    }
    if (has_errors(out.diagnostics)) return out;

    std::set<std::string> generated;  // relation symbols that the compiler would generate again
    std::set<std::string> actions;
    std::set<std::string> timeouts;
    for (const auto& c : ta.channels) {
        const char dir = direction.contains(c.name) ? direction[c.name] : '!';
        const auto m = meaning_of(c.name + "!");
        const std::string req = c.name + "_requested";
        if (m && (*m == req || (m->rfind("(and " + req + " ", 0) == 0))) {
            Action a;
            a.id = "a_" + c.name;
            a.name = c.name;
            a.description = c.note;
            a.direction = dir;
            if (*m != req) {
                // (and ℓ_requested m₁ … mₙ) -> meaning m₁ and … and mₙ
                std::vector<std::string> parts = expr::conjuncts(*m);
                parts.erase(parts.begin());
                std::string rest;
                if (parts.size() == 1) {
                    rest = parts.front();
                } else {
                    rest = "(and";
                    for (const std::string& x : parts) rest += " " + x;
                    rest += ")";
                }
                a.meaning = expression_of(rest);
            }
            root.actions.push_back(std::move(a));
            generated.insert(req);
            actions.insert(c.name);
            continue;
        }
        if (m && *m == c.name + "_elapsed" && uses[c.name] == 1) {
            timeouts.insert(c.name);  // decided per edge below
        }
        Event e;
        e.id = "e_" + c.name;
        e.name = c.name;
        e.description = c.note;
        e.direction = dir;
        if (!m) {
            e.uninterpreted = dir == '!';
        } else if (*m == c.name + "_reported") {
            generated.insert(*m);
        } else if (*m != c.name + "_elapsed") {
            e.meaning = expression_of(*m);
        }
        root.events.push_back(std::move(e));
    }

    // ------------------------------------------------------------------ behaviour
    Behavior b;
    for (const auto& c : ta.constants) b.constants.push_back({"c_" + c.name, c.name, c.value, c.note});
    for (const auto& c : ta.clocks) {
        bool state_change = true;
        for (const auto& e : ta.edges) {
            if (e.source != e.target && std::find(e.resets.begin(), e.resets.end(), c.name) == e.resets.end()) state_change = false;
        }
        b.timers.push_back({"x_" + c.name, c.name, state_change ? Restart::OnStateChange : Restart::Explicit, c.note});
    }
    const auto timer_policy = [&](const std::string& clock) {
        for (const Timer& t : b.timers) {
            if (t.name == clock) return t.restart;
        }
        return Restart::Explicit;
    };
    for (const auto& l : ta.locations) {
        State s;
        s.id = "s_" + l.name;
        s.name = l.name;
        s.initial = l.initial;
        s.description = l.note;
        for (const auto& a : l.invariant) s.limits.push_back(time_atom(a));
        if (const auto m = meaning_of(l.name)) s.meaning = expression_of(*m);
        b.states.push_back(std::move(s));
    }
    const auto state = [&](const std::string& name) -> State* {
        for (State& s : b.states) {
            if (s.name == name) return &s;
        }
        return nullptr;
    };
    // Timeout edges (our own format): label <State>_timeout, interpreted as ℓ_elapsed, guard x >= d, source limit x <= d.
    std::set<const authoring::EdgeDecl*> timeout_edges;
    for (const auto& e : ta.edges) {
        if (!e.sync || !timeouts.contains(e.sync->channel) || e.sync->channel != e.source + "_timeout") continue;
        State* src = state(e.source);
        if (src == nullptr) continue;
        const auto g = std::find_if(e.guard.begin(), e.guard.end(), [](const authoring::Atom& a) { return !a.minus && a.op == ir::Comparison::GreaterEqual; });
        if (g == e.guard.end()) continue;
        authoring::Atom inv = *g;
        inv.op = ir::Comparison::LessEqual;
        const auto lim = std::find_if(src->limits.begin(), src->limits.end(), [&](const TimeAtom& t) { return t == time_atom(inv); });
        if (lim == src->limits.end()) continue;
        src->limits.erase(lim);
        timeout_edges.insert(&e);
    }
    // Edges -> transitions, grouping edges that differ only in their source.
    struct Key {
        std::optional<authoring::Sync> sync;
        std::string target;
        authoring::Constraint guard;
        std::vector<std::string> resets;
        bool timeout{false};
        bool operator==(const Key& o) const {
            const bool same_sync = sync.has_value() == o.sync.has_value() && (!sync || (sync->channel == o.sync->channel && sync->direction == o.sync->direction));
            return same_sync && target == o.target && guard == o.guard && resets == o.resets && timeout == o.timeout;
        }
    };
    std::vector<std::pair<Key, std::vector<std::string>>> groups;
    std::vector<std::string> notes;
    for (const auto& e : ta.edges) {
        Key k{e.sync, e.target, e.guard, e.resets, timeout_edges.contains(&e)};
        auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == k; });
        if (it == groups.end() || k.timeout) {
            groups.emplace_back(k, std::vector<std::string>{e.source});
            notes.push_back(e.note);
        } else {
            it->second.push_back(e.source);
        }
    }
    std::size_t n = 0;
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const auto& [k, sources] = groups[gi];
        Transition t;
        t.id = "r" + std::to_string(++n);
        t.from = sources;
        t.to = k.target;
        t.description = notes[gi];
        if (k.timeout) {
            t.trigger.kind = TriggerKind::Timeout;
            const auto g = std::find_if(k.guard.begin(), k.guard.end(), [](const authoring::Atom& a) { return !a.minus && a.op == ir::Comparison::GreaterEqual; });
            t.trigger.after = g->bound;
            const Timer* st = nullptr;
            for (const Timer& x : b.timers) {
                if (x.restart == Restart::OnStateChange) {
                    st = &x;
                    break;
                }
            }
            if (st == nullptr || st->name != g->clock) t.trigger.timer = g->clock;
            for (const auto& a : k.guard) {
                if (&a != &*g) t.window.push_back(time_atom(a));
            }
        } else {
            if (!k.sync) {
                t.trigger.kind = TriggerKind::Internal;
            } else {
                t.trigger.kind = actions.contains(k.sync->channel) ? TriggerKind::Action : TriggerKind::Event;
                t.trigger.ref = k.sync->channel;
            }
            for (const auto& a : k.guard) t.window.push_back(time_atom(a));
        }
        for (const std::string& r : k.resets) {
            const bool implied = timer_policy(r) == Restart::OnStateChange &&
                                 std::all_of(sources.begin(), sources.end(), [&](const std::string& s) { return s != k.target; });
            if (!implied) t.restart.push_back(r);
        }
        b.transitions.push_back(std::move(t));
    }
    root.behavior = std::move(b);

    // ------------------------------------------------------------------ ontology -> properties and assumptions
    if (!input.ontology.empty()) {
        Ontology o = parse_ontology(input.ontology);
        std::set<std::string> used_sorts;
        std::vector<std::string> declarations;
        for (const OntDecl& x : o.decls) {
            if (x.kind == "sort") continue;
            const bool nullary = x.args.empty();
            if (!nullary || !is_name(x.name)) {
                declarations.push_back(trim(x.raw));
                if (x.kind == "fun") used_sorts.insert(x.ret);
                for (const std::string& a : x.args) used_sorts.insert(a);
                continue;
            }
            if (x.kind == "rel" && generated.contains(x.name)) continue;
            if (x.kind == "rel" && (x.name.size() > 8 && x.name.ends_with("_elapsed"))) {
                const std::string ch = x.name.substr(0, x.name.size() - 8);
                if (timeouts.contains(ch)) continue;
            }
            Property p;
            p.id = "p_" + x.name;
            p.name = x.name;
            p.description = x.comment;
            if (x.kind == "rel") {
                p.type = DataType::Boolean;
            } else {
                p.type = DataType::Real;
                if (x.ret != detail::quantity_sort("") ) p.sort = x.ret;
                for (OntAxiom& a : o.axioms) {
                    if (a.used) continue;
                    if (const auto v = value_axiom(a.formula, x.name)) {
                        if (!p.value) {
                            p.kind = PropertyKind::Fixed;
                            p.value = *v;
                            a.used = true;
                        }
                    } else if (const auto r = range_axiom(a.formula, x.name)) {
                        if (r->first == ">=" && !p.min) {
                            p.min = r->second;
                            a.used = true;
                        } else if (r->first == "<=" && !p.max) {
                            p.max = r->second;
                            a.used = true;
                        }
                    }
                }
                if (p.kind == PropertyKind::Fixed) {
                    // A fixed value with a range would be checked against it; keep bounds as assumptions instead.
                    if (p.min || p.max) {
                        p.min.reset();
                        p.max.reset();
                        for (OntAxiom& a : o.axioms) {
                            if (range_axiom(a.formula, x.name)) a.used = false;
                        }
                    }
                }
                if (!p.sort.empty()) used_sorts.insert(p.sort);
            }
            root.properties.push_back(std::move(p));
        }
        // Sorts the generated K will not declare again.
        for (const OntDecl& x : o.decls) {
            if (x.kind == "sort" && !used_sorts.contains(x.name)) declarations.insert(declarations.begin(), trim(x.raw));
        }
        std::size_t k = 0;
        for (const OntAxiom& a : o.axioms) {
            if (a.used) continue;
            Assumption as;
            as.id = "as_" + std::to_string(++k);
            as.name = is_name(a.id) ? a.id : "assumption_" + std::to_string(k);
            as.expr = expression_of(a.formula);
            as.description = a.comment;
            root.assumptions.push_back(std::move(as));
        }
        for (const std::string& u : o.unknown) declarations.push_back(trim(u));
        d.declarations = std::move(declarations);
        if (!d.declarations.empty()) {
            out.diagnostics.push_back({"info", "TML201", std::to_string(d.declarations.size()) + " ontology declarations are kept verbatim (functions with arguments or unused sorts).",
                                       "", "model.declarations", ""});
        }
    }
    // Interpretations that use symbols the ontology did not declare stay raw; the compiler will report them.
    d.types.push_back(std::move(root));
    out.document = std::move(d);
    return out;
}

Reference reference_suggestion(const Document& model, const authoring::Model& automaton) {
    Reference ref;
    ref.automaton = automaton;
    std::vector<Diagnostic> ignored;
    const detail::Resolved r(model, ignored);
    if (r.root() == nullptr) return ref;
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::map<std::string, std::string> states;    // lower name -> path
    std::map<std::string, std::string> triggers;  // lower name -> path
    for (const auto& inst : r.instances()) {
        const std::string prefix = inst->dotted.empty() ? std::string{} : inst->dotted + ".";
        if (inst->type->behavior != nullptr && !inst->external) {
            for (const State& s : inst->type->behavior->states) states.try_emplace(lower(s.name), prefix + s.name);
        }
        for (const Event* e : inst->type->events) triggers.try_emplace(lower(e->name), prefix + e->name);
        for (const Action* a : inst->type->actions) triggers.try_emplace(lower(a->name), prefix + a->name);
    }
    for (const auto& l : automaton.locations) {
        if (const auto it = states.find(lower(l.name)); it != states.end()) ref.states[l.name] = ReferenceState{{it->second}, ""};
    }
    for (const auto& c : automaton.channels) {
        if (const auto it = triggers.find(lower(c.name)); it != triggers.end()) ref.labels[c.name] = ReferenceLabel{it->second, false};
    }
    return ref;
}

bool same_automaton(const authoring::Model& a, const authoring::Model& b, std::string* why) {
    const auto say = [&](const std::string& s) {
        if (why != nullptr) *why = s;
        return false;
    };
    if (a.name != b.name) return say("template name " + a.name + " vs " + b.name);
    const auto names = [](const auto& v) {
        std::vector<std::string> out;
        for (const auto& x : v) out.push_back(x.name);
        std::sort(out.begin(), out.end());
        return out;
    };
    if (names(a.clocks) != names(b.clocks)) return say("clocks differ");
    if (names(a.channels) != names(b.channels)) return say("channels differ");
    std::vector<std::pair<std::string, std::int64_t>> ca;
    std::vector<std::pair<std::string, std::int64_t>> cb;
    for (const auto& c : a.constants) ca.emplace_back(c.name, c.value);
    for (const auto& c : b.constants) cb.emplace_back(c.name, c.value);
    std::sort(ca.begin(), ca.end());
    std::sort(cb.begin(), cb.end());
    if (ca != cb) return say("constants differ");
    const auto sorted = [](authoring::Constraint c) {
        std::sort(c.begin(), c.end(), [](const authoring::Atom& x, const authoring::Atom& y) {
            return std::tie(x.clock, x.minus, x.op, x.bound.value) < std::tie(y.clock, y.minus, y.op, y.bound.value);
        });
        c.erase(std::unique(c.begin(), c.end()), c.end());
        return c;
    };
    std::map<std::string, std::pair<bool, authoring::Constraint>> la;
    std::map<std::string, std::pair<bool, authoring::Constraint>> lb;
    for (const auto& l : a.locations) la[l.name] = {l.initial, sorted(l.invariant)};
    for (const auto& l : b.locations) lb[l.name] = {l.initial, sorted(l.invariant)};
    if (la.size() != lb.size()) return say("location count " + std::to_string(la.size()) + " vs " + std::to_string(lb.size()));
    for (const auto& [n, v] : la) {
        const auto it = lb.find(n);
        if (it == lb.end()) return say("location " + n + " missing");
        if (it->second.first != v.first) return say("initial location differs at " + n);
        if (it->second.second != v.second) return say("invariant of " + n + " differs");
    }
    using EdgeKey = std::tuple<std::string, std::string, std::string, authoring::Constraint, std::vector<std::string>>;
    const auto edges = [&](const authoring::Model& m) {
        std::vector<EdgeKey> out;
        for (const auto& e : m.edges) {
            std::vector<std::string> r = e.resets;
            std::sort(r.begin(), r.end());
            out.emplace_back(e.source, e.target, authoring::edge_label(e), sorted(e.guard), r);
        }
        std::sort(out.begin(), out.end(), [](const EdgeKey& x, const EdgeKey& y) {
            const auto& [s1, t1, l1, g1, r1] = x;
            const auto& [s2, t2, l2, g2, r2] = y;
            if (std::tie(s1, t1, l1, r1) != std::tie(s2, t2, l2, r2)) return std::tie(s1, t1, l1, r1) < std::tie(s2, t2, l2, r2);
            return std::lexicographical_compare(g1.begin(), g1.end(), g2.begin(), g2.end(), [](const authoring::Atom& p, const authoring::Atom& q) {
                return std::tie(p.clock, p.minus, p.op, p.bound.value) < std::tie(q.clock, q.minus, q.op, q.bound.value);
            });
        });
        return out;
    };
    const auto ea = edges(a);
    const auto eb = edges(b);
    if (ea.size() != eb.size()) return say("edge count " + std::to_string(ea.size()) + " vs " + std::to_string(eb.size()));
    for (std::size_t i = 0; i < ea.size(); ++i) {
        if (!(ea[i] == eb[i])) return say("edge " + std::get<0>(ea[i]) + " -" + std::get<2>(ea[i]) + "-> " + std::get<1>(ea[i]) + " differs");
    }
    return true;
}

}  // namespace twin::model
