/**
 * @file api_server.cpp
 * @brief REST/SSE routes of twin-runtime, grouped by concern.
 */
#include "twin/runtime/api_server.hpp"

#include <httplib.h>

#include <algorithm>
#include <charconv>
#include <sstream>

#include "twin/core/version.hpp"
#include "twin/ir/codec.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/ledger/verifier.hpp"
#include "twin/runtime/views.hpp"
#include "twin/runtime/what_if.hpp"

namespace twin::runtime {
namespace {

using json::Json;

// ------------------------------------------------------------------ plumbing

void reply(httplib::Response& res, const Json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

Json error_body(const Error& e) {
    Json context = Json::array();
    for (const auto& [k, v] : e.context) context.push_back(Json{{"key", k}, {"value", v}});
    return Json{{"error", {{"code", std::string(to_string(e.code))}, {"message", e.message}, {"context", context}}}};
}

int status_for(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument:
        case ErrorCode::ParseError:
        case ErrorCode::TimeNotRepresentable:
            return 400;
        case ErrorCode::NotFound:
            return 404;
        case ErrorCode::Unavailable:
            return 503;
        default:
            return 409;
    }
}

void reply_error(httplib::Response& res, const Error& e) { reply(res, error_body(e), status_for(e.code)); }

Result<Json> body_json(const httplib::Request& req) {
    Result<Json> j = json::parse(req.body.empty() ? std::string_view("{}") : std::string_view(req.body));
    if (!j) return j;
    if (!j.value().is_object()) return make_error(ErrorCode::InvalidArgument, "request body must be a JSON object");
    return j;
}

Result<std::uint64_t> param_u64(const httplib::Request& req, const std::string& key, std::uint64_t fallback) {
    if (!req.has_param(key)) return fallback;
    const std::string v = req.get_param_value(key);
    std::uint64_t out = 0;
    const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (ec != std::errc() || ptr != v.data() + v.size()) {
        return make_error(ErrorCode::InvalidArgument, "query parameter must be a non-negative integer").with("parameter", key);
    }
    return out;
}

using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

/// Wrap a handler that needs the current session (409 if none).
Handler with_session(ApiContext& ctx,
                     std::function<void(TwinSession&, const httplib::Request&, httplib::Response&)> f) {
    return [&ctx, f = std::move(f)](const httplib::Request& req, httplib::Response& res) {
        std::shared_ptr<TwinSession> s = ctx.host->session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no twin session is running"));
        f(*s, req, res);
    };
}

/// The execution addressed by a request ("session" in the body/query), or the current session's.
Result<ExecutionInfo> resolve_execution(ApiContext& ctx, const std::string& session) {
    if (!session.empty()) return ctx.executions->find(session);
    std::shared_ptr<TwinSession> current = ctx.host->session();
    if (!current) return make_error(ErrorCode::StateError, "no twin session is running and no session was named");
    return ctx.executions->find(current->session_id());
}

std::string requested_session(const httplib::Request& req, const Result<Json>& body) {
    if (body && body.value().contains("session") && body.value().at("session").is_string()) {
        return body.value().at("session").get<std::string>();
    }
    return req.has_param("session") ? req.get_param_value("session") : std::string();
}

// ------------------------------------------------------------ route groups

void health_routes(httplib::Server& srv, ApiContext& ctx) {
    srv.Get("/health", [&ctx](const httplib::Request&, httplib::Response& res) {
        std::shared_ptr<TwinSession> s = ctx.host->session();
        reply(res, Json{{"status", "ok"},
                        {"service", "twin-runtime"},
                        {"kernel_version", std::string(version::kKernel)},
                        {"package_hash", ctx.host->package().package_hash},
                        {"session", s ? s->session_id() : std::string()},
                        {"mode", ctx.host->mode()},
                        {"simulation", ctx.host->status()}});
    });
}

void state_routes(httplib::Server& srv, ApiContext& ctx) {
    srv.Get("/runtime/state", with_session(ctx, [](TwinSession& s, const httplib::Request&, httplib::Response& res) {
                reply(res, state_view(s, s.snapshot()));
            }));
    srv.Get("/runtime/enabled-transitions",
            with_session(ctx, [](TwinSession& s, const httplib::Request&, httplib::Response& res) {
                reply(res, Json{{"enabled", enabled_view(s, s.snapshot())}});
            }));
    srv.Get("/runtime/propositions",
            with_session(ctx, [](TwinSession& s, const httplib::Request&, httplib::Response& res) {
                reply(res, Json{{"propositions", propositions_view(s, s.snapshot())}});
            }));
    srv.Get("/runtime/package", [&ctx](const httplib::Request&, httplib::Response& res) {
        reply(res, package_view(ctx.host->package()));
    });
    srv.Post("/runtime/package/verify", [&ctx](const httplib::Request&, httplib::Response& res) {
        // Re-run every integrity check on the package directory NOW (files may have been altered on disk).
        const package::LoadedPackage& pkg = ctx.host->package();
        Json checks = Json::array();
        bool all = true;
        for (const package::Check& c : package::verify_report(pkg.directory)) {
            all = all && c.passed;
            checks.push_back(Json{{"name", c.name}, {"passed", c.passed}, {"detail", c.detail}});
        }
        reply(res, Json{{"package_hash", pkg.package_hash},
                        {"directory", pkg.directory.string()},
                        {"valid", all},
                        {"checks", checks},
                        {"note", "integrity re-verified on request; the running twin keeps executing the IR "
                                 "loaded (and verified) at start-up"}});
    });
    srv.Get("/runtime/model", [&ctx](const httplib::Request&, httplib::Response& res) {
        reply(res, ir::to_json(ctx.host->package().model));
    });
}

void input_routes(httplib::Server& srv, ApiContext& ctx) {
    auto submit = [&ctx](bool advance) {
        return with_session(ctx, [&ctx, advance](TwinSession& s, const httplib::Request& req, httplib::Response& res) {
            Result<Json> body = body_json(req);
            if (!body) return reply_error(res, body.error());
            Result<ledger::Input> in = input_from_request(body.value(), s.model().time_base(), advance);
            if (!in) return reply_error(res, in.error());
            Result<SubmitResult> r = s.submit(in.value());  // the kernel decides; the ledger records
            if (!r) return reply_error(res, r.error());
            const Json view = submission_view(s, in.value(), r.value());
            ctx.hub->publish("observation", view);
            reply(res, view, r.value().accepted ? 200 : 422);
        });
    };
    srv.Post("/runtime/event", submit(false));
    srv.Post("/runtime/advance", submit(true));
}

void prediction_routes(httplib::Server& srv, ApiContext& ctx) {
    srv.Post("/runtime/predict",
             with_session(ctx, [](TwinSession& s, const httplib::Request& req, httplib::Response& res) {
                 Result<Json> body = body_json(req);
                 if (!body) return reply_error(res, body.error());
                 const Json& b = body.value();
                 kernel::ExplorationLimits limits;
                 limits.max_depth = static_cast<std::uint32_t>(std::clamp<std::int64_t>(b.value("depth", 3), 1, 8));
                 limits.max_nodes =
                     static_cast<std::uint32_t>(std::clamp<std::int64_t>(b.value("max_nodes", 400), 1, 4000));
                 if (b.contains("horizon_ticks")) {
                     if (!b.at("horizon_ticks").is_number_integer()) {
                         return reply_error(res, make_error(ErrorCode::InvalidArgument, "'horizon_ticks' must be an integer"));
                     }
                     limits.horizon = b.at("horizon_ticks").get<Ticks>();
                 }
                 const Snapshot snap = s.snapshot();
                 reply(res, Json{{"from", state_view(s, snap)},
                                 {"limits", {{"depth", limits.max_depth}, {"max_nodes", limits.max_nodes}}},
                                 {"exploration", prediction_view(s, s.predict(limits))},
                                 {"note", "computed by the semantic kernel on copies of the committed state; the "
                                          "live state is not modified"}});
             }));
    srv.Post("/runtime/what-if",
             with_session(ctx, [](TwinSession& s, const httplib::Request& req, httplib::Response& res) {
                 Result<Json> body = body_json(req);
                 if (!body) return reply_error(res, body.error());
                 Result<Json> r = what_if(s.model(), s.snapshot().state, body.value());
                 if (!r) return reply_error(res, r.error());
                 reply(res, r.value());
             }));
    srv.Post("/runtime/simulate",
             with_session(ctx, [](TwinSession& s, const httplib::Request& req, httplib::Response& res) {
                 Result<Json> body = body_json(req);
                 if (!body) return reply_error(res, body.error());
                 std::vector<kernel::ScheduledObservation> schedule;
                 for (const Json& item : body.value().value("schedule", Json::array())) {
                     Result<ledger::Input> in = input_from_request(item, s.model().time_base(), false);
                     if (!in) return reply_error(res, in.error());
                     std::optional<kernel::Selector> selector;
                     if (in.value().kind == ledger::InputKind::Label) {
                         if (std::optional<kernel::LabelId> l = s.model().label_id(in.value().name)) {
                             selector = kernel::Selector::label(*l);
                         }
                     } else {
                         const auto& ts = s.model().ir().transitions;
                         for (std::size_t i = 0; i < ts.size(); ++i) {
                             if (ts[i].id == in.value().name) {
                                 selector = kernel::Selector::transition(static_cast<ir::TransitionIndex>(i));
                             }
                         }
                     }
                     if (!selector) {
                         return reply_error(res, make_error(ErrorCode::InvalidArgument, "the model has no such event")
                                                     .with("event", in.value().name));
                     }
                     schedule.push_back(kernel::ScheduledObservation{in.value().at, *selector});
                 }
                 Result<std::vector<kernel::ObservationOutcome>> sim = s.simulate(schedule);
                 Json steps = Json::array();
                 if (sim) {
                     for (const kernel::ObservationOutcome& o : sim.value()) {
                         steps.push_back(ledger::encode_outcome(s.model(), o));
                     }
                 }
                 reply(res, Json{{"admissible", sim.ok()},
                                 {"steps", steps},
                                 {"refusal", sim ? Json() : error_body(sim.error()).at("error")},
                                 {"note", "what-if simulation by the semantic kernel on a copy of the committed "
                                          "state; nothing is recorded and the live state is not modified"}});
             }));
}

/// A replay report with the identity of what was replayed (same shape wherever replay appears).
Json replay_view(ApiContext& ctx, const ledger::ReplayReport& report, const ExecutionInfo& e,
                 const package::LoadedPackage& pkg) {
    Json out = ledger::to_json(report);
    out["session"] = e.session;
    out["package"] = Json{{"package_hash", pkg.package_hash},
                          {"model_id", pkg.manifest.model_id},
                          {"model_version", pkg.manifest.model_version},
                          {"ir_sha256", pkg.ir_sha256},
                          {"running", pkg.package_hash == ctx.host->package().package_hash}};
    return out;
}

/// An execution summary with the runtime's view of it (current session? package available?).
Json execution_view(ApiContext& ctx, const ExecutionInfo& e) {
    std::shared_ptr<TwinSession> current = ctx.host->session();
    Json j = to_json(e);
    j["current"] = current && current->session_id() == e.session;
    j["replayable"] = ctx.packages->get(e.package_hash).ok();
    return j;
}

void ledger_routes(httplib::Server& srv, ApiContext& ctx) {
    srv.Get("/runtime/executions", [&ctx](const httplib::Request&, httplib::Response& res) {
        Json out = Json::array();
        for (const ExecutionInfo& e : ctx.executions->list()) out.push_back(execution_view(ctx, e));
        reply(res, Json{{"executions", out}});
    });
    srv.Get(R"(/runtime/executions/([^/]+))", [&ctx](const httplib::Request& req, httplib::Response& res) {
        Result<ExecutionInfo> e = ctx.executions->find(req.matches[1].str());
        if (!e) return reply_error(res, e.error());
        reply(res, execution_view(ctx, e.value()));
    });
    srv.Get(R"(/runtime/executions/([^/]+)/telemetry)", [&ctx](const httplib::Request& req, httplib::Response& res) {
        Result<ExecutionInfo> e = ctx.executions->find(req.matches[1].str());
        if (!e) return reply_error(res, e.error());
        Result<std::uint64_t> max = param_u64(req, "max", 2000);
        if (!max) return reply_error(res, max.error());
        Result<Json> t = ctx.executions->telemetry(e.value(), static_cast<std::size_t>(max.value()));
        if (!t) return reply_error(res, t.error());
        reply(res, t.value());
    });
    srv.Get("/runtime/ledger", [&ctx](const httplib::Request& req, httplib::Response& res) {
        Result<ExecutionInfo> e = resolve_execution(ctx, requested_session(req, Result<Json>(Json())));
        if (!e) return reply_error(res, e.error());
        Result<std::uint64_t> since = param_u64(req, "since", 0);
        Result<std::uint64_t> limit = param_u64(req, "limit", 200);
        Result<std::uint64_t> tail = param_u64(req, "tail", 0);
        if (!since || !limit || !tail) return reply_error(res, make_error(ErrorCode::InvalidArgument, "bad paging"));
        const std::string kind = req.has_param("kind") ? req.get_param_value("kind") : std::string();
        Result<std::string> text = ctx.executions->ledger_text(e.value());
        if (!text) return reply_error(res, text.error());
        std::vector<Json> lines;
        std::istringstream in(text.value());
        for (std::string line; std::getline(in, line);) {
            Result<Json> j = json::parse(line);
            if (!j || !j.value().contains("body")) continue;
            const Json& body = j.value().at("body");
            if (body.value("seq", std::uint64_t{0}) < since.value()) continue;
            if (!kind.empty() && body.value("kind", std::string()) != kind) continue;
            lines.push_back(Json{{"seq", body.value("seq", std::uint64_t{0})},
                                 {"kind", body.value("kind", std::string())},
                                 {"hash", j.value().value("hash", std::string())},
                                 {"body", body}});
        }
        std::size_t first = 0;
        std::size_t count = std::min<std::size_t>(lines.size(), limit.value());
        if (tail.value() > 0) {
            count = std::min<std::size_t>(lines.size(), tail.value());
            first = lines.size() - count;
        }
        Json records = Json::array();
        for (std::size_t i = first; i < first + count; ++i) records.push_back(std::move(lines[i]));
        reply(res, Json{{"session", e.value().session},
                        {"ledger", e.value().ledger.string()},
                        {"total_records", e.value().records},
                        {"matched", lines.size()},
                        {"records", records}});
    });
    srv.Post("/runtime/ledger/verify", [&ctx](const httplib::Request& req, httplib::Response& res) {
        Result<Json> body = body_json(req);
        if (!body) return reply_error(res, body.error());
        Result<ExecutionInfo> e = resolve_execution(ctx, requested_session(req, body));
        if (!e) return reply_error(res, e.error());
        Result<std::string> text = ctx.executions->ledger_text(e.value());
        if (!text) return reply_error(res, text.error());
        const ledger::VerificationReport report =
            ledger::verify_text(text.value(), ledger::VerifyOptions{e.value().package_hash, std::nullopt, false});
        Json out = ledger::to_json(report);
        out["session"] = e.value().session;
        out["package_hash"] = e.value().package_hash;
        out["running_package"] = e.value().package_hash == ctx.host->package().package_hash;
        if (body.value().value("replay", false)) {
            Result<package::LoadedPackage> pkg = ctx.packages->get(e.value().package_hash);
            if (!pkg) {
                out["replay"] = error_body(pkg.error()).at("error");
            } else {
                Result<ledger::ReplayReport> rep = ledger::replay_text(pkg.value(), text.value());
                out["replay"] = rep ? replay_view(ctx, rep.value(), e.value(), pkg.value())
                                    : error_body(rep.error()).at("error");
            }
        }
        reply(res, out);
    });
    srv.Post("/runtime/ledger/tamper-drill", [&ctx](const httplib::Request& req, httplib::Response& res) {
        Result<Json> body = body_json(req);
        if (!body) return reply_error(res, body.error());
        Result<ExecutionInfo> e = resolve_execution(ctx, requested_session(req, body));
        if (!e) return reply_error(res, e.error());
        Result<std::string> text = ctx.executions->ledger_text(e.value());
        if (!text) return reply_error(res, text.error());
        const auto lines = static_cast<std::uint64_t>(std::count(text.value().begin(), text.value().end(), '\n'));
        const std::uint64_t line = body.value().value("line", lines / 2);
        Json out = ledger::to_json(ledger::tamper_drill(text.value(), line));
        out["session"] = e.value().session;
        out["tampered_line"] = line;
        out["note"] = "a COPY of the ledger with one altered field was verified; the real ledger is untouched";
        reply(res, out);
    });
    srv.Post("/runtime/replay", [&ctx](const httplib::Request& req, httplib::Response& res) {
        Result<Json> body = body_json(req);
        if (!body) return reply_error(res, body.error());
        Result<ExecutionInfo> e = resolve_execution(ctx, requested_session(req, body));
        if (!e) return reply_error(res, e.error());
        Result<package::LoadedPackage> pkg = ctx.packages->get(e.value().package_hash);
        if (!pkg) return reply_error(res, pkg.error());
        Result<std::string> text = ctx.executions->ledger_text(e.value());
        if (!text) return reply_error(res, text.error());
        const bool frames = body.value().value("frames", true);
        Result<ledger::ReplayReport> rep = ledger::replay_text(pkg.value(), text.value(), ledger::ReplayOptions{frames});
        if (!rep) return reply_error(res, rep.error());
        Json out = replay_view(ctx, rep.value(), e.value(), pkg.value());
        if (body.value().contains("telemetry_max")) {
            const auto max = static_cast<std::size_t>(std::max<std::int64_t>(2, body.value().value("telemetry_max", 2000)));
            Result<Json> t = ctx.executions->telemetry(e.value(), max);
            out["telemetry"] = t ? t.value().at("samples") : Json::array();
        }
        reply(res, out);
    });
}

void stream_route(httplib::Server& srv, ApiContext& ctx) {
    srv.Get("/runtime/stream", [&ctx](const httplib::Request& req, httplib::Response& res) {
        // A fresh subscriber receives only new events (it reads the authoritative state over REST);
        // Last-Event-ID or ?after=<id> resumes from the replay buffer (?after=0: everything buffered).
        std::uint64_t after = ctx.hub->head();
        const std::string last = req.has_header("Last-Event-ID") ? req.get_header_value("Last-Event-ID")
                                 : req.has_param("after")        ? req.get_param_value("after")
                                                                 : std::string();
        if (!last.empty()) {
            const auto [ptr, ec] = std::from_chars(last.data(), last.data() + last.size(), after);
            if (ec != std::errc() || ptr != last.data() + last.size()) after = 0;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        auto cursor = std::make_shared<std::uint64_t>(after);
        EventHub* hub = ctx.hub;
        res.set_chunked_content_provider("text/event-stream", [hub, cursor](std::size_t, httplib::DataSink& sink) {
            const std::vector<HubEvent> events = hub->wait_after(*cursor, 1000);
            std::string frames;
            for (const HubEvent& e : events) {
                frames += EventHub::sse_frame(e);
                *cursor = e.seq;
            }
            if (frames.empty()) frames = ": keep-alive\n\n";
            return sink.write(frames.data(), frames.size());
        });
    });
}

void lifecycle_routes(httplib::Server& srv, ApiContext& ctx) {
    srv.Get("/simulation/state", [&ctx](const httplib::Request&, httplib::Response& res) {
        reply(res, ctx.host->status());
    });
    srv.Post("/simulation/reset", [&ctx](const httplib::Request&, httplib::Response& res) {
        if (Status s = ctx.host->reset(); !s) return reply_error(res, s.error());
        reply(res, ctx.host->status());
    });
}

void cosimulation_routes(httplib::Server& srv, CoSimDriver& driver) {
    srv.Post("/simulation/start", [&driver](const httplib::Request&, httplib::Response& res) {
        driver.request_mission_start();  // the operator's start request (idempotent)
        driver.play();
        reply(res, driver.status());
    });
    srv.Post("/simulation/pause", [&driver](const httplib::Request&, httplib::Response& res) {
        driver.pause();
        reply(res, driver.status());
    });
    srv.Post("/simulation/step", [&driver](const httplib::Request&, httplib::Response& res) {
        if (Status s = driver.tick(); !s) return reply_error(res, s.error());
        reply(res, driver.status());
    });
    srv.Post("/simulation/speed", [&driver](const httplib::Request& req, httplib::Response& res) {
        Result<Json> body = body_json(req);
        if (!body || !body.value().contains("speed_permille") || !body.value().at("speed_permille").is_number_integer()) {
            return reply_error(res, make_error(ErrorCode::InvalidArgument, "expected {\"speed_permille\": <integer>}"));
        }
        driver.set_speed(static_cast<double>(body.value().at("speed_permille").get<std::int64_t>()) / 1000.0);
        reply(res, driver.status());
    });
    srv.Get("/mission", [&driver](const httplib::Request&, httplib::Response& res) { reply(res, driver.mission()); });
    srv.Post("/mission/start", [&driver](const httplib::Request&, httplib::Response& res) {
        driver.request_mission_start();
        reply(res, driver.status());
    });
    srv.Get("/planner/current-plan", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.plans().value("active", Json()));
    });
    srv.Get("/planner/history", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.plans());
    });
    srv.Get("/planner/episodes", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, Json{{"episodes", driver.episodes()}});
    });
    srv.Get("/world/known", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.known_world());
    });
    srv.Get("/world/telemetry", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.telemetry());
    });
}

void monitor_routes(httplib::Server& srv, MonitorHost& monitor) {
    srv.Post("/runtime/pt-event", [&monitor](const httplib::Request& req, httplib::Response& res) {
        Result<Json> body = body_json(req);
        if (!body) return reply_error(res, body.error());
        Result<Json> view = monitor.pt_event(body.value());
        if (!view) return reply_error(res, view.error());
        reply(res, view.value(), view.value().value("accepted", false) ? 200 : 422);
    });
    srv.Post("/runtime/telemetry", [&monitor](const httplib::Request& req, httplib::Response& res) {
        Result<Json> body = body_json(req);
        if (!body) return reply_error(res, body.error());
        if (Status s = monitor.telemetry(body.value()); !s) return reply_error(res, s.error());
        reply(res, Json{{"logged", true}});
    });
}

}  // namespace

void register_routes(httplib::Server& srv, ApiContext& ctx, const std::filesystem::path& static_dir) {
    srv.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Headers", "Content-Type, Last-Event-ID"},
                             {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}});
    srv.Options(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });
    health_routes(srv, ctx);
    state_routes(srv, ctx);
    input_routes(srv, ctx);
    prediction_routes(srv, ctx);
    ledger_routes(srv, ctx);
    stream_route(srv, ctx);
    lifecycle_routes(srv, ctx);
    if (ctx.cosim != nullptr) cosimulation_routes(srv, *ctx.cosim);
    if (ctx.monitor != nullptr) monitor_routes(srv, *ctx.monitor);
    if (!static_dir.empty() && std::filesystem::exists(static_dir)) {
        srv.set_mount_point("/", static_dir.string());
    }
}

}  // namespace twin::runtime
