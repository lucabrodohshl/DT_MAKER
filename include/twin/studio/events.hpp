/**
 * @file events.hpp
 * @brief In-process event hub behind the Studio live stream (Server-Sent Events).
 * @ingroup studio
 *
 * Every published event gets a strictly increasing sequence number `seq`
 * (the SSE `id`). The hub keeps a bounded ring buffer; a client that resumes
 * with `Last-Event-ID` receives the missed events if they are still buffered,
 * otherwise a single `resync` event telling it that a **gap** occurred and that
 * it must refetch authoritative state. Each hub instance also has an `epoch`
 * (random per process start): a client that sees a different epoch knows the
 * server restarted and its sequence numbers are not comparable.
 *
 * Events are notifications ("something changed"), never the source of truth:
 * the UI always re-reads state through the REST API.
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "twin/json/canonical.hpp"

namespace twin::studio {

/// @brief One live event.
struct LiveEvent {
    std::uint64_t seq{0};   ///< Strictly increasing within an epoch.
    std::string topic;      ///< e.g. "artifact", "evidence", "telemetry", "deployment", "change", "audit".
    json::Json data;        ///< Small payload (ids, states), never full documents.
    std::string at;         ///< ISO 8601 UTC wall-clock time.
};

/// @brief Result of waiting for events after a cursor.
struct EventBatch {
    std::vector<LiveEvent> events;  ///< Events with seq > cursor (possibly empty on timeout).
    bool gap{false};                ///< Events after the cursor were already evicted.
};

/// @brief See file documentation.
class EventHub {
public:
    explicit EventHub(std::size_t capacity = 2048);

    /// @brief Publish an event; returns its seq.
    std::uint64_t publish(std::string topic, json::Json data, std::string at);

    /// @brief Events with seq > @p after, waiting up to @p timeout for at least one.
    [[nodiscard]] EventBatch wait_after(std::uint64_t after, std::chrono::milliseconds timeout);

    /// @brief Last assigned seq (0 if none).
    [[nodiscard]] std::uint64_t head() const;

    /// @brief Random identifier of this hub instance (changes on restart).
    [[nodiscard]] const std::string& epoch() const noexcept { return epoch_; }

    /// @brief Wake all waiters (shutdown).
    void close();

private:
    std::size_t capacity_;
    std::string epoch_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<LiveEvent> buffer_;
    std::uint64_t next_{1};
    bool closed_{false};
};

}  // namespace twin::studio
