/**
 * @file server.cpp
 * @brief Studio HTTP server (see server.hpp). Routes are thin adapters onto Services.
 */
#include "twin/studio/server.hpp"
#include "twin/studio_engine/engine.hpp"

#include <httplib.h>

#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "twin/platform/clock.hpp"
#include "twin/studio/blueprints.hpp"
#include "twin/studio/supervisor.hpp"
#include "services_impl.hpp"
#include <set>

namespace twin::studio {

namespace fs = std::filesystem;
using namespace twin::platform;
using json::Json;

namespace {

int http_status(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument:
        case ErrorCode::ParseError:
        case ErrorCode::TimeNotRepresentable: return 400;
        case ErrorCode::NotFound: return 404;
        case ErrorCode::StateError:
        case ErrorCode::IntegrityError: return 409;
        case ErrorCode::ValidationError:
        case ErrorCode::UnsupportedConstruct:
        case ErrorCode::IncompatibleObservation:
        case ErrorCode::InvariantViolation:
        case ErrorCode::TransitionNotEnabled:
        case ErrorCode::TimeRegression:
        case ErrorCode::ArithmeticOverflow: return 422;
        case ErrorCode::Unavailable: return 503;
        case ErrorCode::IoError:
        case ErrorCode::Internal: return 500;
    }
    return 500;
}

Json error_body(const Error& e) {
    Json context = Json::array();
    for (const auto& [k, v] : e.context) context.push_back({{"key", k}, {"value", v}});
    return {{"error", {{"code", std::string(to_string(e.code))}, {"message", e.message}, {"context", context}}}};
}

Json error_body(std::string_view code, std::string_view message) {
    return {{"error", {{"code", std::string(code)}, {"message", std::string(message)}, {"context", Json::array()}}}};
}

void send(httplib::Response& res, int status, const Json& body) {
    res.status = status;
    res.set_content(body.dump(-1, ' ', false, Json::error_handler_t::replace), "application/json; charset=utf-8");
}

std::string random_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::array<char, 17> buf{};
    std::snprintf(buf.data(), buf.size(), "%016llx", static_cast<unsigned long long>(rng()));
    return buf.data();
}

std::optional<std::string> param(const httplib::Request& req, const char* name) {
    if (!req.has_param(name)) return std::nullopt;
    std::string v = req.get_param_value(name);
    if (v.empty()) return std::nullopt;
    return v;
}

std::int64_t int_param(const httplib::Request& req, const char* name, std::int64_t fallback) {
    auto v = param(req, name);
    if (!v) return fallback;
    try {
        return std::stoll(*v);
    } catch (const std::exception&) {
        return fallback;
    }
}

Result<std::int64_t> time_param(const httplib::Request& req, const char* name, std::int64_t fallback) {
    auto v = param(req, name);
    if (!v) return fallback;
    if (!v->empty() && std::all_of(v->begin(), v->end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return std::stoll(*v);
    }
    return parse_iso8601_utc(*v);
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

Result<Json> body_json(const httplib::Request& req) {
    if (req.body.empty()) return Json::object();
    auto j = json::parse(req.body);
    if (!j) return make_error(ErrorCode::ParseError, "request body is not valid JSON");
    return j;
}

Result<ArtifactRef> path_ref(const httplib::Request& req) {
    const auto& id = req.path_params.at("id");
    try {
        const std::int64_t v = std::stoll(req.path_params.at("v"));
        if (v < 1) throw std::invalid_argument("v");
        return ArtifactRef{id, v};
    } catch (const std::exception&) {
        return make_error(ErrorCode::InvalidArgument, "version must be a positive integer");
    }
}

Result<std::optional<ArtifactRef>> opt_ref(const Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::optional<ArtifactRef>{};
    if (!it->is_string()) return make_error(ErrorCode::InvalidArgument, std::string(key) + " must be '<id>@<version>'");
    auto r = parse_ref(it->get<std::string>());
    if (!r) return std::move(r).error();
    return std::optional<ArtifactRef>(r.value());
}

Result<Services::PhiRefs> phi_from(const Json& j) {
    auto o = opt_ref(j, "ontology");
    if (!o) return std::move(o).error();
    if (!o.value()) return make_error(ErrorCode::InvalidArgument, "ontology is required");
    auto p = opt_ref(j, "ptInterpretation");
    if (!p) return std::move(p).error();
    auto d = opt_ref(j, "dtInterpretation");
    if (!d) return std::move(d).error();
    return Services::PhiRefs{*o.value(), p.value(), d.value()};
}

/// @brief RFC 3986 percent-encoding of a query component.
std::string url_encode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4U];
            out += kHex[c & 15U];
        }
    }
    return out;
}

std::string content_type_for(const fs::path& p) {
    static const std::map<std::string, std::string> kTypes = {
        {".html", "text/html; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
        {".mjs", "text/javascript; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
        {".json", "application/json"},          {".svg", "image/svg+xml"},
        {".png", "image/png"},                  {".ico", "image/x-icon"},
        {".woff2", "font/woff2"},               {".woff", "font/woff"},
        {".map", "application/json"},           {".txt", "text/plain; charset=utf-8"},
        // Sources linked from the documentation site are shown, not downloaded.
        {".md", "text/plain; charset=utf-8"},   {".yaml", "text/plain; charset=utf-8"},
        {".cpp", "text/plain; charset=utf-8"},  {".hpp", "text/plain; charset=utf-8"}};
    auto it = kTypes.find(p.extension().string());
    return it == kTypes.end() ? "application/octet-stream" : it->second;
}

}  // namespace

struct StudioServer::Impl {
    Services& services;
    ServerOptions options;
    httplib::Server server;
    std::atomic<bool> stopping{false};
    /// Engine operations behind Studio authoring (parse, import, render, diff, compile, ...).
    studio_engine::Engine engine;

    Impl(Services& s, ServerOptions o)
        : services(s), options(std::move(o)), engine(studio_engine::EngineConfig{s.config().data_dir / "engine"}) {}

    using Handler = std::function<Result<Json>(const httplib::Request&, const Actor&)>;

    /// @brief Register a JSON route with uniform error handling.
    void route(const std::string& method, const std::string& pattern, Handler handler, int ok_status = 200) {
        auto wrapped = [this, handler = std::move(handler), ok_status](const httplib::Request& req, httplib::Response& res) {
            Actor actor;
            if (req.has_header("X-Twin-Actor")) {
                std::string a = req.get_header_value("X-Twin-Actor");
                if (!a.empty() && a.size() <= 64) actor.name = a;
            }
            try {
                auto r = handler(req, actor);
                if (r) {
                    send(res, ok_status, r.value());
                } else {
                    const int status = http_status(r.error().code);
                    send(res, status, status == 500 ? error_body("internal", "The server could not complete the request; "
                                                                             "see the application log (correlation id " +
                                                                                 res.get_header_value("X-Request-Id") + ").")
                                                    : error_body(r.error()));
                    if (status == 500) {
                        services.app_log().write(LogLevel::Error, "studio.http", "request failed",
                                                 {{"path", req.path}, {"error", r.error().to_string()}},
                                                 res.get_header_value("X-Request-Id"));
                    }
                }
            } catch (const std::exception& e) {
                services.app_log().write(LogLevel::Error, "studio.http", "unhandled exception",
                                         {{"path", req.path}, {"what", e.what()}}, res.get_header_value("X-Request-Id"));
                send(res, 500, error_body("internal", "Unexpected server error; see the application log."));
            }
        };
        if (method == "GET") server.Get(pattern, wrapped);
        else if (method == "POST") server.Post(pattern, wrapped);
        else if (method == "PUT") server.Put(pattern, wrapped);
        else if (method == "DELETE") server.Delete(pattern, wrapped);
    }

    void routes();
    void blueprint_routes();
    void stream_route();
    void proxy_routes();
    void static_routes();
    std::optional<std::string> upstream_for(const std::string& twin_id, const std::string& family);
};

std::optional<std::string> StudioServer::Impl::upstream_for(const std::string& twin_id, const std::string& family) {
    if (family == "observer" || family == "scenario") {
        auto it = options.world_urls.find(twin_id);
        if (it != options.world_urls.end()) return it->second;
        auto t = services.twin_record(twin_id);
        if (t && t.value().world_url) return *t.value().world_url;  // started by the deployment supervisor
        return std::nullopt;  // ground truth is never substituted by the twin's belief
    }
    auto it = options.runtime_urls.find(twin_id);
    if (it != options.runtime_urls.end()) return it->second;
    auto t = services.twin_record(twin_id);
    if (t && t.value().runtime_url) return *t.value().runtime_url;
    return std::nullopt;
}

void StudioServer::Impl::routes() {
    Services& s = services;
    const std::string p = "/api/v1";
    // Engine route table (twin::studio_engine), registered with this server's uniform handling.
    for (studio_engine::Route& er : engine.routes()) {
        route(er.method, er.pattern, [h = er.handler](const httplib::Request& r, const Actor& a) -> Result<Json> {
            studio_engine::Request q;
            for (const auto& [k, v] : r.path_params) q.path[k] = v;
            for (const auto& [k, v] : r.params) q.query[k] = v;
            auto b = body_json(r);
            if (!b) return std::move(b).error();
            q.body = std::move(b).value();
            q.actor = a.name;
            return h(q);
        }, er.ok_status);
    }
    route("GET", p + "/about", [&](const auto&, const auto&) -> Result<Json> { return s.about(); });
    route("GET", p + "/overview", [&](const auto&, const auto&) { return s.overview(); });
    route("GET", p + "/search", [&](const httplib::Request& r, const auto&) {
        return s.search(param(r, "q").value_or(""), static_cast<std::size_t>(std::clamp<std::int64_t>(int_param(r, "limit", 30), 1, 100)));
    });

    // Assets & graph.
    route("GET", p + "/assets", [&](const httplib::Request& r, const auto&) {
        AssetFilter f;
        f.type = param(r, "type");
        if (r.has_param("parent")) f.parent_id = r.get_param_value("parent");
        f.text = param(r, "q");
        f.limit = std::clamp<std::int64_t>(int_param(r, "limit", 200), 1, 1000);
        f.offset = std::max<std::int64_t>(0, int_param(r, "offset", 0));
        return s.assets(f);
    });
    route("GET", p + "/assets/:id", [&](const httplib::Request& r, const auto&) { return s.asset(r.path_params.at("id")); });
    route("POST", p + "/assets", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.create_asset(b.value(), a);
    });
    route("POST", p + "/assets/:id/relationships", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.link_assets(r.path_params.at("id"), b.value().value("type", std::string()), b.value().value("targetId", std::string()), a);
    });
    route("GET", p + "/assets/:id/neighborhood", [&](const httplib::Request& r, const auto&) {
        return s.neighborhood(r.path_params.at("id"), static_cast<int>(int_param(r, "depth", 1)),
                              split_csv(param(r, "types").value_or("")),
                              static_cast<std::size_t>(int_param(r, "max", 80)));
    });
    route("GET", p + "/graph/facets", [&](const auto&, const auto&) { return s.graph_facets(); });
    route("GET", p + "/assets/:id/telemetry", [&](const httplib::Request& r, const auto&) {
        return s.telemetry_channels(r.path_params.at("id"), param(r, "descendants").value_or("true") != "false");
    });
    route("GET", p + "/telemetry/:channel/series", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        const std::int64_t now = s.clock().now_ms();
        auto from = time_param(r, "from", now - 3600000);
        auto to = time_param(r, "to", now);
        if (!from) return std::move(from).error();
        if (!to) return std::move(to).error();
        return s.telemetry_series(r.path_params.at("channel"), from.value(), to.value(), int_param(r, "maxPoints", 1000));
    });
    route("POST", p + "/telemetry/ingest", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.ingest(b.value());
    });

    // Twins.
    route("GET", p + "/twins", [&](const auto&, const auto&) { return s.twins(); });
    route("GET", p + "/twins/:id", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto t = s.twin(r.path_params.at("id"));
        if (!t) return t;
        Json j = t.value();
        const auto rt = upstream_for(r.path_params.at("id"), "runtime");
        j["runtimeConnected"] = rt.has_value();
        return j;
    });

    // Artefacts.
    route("GET", p + "/artifacts", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        std::optional<ArtifactKind> kind;
        if (auto k = param(r, "kind")) {
            auto kk = artifact_kind_from_string(*k);
            if (!kk) return std::move(kk).error();
            kind = kk.value();
        }
        return s.list_artifacts(kind);
    });
    route("POST", p + "/artifacts", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        auto kind = artifact_kind_from_string(b.value().value("kind", std::string()));
        if (!kind) return std::move(kind).error();
        return s.create_artifact(kind.value(), b.value().value("id", std::string()), b.value().value("name", std::string()),
                                 b.value().value("description", std::string()), b.value().value("content", std::string()),
                                 b.value().value("refs", Json::object()), a);
    }, 201);
    route("GET", p + "/artifacts/:id", [&](const httplib::Request& r, const auto&) { return s.artifact(r.path_params.at("id")); });
    route("GET", p + "/artifacts/:id/versions/:v", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        return s.version(ref.value());
    });
    route("PUT", p + "/artifacts/:id/versions/:v", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        if (!b.value().contains("content") || !b.value()["content"].is_string()) {
            return make_error(ErrorCode::InvalidArgument, "content (string) is required");
        }
        std::optional<Json> refs;
        if (b.value().contains("refs")) refs = b.value()["refs"];
        std::optional<std::string> desc;
        if (b.value().contains("changeDescription")) desc = b.value()["changeDescription"].get<std::string>();
        return s.save_draft(ref.value(), b.value()["content"].get<std::string>(), refs, desc, a);
    });
    route("POST", p + "/artifacts/:id/versions/:v/drafts", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        std::optional<std::string> change;
        if (b.value().contains("changeId") && b.value()["changeId"].is_string()) change = b.value()["changeId"].get<std::string>();
        return s.create_draft(ref.value(), b.value().value("description", std::string()), change, a);
    }, 201);
    route("POST", p + "/artifacts/:id/versions/:v/validate", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        return s.validate(ref.value(), a);
    });
    route("POST", p + "/artifacts/:id/versions/:v/publish", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        return s.publish(ref.value(), a);
    });
    route("POST", p + "/artifacts/:id/versions/:v/reject", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.reject(ref.value(), b.value().value("reason", std::string()), a);
    });
    route("GET", p + "/artifacts/:id/versions/:v/impact", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        return s.impact(ref.value());
    });
    route("GET", p + "/artifacts/:id/versions/:v/symbols/:name", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        return s.symbol(ref.value(), r.path_params.at("name"));
    });
    route("POST", p + "/artifacts/:id/versions/:v/evaluate", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ref = path_ref(r);
        if (!ref) return std::move(ref).error();
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        std::vector<std::pair<std::string, std::string>> obs;
        const Json o = b.value().value("observations", Json::object());
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.value().is_string()) obs.emplace_back(it.key(), it.value().get<std::string>());
            else if (it.value().is_boolean()) obs.emplace_back(it.key(), it.value().get<bool>() ? "true" : "false");
            else return make_error(ErrorCode::InvalidArgument, "observation values must be exact decimal strings or booleans")
                .with("symbol", it.key());
        }
        std::optional<std::string> asset;
        if (b.value().contains("assetId") && b.value()["assetId"].is_string()) asset = b.value()["assetId"].get<std::string>();
        std::vector<std::string> keys = b.value().value("keys", std::vector<std::string>{});
        return s.evaluate(ref.value(), obs, asset, keys);
    });
    route("GET", p + "/diff", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto from = parse_ref(param(r, "from").value_or(""));
        auto to = parse_ref(param(r, "to").value_or(""));
        if (!from) return std::move(from).error();
        if (!to) return std::move(to).error();
        return s.diff(from.value(), to.value());
    });

    // Formal checks & evidence.
    route("POST", p + "/refinement-checks", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        auto base = phi_from(b.value().value("base", Json::object()));
        auto cand = phi_from(b.value().value("candidate", Json::object()));
        if (!base) return std::move(base).error();
        if (!cand) return std::move(cand).error();
        std::optional<std::string> change;
        if (b.value().contains("changeId") && b.value()["changeId"].is_string()) change = b.value()["changeId"].get<std::string>();
        return s.run_refinement(base.value(), cand.value(), change, a);
    }, 201);
    route("GET", p + "/evidence", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        EvidenceFilter f;
        if (auto k = param(r, "kind")) {
            auto kk = evidence_kind_from_string(*k);
            if (!kk) return std::move(kk).error();
            f.kind = kk.value();
        }
        f.artifact_id = param(r, "artifact");
        if (r.has_param("version")) f.version = int_param(r, "version", 0);
        f.change_id = param(r, "change");
        f.limit = std::clamp<std::int64_t>(int_param(r, "limit", 50), 1, 500);
        f.offset = std::max<std::int64_t>(0, int_param(r, "offset", 0));
        return s.evidence_list(f);
    });
    route("GET", p + "/evidence/:id", [&](const httplib::Request& r, const auto&) { return s.evidence(r.path_params.at("id")); });
    route("GET", p + "/evidence/:id/status", [&](const httplib::Request& r, const auto&) {
        return s.evidence_status(r.path_params.at("id"), param(r, "twin"), param(r, "change"));
    });

    // Changes & release.
    route("GET", p + "/changes", [&](const httplib::Request& r, const auto&) { return s.changes(param(r, "state")); });
    route("POST", p + "/changes", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.create_change(b.value().value("twinId", std::string()), b.value().value("title", std::string()),
                               b.value().value("description", std::string()), a);
    }, 201);
    route("GET", p + "/changes/:id", [&](const httplib::Request& r, const auto&) { return s.change(r.path_params.at("id")); });
    route("POST", p + "/changes/:id/artifacts", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.add_to_change(r.path_params.at("id"), b.value().value("artifactId", std::string()),
                               b.value().value("description", std::string()), a);
    }, 201);
    route("GET", p + "/changes/:id/pipeline", [&](const httplib::Request& r, const auto&) { return s.pipeline(r.path_params.at("id")); });
    route("POST", p + "/changes/:id/stages/:stage/run", [&](const httplib::Request& r, const Actor& a) {
        return s.run_stage(r.path_params.at("id"), r.path_params.at("stage"), a);
    });
    route("POST", p + "/changes/:id/release", [&](const httplib::Request& r, const Actor& a) { return s.release(r.path_params.at("id"), a); });
    route("POST", p + "/changes/:id/abandon", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.abandon_change(r.path_params.at("id"), b.value().value("reason", std::string()), a);
    });
    route("GET", p + "/artifacts-impact", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ref = parse_ref(param(r, "ref").value_or(""));
        if (!ref) return std::move(ref).error();
        return s.impact(ref.value());
    });

    // Packages & deployments.
    route("GET", p + "/packages", [&](const httplib::Request& r, const auto&) { return s.packages(param(r, "twin").value_or("")); });
    route("GET", p + "/packages/:id", [&](const httplib::Request& r, const auto&) { return s.package(r.path_params.at("id")); });
    route("GET", p + "/packages/:id/ir", [&](const httplib::Request& r, const auto&) { return s.package_ir(r.path_params.at("id")); });
    route("POST", p + "/packages/:id/verify", [&](const httplib::Request& r, const auto&) { return s.verify_package(r.path_params.at("id")); });
    route("GET", p + "/deployments", [&](const httplib::Request& r, const auto&) { return s.deployments(param(r, "twin").value_or("")); });
    route("POST", p + "/deployments", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.deploy(b.value().value("twinId", std::string()), b.value().value("packageId", std::string()),
                        b.value().value("reason", std::string()), a);
    }, 201);
    route("GET", p + "/deployments/rollback-preview", [&](const httplib::Request& r, const auto&) {
        return s.rollback_preview(param(r, "twin").value_or(""), param(r, "package").value_or(""));
    });
    route("POST", p + "/deployments/rollback", [&](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto b = body_json(r);
        if (!b) return std::move(b).error();
        return s.rollback(b.value().value("twinId", std::string()), b.value().value("packageId", std::string()),
                          b.value().value("reason", std::string()), a);
    }, 201);

    // Audit & logs.
    route("GET", p + "/audit", [&](const httplib::Request& r, const auto&) {
        AuditFilter f;
        f.operation_prefix = param(r, "operation");
        f.subject = param(r, "subject");
        f.actor = param(r, "actor");
        f.limit = std::clamp<std::int64_t>(int_param(r, "limit", 100), 1, 500);
        f.offset = std::max<std::int64_t>(0, int_param(r, "offset", 0));
        return s.audit(f);
    });
    route("POST", p + "/audit/verify", [&](const auto&, const auto&) { return s.verify_audit(); });
    route("GET", p + "/logs", [&](const httplib::Request& r, const auto&) -> Result<Json> {
        LogFilter f;
        if (auto l = param(r, "level")) {
            if (*l == "debug") f.min_level = LogLevel::Debug;
            else if (*l == "info") f.min_level = LogLevel::Info;
            else if (*l == "warn") f.min_level = LogLevel::Warn;
            else if (*l == "error") f.min_level = LogLevel::Error;
            else return make_error(ErrorCode::InvalidArgument, "level must be debug, info, warn or error");
        }
        f.component = param(r, "component");
        f.correlation_id = param(r, "correlation");
        f.execution_id = param(r, "execution");
        f.asset_id = param(r, "asset");
        f.text = param(r, "q");
        f.since = param(r, "since");
        f.until = param(r, "until");
        f.limit = static_cast<std::size_t>(std::clamp<std::int64_t>(int_param(r, "limit", 200), 1, 2000));
        return s.logs(f);
    });
}

// --- Blueprint Studio: Twin Blueprints, instances, deployment (blueprints.hpp) ------------------

namespace {

Result<std::int64_t> path_version(const httplib::Request& req) {
    try {
        const std::int64_t v = std::stoll(req.path_params.at("v"));
        if (v < 1) throw std::invalid_argument("v");
        return v;
    } catch (const std::exception&) {
        return make_error(ErrorCode::InvalidArgument, "version must be a positive integer");
    }
}

Result<std::int64_t> revision_of(const Json& body) {
    if (!body.contains("revision") || !body.at("revision").is_number_integer()) {
        return make_error(ErrorCode::InvalidArgument, "'revision' (integer) is required: the draft revision your edit is based on");
    }
    return body.at("revision").get<std::int64_t>();
}

const std::string kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

Result<std::string> base64_decode(std::string_view in) {
    std::string out;
    int val = 0;
    int bits = -8;
    for (const char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        const auto pos = kBase64.find(c);
        if (pos == std::string::npos) return make_error(ErrorCode::InvalidArgument, "data is not base64");
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

}  // namespace

void StudioServer::Impl::blueprint_routes() {
    Services& s = services;
    BlueprintService& b = s.blueprints();
    const std::string p = "/api/v1";
    const std::string v = p + "/blueprints/:id/versions/:v";
    auto id_of = [](const httplib::Request& r) { return r.path_params.at("id"); };

    route("GET", p + "/blueprints", [&b](const auto&, const auto&) { return b.list(); });
    route("GET", p + "/blueprints/templates", [&b](const auto&, const auto&) { return b.templates(); });
    route("GET", p + "/blueprints/palettes", [&b](const auto&, const auto&) { return b.palettes(); });
    route("POST", p + "/blueprints", [&b](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.create(body.value(), a);
    }, 201);
    route("GET", p + "/blueprints/:id", [&b, id_of](const httplib::Request& r, const auto&) { return b.get(id_of(r)); });
    route("PUT", p + "/blueprints/:id", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.update_meta(id_of(r), body.value(), a);
    });
    route("GET", v, [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.version(id_of(r), ver.value());
    });
    route("POST", v + "/drafts", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.create_draft(id_of(r), ver.value(), body.value().value("note", std::string()), a);
    }, 201);
    route("PUT", v + "/sections/:section", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        auto rev = revision_of(body.value());
        if (!rev) return std::move(rev).error();
        return b.save_section(id_of(r), ver.value(), r.path_params.at("section"), rev.value(), body.value().value("content", Json()), a);
    });
    route("GET", v + "/models/:role", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.model(id_of(r), ver.value(), r.path_params.at("role"));
    });
    route("PUT", v + "/models/:role", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        auto rev = revision_of(body.value());
        if (!rev) return std::move(rev).error();
        return b.save_model(id_of(r), ver.value(), r.path_params.at("role"), rev.value(), body.value().value("model", Json()),
                            body.value().value("layout", Json()), a);
    });
    route("POST", v + "/models/:role/import", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        auto rev = revision_of(body.value());
        if (!rev) return std::move(rev).error();
        return b.import_model(id_of(r), ver.value(), r.path_params.at("role"), rev.value(), body.value().value("filename", std::string()),
                              body.value().value("content", std::string()), a);
    });
    route("GET", v + "/semantics/:role", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.semantics(id_of(r), ver.value(), r.path_params.at("role"));
    });
    route("PUT", v + "/semantics/:role", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        auto rev = revision_of(body.value());
        if (!rev) return std::move(rev).error();
        return b.save_semantics(id_of(r), ver.value(), r.path_params.at("role"), rev.value(), body.value(), a);
    });
    route("GET", v + "/validation", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.validate(id_of(r), ver.value());
    });
    route("GET", v + "/status", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.status(id_of(r), ver.value());
    });
    route("POST", v + "/checks/:check", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.run_check(id_of(r), ver.value(), r.path_params.at("check"), body.value(), a);
    });
    route("GET", v + "/world/raster", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.world_raster(id_of(r), ver.value());
    });
    route("POST", v + "/timing", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.timing(id_of(r), ver.value(), body.value());
    });
    route("POST", v + "/scenarios/:scenario/run", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.run_scenarios(id_of(r), ver.value(), r.path_params.at("scenario"), a);
    });
    route("GET", v + "/impact", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        std::optional<std::int64_t> against;
        if (auto x = param(r, "against")) against = std::stoll(*x);
        return b.impact(id_of(r), ver.value(), against);
    });
    route("GET", v + "/package", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.package(id_of(r), ver.value());
    });
    route("POST", v + "/publish", [&b, id_of](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.publish(id_of(r), ver.value(), a);
    });
    route("GET", v + "/export", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        return b.export_bundle(id_of(r), ver.value());
    });
    route("POST", v + "/bindings/test", [&b, id_of](const httplib::Request& r, const auto&) -> Result<Json> {
        auto ver = path_version(r);
        if (!ver) return std::move(ver).error();
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.test_binding(id_of(r), ver.value(), body.value());
    });

    // Instances and deployment.
    route("GET", p + "/instances", [&b](const httplib::Request& r, const auto&) { return b.instances(param(r, "blueprint")); });
    route("POST", p + "/instances", [&b](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.create_instance(body.value(), a);
    }, 201);
    route("GET", p + "/instances/:id", [&b](const httplib::Request& r, const auto&) { return b.instance(r.path_params.at("id")); });
    route("POST", p + "/instances/:id/deploy", [&b](const httplib::Request& r, const Actor& a) -> Result<Json> {
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        return b.deploy_instance(r.path_params.at("id"), body.value(), a);
    });
    route("POST", p + "/instances/:id/:action", [&b](const httplib::Request& r, const Actor& a) -> Result<Json> {
        return b.control_instance(r.path_params.at("id"), r.path_params.at("action"), a);
    });
    route("GET", p + "/supervisor", [&s](const auto&, const auto&) -> Result<Json> {
        return Json{{"available", s.supervisor().available()}, {"binDir", s.supervisor().bin_dir().string()},
                    {"instances", s.supervisor().status_all()}};
    });

    // Content-addressed blobs (imported floor-plan images, SVG): never semantic geometry.
    route("POST", p + "/blobs", [&s](const httplib::Request& r, const auto&) -> Result<Json> {
        auto body = body_json(r);
        if (!body) return std::move(body).error();
        const std::string mime = body.value().value("mime", std::string());
        static const std::set<std::string> kMimes = {"image/png", "image/jpeg", "image/svg+xml", "application/geo+json", "application/json"};
        if (!kMimes.count(mime)) return make_error(ErrorCode::InvalidArgument, "supported uploads: PNG, JPEG, SVG, GeoJSON");
        auto bytes = base64_decode(body.value().value("data", std::string()));
        if (!bytes) return std::move(bytes).error();
        if (bytes.value().size() > 16U * 1024U * 1024U) return make_error(ErrorCode::InvalidArgument, "uploads are limited to 16 MB");
        auto sha = s.internals().store->put(bytes.value());
        if (!sha) return std::move(sha).error();
        return Json{{"sha256", sha.value()}, {"mime", mime}, {"size", static_cast<std::int64_t>(bytes.value().size())},
                    {"url", "/api/v1/blobs/" + sha.value() + "?mime=" + mime}};
    }, 201);
    server.Get(R"(/api/v1/blobs/([0-9a-f]{64}))", [&s](const httplib::Request& req, httplib::Response& res) {
        auto bytes = s.internals().store->get(req.matches[1].str());
        if (!bytes) {
            send(res, 404, error_body("not_found", "no such blob"));
            return;
        }
        std::string mime = req.has_param("mime") ? req.get_param_value("mime") : "application/octet-stream";
        if (mime != "image/png" && mime != "image/jpeg" && mime != "image/svg+xml" && mime != "application/geo+json" && mime != "application/json") {
            mime = "application/octet-stream";
        }
        res.set_header("Cache-Control", "public, max-age=31536000, immutable");
        res.set_content(bytes.value(), mime);
    });
}

void StudioServer::Impl::stream_route() {
    server.Get("/api/v1/stream", [this](const httplib::Request& req, httplib::Response& res) {
        std::uint64_t cursor = 0;
        bool resumed = false;
        // Resume point: the standard header (EventSource auto-reconnect) or ?lastEventId= (manual reconnect).
        const std::string resume_from = req.has_header("Last-Event-ID") ? req.get_header_value("Last-Event-ID")
                                                                          : req.get_param_value("lastEventId");
        if (!resume_from.empty()) {
            try {
                cursor = std::stoull(resume_from);
                resumed = true;
            } catch (const std::exception&) {
                cursor = 0;
            }
        }
        if (!resumed) cursor = services.events().head();
        struct Cursor {
            std::uint64_t seq;
            bool hello_sent;
        };
        auto state = std::make_shared<Cursor>(Cursor{cursor, false});
        (void)resumed;
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        res.set_chunked_content_provider("text/event-stream", [this, state](std::size_t, httplib::DataSink& sink) {
            if (stopping.load()) return false;
            auto write = [&](const std::string& s) { return sink.write(s.data(), s.size()); };
            if (!state->hello_sent) {
                // Identify the hub epoch so clients can detect server restarts (seq not comparable).
                const Json hello = {{"epoch", services.events().epoch()}, {"head", services.events().head()}};
                if (!write("event: hello\ndata: " + hello.dump() + "\n\n")) return false;
                state->hello_sent = true;
            }
            auto batch = services.events().wait_after(state->seq, std::chrono::seconds(15));
            if (stopping.load()) return false;
            if (batch.gap) {
                const Json data = {{"epoch", services.events().epoch()}, {"reason", "events were missed"}};
                if (!write("event: resync\ndata: " + data.dump() + "\n\n")) return false;
            }
            if (batch.events.empty()) return write(": keep-alive\n\n");
            for (const auto& e : batch.events) {
                const Json data = {{"seq", e.seq}, {"topic", e.topic}, {"at", e.at}, {"data", e.data}};
                if (!write("id: " + std::to_string(e.seq) + "\nevent: " + e.topic + "\ndata: " + data.dump() + "\n\n")) {
                    return false;
                }
                state->seq = e.seq;
            }
            return true;
        });
    });
}

void StudioServer::Impl::proxy_routes() {
    // /api/v1/twins/{id}/{runtime|simulation|planner|world}/... -> upstream.
    auto handler = [this](const httplib::Request& req, httplib::Response& res) {
        const std::string twin_id = req.matches[1];
        const std::string family = req.matches[2];
        const std::string rest = req.matches[3];
        const auto upstream = upstream_for(twin_id, family);
        if (!upstream) {
            send(res, 503, error_body("runtime_not_connected",
                                      "No twin-runtime is connected for this twin. Live behavioural state, executions, "
                                      "predictions and ledgers are served only by the runtime (start it and pass "
                                      "--runtime " + twin_id + "=<url> to twin-studio)."));
            return;
        }
        httplib::Client cli(*upstream);
        cli.set_connection_timeout(std::chrono::milliseconds(options.runtime_timeout_ms));
        cli.set_read_timeout(std::chrono::milliseconds(options.runtime_timeout_ms + 25000));  // predictions/replays can take a while
        // runtime, simulation, planner, world (the twin's KNOWN world) and mission are served by
        // twin-runtime; observer and scenario (physical ground truth, visualisation only) by twin-world.
        const std::string path = "/" + family + (rest.empty() ? "" : "/" + rest);
        std::string query;
        for (const auto& [k, v] : req.params) query += (query.empty() ? "?" : "&") + url_encode(k) + "=" + url_encode(v);
        httplib::Headers headers;
        if (req.has_header("Last-Event-ID")) headers.emplace("Last-Event-ID", req.get_header_value("Last-Event-ID"));
        const bool is_stream = family == "runtime" && rest == "stream";
        if (is_stream) {
            // Streaming passthrough of the runtime's SSE.
            auto client = std::make_shared<httplib::Client>(*upstream);
            res.set_header("Cache-Control", "no-cache");
            res.set_chunked_content_provider("text/event-stream", [this, client, path, query, headers](std::size_t, httplib::DataSink& sink) {
                client->set_read_timeout(std::chrono::hours(1));
                auto r = client->Get(path + query, headers, [&](const char* data, std::size_t len) {
                    return !stopping.load() && sink.write(data, len);
                });
                if (!r) {
                    const std::string msg = "event: upstream_error\ndata: {\"code\":\"runtime_unreachable\"}\n\n";
                    sink.write(msg.data(), msg.size());
                }
                sink.done();
                return true;
            });
            return;
        }
        httplib::Result r = req.method == "POST"
                                ? cli.Post(path + query, headers, req.body, "application/json")
                                : cli.Get(path + query, headers);
        if (!r) {
            send(res, 503, error_body("runtime_unreachable", "The twin-runtime at " + *upstream +
                                                                 " did not answer (" + httplib::to_string(r.error()) + ")."));
            return;
        }
        res.status = r->status;
        res.set_content(r->body, r->has_header("Content-Type") ? r->get_header_value("Content-Type") : "application/json");
    };
    const std::string pattern = R"(/api/v1/twins/([^/]+)/(runtime|simulation|planner|world|mission|observer|scenario)/?(.*))";
    server.Get(pattern, handler);
    server.Post(pattern, handler);
}

void StudioServer::Impl::static_routes() {
    if (!options.web_root) return;
    const fs::path root = fs::weakly_canonical(*options.web_root);
    server.Get(R"(/(?!api/).*)", [root](const httplib::Request& req, httplib::Response& res) {
        fs::path rel = fs::path(req.path).relative_path();
        fs::path file = fs::weakly_canonical(root / rel);
        // Never serve outside the web root; unknown paths fall back to index.html (SPA routing).
        const bool inside = std::mismatch(root.begin(), root.end(), file.begin()).first == root.end();
        std::error_code ec;
        // A static site inside the web root (the documentation at /docs/) serves its own index.html;
        // "/docs" is redirected to "/docs/" so the site's relative links resolve.
        if (inside && !rel.empty() && fs::is_directory(file, ec) && fs::is_regular_file(file / "index.html", ec)) {
            if (req.path.back() != '/') {
                res.set_redirect(req.path + "/");
                return;
            }
            file /= "index.html";
        }
        if (!inside || rel.empty() || !fs::is_regular_file(file, ec)) file = root / "index.html";
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            send(res, 404, error_body("not_found", "web UI not built: run `npm run build` in web/studio"));
            return;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        res.set_content(ss.str(), content_type_for(file));
        if (file.filename() != "index.html" && req.path.rfind("/assets/", 0) == 0) {
            res.set_header("Cache-Control", "public, max-age=31536000, immutable");
        } else {
            res.set_header("Cache-Control", "no-cache");
        }
    });
}

StudioServer::StudioServer(Services& services, ServerOptions options)
    : impl_(std::make_unique<Impl>(services, std::move(options))) {
    Impl& i = *impl_;
    i.server.new_task_queue = [] { return new httplib::ThreadPool(48); };
    // Request timing: pre-routing and the logger run on the same worker thread.
    static thread_local std::chrono::steady_clock::time_point request_started{};
    i.server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        request_started = std::chrono::steady_clock::now();
        const std::string id = req.has_header("X-Request-Id") ? req.get_header_value("X-Request-Id").substr(0, 64) : random_id();
        res.set_header("X-Request-Id", id);
        return httplib::Server::HandlerResponse::Unhandled;
    });
    i.server.set_logger([&i](const httplib::Request& req, const httplib::Response& res) {
        if (req.path == "/api/v1/stream" || req.path.rfind("/api/", 0) != 0) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - request_started).count();
        const bool streaming = res.get_header_value("Content-Type") == "text/event-stream";
        const bool slow = !streaming && ms >= 2000;
        i.services.app_log().write(res.status >= 500 ? LogLevel::Error : ((res.status >= 400 || slow) ? LogLevel::Warn : LogLevel::Debug),
                                   "studio.http", req.method + " " + req.path + (slow ? " (slow)" : ""),
                                   {{"status", res.status}, {"actor", req.get_header_value("X-Twin-Actor")}, {"durationMs", ms}},
                                   res.get_header_value("X-Request-Id"));
    });
    i.routes();
    i.blueprint_routes();
    i.stream_route();
    i.proxy_routes();
    i.server.Get(R"(/api/.*)", [](const httplib::Request&, httplib::Response& res) {
        send(res, 404, error_body("not_found", "unknown API route"));
    });
    i.static_routes();
}

StudioServer::~StudioServer() { stop(); }

Result<int> StudioServer::bind() {
    int port = impl_->options.port;
    if (port == 0) {
        port = impl_->server.bind_to_any_port(impl_->options.host);
        if (port < 0) return make_error(ErrorCode::Unavailable, "cannot bind any port on " + impl_->options.host);
        return port;
    }
    if (!impl_->server.bind_to_port(impl_->options.host, port)) {
        return make_error(ErrorCode::Unavailable, "cannot bind " + impl_->options.host + ":" + std::to_string(port) +
                                                      " (in use?)");
    }
    return port;
}

Status StudioServer::listen() {
    if (!impl_->server.listen_after_bind()) {
        if (impl_->stopping.load()) return {};
        return make_error(ErrorCode::Unavailable, "server stopped unexpectedly");
    }
    return {};
}

void StudioServer::stop() {
    impl_->stopping.store(true);
    impl_->services.events().close();
    impl_->server.stop();
}

}  // namespace twin::studio
