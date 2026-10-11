/**
 * @file main.cpp
 * @brief twin-ingest: the plant connection of one deployed twin.
 *
 * Usage (normally started by the Studio deployment supervisor):
 *   twin-ingest --config <ingest.json> --runtime <runtime url> --admin-port <port>
 *
 * Inputs, as configured (include/twin/ingest/config.hpp):
 *  - REST endpoint   POST http://<host>:<port>/v1/messages  (a JSON object, a JSON array of
 *                    objects, or JSON lines; header "Authorization: Bearer <token>")
 *  - MQTT endpoint   mqtt://<host>:<port>, any topic, password = the access token
 *  - broker          subscription to the plant's own MQTT broker
 * Every message is mapped to the twin's data contract and forwarded to the twin's runtime,
 * whose kernel decides (include/twin/ingest/service.hpp). The local status port (127.0.0.1)
 * serves GET /health and GET /status for Studio; it is never exposed to the plant network.
 *
 * Exit codes: 0 stopped by a signal, 2 bad usage or a port that cannot be bound, 3 bad
 * configuration.
 */
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "twin/core/wall_clock.hpp"
#include "twin/ingest/config.hpp"
#include "twin/ingest/runtime_link.hpp"

namespace {

using twin::json::Json;
namespace ingest = twin::ingest;

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int /*sig*/) { g_stop = 1; }

constexpr const char* kUsage = "usage: twin-ingest --config <ingest.json> --runtime <runtime url> --admin-port <port>\n";
constexpr std::size_t kMaxBody = std::size_t{16} << 20;  // 16 MiB per REST request

void log_line(const std::string& text) { std::cout << twin::now_utc_millis() << " twin-ingest: " << text << std::endl; }

/// Constant-time comparison (the token is a secret).
bool same_secret(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = static_cast<unsigned char>(diff | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
    return diff == 0;
}

void reply(httplib::Response& res, int status, const Json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void reply_error(httplib::Response& res, int status, const std::string& code, const std::string& message) {
    reply(res, status, Json{{"error", {{"code", code}, {"message", message}}}});
}

/// The messages of a REST body: one JSON object, a JSON array of objects, or JSON lines.
twin::Result<std::vector<Json>> messages_of(const std::string& body) {
    std::vector<Json> out;
    if (auto whole = twin::json::parse(body); whole) {
        const Json& j = whole.value();
        if (j.is_object()) out.push_back(j);
        else if (j.is_array()) out = j.get<std::vector<Json>>();
        else return twin::make_error(twin::ErrorCode::InvalidArgument, "send a JSON object, an array of objects, or JSON lines");
    } else {
        std::size_t line_no = 0;
        std::size_t pos = 0;
        while (pos <= body.size()) {
            const std::size_t nl = body.find('\n', pos);
            std::string line = body.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? body.size() + 1 : nl + 1;
            ++line_no;
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            auto j = twin::json::parse(line);
            if (!j) return twin::make_error(twin::ErrorCode::InvalidArgument, "line " + std::to_string(line_no) + " is not JSON: " + j.error().message);
            out.push_back(std::move(j).value());
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (!out[i].is_object()) return twin::make_error(twin::ErrorCode::InvalidArgument, "message " + std::to_string(i + 1) + " is not a JSON object");
    }
    if (out.empty()) return twin::make_error(twin::ErrorCode::InvalidArgument, "the request contains no message");
    return out;
}

/// Counters of the REST endpoint (the MQTT inputs report their own).
struct RestStats {
    std::atomic<std::uint64_t> requests{0};
    std::atomic<std::uint64_t> messages{0};
    std::atomic<std::uint64_t> unauthorized{0};
    std::atomic<std::uint64_t> bad_requests{0};
};

void rest_routes(httplib::Server& srv, const ingest::IngestConfig& cfg, ingest::IngestService& service, RestStats& stats) {
    auto authorized = [&cfg](const httplib::Request& req) {
        std::string token = req.get_header_value("X-Twin-Token");
        const std::string auth = req.get_header_value("Authorization");
        if (auth.rfind("Bearer ", 0) == 0) token = auth.substr(7);
        return !token.empty() && same_secret(token, cfg.token);
    };
    srv.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        reply(res, 200, Json{{"status", "ok"}, {"service", "twin-ingest"}});
    });
    srv.Get("/v1/contract", [&cfg, &service, &stats, authorized](const httplib::Request& req, httplib::Response& res) {
        if (!authorized(req)) {
            ++stats.unauthorized;
            return reply_error(res, 401, "unauthorized", "missing or wrong access token: send the header Authorization: Bearer <token>");
        }
        reply(res, 200, Json{{"instance", cfg.instance}, {"mapping", ingest::to_json(service.mapping())}});
    });
    srv.Post("/v1/messages", [&cfg, &service, &stats, authorized](const httplib::Request& req, httplib::Response& res) {
        ++stats.requests;
        if (!authorized(req)) {
            ++stats.unauthorized;
            return reply_error(res, 401, "unauthorized", "missing or wrong access token: send the header Authorization: Bearer <token>");
        }
        auto messages = messages_of(req.body);
        if (!messages) {
            ++stats.bad_requests;
            return reply_error(res, 400, "bad_request", messages.error().message);
        }
        const ingest::Origin origin{"rest", req.remote_addr, ""};
        Json results = Json::array();
        Json summary{{"telemetrySamples", 0}, {"eventsAccepted", 0}, {"eventsRejected", 0}, {"unmapped", 0}, {"problems", 0}};
        int status = 200;
        for (const Json& m : messages.value()) {
            Json r = service.handle(m, origin);
            ++stats.messages;
            if (r.contains("error")) {
                results.push_back(std::move(r));
                status = 503;  // the twin runtime is unreachable: the sender should retry the rest later
                break;
            }
            summary["telemetrySamples"] = summary["telemetrySamples"].get<std::int64_t>() + r.value("telemetry", std::int64_t{0});
            for (const Json& e : r.value("events", Json::array())) {
                const char* k = e.value("accepted", false) ? "eventsAccepted" : "eventsRejected";
                summary[k] = summary[k].get<std::int64_t>() + 1;
            }
            summary["unmapped"] = summary["unmapped"].get<std::int64_t>() + static_cast<std::int64_t>(r.value("unmapped", Json::array()).size());
            summary["problems"] = summary["problems"].get<std::int64_t>() + static_cast<std::int64_t>(r.value("problems", Json::array()).size());
            results.push_back(std::move(r));
        }
        reply(res, status, Json{{"instance", cfg.instance},
                                {"messages", messages.value().size()},
                                {"processed", results.size() - (status == 200 ? 0U : 1U)},
                                {"summary", summary},
                                {"results", results}});
    });
}

}  // namespace

/// @brief Entry point of `twin-ingest` (the plant connection of one deployed twin).
int main(int argc, char** argv) {
    std::string config_path;
    std::string runtime_url;
    int admin_port = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        const std::string v = argv[i + 1];
        if (a == "--config") config_path = v;
        else if (a == "--runtime") runtime_url = v;
        else if (a == "--admin-port") admin_port = std::atoi(v.c_str());
        else {
            std::cerr << kUsage;
            return 2;
        }
    }
    if (config_path.empty() || runtime_url.empty() || admin_port <= 0 || argc % 2 == 0) {
        std::cerr << kUsage;
        return 2;
    }
    auto cfg_r = ingest::load_config(config_path);
    if (!cfg_r) {
        std::cerr << "twin-ingest: " << cfg_r.error().to_string() << "\n";
        return 3;
    }
    const ingest::IngestConfig cfg = std::move(cfg_r).value();
    (void)std::signal(SIGINT, on_signal);
    (void)std::signal(SIGTERM, on_signal);
    (void)std::signal(SIGPIPE, SIG_IGN);

    ingest::HttpRuntime runtime(runtime_url);
    ingest::ServiceOptions options = cfg.service;
    options.log = log_line;
    auto service_r = ingest::IngestService::create(cfg.mapping, options, runtime);
    if (!service_r) {
        std::cerr << "twin-ingest: " << service_r.error().to_string() << "\n";
        return 3;
    }
    std::unique_ptr<ingest::IngestService> service = std::move(service_r).value();

    // Plant-facing inputs first: a port that cannot be bound stops the start-up with a clear error.
    RestStats rest_stats;
    httplib::Server rest;
    std::thread rest_thread;
    if (cfg.rest.enabled) {
        rest.set_payload_max_length(kMaxBody);
        rest.set_read_timeout(10, 0);
        rest_routes(rest, cfg, *service, rest_stats);
        if (!rest.bind_to_port(cfg.rest.host, cfg.rest.port)) {
            std::cerr << "twin-ingest: REST endpoint: cannot listen on " << cfg.rest.host << ":" << cfg.rest.port
                      << " (is another program using this port? choose another port in the instance's plant connection)\n";
            return 2;
        }
        rest_thread = std::thread([&rest] { rest.listen_after_bind(); });
    }
    auto deliver = [&service](const ingest::Delivery& d, const char* transport) {
        (void)service->handle(ingest::decode_payload(d.payload), ingest::Origin{transport, d.client_id, d.topic});
    };
    std::unique_ptr<ingest::MqttEndpoint> endpoint;
    if (cfg.mqtt_enabled) {
        auto authenticate = [&cfg](const twin::ingest::mqtt::Connect& c) {
            if (!c.password) return twin::ingest::mqtt::ConnectReturn::NotAuthorized;
            return same_secret(*c.password, cfg.token) ? twin::ingest::mqtt::ConnectReturn::Accepted
                                                       : twin::ingest::mqtt::ConnectReturn::BadCredentials;
        };
        auto e = ingest::MqttEndpoint::start(cfg.mqtt, authenticate, [&deliver](const ingest::Delivery& d) { deliver(d, "mqtt"); });
        if (!e) {
            std::cerr << "twin-ingest: MQTT endpoint: " << e.error().to_string()
                      << " (is another program using this port? choose another port in the instance's plant connection)\n";
            rest.stop();
            if (rest_thread.joinable()) rest_thread.join();
            return 2;
        }
        endpoint = std::move(e).value();
    }
    std::unique_ptr<ingest::MqttSubscriber> subscriber;
    if (cfg.broker_enabled) {
        subscriber = std::make_unique<ingest::MqttSubscriber>(cfg.broker, [&deliver](const ingest::Delivery& d) { deliver(d, "broker"); });
        subscriber->start();
    }

    // Local status port for Studio (loopback only).
    httplib::Server admin;
    admin.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        reply(res, 200, Json{{"status", "ok"}, {"service", "twin-ingest"}});
    });
    admin.Get("/status", [&](const httplib::Request&, httplib::Response& res) {
        Json s = service->status();
        s["instance"] = cfg.instance;
        s["rest"] = cfg.rest.enabled ? Json{{"listening", rest.is_running()},
                                            {"host", cfg.rest.host},
                                            {"port", cfg.rest.port},
                                            {"requests", rest_stats.requests.load()},
                                            {"messages", rest_stats.messages.load()},
                                            {"unauthorized", rest_stats.unauthorized.load()},
                                            {"badRequests", rest_stats.bad_requests.load()}}
                                     : Json(nullptr);
        s["mqtt"] = endpoint ? endpoint->status() : Json(nullptr);
        s["broker"] = subscriber ? subscriber->status() : Json(nullptr);
        reply(res, 200, s);
    });
    if (!admin.bind_to_port("127.0.0.1", admin_port)) {
        std::cerr << "twin-ingest: cannot listen on the local status port " << admin_port << "\n";
        return 2;
    }
    std::thread admin_thread([&admin] { admin.listen_after_bind(); });

    log_line("instance " + cfg.instance + " -> runtime " + runtime_url + (cfg.rest.enabled ? "; REST on " + cfg.rest.host + ":" + std::to_string(cfg.rest.port) : "") +
             (cfg.mqtt_enabled ? "; MQTT on " + cfg.mqtt.host + ":" + std::to_string(endpoint->port()) : "") +
             (cfg.broker_enabled ? "; subscribed to " + cfg.broker.host + ":" + std::to_string(cfg.broker.port) : ""));
    while (g_stop == 0) {
        service->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    log_line("stopping");
    if (subscriber) subscriber->stop();
    if (endpoint) endpoint->stop();
    rest.stop();
    admin.stop();
    if (rest_thread.joinable()) rest_thread.join();
    if (admin_thread.joinable()) admin_thread.join();
    return 0;
}
