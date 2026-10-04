/**
 * @file api_server.hpp
 * @brief The runtime's REST + Server-Sent-Events API.
 * @ingroup runtime
 *
 * Routes (documented with payloads in docs/runtime-api.md):
 * @code
 *   GET  /runtime/state                 committed semantic state, propositions, enabled transitions
 *   GET  /runtime/enabled-transitions
 *   GET  /runtime/propositions
 *   POST /runtime/event                 {"label"|"transition", "time"|"ticks"}  -> kernel decides
 *   POST /runtime/advance               {"time"|"ticks"}                        -> pure delay
 *   POST /runtime/predict               {"depth", "horizon", "schedule": [...]} -> on copies
 *   GET  /runtime/ledger?since=&limit=  ledger records
 *   POST /runtime/ledger/verify         full chain verification (+ replay if "replay": true)
 *   POST /runtime/ledger/tamper-drill   verify a modified COPY (never the real ledger)
 *   GET  /runtime/package               package identity, checks, alignment evidence
 *   GET  /runtime/model                 the IR (for rendering the automaton)
 *   GET  /runtime/stream                SSE (id = monotone seq; Last-Event-ID resume)
 *   GET  /simulation/state  POST /simulation/{start,pause,step,reset,speed}
 *   GET  /planner/current-plan  GET /planner/history  GET /mission  GET /world/known
 * @endcode
 * Every semantic request goes through TwinSession::submit — there is no
 * other write path to semantic state.
 */
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "twin/runtime/cosim_driver.hpp"
#include "twin/runtime/event_hub.hpp"

namespace httplib {
class Server;
}

namespace twin::runtime {

/// @brief Register all routes on @p server; optionally serve the web UI from @p ui_dir.
void register_routes(httplib::Server& server, CoSimDriver& driver, EventHub& hub,
                     const std::filesystem::path& ui_dir);

}  // namespace twin::runtime
