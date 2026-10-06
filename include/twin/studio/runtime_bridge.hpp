/**
 * @file runtime_bridge.hpp
 * @brief Persist a twin-runtime's telemetry stream into Studio's telemetry history.
 * @ingroup studio
 *
 * The runtime keeps only a bounded ring buffer of telemetry (not ledgered, not
 * hashed). For history, trends and explanations Studio subscribes to the
 * runtime's SSE stream (`/runtime/stream`) and stores every `telemetry` event
 * field that a channel declares by source:
 *
 *     "source": "runtime:<twinId>/telemetry#<field>"
 *
 * The runtime's `at` (logical observation time of the co-simulated Physical
 * Twin) is stored as `logical_ticks`; the wall-clock observation time is
 * Studio's reception time, because the Physical Twin has no wall clock (the
 * channel view reports `observedTimeBasis: "reception"`). Reconnects resume with
 * `Last-Event-ID` so the runtime's replay buffer is never re-ingested; ids going
 * backwards mean the runtime restarted and reset the cursor. Telemetry missed
 * while disconnected is a visible gap, never interpolated.
 */
#pragma once

#include <atomic>
#include <string>

#include "twin/studio/services.hpp"

namespace twin::studio {

/// @brief Source prefix that binds a channel to a runtime telemetry field.
[[nodiscard]] std::string runtime_source_prefix(std::string_view twin_id);

/**
 * @brief Stream telemetry from @p runtime_url into channels of @p twin_id until @p stop.
 * Blocking; run on its own thread.
 */
void run_runtime_bridge(Services& services, const std::string& twin_id, const std::string& runtime_url,
                        const std::atomic<bool>& stop);

}  // namespace twin::studio
