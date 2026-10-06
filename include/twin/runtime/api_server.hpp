/**
 * @file api_server.hpp
 * @brief The runtime's REST + Server-Sent-Events API.
 * @ingroup runtime
 *
 * Routes (payloads documented in docs/runtime-api.md):
 * @code
 *   GET  /health
 *   GET  /runtime/state                     committed semantic state, propositions, enabled transitions,
 *                                           last transition, conformance, package/model identity
 *   GET  /runtime/enabled-transitions
 *   GET  /runtime/propositions
 *   POST /runtime/event                     {"label"|"transition", "time"|"ticks", "source"} -> kernel decides
 *   POST /runtime/advance                   {"time"|"ticks"}                                 -> pure delay
 *   POST /runtime/predict                   {"depth", "horizon_ticks", "max_nodes"}  (on copies)
 *   POST /runtime/simulate                  {"schedule": [{"label", "time"|"ticks"}]} (what-if, on a copy)
 *   GET  /runtime/executions                recorded executions (ledgers), newest first
 *   GET  /runtime/executions/{session}      one execution
 *   GET  /runtime/executions/{session}/telemetry?max=
 *   GET  /runtime/ledger?session=&since=&limit=&kind=
 *   POST /runtime/ledger/verify             {"session"?, "replay"?}
 *   POST /runtime/ledger/tamper-drill       {"session"?, "line"?}  (verifies a modified COPY)
 *   POST /runtime/replay                    {"session"?, "frames"?} (re-execution with the recorded package)
 *   GET  /runtime/package                   package identity, live verification checks, alignment evidence
 *   GET  /runtime/model                     the IR (for rendering the behaviour graph)
 *   GET  /runtime/stream                    SSE (id = monotone seq; Last-Event-ID / ?after= resume)
 *   GET  /simulation/state      POST /simulation/reset
 *   co-simulation mode only:
 *   POST /simulation/{start,pause,step,speed}
 *   GET  /mission               POST /mission/start
 *   GET  /planner/current-plan  GET /planner/history  GET /planner/episodes
 *   GET  /world/known           GET /world/telemetry   (the twin's KNOWN world and latest telemetry)
 *   monitor mode only:
 *   POST /runtime/pt-event      {"label": "<PT label>", "ticks"|"time", "detail"}  (E-translated)
 *   POST /runtime/telemetry     {"at": ticks, ...}                                  (logged, streamed)
 * @endcode
 * Every semantic request goes through TwinSession::submit — there is no
 * other write path to semantic state. Errors are
 * `{"error": {"code", "message", "context": [{"key", "value"}]}}`.
 */
#pragma once

#include <filesystem>

#include "twin/runtime/cosim_driver.hpp"
#include "twin/runtime/event_hub.hpp"
#include "twin/runtime/executions.hpp"
#include "twin/runtime/monitor_host.hpp"

namespace httplib {
class Server;
}

namespace twin::runtime {

/// @brief Everything the routes need (non-owning, non-null; must outlive the server).
struct ApiContext {
    RuntimeHost* host{nullptr};            ///< Owner of the current session (either mode).
    CoSimDriver* cosim{nullptr};           ///< Set in co-simulation mode.
    MonitorHost* monitor{nullptr};         ///< Set in monitor mode.
    EventHub* hub{nullptr};                ///< Live events.
    ExecutionStore* executions{nullptr};   ///< Recorded executions.
    PackageRegistry* packages{nullptr};    ///< Verified packages by hash (replay/verify of past executions).
};

/// @brief Register all routes on @p server; optionally serve static files from @p static_dir.
void register_routes(httplib::Server& server, ApiContext& context, const std::filesystem::path& static_dir);

}  // namespace twin::runtime
