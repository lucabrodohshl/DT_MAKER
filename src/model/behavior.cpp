// The behaviour compiler: one machine per asset with a behaviour, group and timeout
// expansion, links, "ignore" self-loops, determinism checks, synchronous product,
// emission as twin-ta/1 (04-compilation.md, section 4.5).
#include "behavior.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <set>

#include "twin/authoring/validate.hpp"
#include "twin/model/compile.hpp"

namespace twin::model::detail {

json::Json trace_entry(const std::string& kind, const std::string& name, const std::vector<Source>& sources) {
    json::Json s = json::Json::array();
    for (const Source& x : sources) s.push_back({{"id", x.id}, {"rule", x.rule}});
    return {{"kind", kind}, {"name", name}, {"sources", s}};
}

namespace {

using authoring::Atom;
using authoring::Bound;

struct MEdge {
    std::size_t src{0};
    std::size_t dst{0};
    std::optional<std::string> label;  // channel; nullopt = internal (tau)
    char direction{'!'};
    std::vector<Atom> guard;
    std::vector<std::string> resets;
    std::vector<Source> trace;
    std::string note;
    bool ignore{false};    // the member through which the label enters tolerates "unexpected"
    bool timeout{false};
};

struct MState {
    std::string name;      // location name (component)
    const State* state{nullptr};
    std::vector<Atom> invariant;
};

struct Machine {
    const Instance* inst{nullptr};
    const Behavior* b{nullptr};
    std::string type_name;           // owner of the behaviour (for constants)
    bool root_type{false};
    std::vector<MState> states;
    std::size_t initial{0};
    std::vector<MEdge> edges;
    std::vector<std::string> clocks;
};

class Compiler {
public:
    Compiler(const Resolved& r, std::vector<Diagnostic>& out) : r_(r), out_(out) {}

    BehaviorOutput run() {
        BehaviorOutput result;
        for (const auto& inst : r_.instances()) {
            if (inst->external || inst->type->behavior == nullptr) continue;
            machines_.push_back(machine(*inst));
        }
        result.machines = machines_.size();
        if (failed_) return result;
        links();
        ignores();
        for (const Machine& m : machines_) checks(m);
        if (failed_ || machines_.empty()) {
            channels_out(result);
            return result;
        }
        emit(result);
        return result;
    }

private:
    // ------------------------------------------------------------------ helpers

    void err(const char* code, const std::string& message, const std::string& hint, const std::string& path, const std::string& id) {
        out_.push_back({"error", code, message, hint, path, id});
        failed_ = true;
    }

    static std::string path_of(const Machine& m) { return "types[" + m.type_name + "].behavior"; }

    std::string constant_name(const Machine& m, const std::string& n) const { return m.root_type ? n : m.type_name + "__" + n; }

    Bound bound(const Machine& m, const Bound& b) const {
        if (const auto* s = std::get_if<std::string>(&b.value)) return Bound{constant_name(m, *s)};
        return b;
    }

    std::int64_t bound_value(const Machine& m, const Bound& b) const {
        if (const auto* i = std::get_if<std::int64_t>(&b.value)) return *i;
        const std::string& n = std::get<std::string>(b.value);
        for (const Constant& c : m.b->constants) {
            if (c.name == n || constant_name(m, c.name) == n) return c.value;
        }
        return 0;
    }

    Atom atom(const Machine& m, const TimeAtom& a) const {
        Atom x;
        x.clock = Resolved::qualify(*m.inst, a.timer);
        if (a.minus) x.minus = Resolved::qualify(*m.inst, *a.minus);
        x.op = a.op;
        x.bound = bound(m, a.bound);
        return x;
    }

    const Timer* state_timer(const Behavior& b, const std::string& name) const {
        if (!name.empty()) {
            for (const Timer& t : b.timers) {
                if (t.name == name) return &t;
            }
            return nullptr;
        }
        for (const Timer& t : b.timers) {
            if (t.restart == Restart::OnStateChange) return &t;
        }
        return b.timers.empty() ? nullptr : &b.timers.front();
    }

    void channel(const ChannelInfo& info) {
        if (std::none_of(channels_.begin(), channels_.end(), [&](const ChannelInfo& c) { return c.name == info.name; })) channels_.push_back(info);
    }

    // ------------------------------------------------------------------ instantiation (B1, B2)

    Machine machine(const Instance& inst) {
        Machine m;
        m.inst = &inst;
        m.b = inst.type->behavior;
        m.type_name = inst.type->behavior_owner->name;
        m.root_type = m.type_name == r_.doc().root;
        const Behavior& b = *m.b;
        for (const Timer& t : b.timers) m.clocks.push_back(Resolved::qualify(inst, t.name));
        std::map<std::string, std::size_t> index;
        for (const State& s : b.states) {
            MState ms;
            ms.name = inst.segments.empty() ? s.name : Resolved::location_component(inst, s.name);
            ms.state = &s;
            for (const TimeAtom& a : s.limits) ms.invariant.push_back(atom(m, a));
            index[s.name] = m.states.size();
            if (s.initial) m.initial = m.states.size();
            m.states.push_back(std::move(ms));
        }
        for (const Transition& tr : b.transitions) {
            // Expand sources (states and groups), keeping the first occurrence of each state.
            std::vector<std::pair<std::string, bool>> sources;  // state, via group
            for (const std::string& f : tr.from) {
                bool group = false;
                for (const Group& g : b.groups) {
                    if (g.name == f) {
                        group = true;
                        for (const std::string& s : g.states) sources.emplace_back(s, true);
                    }
                }
                if (!group) sources.emplace_back(f, false);
            }
            std::set<std::string> done;
            for (const auto& [src, via_group] : sources) {
                if (!done.insert(src).second) continue;
                const auto si = index.find(src);
                const auto ti = index.find(tr.to);
                if (si == index.end() || ti == index.end()) continue;  // reported by validation
                MEdge e;
                e.src = si->second;
                e.dst = ti->second;
                e.note = tr.description;
                e.trace.push_back({tr.id, via_group ? "B2.group" : "B2.transition"});
                switch (tr.trigger.kind) {
                    case TriggerKind::Event: {
                        const auto ref = r_.member(inst, tr.trigger.ref);
                        if (!ref || ref->event == nullptr) continue;
                        e.label = Resolved::qualify(*ref->owner, ref->event->name);
                        e.direction = ref->event->direction;
                        e.ignore = ref->event->unexpected == Unexpected::Ignore;
                        channel({*e.label, ChannelInfo::Kind::Event, ref->owner, ref->event, nullptr, nullptr, ref->event->direction});
                        break;
                    }
                    case TriggerKind::Action: {
                        const auto ref = r_.member(inst, tr.trigger.ref);
                        if (!ref || ref->action == nullptr) continue;
                        e.label = Resolved::qualify(*ref->owner, ref->action->name);
                        e.direction = ref->action->direction;
                        e.ignore = ref->action->unexpected == Unexpected::Ignore;
                        channel({*e.label, ChannelInfo::Kind::Action, ref->owner, nullptr, ref->action, nullptr, ref->action->direction});
                        break;
                    }
                    case TriggerKind::Timeout: {
                        const Timer* t = state_timer(b, tr.trigger.timer);
                        if (t == nullptr) {
                            err("TML052", "The timeout of transition '" + tr.id + "' needs a timer.", "Add a timer (normally 't', reset on every state change).",
                                path_of(m) + ".transitions[" + tr.id + "]", tr.id);
                            continue;
                        }
                        const State* st = m.states[e.src].state;
                        e.label = Resolved::qualify(inst, st->name + "_timeout");
                        e.timeout = true;
                        e.trace.back().rule = "B2.timeout";
                        Atom g;
                        g.clock = Resolved::qualify(inst, t->name);
                        g.op = ir::Comparison::GreaterEqual;
                        g.bound = bound(m, tr.trigger.after);
                        e.guard.push_back(g);
                        Atom inv = g;
                        inv.op = ir::Comparison::LessEqual;
                        auto& invariant = m.states[e.src].invariant;
                        if (std::find(invariant.begin(), invariant.end(), inv) == invariant.end()) invariant.push_back(inv);
                        channel({*e.label, ChannelInfo::Kind::Timeout, &inst, nullptr, nullptr, st, '!'});
                        break;
                    }
                    case TriggerKind::Internal: break;
                }
                for (const TimeAtom& a : tr.window) e.guard.push_back(atom(m, a));
                for (const Timer& t : b.timers) {
                    const bool policy = t.restart == Restart::OnStateChange && e.src != e.dst;
                    const bool listed = std::find(tr.restart.begin(), tr.restart.end(), t.name) != tr.restart.end();
                    if (policy || listed) e.resets.push_back(Resolved::qualify(inst, t.name));
                }
                m.edges.push_back(std::move(e));
            }
        }
        return m;
    }

    // ------------------------------------------------------------------ links (B3)

    void links() {
        for (const auto& inst : r_.instances()) {
            if (inst->external) continue;
            for (const Link* l : inst->type->links) {
                const auto ev = r_.member(*inst, l->from);
                const auto ac = r_.member(*inst, l->to);
                if (!ev || ev->event == nullptr || !ac || ac->action == nullptr) continue;  // reported by validation
                const std::string le = Resolved::qualify(*ev->owner, ev->event->name);
                const std::string la = Resolved::qualify(*ac->owner, ac->action->name);
                channel({le, ChannelInfo::Kind::Event, ev->owner, ev->event, nullptr, nullptr, ev->event->direction});
                for (Machine& m : machines_) {
                    std::vector<MEdge> out;
                    for (MEdge& e : m.edges) {
                        out.push_back(e);
                        if (e.label && *e.label == la && !e.timeout) {
                            MEdge c = e;
                            c.label = le;
                            c.direction = ev->event->direction;
                            c.ignore = ac->action->unexpected == Unexpected::Ignore;
                            c.trace.push_back({l->id, "B3.link"});
                            out.push_back(std::move(c));
                        }
                    }
                    m.edges = std::move(out);
                }
                linked_actions_.insert(la);
                if (!ac->action->requestable) unrequestable_.insert(la);
            }
        }
        for (Machine& m : machines_) {
            std::erase_if(m.edges, [&](const MEdge& e) { return e.label && unrequestable_.contains(*e.label) && e.trace.back().rule != "B3.link"; });
        }
        // Channels of unrequestable actions disappear with their edges.
        std::erase_if(channels_, [&](const ChannelInfo& c) {
            if (!unrequestable_.contains(c.name)) return false;
            return std::none_of(machines_.begin(), machines_.end(), [&](const Machine& m) {
                return std::any_of(m.edges.begin(), m.edges.end(), [&](const MEdge& e) { return e.label && *e.label == c.name; });
            });
        });
    }

    // ------------------------------------------------------------------ ignore self-loops (B2.ignore)

    void ignores() {
        for (Machine& m : machines_) {
            std::vector<std::string> order;
            std::map<std::string, std::pair<bool, char>> policy;  // label -> (ignore, direction)
            std::map<std::string, std::vector<Source>> member_of;
            for (const MEdge& e : m.edges) {
                if (!e.label || e.timeout) continue;
                if (!policy.contains(*e.label)) {
                    order.push_back(*e.label);
                    policy[*e.label] = {false, e.direction};
                }
                if (e.ignore) {
                    policy[*e.label].first = true;
                    member_of[*e.label] = {e.trace.front()};
                }
            }
            for (const std::string& label : order) {
                if (!policy[label].first) continue;
                for (std::size_t s = 0; s < m.states.size(); ++s) {
                    const bool handled = std::any_of(m.edges.begin(), m.edges.end(), [&](const MEdge& e) { return e.src == s && e.label && *e.label == label; });
                    if (handled) continue;
                    MEdge loop;
                    loop.src = s;
                    loop.dst = s;
                    loop.label = label;
                    loop.direction = policy[label].second;
                    loop.trace = {{m.states[s].state->id, "B2.ignore"}};
                    loop.trace.insert(loop.trace.end(), member_of[label].begin(), member_of[label].end());
                    loop.note = "accepted without effect";
                    m.edges.push_back(std::move(loop));
                }
            }
        }
    }

    // ------------------------------------------------------------------ determinism and timeouts (TML053, TML054, TML056)

    struct Interval {
        std::int64_t lo{0};
        bool lo_strict{false};
        std::int64_t hi{std::numeric_limits<std::int64_t>::max()};
        bool hi_strict{false};
    };

    std::map<std::string, Interval> intervals(const Machine& m, const std::vector<Atom>& guard) const {
        std::map<std::string, Interval> out;
        for (const Atom& a : guard) {
            if (a.minus) continue;
            Interval& iv = out[a.clock];
            const std::int64_t v = bound_value(m, a.bound);
            const auto raise = [&](std::int64_t lo, bool strict) {
                if (lo > iv.lo || (lo == iv.lo && strict)) {
                    iv.lo = lo;
                    iv.lo_strict = strict;
                }
            };
            const auto lower = [&](std::int64_t hi, bool strict) {
                if (hi < iv.hi || (hi == iv.hi && strict)) {
                    iv.hi = hi;
                    iv.hi_strict = strict;
                }
            };
            switch (a.op) {
                case ir::Comparison::Less: lower(v, true); break;
                case ir::Comparison::LessEqual: lower(v, false); break;
                case ir::Comparison::Equal: raise(v, false); lower(v, false); break;
                case ir::Comparison::GreaterEqual: raise(v, false); break;
                case ir::Comparison::Greater: raise(v, true); break;
            }
        }
        return out;
    }

    bool overlap(const Machine& m, const MEdge& a, const MEdge& b) const {
        auto ia = intervals(m, a.guard);
        const auto ib = intervals(m, b.guard);
        for (const auto& [clock, y] : ib) {
            Interval& x = ia[clock];
            const std::int64_t lo = std::max(x.lo, y.lo);
            const bool lo_strict = (x.lo == lo && x.lo_strict) || (y.lo == lo && y.lo_strict);
            const std::int64_t hi = std::min(x.hi, y.hi);
            const bool hi_strict = (x.hi == hi && x.hi_strict) || (y.hi == hi && y.hi_strict);
            if (lo > hi || (lo == hi && (lo_strict || hi_strict))) return false;
        }
        return true;
    }

    void checks(const Machine& m) {
        const bool lenient = r_.doc().allow_nondeterminism;
        for (std::size_t s = 0; s < m.states.size(); ++s) {
            const State& st = *m.states[s].state;
            std::vector<const MEdge*> out;
            for (const MEdge& e : m.edges) {
                if (e.src == s) out.push_back(&e);
            }
            int timeouts = 0;
            for (const MEdge* e : out) timeouts += e->timeout ? 1 : 0;
            if (timeouts > 1) {
                err("TML054", "State '" + st.name + "' has more than one timeout.", "Only the shortest one could ever fire; keep one.", path_of(m) + ".states[" + st.name + "]", st.id);
            }
            for (std::size_t i = 0; i < out.size(); ++i) {
                for (std::size_t j = i + 1; j < out.size(); ++j) {
                    if (out[i]->label != out[j]->label) continue;
                    if (!overlap(m, *out[i], *out[j])) continue;
                    const std::string what = out[i]->label ? "'" + *out[i]->label + "'" : "internal steps";
                    const std::string msg = "In state '" + st.name + "', two transitions on " + what + " can be taken at the same time.";
                    const std::string hint = "Make their time windows disjoint (for example 'before 10 s' and 'not before 10 s').";
                    const std::string id = out[j]->trace.front().id;
                    if (lenient) {
                        out_.push_back({"warning", "TML053", msg + " The twin will track both possibilities.", hint, path_of(m) + ".transitions[" + id + "]", id});
                    } else {
                        err("TML053", msg, hint + " Or allow nondeterminism in the model settings.", path_of(m) + ".transitions[" + id + "]", id);
                    }
                }
            }
            // A timeout cut off by a smaller declared limit, or a declared limit equal to it.
            for (const MEdge* e : out) {
                if (!e->timeout) continue;
                const Atom& g = e->guard.front();
                const std::int64_t d = bound_value(m, g.bound);
                for (const TimeAtom& lim : st.limits) {
                    if (lim.minus || Resolved::qualify(*m.inst, lim.timer) != g.clock) continue;
                    const std::int64_t c = bound_value(m, bound(m, lim.bound));
                    const bool strict = lim.op == ir::Comparison::Less;
                    if (c < d || (strict && c <= d)) {
                        err("TML054", "The timeout after " + std::to_string(d) + " in '" + st.name + "' can never fire: the state limit ends earlier.", "Remove the limit or shorten the timeout.",
                            path_of(m) + ".states[" + st.name + "]", st.id);
                    } else if (c == d && !strict) {
                        out_.push_back({"warning", "TML056", "The limit of '" + st.name + "' repeats its timeout.", "Remove the limit; the timeout already bounds the state.",
                                        path_of(m) + ".states[" + st.name + "]", st.id});
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ product (B4) and emission (B5)

    void channels_out(BehaviorOutput& result) const { result.channels = channels_; }

    void emit(BehaviorOutput& result) {
        authoring::Model model;
        model.name = r_.doc().root;
        // Clocks, constants.
        std::set<std::string> constant_seen;
        for (const Machine& m : machines_) {
            for (std::size_t i = 0; i < m.clocks.size(); ++i) {
                model.clocks.push_back({m.clocks[i], m.b->timers[i].description});
                result.trace.push_back(trace_entry("clock", m.clocks[i], {{m.b->timers[i].id, "B1.timer"}}));
            }
            for (const Constant& c : m.b->constants) {
                const std::string n = constant_name(m, c.name);
                if (!constant_seen.insert(n).second) continue;
                model.constants.push_back({n, c.value, c.description});
                result.trace.push_back(trace_entry("constant", n, {{c.id, "B1.constant"}}));
            }
        }
        // Channel order: first appearance over the machines' edges.
        std::vector<std::string> order;
        for (const Machine& m : machines_) {
            for (const MEdge& e : m.edges) {
                if (e.label && std::find(order.begin(), order.end(), *e.label) == order.end()) order.push_back(*e.label);
            }
        }
        for (const std::string& c : order) model.channels.push_back({c, ""});

        if (machines_.size() == 1) {
            single(model, result);
        } else {
            product(model, result, order);
        }
        if (failed_) return;

        // Channels actually emitted (with what they stand for).
        for (const std::string& c : order) {
            for (const ChannelInfo& info : channels_) {
                if (info.name == c) result.channels.push_back(info);
            }
        }
        for (const ChannelInfo& info : result.channels) {
            std::vector<Source> src;
            if (info.event != nullptr) src.push_back({info.event->id, "B1.channel"});
            if (info.action != nullptr) src.push_back({info.action->id, "B1.channel"});
            if (info.state != nullptr) src.push_back({info.state->id, "B2.timeout"});
            result.trace.push_back(trace_entry("channel", info.name, src));
        }
        collisions(model);
        if (failed_) return;
        const auto findings = authoring::validate(model);
        for (const auto& f : findings) {
            if (f.severity == "error") {
                err("TML099", "Internal: the generated automaton was rejected (" + f.code + ": " + f.message + ").", "Please report this; it is a compiler bug.", "model", "");
            }
        }
        if (!failed_) result.automaton = std::move(model);
    }

    void single(authoring::Model& model, BehaviorOutput& result) {
        const Machine& m = machines_.front();
        for (std::size_t s = 0; s < m.states.size(); ++s) {
            authoring::LocationDecl l;
            l.name = m.states[s].name;
            l.initial = s == m.initial;
            l.invariant = m.states[s].invariant;
            l.note = m.states[s].state->description;
            model.locations.push_back(std::move(l));
            result.locations.push_back({m.states[s].name, {{m.inst, m.states[s].state}}});
            result.trace.push_back(trace_entry("location", m.states[s].name, {{m.states[s].state->id, "B2.state"}}));
        }
        std::size_t k = 0;
        for (const MEdge& e : m.edges) {
            authoring::EdgeDecl d;
            d.id = "e" + std::to_string(++k);
            d.source = m.states[e.src].name;
            d.target = m.states[e.dst].name;
            if (e.label) d.sync = authoring::Sync{*e.label, e.direction};
            d.guard = e.guard;
            d.resets = e.resets;
            d.note = e.note;
            model.edges.push_back(std::move(d));
            result.trace.push_back(trace_entry("edge", "e" + std::to_string(k), e.trace));
        }
    }

    void product(authoring::Model& model, BehaviorOutput& result, const std::vector<std::string>& order) {
        const std::size_t n = machines_.size();
        std::vector<std::set<std::string>> alphabet(n);
        for (std::size_t i = 0; i < n; ++i) {
            for (const MEdge& e : machines_[i].edges) {
                if (e.label) alphabet[i].insert(*e.label);
            }
        }
        using Tuple = std::vector<std::size_t>;
        std::map<Tuple, std::size_t> index;
        std::vector<Tuple> tuples;
        std::deque<std::size_t> queue;
        const auto visit = [&](const Tuple& t) -> std::size_t {
            const auto it = index.find(t);
            if (it != index.end()) return it->second;
            const std::size_t id = tuples.size();
            index.emplace(t, id);
            tuples.push_back(t);
            queue.push_back(id);
            return id;
        };
        Tuple init(n);
        for (std::size_t i = 0; i < n; ++i) init[i] = machines_[i].initial;
        visit(init);

        struct PEdge {
            std::size_t src{0};
            std::size_t dst{0};
            std::optional<std::string> label;
            char direction{'!'};
            std::vector<Atom> guard;
            std::vector<std::string> resets;
            std::vector<Source> trace;
        };
        std::vector<PEdge> edges;
        while (!queue.empty()) {
            const std::size_t u = queue.front();
            queue.pop_front();
            const Tuple t = tuples[u];
            // Internal steps interleave.
            for (std::size_t i = 0; i < n; ++i) {
                for (const MEdge& e : machines_[i].edges) {
                    if (e.src != t[i] || e.label) continue;
                    Tuple v = t;
                    v[i] = e.dst;
                    edges.push_back({u, visit(v), std::nullopt, '!', e.guard, e.resets, e.trace});
                }
            }
            // Labels synchronise every machine that knows them.
            for (const std::string& label : order) {
                std::vector<std::size_t> parts;
                std::vector<std::vector<const MEdge*>> choices;
                bool blocked = false;
                for (std::size_t i = 0; i < n && !blocked; ++i) {
                    if (!alphabet[i].contains(label)) continue;
                    std::vector<const MEdge*> c;
                    for (const MEdge& e : machines_[i].edges) {
                        if (e.src == t[i] && e.label && *e.label == label) c.push_back(&e);
                    }
                    if (c.empty()) blocked = true;
                    parts.push_back(i);
                    choices.push_back(std::move(c));
                }
                if (blocked || parts.empty()) continue;
                std::vector<std::size_t> pick(parts.size(), 0);
                for (;;) {
                    PEdge pe;
                    pe.src = u;
                    pe.label = label;
                    Tuple v = t;
                    for (std::size_t k = 0; k < parts.size(); ++k) {
                        const MEdge& e = *choices[k][pick[k]];
                        v[parts[k]] = e.dst;
                        pe.direction = e.direction;
                        pe.guard.insert(pe.guard.end(), e.guard.begin(), e.guard.end());
                        for (const std::string& r : e.resets) {
                            if (std::find(pe.resets.begin(), pe.resets.end(), r) == pe.resets.end()) pe.resets.push_back(r);
                        }
                        pe.trace.insert(pe.trace.end(), e.trace.begin(), e.trace.end());
                    }
                    pe.dst = visit(v);
                    edges.push_back(std::move(pe));
                    // Odometer over the choices, last machine fastest.
                    bool done = true;
                    for (std::size_t k = parts.size(); k-- > 0;) {
                        if (++pick[k] < choices[k].size()) {
                            done = false;
                            break;
                        }
                        pick[k] = 0;
                    }
                    if (done) break;
                }
            }
            if (tuples.size() > kMaxLocations) {
                std::string breakdown;
                for (const Machine& m : machines_) {
                    breakdown += (breakdown.empty() ? "" : " × ") + (m.inst->dotted.empty() ? r_.doc().root : m.inst->dotted) + " (" + std::to_string(m.states.size()) + ")";
                }
                err("TML060", "The combined behaviour has more than " + std::to_string(kMaxLocations) + " states: " + breakdown + ".",
                    "Reduce the number of assets with behaviour or their states.", "model", "");
                return;
            }
        }
        // Locations.
        std::vector<std::string> names;
        for (const Tuple& t : tuples) {
            std::string name;
            LocationInfo info;
            std::vector<Source> src;
            std::vector<Atom> inv;
            for (std::size_t i = 0; i < n; ++i) {
                const MState& s = machines_[i].states[t[i]];
                name += (name.empty() ? "" : "__") + Resolved::location_component(*machines_[i].inst, s.state->name);
                info.components.emplace_back(machines_[i].inst, s.state);
                src.push_back({s.state->id, "B4.product"});
                inv.insert(inv.end(), s.invariant.begin(), s.invariant.end());
            }
            info.name = name;
            names.push_back(name);
            authoring::LocationDecl l;
            l.name = name;
            l.initial = names.size() == 1;
            l.invariant = inv;
            model.locations.push_back(std::move(l));
            result.locations.push_back(std::move(info));
            result.trace.push_back(trace_entry("location", name, src));
        }
        std::size_t k = 0;
        for (const PEdge& e : edges) {
            authoring::EdgeDecl d;
            d.id = "e" + std::to_string(++k);
            d.source = names[e.src];
            d.target = names[e.dst];
            if (e.label) d.sync = authoring::Sync{*e.label, e.direction};
            d.guard = e.guard;
            d.resets = e.resets;
            model.edges.push_back(std::move(d));
            result.trace.push_back(trace_entry("edge", "e" + std::to_string(k), e.trace));
        }
    }

    void collisions(const authoring::Model& model) {
        std::map<std::string, std::string> seen;
        const auto see = [&](const std::string& name, const std::string& what) {
            const auto [it, fresh] = seen.emplace(name, what);
            if (!fresh) {
                err("TML090", "The generated name '" + name + "' is used by a " + it->second + " and a " + what + ".", "Rename one of the elements.", "model", "");
            }
        };
        for (const auto& c : model.clocks) see(c.name, "timer");
        for (const auto& c : model.constants) see(c.name, "constant");
        for (const auto& c : model.channels) see(c.name, "event or action");
        for (const auto& l : model.locations) see(l.name, "state");
    }

    const Resolved& r_;
    std::vector<Diagnostic>& out_;
    std::vector<Machine> machines_;
    std::vector<ChannelInfo> channels_;
    std::set<std::string> linked_actions_;
    std::set<std::string> unrequestable_;
    bool failed_{false};
};

}  // namespace

BehaviorOutput compile_behavior(const Resolved& model, std::vector<Diagnostic>& diagnostics) {
    return Compiler(model, diagnostics).run();
}

}  // namespace twin::model::detail
