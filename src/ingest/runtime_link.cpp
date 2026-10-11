/**
 * @file runtime_link.cpp
 * @brief HTTP client of a monitor-mode twin-runtime (see runtime_link.hpp).
 */
#include "twin/ingest/runtime_link.hpp"

#include <httplib.h>

namespace twin::ingest {

using json::Json;

struct HttpRuntime::Impl {
    explicit Impl(const std::string& url) : client(url) {
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(10, 0);
        client.set_write_timeout(10, 0);
        client.set_keep_alive(true);
    }
    std::mutex mu;
    httplib::Client client;
};

namespace {

/// The runtime's structured error ({"error": {code, message}}) or a transport error.
Error runtime_error(const httplib::Result& r, const char* what) {
    if (!r) return make_error(ErrorCode::Unavailable, std::string("twin runtime unreachable (") + what + "): " + httplib::to_string(r.error()));
    auto body = json::parse(r->body);
    const Json err = body && body.value().is_object() ? body.value().value("error", Json::object()) : Json::object();
    const std::string code = err.value("code", std::string());
    const std::string message = err.value("message", "HTTP " + std::to_string(r->status));
    const ErrorCode ec = error_code_from_string(code).value_or(r->status >= 500 ? ErrorCode::Unavailable : ErrorCode::InvalidArgument);
    Error e = make_error(ec, message);
    e.with("status", std::to_string(r->status));
    return e;
}

/// A verdict view: 200 (accepted) and 422 (rejected) both carry one.
Result<Json> verdict(const httplib::Result& r, const char* what) {
    if (!r || (r->status != 200 && r->status != 422)) return runtime_error(r, what);
    auto body = json::parse(r->body);
    if (!body || !body.value().is_object()) return make_error(ErrorCode::ParseError, std::string("unexpected answer of the runtime to ") + what);
    return std::move(body).value();
}

}  // namespace

HttpRuntime::HttpRuntime(std::string base_url) : impl_(std::make_unique<Impl>(base_url)) {}

HttpRuntime::~HttpRuntime() = default;

Result<RuntimeState> HttpRuntime::state() {
    std::lock_guard l(impl_->mu);
    auto r = impl_->client.Get("/runtime/state");
    if (!r || r->status != 200) return runtime_error(r, "state");
    auto body = json::parse(r->body);
    if (!body || !body.value().is_object()) return make_error(ErrorCode::ParseError, "unexpected runtime state document");
    const Json& j = body.value();
    RuntimeState s;
    s.ticks = j.value("time", Json::object()).value("ticks", Ticks{0});
    const Json deadline = j.value("deadline", Json());
    if (deadline.is_object() && deadline.contains("ticks") && deadline.at("ticks").is_number_integer()) {
        s.deadline = deadline.at("ticks").get<Ticks>();
    }
    s.failed = j.value("failed", false);
    return s;
}

Result<Json> HttpRuntime::pt_event(const std::string& label, Ticks at, const Json& detail) {
    const Json body{{"label", label}, {"ticks", at}, {"detail", detail}, {"source", "twin-ingest"}};
    std::lock_guard l(impl_->mu);
    return verdict(impl_->client.Post("/runtime/pt-event", body.dump(), "application/json"), "pt-event");
}

Status HttpRuntime::telemetry(Ticks at, const Json& fields) {
    Json body = fields;
    body["at"] = at;
    std::lock_guard l(impl_->mu);
    auto r = impl_->client.Post("/runtime/telemetry", body.dump(), "application/json");
    if (!r || r->status != 200) return runtime_error(r, "telemetry");
    return ok_status();
}

Result<Json> HttpRuntime::advance(Ticks to) {
    const Json body{{"ticks", to}, {"source", "twin-ingest"}};
    std::lock_guard l(impl_->mu);
    return verdict(impl_->client.Post("/runtime/advance", body.dump(), "application/json"), "advance");
}

}  // namespace twin::ingest
