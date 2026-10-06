/**
 * @file feed.hpp
 * @brief Scripted Physical-Twin feeds: a deterministic storyline of PT events and telemetry.
 * @ingroup ptfeed
 *
 * @defgroup ptfeed Scripted Physical-Twin feed (untrusted test/demo source)
 * @brief Plays the role of an external Physical Twin for monitor-mode runtimes.
 *
 * A feed file describes one operating cycle of a physical asset: telemetry
 * channels as keyframes (numbers interpolated linearly, booleans as steps,
 * optional deterministic noise — never added to an exact zero reading such as
 * a stopped shaft, and clamped to the channel's physical minimum) and PT events (in the PT vocabulary of V_P) at
 * logical times consistent with that telemetry. The feed repeats the cycle,
 * shifting logical time by the cycle length each time.
 *
 * The feed is outside the trusted computing base: it produces observations,
 * and the runtime's kernel decides whether the verified model can explain
 * them. A wrong feed makes the twin report non-conformance; it cannot change
 * what the twin considers admissible.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::ptfeed {

/// @brief A telemetry channel of the feed.
struct Channel {
    std::string name;                               ///< Field name in the telemetry sample.
    bool boolean{false};                            ///< Step-valued true/false channel.
    std::vector<std::pair<double, double>> keys;    ///< (cycle time in s, value), sorted by time.
    double noise{0.0};                              ///< Peak amplitude of deterministic noise (numbers).
    std::optional<double> min;                      ///< Physical lower bound (values are clamped).
    int precision{2};                               ///< Decimal places of emitted numbers.
};

/// @brief A PT event of the feed.
struct Event {
    double at_s{0.0};                          ///< Cycle time in seconds.
    std::string label;                         ///< PT label, e.g. "alarm_raise!".
    json::Json detail = json::Json::object();  ///< Context sent with the event.
};

/// @brief A complete feed.
struct Feed {
    std::string name;                 ///< Human-readable name.
    std::string description;          ///< What the cycle shows.
    double cycle_s{60.0};             ///< Length of one cycle (logical seconds).
    std::int64_t period_ms{1000};     ///< Telemetry period (logical ms).
    std::uint64_t seed{1};            ///< Noise seed.
    std::vector<Channel> channels;    ///< Telemetry channels.
    std::vector<Event> events;        ///< Events, sorted by time.
};

/// @brief One item to send, in logical-time order.
struct Item {
    Ticks at{0};          ///< Absolute logical time (ticks, 1 tick = 1 ms).
    bool is_event{false}; ///< Event or telemetry sample.
    json::Json body;      ///< Request body (pt-event or telemetry).
};

/// @brief Parse and validate a feed document.
[[nodiscard]] Result<Feed> feed_from_json(const json::Json& j);
/// @brief Load a feed file.
[[nodiscard]] Result<Feed> load_feed(const std::filesystem::path& path);

/**
 * @brief The items of cycle @p cycle, starting at absolute logical time @p base:
 * events and telemetry samples in non-decreasing time order; at equal times
 * events come first (the event is what the sample then reflects).
 */
[[nodiscard]] std::vector<Item> cycle_items(const Feed& feed, std::uint64_t cycle, Ticks base);

/// @brief Value of a channel at cycle time @p t_s (without noise).
[[nodiscard]] double channel_value(const Channel& channel, double t_s);

}  // namespace twin::ptfeed
