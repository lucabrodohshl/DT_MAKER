/**
 * @file event_hub.hpp
 * @brief Fan-out of runtime events to live observers (Server-Sent Events).
 * @ingroup runtime
 *
 * Every published event gets a monotonically increasing sequence number used
 * as the SSE `id`, so clients detect gaps and resume with `Last-Event-ID`
 * from a bounded replay buffer. Publishing is an internal (tau) action of the
 * production runtime: it reads copies and never touches semantic state.
 */
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "twin/json/canonical.hpp"

namespace twin::runtime {

/// @brief One published event.
struct HubEvent {
    std::uint64_t seq{0};  ///< Monotone sequence number (SSE id).
    std::string type;      ///< Event type (SSE event name).
    json::Json data;       ///< Payload.
};

/// @brief Thread-safe publish/subscribe buffer (see file documentation).
class EventHub {
public:
    /// @brief Hub keeping the last @p capacity events for resumption.
    explicit EventHub(std::size_t capacity = 4000) : capacity_(capacity) {}

    /// @brief Publish an event; returns its sequence number.
    std::uint64_t publish(std::string type, json::Json data);

    /// @brief Events with seq > @p after; waits up to @p timeout_ms if none are available.
    [[nodiscard]] std::vector<HubEvent> wait_after(std::uint64_t after, int timeout_ms);

    /// @brief Sequence number of the newest event (0 if none).
    [[nodiscard]] std::uint64_t head() const;

    /// @brief Format one event as an SSE frame.
    [[nodiscard]] static std::string sse_frame(const HubEvent& e);

    /// @brief Wake all waiters (shutdown).
    void close();

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<HubEvent> buffer_;
    std::size_t capacity_;
    std::uint64_t next_seq_{1};
    bool closed_{false};
};

}  // namespace twin::runtime
