/**
 * @file service.cpp
 * @brief Routing, validation and JSON encoding of the world API (see service.hpp).
 */
#include "twin/world/service.hpp"

#include <charconv>
#include <functional>

namespace twin::world {
namespace {

using json::Json;

/// Largest co-simulation step accepted by /pt/step (one logical minute).
constexpr Ticks kMaxStep = 60000;

ApiReply ok(Json body) { return ApiReply{200, std::move(body)}; }

ApiReply fail(int status, const Error& e) {
    Json context = Json::array();
    for (const auto& [k, v] : e.context) context.push_back(Json{{"key", k}, {"value", v}});
    return ApiReply{status, Json{{"error", {{"code", std::string(to_string(e.code))},
                                            {"message", e.message},
                                            {"context", context}}}}};
}

ApiReply fail(int status, ErrorCode code, std::string message) { return fail(status, make_error(code, std::move(message))); }

Result<Json> body_json(const ApiRequest& r) {
    Result<Json> j = json::parse(r.body.empty() ? std::string("{}") : r.body);
    if (!j) return std::move(j).error();
    if (!j.value().is_object()) return make_error(ErrorCode::InvalidArgument, "request body must be a JSON object");
    return j;
}

Result<std::uint64_t> query_u64(const ApiRequest& r, const std::string& key, std::uint64_t fallback) {
    const auto it = r.query.find(key);
    if (it == r.query.end() || it->second.empty()) return fallback;
    std::uint64_t value = 0;
    const char* first = it->second.data();
    const char* last = first + it->second.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc() || ptr != last) {
        return make_error(ErrorCode::InvalidArgument, "query parameter must be a non-negative integer").with("parameter", key);
    }
    return value;
}

std::optional<geo::Occupancy> injectable_occupancy(const std::string& name) {
    for (geo::Occupancy o : {geo::Occupancy::Free, geo::Occupancy::Wall, geo::Occupancy::Obstacle,
                             geo::Occupancy::DoorOpen, geo::Occupancy::DoorClosed, geo::Occupancy::Hazard}) {
        if (geo::to_string(o) == name) return o;
    }
    return std::nullopt;
}

ApiReply step(World& w, const ApiRequest& r) {
    Result<Json> body = body_json(r);
    if (!body) return fail(400, body.error());
    const Json dt_json = body.value().value("dt", Json(std::int64_t{100}));
    if (!dt_json.is_number_integer()) return fail(400, ErrorCode::InvalidArgument, "dt must be an integer number of ticks");
    const auto dt = dt_json.get<std::int64_t>();
    if (dt <= 0 || dt > kMaxStep) return fail(400, ErrorCode::InvalidArgument, "dt must be in (0, 60000] ticks");
    StepResult result = w.step(dt);
    Json events = Json::array();
    for (const PtEvent& e : result.events) {
        events.push_back(Json{{"at", e.at}, {"label", e.label}, {"detail", e.detail}});
    }
    return ok(Json{{"telemetry", to_json(result.telemetry)}, {"events", events}, {"facility_log", result.facility_log}});
}

ApiReply command(World& w, const ApiRequest& r) {
    Result<Json> body = body_json(r);
    if (!body) return fail(400, body.error());
    Result<Command> c = command_from_json(body.value());
    if (!c) return fail(400, c.error());
    if (Status s = w.command(c.value()); !s) return fail(409, s.error());
    return ok(Json{{"accepted", true}});
}

ApiReply inject(World& w, const ApiRequest& r) {
    Result<Json> body = body_json(r);
    if (!body) return fail(400, body.error());
    const Json& b = body.value();
    std::vector<geo::Cell> cells;
    for (const Json& c : b.value("cells", Json::array())) {
        Result<geo::Cell> cell = geo::cell_from_json(c);
        if (!cell) return fail(400, cell.error());
        cells.push_back(cell.value());
    }
    if (cells.empty()) return fail(400, ErrorCode::InvalidArgument, "no cells to inject");
    const std::optional<geo::Occupancy> o = injectable_occupancy(b.value("occupancy", std::string("obstacle")));
    if (!o) return fail(400, ErrorCode::InvalidArgument, "unknown occupancy");
    const bool notify = b.value("notify", *o == geo::Occupancy::Hazard);
    const std::string description = b.value("description", std::string("Operator injection"));
    if (Status s = w.observer_inject(cells, *o, description, notify); !s) return fail(400, s.error());
    return ok(Json{{"injected", static_cast<std::int64_t>(cells.size())}});
}

/// One route of the table: method, path and handler.
struct Route {
    const char* method;
    const char* path;
    std::function<ApiReply(World&, const ApiRequest&)> handler;
};

const std::vector<Route>& routes() {
    static const std::vector<Route> table = {
        {"GET", "/health", [](World&, const ApiRequest&) { return ok(Json{{"status", "ok"}, {"service", "twin-world"}}); }},
        {"GET", "/scenario", [](World& w, const ApiRequest&) { return ok(w.scenario_info()); }},
        {"GET", "/env/map/known", [](World& w, const ApiRequest&) { return ok(w.env_known_map()); }},
        {"GET", "/env/map/updates",
         [](World& w, const ApiRequest& r) {
             Result<std::uint64_t> since = query_u64(r, "since", 0);
             if (!since) return fail(400, since.error());
             return ok(w.env_updates_since(since.value()));
         }},
        {"GET", "/env/mission", [](World& w, const ApiRequest&) { return ok(w.env_mission()); }},
        {"GET", "/env/hazards", [](World& w, const ApiRequest&) { return ok(w.env_hazards()); }},
        {"POST", "/pt/command", command},
        {"POST", "/pt/step", step},
        {"GET", "/pt/telemetry", [](World& w, const ApiRequest&) { return ok(to_json(w.telemetry())); }},
        {"GET", "/observer/ground-truth", [](World& w, const ApiRequest&) { return ok(w.observer_ground_truth()); }},
        {"GET", "/observer/state", [](World& w, const ApiRequest&) { return ok(w.observer_state()); }},
        {"POST", "/observer/inject", inject},
    };
    return table;
}

ApiReply dispatch(const ApiRequest& request, World& world) {
    bool path_known = false;
    for (const Route& r : routes()) {
        if (request.path != r.path) continue;
        path_known = true;
        if (request.method == r.method) return r.handler(world, request);
    }
    if (path_known) return fail(405, ErrorCode::InvalidArgument, "method not allowed");
    return fail(404, make_error(ErrorCode::NotFound, "no such route").with("path", request.path));
}

}  // namespace

WorldService::WorldService(std::filesystem::path scenario_path) : scenario_path_(std::move(scenario_path)) {}

Result<std::unique_ptr<WorldService>> WorldService::open(const std::filesystem::path& scenario_path) {
    std::unique_ptr<WorldService> service(new WorldService(scenario_path));
    if (Status s = service->reload(); !s) return s.error();
    return service;
}

Status WorldService::reload() {
    Result<Scenario> scenario = load_scenario(scenario_path_);
    if (!scenario) return scenario.error();
    auto fresh = std::make_unique<World>(std::move(scenario).value());
    std::lock_guard lock(mutex_);
    world_ = std::move(fresh);
    return ok_status();
}

ApiReply WorldService::handle(const ApiRequest& request) {
    if (request.path == "/admin/reset") {
        if (request.method != "POST") return fail(405, ErrorCode::InvalidArgument, "method not allowed");
        if (Status s = reload(); !s) return fail(500, s.error());
        return ok(Json{{"reset", true}});
    }
    std::lock_guard lock(mutex_);
    return dispatch(request, *world_);
}

ApiReply WorldService::get(const std::string& path, std::map<std::string, std::string> query) {
    return handle(ApiRequest{"GET", path, std::move(query), {}});
}

ApiReply WorldService::post(const std::string& path, const json::Json& body) {
    return handle(ApiRequest{"POST", path, {}, body.dump()});
}

}  // namespace twin::world
