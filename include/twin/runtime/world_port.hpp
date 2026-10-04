/**
 * @file world_port.hpp
 * @brief The Digital Twin's only channel to the physical side (PT + environment service).
 * @ingroup runtime
 *
 * WorldPort exposes exactly the external interfaces a deployed Digital Twin
 * would have: the building-information service (/env/...) and the flight
 * controller (/pt/...). It is JSON-level on purpose, so the runtime never links
 * the simulator's C++ types — and in particular cannot reach the ground truth,
 * which the world exposes only on /observer/... (no method here can address it).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::runtime {

/// @brief Abstract port to the physical side (see file documentation).
class WorldPort {
public:
    virtual ~WorldPort() = default;
    WorldPort() = default;
    WorldPort(const WorldPort&) = delete;
    WorldPort& operator=(const WorldPort&) = delete;
    WorldPort(WorldPort&&) = delete;
    WorldPort& operator=(WorldPort&&) = delete;

    /// @brief GET /env/map/known — published building knowledge.
    [[nodiscard]] virtual Result<json::Json> known_map() = 0;
    /// @brief GET /env/map/updates?since= — update feed.
    [[nodiscard]] virtual Result<json::Json> updates_since(std::uint64_t since) = 0;
    /// @brief GET /env/mission — mission definition.
    [[nodiscard]] virtual Result<json::Json> mission() = 0;
    /// @brief POST /pt/step — advance the co-simulation by @p dt ticks; telemetry + PT events.
    [[nodiscard]] virtual Result<json::Json> step(Ticks dt) = 0;
    /// @brief POST /pt/command — command the flight controller.
    [[nodiscard]] virtual Status command(const json::Json& command) = 0;
    /// @brief POST /admin/reset — restart the physical scenario (demo control).
    [[nodiscard]] virtual Status reset() = 0;
};

/**
 * @brief Interpret a world-API reply (status code + JSON body).
 *
 * Shared by every WorldPort transport, so refusals map identically whether the
 * world is reached over HTTP or in-process: a 2xx status yields the body; any
 * other status yields the error carried in the body
 * (`{"error": {"code", "message"}}`), with the route and status as context.
 */
[[nodiscard]] Result<json::Json> world_reply(int status, json::Json body, const std::string& route);

/// @brief WorldPort over HTTP (cpp-httplib), e.g. "http://127.0.0.1:8091".
[[nodiscard]] std::unique_ptr<WorldPort> make_http_world_port(const std::string& base_url);

}  // namespace twin::runtime
