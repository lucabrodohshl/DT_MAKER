/**
 * @file clock.hpp
 * @brief Logical time of plant messages: monotonic ticks from the plant's timestamps or from
 *        arrival times.
 * @ingroup ingest
 *
 * The twin's kernel works in logical time (ticks; see docs/logical-time-model.md). The plant
 * works in its own clock. This class maps one to the other, starting at the twin's current
 * logical time when the connector starts:
 *  - a message with a plant timestamp is placed relative to the first timestamped message
 *    (so a recorded log replays with its original spacing, whatever the arrival speed);
 *  - a message without one is placed at its arrival time;
 *  - logical time never goes backwards: a late message is stamped with the last time used and
 *    reported as late. A plant clock that jumps back by more than a minute (a device reboot, a
 *    log replayed again) is re-anchored instead of clamping everything after it.
 *
 * Wall-clock values only position the plant's data on the twin's time line; they never take a
 * decision. No real-time guarantee is implied.
 */
#pragma once

#include <optional>
#include <string_view>

#include "twin/core/logical_time.hpp"

namespace twin::ingest {

/// @brief Ticks per wall-clock second of a time base (@p unit is s, ms, min or h; none if unknown).
[[nodiscard]] std::optional<double> ticks_per_second(std::int64_t ticks_per_unit, std::string_view unit) noexcept;

/// @brief See file documentation.
class LogicalClock {
public:
    /// @brief Clock with @p ticks_per_second ticks per wall-clock second.
    explicit LogicalClock(double ticks_per_second) : tps_(ticks_per_second) {}

    /// @brief Logical time assigned to one message.
    struct Stamp {
        Ticks ticks{0};            ///< Logical time.
        bool late{false};          ///< The message was earlier than the last time used (clamped).
        double late_seconds{0};    ///< By how much.
        bool reanchored{false};    ///< The plant clock jumped back; the mapping was restarted.
    };

    /// @brief (Re)start at the twin's logical time @p now_ticks, at wall time @p wall (seconds).
    void reset(Ticks now_ticks, double wall);
    /// @brief A message carrying the plant time @p source_seconds, received at @p wall.
    [[nodiscard]] Stamp from_source(double source_seconds, double wall);
    /// @brief A message without a plant time, received at @p wall.
    [[nodiscard]] Stamp from_arrival(double wall);
    /// @brief The logical time the plant has reached at @p wall: the last message's time plus the
    ///        wall time since it arrived (may be below last() right after a heartbeat).
    [[nodiscard]] Ticks due(double wall) const;
    /// @brief The twin's time was advanced to @p ticks without a message (a heartbeat): never
    ///        stamp before it, but keep extrapolating from the last message.
    void raise_floor(Ticks ticks) noexcept;
    /// @brief The twin is at @p ticks (another writer moved it, or a resynchronisation): never
    ///        stamp before it, and re-anchor the plant clock at the next timestamped message.
    void observe(Ticks ticks, double wall);
    /// @brief The last logical time used.
    [[nodiscard]] Ticks last() const noexcept { return last_; }
    /// @brief Ticks per wall-clock second.
    [[nodiscard]] double rate() const noexcept { return tps_; }

    /// @brief A backward jump of the plant clock larger than this restarts the mapping (seconds).
    static constexpr double kReanchorSeconds = 60.0;

private:
    double tps_;
    Ticks last_{0};                        ///< Monotonic floor: the last logical time used.
    Ticks ref_ticks_{0};                   ///< Extrapolation reference: last message's time ...
    double ref_wall_{0};                   ///< ... and its arrival (wall seconds).
    std::optional<double> source_origin_;  ///< Plant time of the anchor.
    Ticks ticks_origin_{0};                ///< Logical time of the anchor.
};

}  // namespace twin::ingest
