/**
 * @file clock.cpp
 * @brief Logical time of plant messages (see clock.hpp).
 */
#include "twin/ingest/clock.hpp"

#include <algorithm>
#include <cmath>

namespace twin::ingest {

std::optional<double> ticks_per_second(std::int64_t ticks_per_unit, std::string_view unit) noexcept {
    if (ticks_per_unit <= 0) return std::nullopt;
    const auto tpu = static_cast<double>(ticks_per_unit);
    if (unit == "s") return tpu;
    if (unit == "ms") return tpu * 1000.0;
    if (unit == "min") return tpu / 60.0;
    if (unit == "h") return tpu / 3600.0;
    return std::nullopt;
}

void LogicalClock::reset(Ticks now_ticks, double wall) {
    last_ = now_ticks;
    ref_ticks_ = now_ticks;
    ref_wall_ = wall;
    source_origin_.reset();
}

Ticks LogicalClock::due(double wall) const {
    const double elapsed = std::max(0.0, wall - ref_wall_);
    return ref_ticks_ + static_cast<Ticks>(std::llround(elapsed * tps_));
}

LogicalClock::Stamp LogicalClock::from_source(double source_seconds, double wall) {
    Stamp s;
    if (!source_origin_) {
        source_origin_ = source_seconds;
        ticks_origin_ = std::max(last_, due(wall));
    }
    Ticks t = ticks_origin_ + static_cast<Ticks>(std::llround((source_seconds - *source_origin_) * tps_));
    if (t < last_ && static_cast<double>(last_ - t) / tps_ > kReanchorSeconds) {
        source_origin_ = source_seconds;  // the plant clock jumped back: restart the mapping here
        ticks_origin_ = std::max(last_, due(wall));
        t = ticks_origin_;
        s.reanchored = true;
    }
    if (t < last_) {
        s.late = true;
        s.late_seconds = static_cast<double>(last_ - t) / tps_;
        t = last_;
    }
    last_ = t;
    ref_ticks_ = t;
    ref_wall_ = wall;
    s.ticks = t;
    return s;
}

LogicalClock::Stamp LogicalClock::from_arrival(double wall) {
    Stamp s;
    s.ticks = std::max(last_, due(wall));
    last_ = s.ticks;
    ref_ticks_ = s.ticks;
    ref_wall_ = wall;
    return s;
}

void LogicalClock::raise_floor(Ticks ticks) noexcept { last_ = std::max(last_, ticks); }

void LogicalClock::observe(Ticks ticks, double wall) {
    if (ticks <= last_) return;
    last_ = ticks;
    ref_ticks_ = ticks;
    ref_wall_ = wall;
    source_origin_.reset();  // re-anchor the plant clock at the next timestamped message
}

}  // namespace twin::ingest
