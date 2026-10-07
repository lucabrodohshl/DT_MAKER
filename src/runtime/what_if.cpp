/**
 * @file what_if.cpp
 * @brief What-if scenarios and timed action availability (see what_if.hpp).
 */
#include "twin/runtime/what_if.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "twin/core/logical_time.hpp"
#include "twin/kernel/semantics.hpp"
#include "twin/runtime/views.hpp"

namespace twin::runtime {
namespace {

using json::Json;

Json tv(Ticks t, TimeBase base) { return time_view(t, base); }

Json configuration_json(const kernel::Model& m, const kernel::Configuration& c) {
    Json clocks = Json::object();
    for (std::size_t i = 0; i < c.clocks.size(); ++i) clocks[m.ir().clocks[i]] = tv(c.clocks[i], m.time_base());
    return Json{{"location", m.ir().locations.at(c.location).id}, {"clocks", clocks}, {"time", tv(c.time, m.time_base())}};
}

Json set_json(const kernel::Model& m, const kernel::StateSet& s) {
    Json configs = Json::array();
    for (const kernel::Configuration& c : s.members()) configs.push_back(configuration_json(m, c));
    return Json{{"time", tv(s.time(), m.time_base())}, {"configurations", configs}, {"deterministic", s.is_singleton()}};
}

/// Propositions that hold in every possible configuration.
Json propositions_json(const kernel::Model& m, const kernel::StateSet& s) {
    std::vector<ir::PropositionIndex> certain;
    bool first = true;
    for (const kernel::Configuration& c : s.members()) {
        std::vector<ir::PropositionIndex> p = kernel::propositions(m, c);
        std::sort(p.begin(), p.end());
        if (first) {
            certain = std::move(p);
            first = false;
        } else {
            std::vector<ir::PropositionIndex> both;
            std::set_intersection(certain.begin(), certain.end(), p.begin(), p.end(), std::back_inserter(both));
            certain = std::move(both);
        }
    }
    Json out = Json::array();
    for (ir::PropositionIndex p : certain) {
        const ir::Proposition& prop = m.ir().propositions.at(p);
        out.push_back(Json{{"id", prop.id}, {"interpretation", prop.interpretation}});
    }
    return out;
}

const char* origin_name(kernel::WindowOrigin o) {
    switch (o) {
        case kernel::WindowOrigin::SourceInvariant: return "source_invariant";
        case kernel::WindowOrigin::Guard: return "guard";
        case kernel::WindowOrigin::TargetInvariant: return "target_invariant";
    }
    return "guard";
}

std::string origin_text(kernel::WindowOrigin o) {
    switch (o) {
        case kernel::WindowOrigin::SourceInvariant: return "the source location's invariant";
        case kernel::WindowOrigin::Guard: return "the guard";
        case kernel::WindowOrigin::TargetInvariant: return "the target location's invariant (after resets)";
    }
    return "the guard";
}

/// The delay window of one alternative (transition from one possible configuration).
struct Alternative {
    std::uint32_t member{0};
    ir::TransitionIndex transition{0};
    kernel::WindowExplanation explanation;
};

Json alternative_json(const kernel::Model& m, const kernel::StateSet& s, const Alternative& a) {
    const TimeBase base = m.time_base();
    const ir::Transition& t = m.transition(a.transition);
    const Ticks now = s.time();
    Json resets = Json::array();
    for (ir::ClockIndex r : t.resets) resets.push_back(ir::clock_name(m.ir(), r));
    Json factors = Json::array();
    for (const kernel::WindowFactor& f : a.explanation.factors) {
        factors.push_back(Json{{"origin", origin_name(f.origin)},
                               {"atom", ir::to_string(m.ir(), f.atom)},
                               {"value_now", tv(f.value_now, base)},
                               {"bound", tv(f.bound, base)},
                               {"depends_on_delay", f.depends_on_delay},
                               {"min_delay", f.min_delay ? tv(*f.min_delay, base) : Json()},
                               {"max_delay", f.max_delay ? tv(*f.max_delay, base) : Json()},
                               {"never", f.never}});
    }
    Json window;
    const auto& w = a.explanation.window;
    if (w) {
        window = Json{{"earliest", tv(w->earliest, base)},
                      {"latest", w->latest ? tv(*w->latest, base) : Json()},
                      {"earliest_at", tv(now + w->earliest, base)},
                      {"latest_at", w->latest ? tv(now + *w->latest, base) : Json()}};
    }
    return Json{{"member", a.member},
                {"location", m.ir().locations.at(s.members()[a.member].location).id},
                {"transition", t.id},
                {"source", m.ir().locations.at(t.source).id},
                {"target", m.ir().locations.at(t.target).id},
                {"guard", ir::to_string(m.ir(), t.guard)},
                {"resets", resets},
                {"window", window},
                {"enabled_now", w.has_value() && w->earliest == 0},
                {"factors", factors}};
}

std::vector<Alternative> alternatives(const kernel::Model& m, const kernel::StateSet& s) {
    std::vector<Alternative> out;
    for (std::uint32_t i = 0; i < s.members().size(); ++i) {
        const kernel::Configuration& c = s.members()[i];
        for (ir::TransitionIndex t : m.outgoing(c.location)) {
            out.push_back(Alternative{i, t, kernel::explain_window(m, c, t)});
        }
    }
    return out;
}

/// Union of delay windows as a sorted list of disjoint intervals (exact on the tick grid).
Json union_json(std::vector<kernel::DelayWindow> ws, Ticks now, TimeBase base) {
    std::sort(ws.begin(), ws.end(), [](const auto& a, const auto& b) { return a.earliest < b.earliest; });
    std::vector<kernel::DelayWindow> merged;
    for (const kernel::DelayWindow& w : ws) {
        if (!merged.empty()) {
            kernel::DelayWindow& last = merged.back();
            if (!last.latest || w.earliest <= *last.latest + 1) {  // overlapping or adjacent ticks
                if (last.latest && (!w.latest || *w.latest > *last.latest)) last.latest = w.latest;
                continue;
            }
        }
        merged.push_back(w);
    }
    Json out = Json::array();
    for (const auto& w : merged) {
        out.push_back(Json{{"earliest", tv(w.earliest, base)},
                           {"latest", w.latest ? tv(*w.latest, base) : Json()},
                           {"earliest_at", tv(now + w.earliest, base)},
                           {"latest_at", w.latest ? tv(now + *w.latest, base) : Json()}});
    }
    return out;
}

std::string label_of(const kernel::Model& m, ir::TransitionIndex t) { return m.transition(t).action.label(); }

/// Reason for an event that no current location offers.
std::string elsewhere_text(const kernel::Model& m, const std::string& label, std::optional<ir::TransitionIndex> only) {
    std::set<std::string> from;
    for (std::size_t i = 0; i < m.transition_count(); ++i) {
        const auto idx = static_cast<ir::TransitionIndex>(i);
        if (only ? idx == *only : label_of(m, idx) == label) from.insert(m.ir().locations.at(m.transition(idx).source).id);
    }
    if (from.empty()) return "the model has no such event";
    std::string where;
    for (const auto& l : from) where += (where.empty() ? "" : ", ") + l;
    return "no transition with this event leaves the current location; it leaves only " + where;
}

/// Why one alternative cannot fire after @p delay, citing the atoms that bound its window.
std::string reason_text(const kernel::Model& m, const kernel::WindowExplanation& x, Ticks now, Ticks delay) {
    const TimeBase base = m.time_base();
    const auto& w = x.window;
    auto cite = [&](auto pick) {
        std::string out;
        for (const kernel::WindowFactor& f : x.factors) {
            if (pick(f)) out += ", because " + origin_text(f.origin) + " requires " + ir::to_string(m.ir(), f.atom);
        }
        return out;
    };
    if (!w) return "can never fire from this state" + cite([](const kernel::WindowFactor& f) { return f.never; });
    if (delay < w->earliest) {
        return "too early: the earliest permitted time is t = " + format_time(now + w->earliest, base) + " (+" + format_time(w->earliest, base) + ")" +
               cite([&](const kernel::WindowFactor& f) { return f.min_delay && *f.min_delay == w->earliest; });
    }
    if (w->latest && delay > *w->latest) {
        return "too late: the latest permitted time is t = " + format_time(now + *w->latest, base) + " (+" + format_time(*w->latest, base) + ")" +
               cite([&](const kernel::WindowFactor& f) { return f.max_delay && *f.max_delay == *w->latest; });
    }
    return "admissible at this delay";
}

/// Why requesting @p label (or transition) after @p delay is impossible, from the windows' factors.
Json refusal_explanation(const kernel::Model& m, const kernel::StateSet& s, Ticks delay,
                         const std::string& label, std::optional<ir::TransitionIndex> only) {
    const TimeBase base = m.time_base();
    Json alts = Json::array();
    Json reasons = Json::array();
    for (const Alternative& a : alternatives(m, s)) {
        if (only ? a.transition != *only : label_of(m, a.transition) != label) continue;
        alts.push_back(alternative_json(m, s, a));
        const ir::Transition& t = m.transition(a.transition);
        const std::string why = reason_text(m, a.explanation, s.time(), delay);
        reasons.push_back(Json{{"transition", t.id}, {"target", m.ir().locations.at(t.target).id}, {"reason", why}});
    }
    if (alts.empty()) reasons.push_back(Json{{"transition", nullptr}, {"target", nullptr}, {"reason", elsewhere_text(m, label, only)}});
    return Json{{"requested_delay", tv(delay, base)}, {"alternatives", alts}, {"reasons", reasons}};
}

Result<Ticks> parse_decimal(const Json& j, const char* field, TimeBase base) {
    if (!j.contains(field)) return make_error(ErrorCode::InvalidArgument, "missing time").with("field", field);
    if (!j[field].is_string()) {
        return make_error(ErrorCode::InvalidArgument, "times are exact decimal strings (for example \"2.5\"), never JSON numbers")
            .with("field", field);
    }
    return parse_time(j[field].get<std::string>(), base);
}

Result<kernel::StateSet> start_state(const kernel::Model& m, const kernel::StateSet& current, const Json& start) {
    const std::string kind = start.value("kind", std::string("current"));
    if (kind == "current") return current;
    if (kind == "initial") {
        Result<kernel::Configuration> c0 = kernel::initial_configuration(m);
        if (!c0) return std::move(c0).error();
        return kernel::StateSet::of(c0.value());
    }
    if (kind != "configurations") {
        return make_error(ErrorCode::InvalidArgument, "start.kind must be current, initial or configurations").with("kind", kind);
    }
    std::vector<kernel::Configuration> members;
    for (const Json& j : start.value("configurations", Json::array())) {
        kernel::Configuration c;
        const std::string loc = j.value("location", std::string());
        bool found = false;
        for (std::size_t i = 0; i < m.location_count(); ++i) {
            if (m.ir().locations[i].id == loc) {
                c.location = static_cast<ir::LocationIndex>(i);
                found = true;
            }
        }
        if (!found) return make_error(ErrorCode::InvalidArgument, "no such location").with("location", loc);
        c.clocks.assign(m.clock_count(), 0);
        const Json clocks = j.value("clocks", Json::object());
        for (std::size_t i = 0; i < m.clock_count(); ++i) {
            const std::string& name = m.ir().clocks[i];
            if (clocks.contains(name)) {
                Result<Ticks> v = parse_decimal(clocks, name.c_str(), m.time_base());
                if (!v) return std::move(v).error();
                c.clocks[i] = v.value();
            }
        }
        Result<Ticks> t = parse_decimal(j, "time", m.time_base());
        if (!t) return std::move(t).error();
        c.time = t.value();
        if (!kernel::satisfies(c, m.invariant(c.location))) {
            return make_error(ErrorCode::InvariantViolation, "the configuration violates its location invariant")
                .with("location", loc)
                .with("invariant", ir::to_string(m.ir(), m.ir().locations[c.location].invariant));
        }
        members.push_back(std::move(c));
    }
    return kernel::StateSet::of(std::move(members));
}

}  // namespace

Json availability_view(const kernel::Model& m, const kernel::StateSet& s) {
    const TimeBase base = m.time_base();
    struct Event {
        Json alternatives = Json::array();
        std::vector<kernel::DelayWindow> windows;
        bool now{false};
    };
    std::map<std::string, Event> events;
    for (const Alternative& a : alternatives(m, s)) {
        Event& e = events[label_of(m, a.transition)];
        e.alternatives.push_back(alternative_json(m, s, a));
        if (a.explanation.window) {
            e.windows.push_back(*a.explanation.window);
            e.now = e.now || a.explanation.window->earliest == 0;
        }
    }
    Json availability = Json::array();
    for (auto& [label, e] : events) {
        const std::string status = e.now ? "now" : e.windows.empty() ? "blocked" : "later";
        availability.push_back(Json{{"label", label},
                                    {"status", status},
                                    {"intervals", union_json(e.windows, s.time(), base)},
                                    {"alternatives", e.alternatives}});
    }
    // Events the model has but no current location offers: reachable only after another event.
    std::map<std::string, std::set<std::string>> elsewhere;
    for (std::size_t i = 0; i < m.transition_count(); ++i) {
        const auto idx = static_cast<ir::TransitionIndex>(i);
        const std::string l = label_of(m, idx);
        if (!events.contains(l)) elsewhere[l].insert(m.ir().locations.at(m.transition(idx).source).id);
    }
    Json unavailable = Json::array();
    for (const auto& [label, locs] : elsewhere) unavailable.push_back(Json{{"label", label}, {"from_locations", Json(locs)}});
    // Largest admissible delay: time may advance while at least one configuration admits it.
    std::optional<Ticks> max_delay = Ticks{0};
    for (const kernel::Configuration& c : s.members()) {
        const std::optional<Ticks> md = kernel::max_delay(m, c);
        if (!md) {
            max_delay.reset();
            break;
        }
        max_delay = std::max(*max_delay, *md);
    }
    Json deadline_atoms = Json::array();
    for (const kernel::Configuration& c : s.members()) {
        const ir::Conjunction& inv = m.ir().locations.at(c.location).invariant;
        if (!inv.empty()) deadline_atoms.push_back(Json{{"location", m.ir().locations.at(c.location).id}, {"invariant", ir::to_string(m.ir(), inv)}});
    }
    return Json{{"state", set_json(m, s)},
                {"propositions", propositions_json(m, s)},
                {"max_delay", max_delay ? tv(*max_delay, base) : Json()},
                {"max_delay_at", max_delay ? tv(s.time() + *max_delay, base) : Json()},
                {"invariants", deadline_atoms},
                {"availability", availability},
                {"unavailable", unavailable}};
}

namespace {

/// A delay step: let time advance (refused if no configuration's invariant admits it).
Result<Json> delay_step(const kernel::Model& m, kernel::StateSet& s, const Json& step, Json out, bool& invalid) {
    const TimeBase base = m.time_base();
    const Ticks now = s.time();
    // {"until": t} lets time pass up to the absolute time t (no-op when t is not in the future).
    Result<Ticks> d = Ticks{0};
    if (!step.contains("delay") && step.contains("until")) {
        Result<Ticks> until = parse_decimal(step, "until", base);
        if (!until) return std::move(until).error();
        d = std::max<Ticks>(0, until.value() - now);
    } else {
        d = parse_decimal(step, "delay", base);
    }
    if (!d) return std::move(d).error();
    out["requested"] = Json{{"delay", tv(d.value(), base)}, {"at", tv(now + d.value(), base)}};
    Result<kernel::StateSet> r = d.value() < 0 ? Result<kernel::StateSet>(make_error(ErrorCode::InvalidArgument, "a delay cannot be negative"))
                                               : kernel::advance_to(m, s, now + d.value());
    if (!r) {
        invalid = true;
        out["status"] = "invalid";
        out["error"] = Json{{"code", to_string(r.error().code)}, {"message", r.error().message}};
        const Json av = availability_view(m, s);
        out["explanation"] = Json{{"max_delay", av["max_delay"]}, {"max_delay_at", av["max_delay_at"]}, {"invariants", av["invariants"]}};
        return out;
    }
    s = r.value();
    out["status"] = "ok";
    out["after"] = set_json(m, s);
    out["propositions"] = propositions_json(m, s);
    return out;
}

/// The time an event step asks for: absolute `at`, or `delay` after the current time.
Result<Ticks> event_time(const Json& step, Ticks now, TimeBase base) {
    if (step.contains("at")) return parse_decimal(step, "at", base);
    if (step.contains("delay")) {
        Result<Ticks> d = parse_decimal(step, "delay", base);
        if (!d) return std::move(d).error();
        return now + d.value();
    }
    return now;
}

/// An event step: the observation of a label (or of one chosen transition) at a time.
Result<Json> event_step(const kernel::Model& m, kernel::StateSet& s, const Json& step, Json out, bool& invalid) {
    const TimeBase base = m.time_base();
    const Ticks now = s.time();
    Result<Ticks> at_r = event_time(step, now, base);
    if (!at_r) return std::move(at_r).error();
    const Ticks at = at_r.value();
    std::optional<kernel::Selector> selector;
    std::optional<ir::TransitionIndex> only;
    std::string label = step.value("label", std::string());
    if (step.contains("transition")) {
        const std::string id = step.value("transition", std::string());
        for (std::size_t t = 0; t < m.transition_count(); ++t) {
            if (m.transition(static_cast<ir::TransitionIndex>(t)).id == id) only = static_cast<ir::TransitionIndex>(t);
        }
        if (!only) return make_error(ErrorCode::InvalidArgument, "the model has no such transition").with("transition", id);
        selector = kernel::Selector::transition(*only);
        label = label_of(m, *only);
    } else if (std::optional<kernel::LabelId> l = m.label_id(label)) {
        selector = kernel::Selector::label(*l);
    }
    out["requested"] = Json{{"label", label}, {"transition", only ? Json(m.transition(*only).id) : Json()},
                            {"delay", tv(at - now, base)}, {"at", tv(at, base)}};
    auto refuse = [&](Json error, Json explanation) {
        invalid = true;
        out["status"] = "invalid";
        out["error"] = std::move(error);
        if (!explanation.is_null()) out["explanation"] = std::move(explanation);
        return out;
    };
    if (!selector) return refuse(Json{{"code", "invalid_argument"}, {"message", "the model has no event with this label"}}, Json());
    if (at < now) {
        return refuse(Json{{"code", "time_regression"},
                           {"message", "the requested time " + format_time(at, base) + " is before the scenario's current time " + format_time(now, base)}},
                      Json());
    }
    Result<kernel::ObservationOutcome> r = kernel::observe(m, s, at, *selector);
    if (!r) {
        return refuse(Json{{"code", to_string(r.error().code)}, {"message", r.error().message}},
                      refusal_explanation(m, s, at - now, label, only));
    }
    Json branches = Json::array();
    for (const kernel::Branch& b : r.value().branches) {
        const ir::Transition& t = m.transition(b.transition);
        branches.push_back(Json{{"from", b.from}, {"to", b.to}, {"transition", t.id}, {"label", t.action.label()},
                                {"source", m.ir().locations.at(t.source).id}, {"target", m.ir().locations.at(t.target).id}});
    }
    s = r.value().after;
    out["status"] = "ok";
    out["branches"] = branches;
    out["after"] = set_json(m, s);
    out["propositions"] = propositions_json(m, s);
    return out;
}

}  // namespace

Result<Json> what_if(const kernel::Model& m, const kernel::StateSet& current, const Json& request) {
    Result<kernel::StateSet> started = start_state(m, current, request.value("start", Json::object()));
    if (!started) return std::move(started).error();
    kernel::StateSet s = started.value();
    const Json start_json = Json{{"kind", request.value("start", Json::object()).value("kind", std::string("current"))}, {"state", set_json(m, s)}};

    Json steps = Json::array();
    std::optional<std::size_t> first_invalid;
    const Json requested = request.value("steps", Json::array());
    for (std::size_t i = 0; i < requested.size(); ++i) {
        const Json& step = requested[i];
        const std::string kind = step.value("kind", std::string("event"));
        Json out{{"index", i}, {"kind", kind}};
        if (first_invalid) {
            out["status"] = "not_evaluated";
            out["note"] = "follows an invalid step";
            steps.push_back(out);
            continue;
        }
        if (kind != "delay" && kind != "event") return make_error(ErrorCode::InvalidArgument, "step.kind must be delay or event").with("kind", kind);
        bool invalid = false;
        Result<Json> r = kind == "delay" ? delay_step(m, s, step, out, invalid) : event_step(m, s, step, out, invalid);
        if (!r) return std::move(r).error();
        if (invalid) first_invalid = i;
        steps.push_back(r.value());
    }
    return Json{{"start", start_json},
                {"steps", steps},
                {"first_invalid", first_invalid ? Json(*first_invalid) : Json()},
                {"final", availability_view(m, s)},
                {"note", "computed by the semantic kernel on copies of the state; nothing is recorded and the live "
                         "state is not modified. Windows are exact (tick grid); an event's intervals are the union of "
                         "its alternatives' windows."}};
}

}  // namespace twin::runtime
