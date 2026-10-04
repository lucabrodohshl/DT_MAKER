/**
 * @file api_server.cpp
 * @brief REST/SSE routes of twin-runtime.
 */
#include "twin/runtime/api_server.hpp"

#include <httplib.h>

#include <fstream>
#include <sstream>

#include "twin/ir/codec.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/ledger/verifier.hpp"
#include "twin/runtime/views.hpp"

namespace twin::runtime {
namespace {

using json::Json;

void reply(httplib::Response& res, const Json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void reply_error(httplib::Response& res, const Error& e, int status) {
    Json context = Json::object();
    for (const auto& [k, v] : e.context) context[k] = v;
    reply(res, Json{{"error", {{"code", std::string(to_string(e.code)), }, {"message", e.message}, {"context", context}}}},
          status);
}

int status_for(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument:
        case ErrorCode::ParseError:
        case ErrorCode::TimeNotRepresentable:
            return 400;
        case ErrorCode::Unavailable:
            return 503;
        default:
            return 409;
    }
}

Result<Json> body_json(const httplib::Request& req) {
    return json::parse(req.body.empty() ? std::string_view("{}") : std::string_view(req.body));
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}

}  // namespace

void register_routes(httplib::Server& srv, CoSimDriver& driver, EventHub& hub, const std::filesystem::path& ui_dir) {
    srv.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Headers", "Content-Type, Last-Event-ID"},
                             {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}});
    srv.Options(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });

    srv.Get("/health", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, Json{{"status", "ok"}, {"service", "twin-runtime"}, {"simulation", driver.status()}});
    });

    // ------------------------------------------------------------- semantic state
    srv.Get("/runtime/state", [&driver](const httplib::Request&, httplib::Response& res) {
        auto s = driver.session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
        reply(res, state_view(*s, s->snapshot()));
    });
    srv.Get("/runtime/enabled-transitions", [&driver](const httplib::Request&, httplib::Response& res) {
        auto s = driver.session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
        reply(res, Json{{"enabled", enabled_view(*s, s->snapshot())}});
    });
    srv.Get("/runtime/propositions", [&driver](const httplib::Request&, httplib::Response& res) {
        auto s = driver.session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
        reply(res, Json{{"propositions", propositions_view(*s, s->snapshot())}});
    });
    auto submit_route = [&driver, &hub](bool advance) {
        return [&driver, &hub, advance](const httplib::Request& req, httplib::Response& res) {
            auto s = driver.session();
            if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
            Result<Json> body = body_json(req);
            if (!body) return reply_error(res, body.error(), 400);
            Result<ledger::Input> in = input_from_request(body.value(), s->model().time_base(), advance);
            if (!in) return reply_error(res, in.error(), status_for(in.error().code));
            Result<SubmitResult> r = s->submit(in.value());
            if (!r) return reply_error(res, r.error(), status_for(r.error().code));
            const Json view = submission_view(*s, in.value(), r.value());
            hub.publish("observation", view);
            reply(res, view, r.value().accepted ? 200 : 422);
        };
    };
    srv.Post("/runtime/event", submit_route(false));
    srv.Post("/runtime/advance", submit_route(true));
    srv.Post("/runtime/predict", [&driver](const httplib::Request& req, httplib::Response& res) {
        auto s = driver.session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
        Result<Json> body = body_json(req);
        if (!body) return reply_error(res, body.error(), 400);
        kernel::ExplorationLimits limits;
        limits.max_depth = static_cast<std::uint32_t>(std::clamp<std::int64_t>(body.value().value("depth", 3), 1, 8));
        limits.max_nodes = 400;
        if (body.value().contains("horizon_ticks")) limits.horizon = body.value().at("horizon_ticks").get<Ticks>();
        Json out{{"exploration", prediction_view(*s, s->predict(limits))}};
        if (body.value().contains("schedule")) {
            std::vector<kernel::ScheduledObservation> schedule;
            for (const Json& item : body.value().at("schedule")) {
                Result<ledger::Input> in = input_from_request(item, s->model().time_base(), false);
                if (!in) return reply_error(res, in.error(), 400);
                std::optional<kernel::LabelId> l = s->model().label_id(in.value().name);
                if (!l) return reply_error(res, make_error(ErrorCode::InvalidArgument, "unknown label"), 400);
                schedule.push_back(kernel::ScheduledObservation{in.value().at, kernel::Selector::label(*l)});
            }
            Result<std::vector<kernel::ObservationOutcome>> sim = s->simulate(schedule);
            out["simulation"] = sim ? Json{{"admissible", true}, {"steps", sim.value().size()}}
                                    : Json{{"admissible", false}, {"error", sim.error().to_string()}};
        }
        reply(res, out);
    });

    // --------------------------------------------------------------------- ledger
    srv.Get("/runtime/ledger", [&driver](const httplib::Request& req, httplib::Response& res) {
        auto s = driver.session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
        const std::uint64_t since = req.has_param("since") ? std::stoull(req.get_param_value("since")) : 0;
        const std::size_t limit = req.has_param("limit") ? std::stoul(req.get_param_value("limit")) : 50;
        std::istringstream in(read_file(s->ledger_path()));
        std::vector<Json> lines;
        std::string line;
        while (std::getline(in, line)) {
            Result<Json> j = json::parse(line);
            if (j && j.value().at("body").value("seq", std::uint64_t{0}) >= since) lines.push_back(std::move(j).value());
        }
        const std::size_t first = lines.size() > limit ? lines.size() - limit : 0;
        Json records = Json::array();
        for (std::size_t i = first; i < lines.size(); ++i) records.push_back(lines[i]);
        reply(res, Json{{"path", s->ledger_path().string()}, {"records", records}});
    });
    srv.Post("/runtime/ledger/verify", [&driver](const httplib::Request& req, httplib::Response& res) {
        auto s = driver.session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
        Result<Json> body = body_json(req);
        const std::string text = read_file(s->ledger_path());
        const ledger::VerificationReport r =
            ledger::verify_text(text, ledger::VerifyOptions{s->package().package_hash, std::nullopt, false});
        Json out = ledger::to_json(r);
        if (body && body.value().value("replay", false)) {
            Result<ledger::ReplayReport> rep = ledger::replay_text(s->package(), text);
            out["replay"] = rep ? ledger::to_json(rep.value()) : Json{{"error", rep.error().to_string()}};
        }
        reply(res, out);
    });
    srv.Post("/runtime/ledger/tamper-drill", [&driver](const httplib::Request& req, httplib::Response& res) {
        auto s = driver.session();
        if (!s) return reply_error(res, make_error(ErrorCode::StateError, "no session"), 409);
        Result<Json> body = body_json(req);
        const std::string text = read_file(s->ledger_path());
        const std::uint64_t lines = static_cast<std::uint64_t>(std::count(text.begin(), text.end(), '\n'));
        const std::uint64_t line = body ? body.value().value("line", lines / 2) : lines / 2;
        Json out = ledger::to_json(ledger::tamper_drill(text, line));
        out["tampered_line"] = line;
        out["note"] = "a copy of the ledger with one altered field was verified; the real ledger is untouched";
        reply(res, out);
    });

    // ----------------------------------------------------------- package & model
    srv.Get("/runtime/package", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, package_view(driver.package()));
    });
    srv.Get("/runtime/model", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, ir::to_json(driver.package().model));
    });

    // ------------------------------------------------------------------ stream
    srv.Get("/runtime/stream", [&hub](const httplib::Request& req, httplib::Response& res) {
        std::uint64_t after = 0;
        if (req.has_header("Last-Event-ID")) after = std::stoull(req.get_header_value("Last-Event-ID"));
        else if (req.has_param("after")) after = std::stoull(req.get_param_value("after"));
        res.set_header("Cache-Control", "no-cache");
        auto cursor = std::make_shared<std::uint64_t>(after);
        res.set_chunked_content_provider("text/event-stream", [&hub, cursor](std::size_t, httplib::DataSink& sink) {
            const std::vector<HubEvent> events = hub.wait_after(*cursor, 1000);
            std::string frames;
            for (const HubEvent& e : events) {
                frames += EventHub::sse_frame(e);
                *cursor = e.seq;
            }
            if (frames.empty()) frames = ": keep-alive\n\n";
            return sink.write(frames.data(), frames.size());
        });
    });

    // ------------------------------------------------------- simulation controls
    srv.Get("/simulation/state", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.status());
    });
    srv.Post("/simulation/start", [&driver](const httplib::Request&, httplib::Response& res) {
        driver.play();
        reply(res, driver.status());
    });
    srv.Post("/simulation/pause", [&driver](const httplib::Request&, httplib::Response& res) {
        driver.pause();
        reply(res, driver.status());
    });
    srv.Post("/simulation/step", [&driver](const httplib::Request&, httplib::Response& res) {
        if (Status s = driver.tick(); !s) return reply_error(res, s.error(), 409);
        reply(res, driver.status());
    });
    srv.Post("/simulation/reset", [&driver](const httplib::Request&, httplib::Response& res) {
        if (Status s = driver.reset(); !s) return reply_error(res, s.error(), 503);
        reply(res, driver.status());
    });
    srv.Post("/simulation/speed", [&driver](const httplib::Request& req, httplib::Response& res) {
        Result<Json> body = body_json(req);
        if (!body || !body.value().contains("speed_permille")) {
            return reply_error(res, make_error(ErrorCode::InvalidArgument, "expected {\"speed_permille\": int}"), 400);
        }
        driver.set_speed(static_cast<double>(body.value().at("speed_permille").get<std::int64_t>()) / 1000.0);
        reply(res, driver.status());
    });
    srv.Get("/planner/current-plan", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.plans().value("active", Json()));
    });
    srv.Get("/planner/history", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.plans());
    });
    srv.Get("/mission", [&driver](const httplib::Request&, httplib::Response& res) { reply(res, driver.mission()); });
    srv.Get("/world/known", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.known_world());
    });
    srv.Get("/world/telemetry", [&driver](const httplib::Request&, httplib::Response& res) {
        reply(res, driver.telemetry());
    });

    // ------------------------------------------------------------------- web UI
    if (!ui_dir.empty() && std::filesystem::exists(ui_dir)) {
        srv.set_mount_point("/", ui_dir.string());
    }
}

}  // namespace twin::runtime
