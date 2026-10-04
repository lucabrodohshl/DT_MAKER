/**
 * @file in_process_world_port.hpp
 * @brief Test-only WorldPort that drives twin::world::WorldService in-process.
 *
 * The port speaks the world API exactly as the HTTP transport does: it builds
 * the same requests (method, route, query, JSON body), the service dispatches
 * them through the same route table as the twin-world executable, and replies
 * are interpreted by the same runtime::world_reply() as HttpWorldPort. Only the
 * socket is missing — which makes end-to-end runs deterministic and fast.
 *
 * Like the HTTP port it exposes only /env, /pt and /admin routes. Tests may
 * query the ground truth through service() (/observer/...) to check physical
 * outcomes; the runtime under test never sees it.
 */
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "twin/runtime/world_port.hpp"
#include "twin/world/service.hpp"

namespace twin::test {

/// @brief See file documentation.
class InProcessWorldPort final : public runtime::WorldPort {
public:
    explicit InProcessWorldPort(std::shared_ptr<world::WorldService> service) : service_(std::move(service)) {}

    Result<json::Json> known_map() override { return send(service_->get("/env/map/known"), "/env/map/known"); }
    Result<json::Json> updates_since(std::uint64_t since) override {
        return send(service_->get("/env/map/updates", {{"since", std::to_string(since)}}), "/env/map/updates");
    }
    Result<json::Json> mission() override { return send(service_->get("/env/mission"), "/env/mission"); }
    Result<json::Json> step(Ticks dt) override { return send(service_->post("/pt/step", json::Json{{"dt", dt}}), "/pt/step"); }
    Status command(const json::Json& c) override {
        Result<json::Json> r = send(service_->post("/pt/command", c), "/pt/command");
        if (!r) return r.error();
        return ok_status();
    }
    Status reset() override {
        Result<json::Json> r = send(service_->post("/admin/reset"), "/admin/reset");
        if (!r) return r.error();
        return ok_status();
    }

    /// @brief The world service (tests only: e.g. ground-truth checks via /observer/...).
    [[nodiscard]] world::WorldService& service() { return *service_; }

private:
    static Result<json::Json> send(world::ApiReply reply, const std::string& route) {
        // Round-trip through text, as on the wire, so the port sees exactly the HTTP bytes' meaning.
        Result<json::Json> body = json::parse(reply.body.dump());
        if (!body) return std::move(body).error();
        return runtime::world_reply(reply.status, std::move(body).value(), route);
    }

    std::shared_ptr<world::WorldService> service_;
};

}  // namespace twin::test
