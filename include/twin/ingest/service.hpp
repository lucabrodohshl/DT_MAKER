/**
 * @file service.hpp
 * @brief The plant connector's core: translate each message, place it in logical time, forward
 *        it to the twin runtime, and keep the counters Studio shows on the connection page.
 * @ingroup ingest
 *
 * Every transport (REST endpoint, MQTT endpoint, broker subscription) hands its messages to
 * one IngestService, which processes them one at a time so that logical time stays monotonic:
 *  1. translate() maps the message to telemetry values and PT event labels (mapping.hpp);
 *  2. the LogicalClock stamps it (clock.hpp);
 *  3. telemetry goes to POST /runtime/telemetry, each event to POST /runtime/pt-event. The
 *     runtime translates the PT label through the verified label equivalence and its kernel
 *     accepts or rejects it; the verdict is returned to the sender and counted, never altered.
 *
 * Deadlines. A location invariant of the verified model is a deadline the plant must meet.
 * When the twin's state has one (GET /runtime/state reports it) and the plant's time passes it
 * by more than the grace period without an event, tick() asks the runtime to let that much
 * time pass; the kernel refuses (the invariant would be violated) and the ledger records the
 * missed deadline. After that the connector stops advancing time: further inputs are judged by
 * the kernel as they come. Optionally (sync_seconds > 0), tick() also advances the twin's clock
 * when the plant is quiet, so that its displayed time follows the plant; each such advance is a
 * ledger record.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "twin/ingest/clock.hpp"
#include "twin/ingest/mapping.hpp"
#include "twin/ingest/runtime_link.hpp"

namespace twin::ingest {

/// @brief Where a message came from (for status and the ledger's detail).
struct Origin {
    std::string transport;  ///< "rest", "mqtt" (the twin's endpoint) or "broker".
    std::string client;     ///< Client id or remote address.
    std::string topic;      ///< MQTT topic ("" for REST).
};

/// @brief Time base and clock policy.
struct ServiceOptions {
    std::int64_t ticks_per_unit{1000};  ///< Ticks per model time unit.
    std::string time_unit{"s"};         ///< s, ms, min or h.
    double sync_seconds{0};             ///< Advance the twin's clock after this much plant silence (0 = only at deadlines).
    double grace_seconds{0.5};          ///< How long past a deadline before it is reported missed.
    std::function<void(const std::string&)> log;  ///< Receives one line per notable event (optional).
};

/// @brief See file documentation.
class IngestService {
public:
    /// @brief Monotonic wall clock in seconds (injectable for tests).
    using WallClock = std::function<double()>;

    /// @brief Service forwarding to @p runtime (which must outlive it); fails for an unknown time unit.
    [[nodiscard]] static Result<std::unique_ptr<IngestService>> create(Mapping mapping, ServiceOptions options, RuntimeApi& runtime,
                                                                       WallClock wall = {});

    /**
     * @brief Process one message.
     * @return {at: {ticks, text}, telemetry: n, events: [{pt, label, accepted, from?, to?, error?}],
     *         unmapped: [...], problems: [...], late: bool} or, when the runtime cannot be
     *         reached, {error: {code, message}} (nothing was forwarded).
     */
    [[nodiscard]] json::Json handle(const json::Json& message, const Origin& origin);
    /// @brief Periodic step (call every 250 ms): reconnect, refresh the deadline, detect a missed
    ///        deadline, optional clock sync.
    void tick();
    /// @brief {runtime, clock, counters, lastMessage, lastEvent, deadline, missedDeadline, recent[]}.
    [[nodiscard]] json::Json status() const;
    /// @brief The mapping (for GET /v1/contract).
    [[nodiscard]] const Mapping& mapping() const noexcept { return mapping_; }

private:
    IngestService(Mapping mapping, ServiceOptions options, RuntimeApi& runtime, WallClock wall, double tps);
    /// Read the runtime's state; on the first success, start the clock there.
    [[nodiscard]] Status refresh_runtime(double wall);
    void note(const std::string& kind, const std::string& text);
    [[nodiscard]] std::string time_text(Ticks t) const;
    [[nodiscard]] json::Json time_json(Ticks t) const;

    Mapping mapping_;
    ServiceOptions options_;
    RuntimeApi& runtime_;
    WallClock wall_;
    LogicalClock clock_;
    mutable std::mutex mu_;
    bool connected_{false};
    std::optional<RuntimeState> runtime_state_;
    double state_read_at_{-1e18};
    bool state_stale_{true};
    std::string runtime_error_;
    double last_input_wall_{0};
    double last_advance_wall_{0};
    std::optional<json::Json> missed_deadline_;  ///< Set once a deadline was reported missed.
    std::string started_at_;

    struct Counters {
        std::uint64_t messages{0};
        std::uint64_t telemetry{0};
        std::uint64_t events{0};
        std::uint64_t accepted{0};
        std::uint64_t rejected{0};
        std::uint64_t failed{0};
        std::uint64_t unmapped{0};
        std::uint64_t problems{0};
        std::uint64_t late{0};
        std::uint64_t advances{0};
    } counters_;
    json::Json last_message_;
    json::Json last_event_;
    std::deque<json::Json> recent_;
};

}  // namespace twin::ingest
