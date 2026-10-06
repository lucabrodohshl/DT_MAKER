/**
 * @file server.hpp
 * @brief The Verified Twin Studio HTTP server: REST API, live stream (SSE), runtime proxy, web UI hosting.
 * @ingroup studio
 *
 * All routes live under `/api/v1` and are specified in `api/studio.openapi.yaml`.
 *
 * Conventions:
 *  - JSON bodies; errors are `{"error":{"code","message","context":[{"key","value"}]}}`
 *    with the HTTP status derived from twin::ErrorCode (400 invalid input,
 *    404 not found, 409 lifecycle/state conflict, 422 semantically invalid,
 *    503 unavailable). Internal failures never expose stack traces.
 *  - `X-Twin-Actor` names the caller in audit records. It is **not**
 *    authentication; deployments that need access control must put Studio
 *    behind an authenticating proxy (see docs/studio/security.md).
 *  - `X-Request-Id` is accepted or generated, echoed, and recorded in the
 *    application log as the correlation id.
 *  - `GET /api/v1/stream` is a Server-Sent-Events stream: `id` is the event
 *    sequence number, `event` the topic; a `hello` event carries the hub
 *    epoch; resuming with `Last-Event-ID` after evicted events yields a
 *    `resync` event (the client must refetch state).
 *  - `/api/v1/twins/{id}/{runtime|simulation|planner|world|mission}/...` is
 *    forwarded to that twin's twin-runtime (`world` is the twin's *known* world);
 *    `/api/v1/twins/{id}/{observer|scenario}/...` to twin-world (physical ground
 *    truth, for visualisation only). Without a configured runtime the answer is
 *    503 `runtime_not_connected` — Studio never fabricates behavioural state.
 */
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "twin/core/result.hpp"
#include "twin/studio/services.hpp"

namespace twin::studio {

/// @brief Server options.
struct ServerOptions {
    std::string host{"127.0.0.1"};                 ///< Bind address.
    int port{8080};                                ///< Port (0 = any free port).
    std::optional<std::filesystem::path> web_root; ///< Built web UI (index.html) to serve, if any.
    int runtime_timeout_ms{5000};                  ///< Timeout for proxied runtime requests.
    std::map<std::string, std::string> runtime_urls;  ///< twin id -> twin-runtime base URL (overrides the twin record).
    std::map<std::string, std::string> world_urls;    ///< twin id -> twin-world (ground truth: /observer, /scenario).
};

/// @brief See file documentation.
class StudioServer {
public:
    /// @brief HTTP server exposing @p services; it does not listen until started.
    StudioServer(Services& services, ServerOptions options);
    ~StudioServer();
    StudioServer(const StudioServer&) = delete;
    StudioServer& operator=(const StudioServer&) = delete;
    StudioServer(StudioServer&&) = delete;
    StudioServer& operator=(StudioServer&&) = delete;

    /// @brief Bind the socket (call before listen()); returns the bound port.
    [[nodiscard]] Result<int> bind();
    /// @brief Serve until stop() (blocking).
    [[nodiscard]] Status listen();
    /// @brief Stop serving (thread-safe).
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace twin::studio
