/**
 * @file blueprints_test.cpp
 * @brief BlueprintService: timing windows, scenario tests and data-binding tests.
 *
 * Timing windows and scenario steps are decided by the semantic kernel (twin::runtime::what_if on
 * the compiled Digital Twin View: exact delay windows on the tick grid, explanations derived from
 * the same arithmetic). Scenario results are TEST results, recorded as scenario evidence; they
 * never claim more than "this run behaved as expected".
 */
#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "blueprints_impl.hpp"
#include "twin/alignment/entailment.hpp"
#include "twin/core/logical_time.hpp"
#include "twin/kernel/semantics.hpp"
#include "twin/kernel/state_set.hpp"
#include "twin/monitoring/monitors.hpp"
#include "twin/monitoring/property.hpp"
#include "twin/ptfeed/feed.hpp"
#include "twin/runtime/what_if.hpp"
#include "twin/scene/robot_sim.hpp"
#include "twin/scene/world.hpp"
#include "twin/world/world.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using json::Json;
using namespace twin::platform;

namespace {

/// Rebuild a kernel state set from a what-if state ({configurations: [{location, clocks, time}]}).
Result<kernel::StateSet> state_from_json(const kernel::Model& m, const Json& state) {
    std::vector<kernel::Configuration> members;
    for (const Json& j : state.value("configurations", Json::array())) {
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
            const Json& v = clocks.value(m.ir().clocks[i], Json());
            const std::string text = v.is_object() ? v.value("text", v.value("value", std::string("0"))) : v.is_string() ? v.get<std::string>() : std::string("0");
            auto t = parse_time(text, m.time_base());
            if (t) c.clocks[i] = t.value();
        }
        const Json& tj = j.value("time", Json());
        const std::string ttext = tj.is_object() ? tj.value("text", tj.value("value", std::string("0"))) : tj.is_string() ? tj.get<std::string>() : std::string("0");
        auto t = parse_time(ttext, m.time_base());
        if (!t) return std::move(t).error();
        c.time = t.value();
        members.push_back(std::move(c));
    }
    return kernel::StateSet::of(std::move(members));
}

std::string time_text(const Json& tv) {
    if (tv.is_object()) return tv.value("text", tv.value("value", std::string()));
    if (tv.is_string()) return tv.get<std::string>();
    return {};
}

/// What-if "start" configurations (decimal strings) from a what-if state (time views).
Json start_configurations(const Json& state) {
    Json out = Json::array();
    for (const Json& c : state.value("configurations", Json::array())) {
        Json clocks = Json::object();
        for (const Json items_name_v = c.value("clocks", Json::object()); const auto& [name, v] : items_name_v.items()) clocks[name] = time_text(v);
        out.push_back(Json{{"location", c.value("location", std::string())}, {"clocks", clocks}, {"time", time_text(c.value("time", Json()))}});
    }
    return Json{{"kind", "configurations"}, {"configurations", out}};
}

/// PT label -> DT labels from the alignment evidence's label equivalence E.
std::map<std::string, std::vector<std::string>> label_map(const std::optional<Json>& alignment) {
    std::map<std::string, std::vector<std::string>> out;
    if (!alignment) return out;
    for (const Json& row : alignment->value("label_equivalence", Json::array())) {
        std::vector<std::string> dts;
        for (const Json& d : row.value("dt", Json::array())) dts.push_back(d.get<std::string>());
        out[row.value("pt", std::string())] = dts;
    }
    return out;
}

/// Simple JSON path: "$.a.b[0].c" or "a.b"; returns nullptr when absent.
const Json* select_path(const Json& doc, std::string path) {
    if (path.rfind("$.", 0) == 0) path = path.substr(2);
    else if (path == "$") return &doc;
    const Json* cur = &doc;
    std::size_t i = 0;
    while (i < path.size()) {
        std::size_t j = i;
        while (j < path.size() && path[j] != '.' && path[j] != '[') ++j;
        const std::string key = path.substr(i, j - i);
        if (!key.empty()) {
            if (!cur->is_object() || !cur->contains(key)) return nullptr;
            cur = &cur->at(key);
        }
        while (j < path.size() && path[j] == '[') {
            const std::size_t close = path.find(']', j);
            if (close == std::string::npos) return nullptr;
            const std::size_t idx = static_cast<std::size_t>(std::strtoul(path.substr(j + 1, close - j - 1).c_str(), nullptr, 10));
            if (!cur->is_array() || idx >= cur->size()) return nullptr;
            cur = &(*cur)[idx];
            j = close + 1;
        }
        i = j < path.size() && path[j] == '.' ? j + 1 : j;
    }
    return cur;
}

double decimal_or(const Json& j, const char* key, double fallback) {
    if (!j.contains(key)) return fallback;
    const Json& v = j.at(key);
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        char* end = nullptr;
        const double d = std::strtod(v.get<std::string>().c_str(), &end);
        if (end != v.get<std::string>().c_str()) return d;
    }
    return fallback;
}

/// Apply a world change of a scenario step to a world document copy.
Status apply_world_change(Json& world, const Json& change) {
    const std::string action = change.value("action", std::string("set_property"));
    Json& objects = world["objects"];
    auto find = [&](const std::string& id) -> Json* {
        for (Json& o : objects) {
            if (o.value("id", std::string()) == id) return &o;
        }
        return nullptr;
    };
    if (action == "add_object") {
        const Json o = change.value("object", Json::object());
        if (o.value("id", std::string()).empty()) return make_error(ErrorCode::InvalidArgument, "an added object needs an id");
        if (find(o.value("id", std::string())) != nullptr) return make_error(ErrorCode::InvalidArgument, "the object already exists");
        objects.push_back(o);
        return {};
    }
    const std::string id = change.value("objectId", std::string());
    Json* o = find(id);
    if (o == nullptr) return make_error(ErrorCode::NotFound, "no such world object").with("object", id);
    if (action == "remove_object") {
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Json& x) { return x.value("id", std::string()) == id; }), objects.end());
    } else if (action == "set_property") {
        (*o)["properties"][change.value("property", std::string())] = change.value("value", Json());
    } else if (action == "move_object") {
        (*o)["geometry"] = change.value("geometry", Json::object());
    } else if (action == "set_layer") {
        (*o)["layer"] = change.value("layer", std::string());
    } else {
        return make_error(ErrorCode::InvalidArgument, "world change action is add_object, remove_object, set_property, move_object or set_layer");
    }
    return {};
}

struct ScenarioContext {
    const kernel::Model* model{nullptr};
    std::map<std::string, std::vector<std::string>> pt_to_dt;
    Json data = Json::object();         // data contract (events -> formal labels)
    Json assurance = Json::object();    // monitors
    Json simulation = Json::object();
    std::string ontology;               // texts for semantic expectations
    std::string dt_interpretation;
};

/// The formal label of an observation produced by a world change ({event: <data event>} or {label, level}).
Result<std::pair<std::string, std::string>> observation_label(const ScenarioContext& ctx, const Json& obs) {
    if (obs.contains("label")) return std::make_pair(obs.value("label", std::string()), obs.value("level", std::string("dt")));
    const std::string ev = obs.value("event", std::string());
    for (const Json& e : ctx.data.value("events", Json::array())) {
        if (e.value("id", std::string()) != ev) continue;
        const Json f = e.value("formal", Json::object());
        if (!f.value("dt", std::string()).empty()) return std::make_pair(f.value("dt", std::string()), std::string("dt"));
        if (!f.value("pt", std::string()).empty()) return std::make_pair(f.value("pt", std::string()), std::string("pt"));
        return make_error(ErrorCode::InvalidArgument, "data event '" + ev + "' is not mapped to a formal PT or DT event (Data contract → Events)");
    }
    return make_error(ErrorCode::NotFound, "no data-contract event '" + ev + "'");
}

}  // namespace

// ------------------------------------------------------------------------------ timing

Result<Json> BlueprintService::timing(std::string_view id, std::int64_t v, const Json& request) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    auto compiled = impl_->compile_dt(ver.value());
    if (!compiled) return std::move(compiled).error();
    const kernel::Model& m = *compiled.value()->model;
    auto c0 = kernel::initial_configuration(m);
    if (!c0) return std::move(c0).error();
    Json req = request.is_object() ? request : Json::object();
    if (!req.contains("start")) req["start"] = Json{{"kind", "initial"}};
    // PT-level events are translated through E (from the alignment evidence of exactly these pins).
    const auto e = label_map(impl_->alignment_for(ver.value()));
    Json steps = Json::array();
    for (Json step : req.value("steps", Json::array())) {
        if (step.value("level", std::string("dt")) == "pt" && step.contains("label")) {
            const std::string pt = step.value("label", std::string());
            auto it = e.find(pt);
            if (it == e.end() || it->second.size() != 1) {
                return make_error(ErrorCode::InvalidArgument,
                                  it == e.end() ? "PT label '" + pt + "' has no DT counterpart in the alignment evidence (run the alignment first)"
                                                : "PT label '" + pt + "' corresponds to several DT labels; schedule the DT label instead")
                    .with("label", pt);
            }
            step["label"] = it->second.front();
            step.erase("level");
        }
        steps.push_back(step);
    }
    req["steps"] = steps;
    auto r = runtime::what_if(m, kernel::StateSet::of(c0.value()), req);
    if (!r) return std::move(r).error();
    Json out = r.value();
    out["irSha256"] = compiled.value()->ir_sha256;
    out["timeUnit"] = ver.value().document.value("identity", Json::object()).value("timeUnit", std::string("s"));
    out["authority"] = "semantic kernel (twin::kernel) on the compiled Digital Twin View";
    return out;
}

// --------------------------------------------------------------------------- scenarios

Result<Json> BlueprintService::run_scenarios(std::string_view id, std::int64_t v, const std::optional<std::string>& only,
                                             const Actor& actor) {
    auto ver_r = impl_->load(id, v);
    if (!ver_r) return std::move(ver_r).error();
    const BlueprintVersion ver = ver_r.value();
    auto compiled = impl_->compile_dt(ver);
    if (!compiled) return std::move(compiled).error();
    const kernel::Model& m = *compiled.value()->model;
    auto c0 = kernel::initial_configuration(m);
    if (!c0) return std::move(c0).error();

    ScenarioContext ctx;
    ctx.model = &m;
    ctx.pt_to_dt = label_map(impl_->alignment_for(ver));
    ctx.data = ver.document.value("data", Json::object());
    ctx.assurance = ver.document.value("assurance", Json::object());
    ctx.simulation = ver.document.value("simulation", Json::object());
    {
        auto l = impl_->core.lock();
        for (const auto& [role, target] : std::vector<std::pair<std::string, std::string*>>{{"ontology", &ctx.ontology}, {"dt_interpretation", &ctx.dt_interpretation}}) {
            auto p = ver.pins.find(role);
            if (p == ver.pins.end()) continue;
            auto ref = parse_ref(p->second);
            if (!ref) continue;
            auto c = impl_->core.artifacts->content(ref.value());
            if (c) *target = c.value();
        }
    }
    const Json scenarios = ver.document.value("scenarios", Json::array());
    Json results = Json::array();
    bool all_pass = true;
    std::int64_t total_pass = 0;
    std::int64_t total_fail = 0;
    const auto started = std::chrono::steady_clock::now();
    for (const Json& sc : scenarios) {
        if (only && sc.value("id", std::string()) != *only) continue;
        Json world = ver.document.value("world", Json::object());
        Json state = Json::object();
        {
            // Start state: initial configuration, or explicit configurations.
            Json start = sc.value("start", Json::object());
            Json req{{"start", start.value("kind", std::string("initial")) == "configurations" ? start : Json{{"kind", "initial"}}}, {"steps", Json::array()}};
            auto r = runtime::what_if(m, kernel::StateSet::of(c0.value()), req);
            if (!r) {
                results.push_back(Json{{"id", sc.value("id", std::string())}, {"name", sc.value("name", std::string())}, {"outcome", "error"},
                                       {"error", r.error().message}, {"steps", Json::array()}});
                all_pass = false;
                continue;
            }
            state = r.value()["start"]["state"];
        }
        Json last_branches = Json::array();
        Json propositions = Json::array();
        Json steps_out = Json::array();
        bool invalid = false;
        std::int64_t passed = 0;
        std::int64_t failed = 0;
        std::string first_failure;
        // Formal steps generated by world observations are queued and executed in time order.
        std::vector<Json> queue;
        for (const Json& st : sc.value("steps", Json::array())) {
            queue.push_back(st);
            if (st.value("kind", std::string()) == "world" && st.contains("observation")) {
                const Json obs = st.at("observation");
                auto lab = observation_label(ctx, obs);
                Json gen{{"kind", "event"}, {"generated", true}, {"from", st.value("id", std::string())}};
                if (lab) {
                    gen["label"] = lab.value().first;
                    gen["level"] = lab.value().second;
                } else {
                    gen["error"] = lab.error().message;
                }
                gen["at"] = obs.value("at", st.value("at", std::string()));
                gen["id"] = st.value("id", std::string("world")) + "/observed";
                queue.push_back(gen);
            }
        }
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const Json& st = queue[i];
            const std::string kind = st.value("kind", std::string());
            Json out{{"index", i}, {"id", st.value("id", std::string())}, {"kind", kind}, {"generated", st.value("generated", false)}};
            if (invalid && kind != "world") {
                out["status"] = "not_evaluated";
                out["detail"] = "follows a step the kernel refused";
                steps_out.push_back(out);
                if (kind == "expect") {
                    ++failed;
                    if (first_failure.empty()) first_failure = "an expectation could not be evaluated after a refused step";
                }
                continue;
            }
            const std::string now_text = time_text(state.value("time", Json()));
            if (kind == "event" || kind == "delay") {
                Json step = st;
                if (st.contains("error")) {
                    out["status"] = "invalid";
                    out["detail"] = st.value("error", std::string());
                    invalid = true;
                    steps_out.push_back(out);
                    continue;
                }
                if (kind == "event" && st.value("level", std::string("dt")) == "pt") {
                    auto it = ctx.pt_to_dt.find(st.value("label", std::string()));
                    if (it == ctx.pt_to_dt.end() || it->second.size() != 1) {
                        out["status"] = "invalid";
                        out["detail"] = it == ctx.pt_to_dt.end()
                                            ? "PT label '" + st.value("label", std::string()) + "' has no DT counterpart in the alignment evidence (run the alignment first)."
                                            : "PT label '" + st.value("label", std::string()) + "' corresponds to several DT labels.";
                        invalid = true;
                        steps_out.push_back(out);
                        continue;
                    }
                    step["label"] = it->second.front();
                    out["translated"] = Json{{"pt", st.value("label", std::string())}, {"dt", it->second.front()}};
                }
                step.erase("level");
                step.erase("generated");
                step.erase("from");
                Json req{{"start", start_configurations(state)}, {"steps", Json::array({step})}};
                auto cur = state_from_json(m, state);
                if (!cur) return std::move(cur).error();
                auto r = runtime::what_if(m, cur.value(), req);
                if (!r) {
                    out["status"] = "invalid";
                    out["detail"] = r.error().message;
                    invalid = true;
                    steps_out.push_back(out);
                    continue;
                }
                const Json& res = r.value()["steps"][0];
                out["status"] = res.value("status", std::string());
                out["requested"] = res.value("requested", Json());
                if (res.value("status", std::string()) == "ok") {
                    state = res["after"];
                    propositions = res.value("propositions", Json::array());
                    if (kind == "event") last_branches = res.value("branches", Json::array());
                    out["after"] = state;
                    out["branches"] = res.value("branches", Json::array());
                } else {
                    out["error"] = res.value("error", Json());
                    out["explanation"] = res.value("explanation", Json());
                    invalid = true;
                }
            } else if (kind == "world") {
                const Json change = st.value("change", Json::object());
                Status applied = apply_world_change(world, change);
                out["status"] = applied ? "ok" : "invalid";
                out["detail"] = applied ? "Environment changed (the twin learns it only through observations)." : applied.error().message;
                if (!applied) invalid = true;
            } else if (kind == "observe") {
                out["status"] = "ok";
                out["detail"] = "Telemetry recorded for the test context (observations are not semantic state).";
                out["telemetry"] = st.value("telemetry", Json::object());
            } else if (kind == "expect") {
                const Json e = st.value("expect", Json::object());
                // An expectation at a later time first lets logical time pass.
                const std::string at = st.value("at", std::string());
                if (!at.empty() && at != now_text) {
                    auto cur = state_from_json(m, state);
                    auto at_t = parse_time(at, m.time_base());
                    auto now_t = parse_time(now_text, m.time_base());
                    if (cur && at_t && now_t && at_t.value() > now_t.value()) {
                        Json req{{"start", start_configurations(state)},
                                 {"steps", Json::array({Json{{"kind", "delay"}, {"delay", format_time(at_t.value() - now_t.value(), m.time_base())}}})}};
                        auto r = runtime::what_if(m, cur.value(), req);
                        if (r && r.value()["steps"][0].value("status", std::string()) == "ok") {
                            state = r.value()["steps"][0]["after"];
                            propositions = r.value()["steps"][0].value("propositions", Json::array());
                        } else if (r) {
                            out["status"] = "fail";
                            out["detail"] = "Logical time cannot reach t = " + at + " without an event: " +
                                            r.value()["steps"][0].value("error", Json::object()).value("message", std::string());
                            out["explanation"] = r.value()["steps"][0].value("explanation", Json());
                            ++failed;
                            if (first_failure.empty()) first_failure = out["detail"].get<std::string>();
                            steps_out.push_back(out);
                            continue;
                        }
                    }
                }
                std::set<std::string> locations;
                for (const Json& c : state.value("configurations", Json::array())) locations.insert(c.value("location", std::string()));
                bool ok = false;
                std::string actual;
                std::string verdict;
                if (e.contains("location")) {
                    const std::string want = e.value("location", std::string());
                    ok = locations.size() == 1 && locations.count(want);
                    for (const auto& l : locations) actual += (actual.empty() ? "" : ", ") + l;
                    verdict = locations.count(want) && !ok ? "inconclusive" : ok ? "pass" : "fail";
                } else if (e.contains("proposition")) {
                    const std::string want = e.value("proposition", std::string());
                    for (const Json& p : propositions) {
                        actual += (actual.empty() ? "" : ", ") + p.value("id", std::string());
                        ok = ok || p.value("id", std::string()) == want;
                    }
                    verdict = ok ? "pass" : "fail";
                } else if (e.contains("transition") || e.contains("event")) {
                    const std::string want = e.value("transition", e.value("event", std::string()));
                    for (const Json& b : last_branches) {
                        const std::string full = b.value("source", std::string()) + "." + b.value("label", std::string()) + "." + b.value("target", std::string());
                        actual += (actual.empty() ? "" : ", ") + full;
                        ok = ok || full == want || b.value("label", std::string()) == want || b.value("transition", std::string()) == want;
                    }
                    verdict = ok ? "pass" : "fail";
                } else if (e.contains("semantic")) {
                    // Δ ⊨ I_D(L) → φ for every location the twin may be in (the aligner's ontology and Z3).
                    const std::string phi = e.value("semantic", std::string());
                    std::map<std::string, std::string> by_location;
                    for (const auto& l : locations) by_location[l] = phi;
                    auto ent = alignment::entailment_by_location(ctx.ontology, ctx.dt_interpretation, by_location);
                    if (!ent) {
                        verdict = "error";
                        actual = ent.error().message;
                    } else {
                        ok = !locations.empty();
                        for (const auto& [loc, holds] : ent.value()) {
                            actual += (actual.empty() ? "" : ", ") + loc + ": " + (holds ? (*holds ? "entailed" : "not entailed") : "not interpreted");
                            ok = ok && holds && *holds;
                        }
                        verdict = ok ? "pass" : "fail";
                    }
                } else if (e.contains("monitor")) {
                    const Json want = e.at("monitor");
                    const std::string mid = want.value("id", std::string());
                    const std::string expected = want.value("status", std::string("satisfied"));
                    std::string property;
                    for (const Json& mon : ctx.assurance.value("monitors", Json::array())) {
                        if (mon.value("id", std::string()) == mid) property = mon.value("property", std::string());
                    }
                    auto p = monitoring::parse_property(property);
                    auto cur = state_from_json(m, state);
                    if (property.empty() || !p || !cur) {
                        verdict = "error";
                        actual = property.empty() ? "monitor '" + mid + "' is not a property monitor" : !p ? p.error().message : cur.error().message;
                    } else {
                        auto r = monitoring::evaluate(p.value().phi, m, cur.value());
                        if (!r) {
                            verdict = "error";
                            actual = r.error().message + " (semantic atoms are evaluated on live observations, not in kernel tests)";
                        } else {
                            actual = std::string(monitoring::to_string(r.value()));
                            ok = actual == expected;
                            verdict = ok ? "pass" : "fail";
                        }
                    }
                } else if (e.contains("window")) {
                    const Json want = e.at("window");
                    auto cur = state_from_json(m, state);
                    if (!cur) return std::move(cur).error();
                    const Json av = runtime::availability_view(m, cur.value());
                    const std::string label = want.value("label", std::string());
                    Json found;
                    for (const Json& a : av.value("availability", Json::array())) {
                        if (a.value("label", std::string()) == label) found = a;
                    }
                    const std::string status = found.is_null() ? "unavailable" : found.value("status", std::string());
                    actual = status;
                    if (!found.is_null() && !found.value("intervals", Json::array()).empty()) {
                        const Json& iv = found["intervals"][0];
                        actual += " [" + time_text(iv.value("earliest", Json())) + ", " + (iv.value("latest", Json()).is_null() ? std::string("∞") : time_text(iv["latest"])) + "]";
                    }
                    ok = want.value("status", status) == status;
                    if (ok && want.contains("earliest") && !found.is_null() && !found.value("intervals", Json::array()).empty()) {
                        ok = time_text(found["intervals"][0].value("earliest", Json())) == want.value("earliest", std::string());
                    }
                    if (ok && want.contains("latest") && !found.is_null() && !found.value("intervals", Json::array()).empty()) {
                        ok = time_text(found["intervals"][0].value("latest", Json())) == want.value("latest", std::string());
                    }
                    verdict = ok ? "pass" : "fail";
                } else if (e.contains("world")) {
                    const Json want = e.at("world");
                    const std::string obj = want.value("object", std::string());
                    Json found;
                    for (const Json& o : world.value("objects", Json::array())) {
                        if (o.value("id", std::string()) == obj) found = o;
                    }
                    if (found.is_null()) {
                        actual = "absent";
                        ok = want.value("exists", true) == false;
                    } else if (want.contains("property")) {
                        const Json val = found.value("properties", Json::object()).value(want.value("property", std::string()), Json());
                        actual = val.is_string() ? val.get<std::string>() : val.dump();
                        ok = val == want.value("equals", Json());
                    } else {
                        actual = "present";
                        ok = want.value("exists", true);
                    }
                    verdict = ok ? "pass" : "fail";
                } else {
                    verdict = "error";
                    actual = "unknown expectation";
                }
                out["status"] = verdict;
                out["expected"] = e;
                out["actual"] = actual;
                out["at"] = time_text(state.value("time", Json()));
                if (verdict == "pass") {
                    ++passed;
                } else {
                    ++failed;
                    if (first_failure.empty()) first_failure = "expected " + e.dump() + ", actual " + actual;
                }
            } else {
                out["status"] = "invalid";
                out["detail"] = "unknown step kind";
                invalid = true;
            }
            steps_out.push_back(out);
        }
        const bool pass = failed == 0 && !invalid;
        all_pass = all_pass && pass;
        total_pass += passed;
        total_fail += failed;
        results.push_back(Json{{"id", sc.value("id", std::string())},
                               {"name", sc.value("name", std::string())},
                               {"outcome", pass ? "pass" : "fail"},
                               {"passed", passed},
                               {"failed", failed},
                               {"refused", invalid},
                               {"firstFailure", first_failure},
                               {"final", state},
                               {"steps", steps_out}});
    }
    if (only && results.empty()) return make_error(ErrorCode::NotFound, "no such scenario").with("scenario", *only);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    Json doc{{"format", "twin-scenario-results/1"},
             {"kind", "tests"},
             {"note", "Scenario results are tests of example runs, not proofs."},
             {"irSha256", compiled.value()->ir_sha256},
             {"results", results}};
    // A full regression run (every scenario) is recorded as scenario evidence for exactly these inputs.
    Json evidence = nullptr;
    if (!only) {
        std::vector<EvidenceInput> in;
        for (const Binding& b : impl_->pinned_bindings(ver)) {
            if (b.role == "dt_model" || b.role == "dt_interpretation") in.push_back({b.role, b.ref, b.sha256});
        }
        in.push_back(Impl::document_input(ver));
        auto l = impl_->core.lock();
        auto ev = impl_->core.evidence->record(
            EvidenceKind::Scenario, all_pass ? Outcome::Pass : Outcome::Fail, all_pass ? "tests_passed" : "tests_failed",
            std::to_string(results.size()) + " scenario test(s): " + std::to_string(total_pass) + " expectation(s) passed, " +
                std::to_string(total_fail) + " failed.",
            "twin-kernel scenario runner", doc, in, actor.name, Impl::context(ver));
        if (ev) {
            evidence = ev.value().id;
            impl_->core.record("blueprint.scenarios", all_pass ? "pass" : "fail", ver.blueprint_id + "@" + std::to_string(ver.version),
                               {{"evidenceId", ev.value().id}, {"scenarios", results.size()}}, actor, "evidence");
        }
    }
    return Json{{"outcome", all_pass ? "pass" : "fail"},
                {"kind", "tests"},
                {"passed", total_pass},
                {"failed", total_fail},
                {"durationMs", ms},
                {"evidenceId", evidence},
                {"results", results}};
}

// -------------------------------------------------------------------------- binding test

Result<Json> BlueprintService::test_binding(std::string_view id, std::int64_t v, const Json& body) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    const Json& doc = ver.value().document;
    const Json conn = doc.value("connectivity", Json::object());
    Json binding = body.value("binding", Json::object());
    Json source = body.value("source", Json::object());
    if (source.empty()) {
        for (const Json& s : conn.value("sources", Json::array())) {
            if (s.value("id", std::string()) == binding.value("source", std::string())) source = s;
        }
    }
    if (source.empty()) return make_error(ErrorCode::InvalidArgument, "the binding names no known data source");
    const std::string kind = source.value("kind", std::string());
    const Json cfg = source.value("config", Json::object());
    const Json select = binding.value("select", Json::object());
    const Json target = binding.value("target", Json::object());
    Json telemetry_def;
    for (const Json& t : doc.value("data", Json::object()).value("telemetry", Json::array())) {
        if (t.value("id", std::string()) == target.value("id", std::string())) telemetry_def = t;
    }
    Json out{{"source", source.value("id", std::string())}, {"kind", kind}, {"status", "ok"}, {"samples", Json::array()}};
    const auto t0 = std::chrono::steady_clock::now();
    Json raw;            // the raw payload
    Json value;          // the selected raw value
    std::string timestamp;
    std::string quality = "good";
    if (kind == "opcua") {
        out["status"] = "adapter_unavailable";
        out["detail"] = "This build has no OPC UA client library: the binding is valid and stored, but cannot be tested or deployed.";
        return out;
    } else if (kind == "mqtt") {
        out["status"] = "adapter_unavailable";
        out["detail"] = "Connection tests for MQTT need the twin-ingest connector, which this build does not include. The binding is validated and stored.";
        return out;
    } else if (kind == "rest") {
        const std::string url = cfg.value("url", std::string());
        const auto scheme_end = url.find("://");
        const auto path_start = scheme_end == std::string::npos ? std::string::npos : url.find('/', scheme_end + 3);
        httplib::Client cli(url.substr(0, path_start));
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);
        auto r = cli.Get(path_start == std::string::npos ? "/" : url.substr(path_start));
        if (!r) {
            out["status"] = "unreachable";
            out["detail"] = "No answer from " + url + " (" + httplib::to_string(r.error()) + ").";
            return out;
        }
        out["httpStatus"] = r->status;
        auto j = json::parse(r->body);
        if (!j) {
            out["status"] = "bad_payload";
            out["detail"] = "The response is not JSON.";
            out["raw"] = r->body.substr(0, 400);
            return out;
        }
        raw = j.value();
    } else if (kind == "replay" || kind == "file") {
        fs::path file = cfg.value("file", std::string());
        if (file.is_relative()) file = services_.config().data_dir / "uploads" / file;
        std::ifstream in(file);
        if (!in) {
            out["status"] = "unreachable";
            out["detail"] = "Cannot read the trace file " + file.string() + ".";
            return out;
        }
        std::string line;
        std::getline(in, line);
        if (file.extension() == ".csv") {
            std::string values;
            std::getline(in, values);
            Json row = Json::object();
            std::stringstream hs(line);
            std::stringstream vs(values);
            for (std::string h, val; std::getline(hs, h, ',') && std::getline(vs, val, ',');) row[h] = val;
            raw = row;
        } else {
            auto j = json::parse(line);
            if (!j) {
                out["status"] = "bad_payload";
                out["detail"] = "The first line is not JSON (JSON lines expected).";
                return out;
            }
            raw = j.value();
        }
    } else if (kind == "simulator") {
        const Json sim = doc.value("simulation", Json::object());
        if (sim.value("kind", std::string()) == "event-script") {
            auto f = ptfeed::feed_from_json(sim.value("script", Json::object()));
            if (!f) return std::move(f).error();
            raw = Json::object();
            const double at = decimal_or(body, "at", 10.0);
            for (const ptfeed::Channel& ch : f.value().channels) {
                const double val = ptfeed::channel_value(ch, at);
                raw[ch.name] = ch.boolean ? Json(val >= 0.5) : Json(val);
            }
            timestamp = std::to_string(at);
        } else if (sim.value("kind", std::string()) == "mobile-robot") {
            auto world = scene::world_from_json(doc.value("world", Json::object()));
            if (!world) return std::move(world).error();
            auto sc = scene::simulator_scenario(world.value(), sim, "test", "");
            if (!sc) return std::move(sc).error();
            auto parsed = world::scenario_from_json(sc.value());
            if (!parsed) return std::move(parsed).error();
            world::World w(parsed.value());
            raw = world::to_json(w.telemetry());
            timestamp = "0";
        } else {
            out["status"] = "not_configured";
            out["detail"] = "Configure the simulator first (Test → Preview / Simulation).";
            return out;
        }
    } else {
        return make_error(ErrorCode::InvalidArgument, "unknown source kind").with("kind", kind);
    }
    out["latencyMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    out["raw"] = raw;
    const std::string path = select.value("path", select.value("field", std::string()));
    const Json* picked = path.empty() ? nullptr : select_path(raw, path);
    if (picked == nullptr) {
        out["status"] = "not_found";
        out["detail"] = path.empty() ? "Give the field or JSON path to read." : "The payload has no value at '" + path + "'.";
        Json fields = Json::array();
        if (raw.is_object()) for (const auto& [k, unused] : raw.items()) fields.push_back(k);
        out["availableFields"] = fields;
        return out;
    }
    value = *picked;
    out["value"] = value;
    if (const Json* ts = select.contains("timestamp") ? select_path(raw, select.value("timestamp", std::string())) : nullptr) {
        timestamp = ts->is_string() ? ts->get<std::string>() : ts->dump();
    }
    if (const Json* q = select.contains("quality") ? select_path(raw, select.value("quality", std::string())) : nullptr) {
        quality = q->is_string() ? q->get<std::string>() : q->dump();
    }
    // Canonical value: unit conversion and type validation (never coerced silently).
    const std::string type = telemetry_def.value("type", std::string("real"));
    Json canonical;
    std::string type_check = "ok";
    if (type == "real" || type == "integer") {
        double x = 0;
        bool numeric = true;
        if (value.is_number()) x = value.get<double>();
        else if (value.is_string()) {
            char* end = nullptr;
            x = std::strtod(value.get<std::string>().c_str(), &end);
            numeric = end != value.get<std::string>().c_str() && *end == '\0';
        } else numeric = false;
        if (!numeric) {
            type_check = "The value is not a number but the telemetry type is " + type + ".";
        } else {
            const Json unit = binding.value("unit", Json::object());
            const double y = x * decimal_or(unit, "scale", 1.0) + decimal_or(unit, "offset", 0.0);
            canonical = type == "integer" ? Json(static_cast<std::int64_t>(std::llround(y))) : Json(std::round(y * 1e6) / 1e6);
            const Json range = telemetry_def.value("range", Json::object());
            if (range.contains("min") && y < decimal_or(range, "min", y)) type_check = "below the declared range";
            if (range.contains("max") && y > decimal_or(range, "max", y)) type_check = "above the declared range";
        }
    } else if (type == "boolean") {
        if (value.is_boolean()) canonical = value;
        else if (value.is_number()) canonical = value.get<double>() != 0.0;
        else if (value.is_string() && (value == "true" || value == "false")) canonical = value == "true";
        else type_check = "The value is not a boolean.";
    } else {
        canonical = value.is_string() ? value : Json(value.dump());
    }
    out["canonical"] = canonical;
    out["typeCheck"] = type_check;
    out["unit"] = Json{{"source", binding.value("unit", Json::object()).value("from", telemetry_def.value("unit", std::string()))},
                       {"canonical", telemetry_def.value("unit", std::string())},
                       {"scale", binding.value("unit", Json::object()).value("scale", Json("1"))},
                       {"offset", binding.value("unit", Json::object()).value("offset", Json("0"))}};
    out["timestamp"] = timestamp;
    out["quality"] = quality;
    if (type_check != "ok") out["status"] = "type_mismatch";
    return out;
}

}  // namespace twin::studio
