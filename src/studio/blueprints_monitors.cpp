/**
 * @file blueprints_monitors.cpp
 * @brief Live evaluation of an instance's monitors and alert policy (Operate → Behavior → Monitors).
 *
 * The monitors are the instance's Blueprint version's (assurance section). Each is evaluated by
 * its authority, never by the UI:
 *  - conformance: the runtime's own conformance monitoring (GET /runtime/state);
 *  - property: the property evaluator (twin::monitoring::evaluate) on the kernel model of exactly
 *    that version and the runtime's committed state set;
 *  - data quality: the stored telemetry of the instance's channels (freshness, presence, range).
 * A monitor that cannot be evaluated (runtime not running, semantic atoms, no samples) reports
 * "unknown" with the reason — never a guessed verdict.
 */
#include <httplib.h>

#include <cstdlib>
#include <sstream>

#include "blueprints_impl.hpp"
#include "twin/monitoring/property.hpp"
#include "twin/platform/clock.hpp"

namespace twin::studio {

using json::Json;
using namespace twin::platform;

namespace {

/// Numeric bound of a monitor (integer or decimal string).
std::optional<double> bound(const Json& m, const char* key) {
    if (!m.contains(key)) return std::nullopt;
    const Json& v = m.at(key);
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        char* end = nullptr;
        const std::string s = v.get<std::string>();
        const double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0') return d;
    }
    return std::nullopt;
}

Json result(const Json& m, std::string status, std::string detail, std::string evaluator) {
    return Json{{"id", m.value("id", std::string())},
                {"name", m.value("name", m.value("id", std::string()))},
                {"kind", m.value("kind", std::string())},
                {"severity", m.value("severity", std::string("warning"))},
                {"requirement", m.value("requirement", Json(nullptr))},
                {"status", std::move(status)},
                {"detail", std::move(detail)},
                {"evaluator", std::move(evaluator)}};
}

}  // namespace

Result<Json> BlueprintService::instance_monitors(std::string_view twin_id) {
    auto tw = services_.twin_record(twin_id);
    if (!tw) return std::move(tw).error();
    const Twin t = tw.value();
    if (!t.blueprint_id || !t.blueprint_version) {
        return make_error(ErrorCode::StateError, "this twin is not an instance of a Blueprint; it has no Blueprint monitors").with("twin", t.id);
    }
    auto ver = impl_->load(*t.blueprint_id, *t.blueprint_version);
    if (!ver) return std::move(ver).error();
    const Json assurance = ver.value().document.value("assurance", Json::object());
    const std::int64_t now = services_.clock().now_ms();

    // The kernel's committed state, from the running runtime.
    Json state = nullptr;
    std::string runtime_note;
    if (t.runtime_url) {
        httplib::Client cli(*t.runtime_url);
        cli.set_connection_timeout(1, 0);
        cli.set_read_timeout(3, 0);
        auto r = cli.Get("/runtime/state");
        if (r && r->status == 200) {
            auto j = json::parse(r->body);
            if (j) state = std::move(j).value();
            else runtime_note = "the runtime state could not be read";
        } else {
            runtime_note = r ? "the runtime answered HTTP " + std::to_string(r->status) : "the runtime is not reachable";
        }
    } else {
        runtime_note = "the instance is not deployed";
    }
    std::shared_ptr<const CompiledDt> compiled;
    std::optional<kernel::StateSet> states;
    std::string states_note;
    if (!state.is_null()) {
        auto c = impl_->compile_dt(ver.value());
        if (c) {
            compiled = c.value();
            auto s = state_set_from_json(*compiled->model, state);
            if (s) states = std::move(s).value();
            else states_note = s.error().message;
        } else {
            states_note = c.error().message;
        }
    }

    // Latest telemetry of the instance's channels (by telemetry id).
    std::map<std::string, Json> channel_of;
    if (t.asset_id) {
        if (auto ch = services_.telemetry_channels(*t.asset_id, true); ch) {
            std::set<std::string> mine;
            for (const Json& id : t.instance_config.value("channels", Json::array())) {
                if (id.is_string()) mine.insert(id.get<std::string>());
            }
            for (const Json& c : ch.value().value("channels", Json::array())) {
                const std::string id = c.value("id", std::string());
                if (!mine.empty() && !mine.count(id)) continue;
                channel_of[c.value("name", std::string())] = c;
            }
        }
    }

    Json monitors = Json::array();
    for (const Json& m : assurance.value("monitors", Json::array())) {
        const std::string kind = m.value("kind", std::string());
        if (kind == "conformance") {
            if (state.is_null()) {
                monitors.push_back(result(m, "unknown", "Not evaluated: " + runtime_note + ".", "runtime"));
                continue;
            }
            const Json c = state.value("conformance", Json::object());
            const bool ok = c.value("status", std::string()) == "conformant";
            const std::string detail = ok ? std::to_string(c.value("observations", 0)) + " observation(s) admitted by the verified model."
                                          : "First violation: " + c.value("first_violation", std::string("unknown")) + ".";
            monitors.push_back(result(m, ok ? "satisfied" : "violated", detail, "runtime"));
        } else if (kind == "property") {
            const std::string text = m.value("property", std::string());
            auto p = monitoring::parse_property(text);
            if (!p) {
                monitors.push_back(result(m, "error", "The property does not parse: " + p.error().message, "studio"));
            } else if (!states) {
                monitors.push_back(result(m, "unknown", "Not evaluated: " + (state.is_null() ? runtime_note : states_note) + ".", "property evaluator"));
            } else {
                auto v = monitoring::evaluate(p.value().phi, *compiled->model, *states);
                if (!v) {
                    monitors.push_back(result(m, "unknown", v.error().message + " (semantic atoms need observations of the ontology symbols).", "property evaluator"));
                } else {
                    const std::string verdict(monitoring::to_string(v.value()));
                    monitors.push_back(result(m, verdict, text + " on the twin's current state(s) at t = " +
                                                              state.value("time", Json::object()).value("text", std::string("?")) + ".",
                                              "property evaluator"));
                }
            }
        } else if (kind == "data_quality") {
            const std::string field = m.value("field", std::string());
            const std::string check = m.value("check", std::string());
            auto it = channel_of.find(field);
            if (it == channel_of.end()) {
                monitors.push_back(result(m, "unknown", "No telemetry channel '" + field + "' for this instance.", "telemetry store"));
                continue;
            }
            const Json& c = it->second;
            const Json latest = c.value("latest", Json());
            if (latest.is_null()) {
                monitors.push_back(result(m, check == "missing" || check == "stale" ? "finding" : "unknown", "No sample received yet.", "telemetry store"));
                continue;
            }
            const std::int64_t age = now - latest.value("observedMs", now);
            if (check == "stale") {
                const std::int64_t max_age = m.value("maxAgeSeconds", std::int64_t{0}) * 1000;
                const bool stale = age > max_age;
                monitors.push_back(result(m, stale ? "finding" : "satisfied",
                                          "Last sample " + std::to_string(age / 1000) + " s old (limit " + std::to_string(max_age / 1000) + " s).", "telemetry store"));
            } else if (check == "disconnected") {
                const std::int64_t limit = m.value("maxSilenceSeconds", std::int64_t{60}) * 1000;
                monitors.push_back(result(m, age > limit ? "finding" : "satisfied",
                                          "Silent for " + std::to_string(age / 1000) + " s (limit " + std::to_string(limit / 1000) + " s).", "telemetry store"));
            } else if (check == "out_of_range") {
                const Json v = latest.value("value", Json());
                if (!v.is_number()) {
                    monitors.push_back(result(m, "unknown", "The latest value is not numeric.", "telemetry store"));
                    continue;
                }
                const double x = v.get<double>();
                const auto lo = bound(m, "min");
                const auto hi = bound(m, "max");
                const bool out = (lo && x < *lo) || (hi && x > *hi);
                auto num = [](double d) {
                    std::ostringstream o;
                    o << d;
                    return o.str();
                };
                std::ostringstream detail;
                detail << "Latest value " << num(x) << (out ? " is outside " : " within ") << "[" << (lo ? num(*lo) : std::string("-inf")) << ", "
                       << (hi ? num(*hi) : std::string("+inf")) << "].";
                monitors.push_back(result(m, out ? "finding" : "satisfied", detail.str(), "telemetry store"));
            } else if (check == "missing") {
                monitors.push_back(result(m, "satisfied", "Samples are being received.", "telemetry store"));
            } else {
                const std::string q = latest.value("quality", std::string("good"));
                monitors.push_back(result(m, q == "good" ? "satisfied" : "finding", "Latest sample quality: " + q + ".", "telemetry store"));
            }
        } else {
            monitors.push_back(result(m, "unknown", "Monitor kind '" + kind + "' is not evaluated live.", "studio"));
        }
    }

    std::map<std::string, std::string> status_of;
    for (const Json& r : monitors) status_of[r.value("id", std::string())] = r.value("status", std::string());
    Json alerts = Json::array();
    for (const Json& a : assurance.value("alerts", Json::array())) {
        const std::string on = a.value("on", std::string("violated"));
        const std::string s = status_of.count(a.value("monitor", std::string())) ? status_of[a.value("monitor", std::string())] : "unknown";
        const bool active = s == on || (on == "inconclusive" && s == "unknown");
        Json x = a;
        x["active"] = active;
        x["monitorStatus"] = s;
        alerts.push_back(x);
    }
    Json requirements = Json::array();
    for (const Json& r : assurance.value("requirements", Json::array())) {
        std::string st = "not_monitored";
        bool any_violated = false;
        bool any_unknown = false;
        bool any = false;
        for (const Json& mid : r.value("monitors", Json::array())) {
            any = true;
            const std::string s = status_of.count(mid.get<std::string>()) ? status_of[mid.get<std::string>()] : "unknown";
            any_violated = any_violated || s == "violated" || s == "finding";
            any_unknown = any_unknown || (s != "satisfied" && s != "violated" && s != "finding");
        }
        if (any) st = any_violated ? "violated" : any_unknown ? "unknown" : "satisfied";
        requirements.push_back(Json{{"id", r.value("id", std::string())}, {"title", r.value("title", std::string())},
                                    {"severity", r.value("severity", std::string())}, {"category", r.value("category", std::string())},
                                    {"status", st}});
    }
    return Json{{"twin", t.id},
                {"blueprintId", *t.blueprint_id},
                {"blueprintVersion", *t.blueprint_version},
                {"evaluatedAt", iso8601_utc(now)},
                {"runtime", state.is_null() ? Json{{"available", false}, {"note", runtime_note}}
                                            : Json{{"available", true}, {"time", state.value("time", Json())}, {"session", state.value("session", std::string())}}},
                {"monitors", monitors},
                {"alerts", alerts},
                {"requirements", requirements}};
}

}  // namespace twin::studio
