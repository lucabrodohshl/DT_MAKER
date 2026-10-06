/**
 * @file feed.cpp
 * @brief Feed parsing, interpolation and item generation.
 */
#include "twin/ptfeed/feed.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace twin::ptfeed {
namespace {

using json::Json;

/// Deterministic noise in [-1, 1] from (seed, channel, sample index) — splitmix64.
double noise_unit(std::uint64_t seed, std::size_t channel, std::uint64_t index) {
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (index + 1) + 0xBF58476D1CE4E5B9ULL * (channel + 1);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    z ^= z >> 31U;
    return (static_cast<double>(z >> 11U) / static_cast<double>(1ULL << 53U)) * 2.0 - 1.0;
}

double round_to(double v, int precision) {
    const double f = std::pow(10.0, precision);
    return std::round(v * f) / f;
}

}  // namespace

double channel_value(const Channel& c, double t_s) {
    if (c.keys.empty()) return 0.0;
    if (t_s <= c.keys.front().first) return c.keys.front().second;
    if (t_s >= c.keys.back().first) return c.keys.back().second;
    for (std::size_t i = 1; i < c.keys.size(); ++i) {
        const auto [t1, v1] = c.keys[i];
        if (t_s > t1) continue;
        const auto [t0, v0] = c.keys[i - 1];
        if (c.boolean || t1 <= t0) return t_s < t1 ? v0 : v1;  // step semantics for booleans
        return v0 + (v1 - v0) * (t_s - t0) / (t1 - t0);
    }
    return c.keys.back().second;
}

Result<Feed> feed_from_json(const Json& j) {
    if (!j.is_object()) return make_error(ErrorCode::ValidationError, "a feed is a JSON object");
    Feed f;
    f.name = j.value("name", std::string("feed"));
    f.description = j.value("description", std::string());
    f.cycle_s = j.value("cycle_s", 60.0);
    f.period_ms = j.value("telemetry_period_ms", std::int64_t{1000});
    f.seed = j.value("seed", std::uint64_t{1});
    if (!(f.cycle_s > 0.0) || f.period_ms <= 0) {
        return make_error(ErrorCode::ValidationError, "cycle_s and telemetry_period_ms must be positive");
    }
    const Json channels = j.value("channels", Json::object());  // named: items() must not outlive it
    for (const auto& [name, spec] : channels.items()) {
        Channel c;
        c.name = name;
        c.boolean = spec.value("type", std::string("number")) == "boolean";
        c.noise = spec.value("noise", 0.0);
        if (spec.contains("min") && spec.at("min").is_number()) c.min = spec.at("min").get<double>();
        c.precision = spec.value("precision", 2);
        for (const Json& k : spec.value("keys", Json::array())) {
            if (!k.is_array() || k.size() != 2 || !k[0].is_number() || !k[1].is_number()) {
                return make_error(ErrorCode::ValidationError, "channel keys are [time_s, value] pairs").with("channel", name);
            }
            c.keys.emplace_back(k[0].get<double>(), k[1].get<double>());
        }
        if (c.keys.empty()) return make_error(ErrorCode::ValidationError, "channel without keys").with("channel", name);
        if (!std::is_sorted(c.keys.begin(), c.keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; })) {
            return make_error(ErrorCode::ValidationError, "channel keys must be sorted by time").with("channel", name);
        }
        f.channels.push_back(std::move(c));
    }
    for (const Json& e : j.value("events", Json::array())) {
        Event ev;
        ev.at_s = e.value("at", -1.0);
        ev.label = e.value("label", std::string());
        ev.detail = e.value("detail", Json::object());
        if (ev.label.empty() || ev.at_s < 0.0 || ev.at_s >= f.cycle_s) {
            return make_error(ErrorCode::ValidationError, "events need a label and 0 <= at < cycle_s")
                .with("label", ev.label);
        }
        f.events.push_back(std::move(ev));
    }
    std::stable_sort(f.events.begin(), f.events.end(), [](const Event& a, const Event& b) { return a.at_s < b.at_s; });
    return f;
}

Result<Feed> load_feed(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return make_error(ErrorCode::IoError, "cannot read feed").with("file", path.string());
    std::ostringstream buf;
    buf << in.rdbuf();
    Result<Json> j = json::parse(buf.str());
    if (!j) return std::move(j).error().with("file", path.string());
    return feed_from_json(j.value());
}

std::vector<Item> cycle_items(const Feed& feed, std::uint64_t cycle, Ticks base) {
    std::vector<Item> items;
    const auto cycle_ticks = static_cast<Ticks>(std::llround(feed.cycle_s * 1000.0));
    const Ticks start = base + static_cast<Ticks>(cycle) * cycle_ticks;
    for (const Event& e : feed.events) {
        const Ticks at = start + static_cast<Ticks>(std::llround(e.at_s * 1000.0));
        items.push_back(Item{at, true, Json{{"label", e.label}, {"ticks", at}, {"detail", e.detail}}});
    }
    const std::uint64_t samples_per_cycle = static_cast<std::uint64_t>(cycle_ticks / feed.period_ms);
    for (std::uint64_t k = 0; k < samples_per_cycle; ++k) {
        const Ticks offset = static_cast<Ticks>(k) * feed.period_ms;
        const double t_s = static_cast<double>(offset) / 1000.0;
        Json sample{{"at", start + offset}};
        for (std::size_t c = 0; c < feed.channels.size(); ++c) {
            const Channel& ch = feed.channels[c];
            const double v = channel_value(ch, t_s);
            if (ch.boolean) {
                sample[ch.name] = v >= 0.5;
            } else {
                // Sensors at an exact physical zero (stopped shaft, no flow) read zero.
                const double n = v == 0.0 ? 0.0 : ch.noise * noise_unit(feed.seed, c, cycle * samples_per_cycle + k);
                const double value = ch.min ? std::max(*ch.min, v + n) : v + n;
                sample[ch.name] = round_to(value, ch.precision);
            }
        }
        items.push_back(Item{start + offset, false, std::move(sample)});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.at != b.at) return a.at < b.at;
        return a.is_event && !b.is_event;
    });
    return items;
}

}  // namespace twin::ptfeed
