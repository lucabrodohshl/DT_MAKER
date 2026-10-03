/**
 * @file configuration.cpp
 * @brief Configuration helpers: clock lookup, validation of snapshots, rendering.
 */
#include "twin/kernel/configuration.hpp"

#include "twin/kernel/model.hpp"
#include "twin/kernel/semantics.hpp"

namespace twin::kernel {

Ticks clock_value(const Configuration& c, ir::ClockIndex clock) noexcept {
    return clock == ir::kReferenceClock ? Ticks{0} : c.clocks[clock - 1U];
}

Status check_configuration(const Model& model, const Configuration& c) {
    if (c.location >= model.location_count()) {
        return make_error(ErrorCode::ValidationError, "configuration location out of range")
            .with("location", std::to_string(c.location));
    }
    if (c.clocks.size() != model.clock_count()) {
        return make_error(ErrorCode::ValidationError, "configuration has a wrong number of clocks")
            .with("expected", std::to_string(model.clock_count()))
            .with("found", std::to_string(c.clocks.size()));
    }
    if (c.time < 0 || c.time > kMaxTicks) {
        return make_error(ErrorCode::ValidationError, "logical time out of range");
    }
    for (std::size_t i = 0; i < c.clocks.size(); ++i) {
        // 0 <= v(x) <= theta holds in every reachable configuration (clocks start
        // at 0 together with theta, advance together, and are only reset to 0).
        if (c.clocks[i] < 0 || c.clocks[i] > c.time) {
            return make_error(ErrorCode::ValidationError,
                              "clock value outside [0, logical time]")
                .with("clock", model.ir().clocks[i]);
        }
    }
    if (!satisfies(c, model.invariant(c.location))) {
        return make_error(ErrorCode::InvariantViolation,
                          "configuration violates the invariant of its location")
            .with("location", model.ir().locations[c.location].id);
    }
    return ok_status();
}

std::string describe(const Model& model, const Configuration& c) {
    const TimeBase base = model.time_base();
    std::string out = c.location < model.location_count() ? model.ir().locations[c.location].id
                                                           : "<invalid>";
    out += '{';
    for (std::size_t i = 0; i < c.clocks.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += i < model.clock_count() ? model.ir().clocks[i] : "?";
        out += '=';
        out += format_time(c.clocks[i], base);
    }
    out += "}@";
    out += format_time(c.time, base);
    return out;
}

}  // namespace twin::kernel
