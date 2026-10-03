/**
 * @file logical_time.cpp
 * @brief Exact decimal <-> tick conversions for the logical-time grid.
 */
#include "twin/core/logical_time.hpp"

#include <limits>

namespace twin {
namespace {

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

}  // namespace

Status validate_time_base(TimeBase base) {
    std::int64_t r = base.ticks_per_unit;
    if (r < 1 || r > kMaxTicksPerUnit) {
        return make_error(ErrorCode::ValidationError,
                          "ticks_per_unit must be a power of ten in [1, 1e9]")
            .with("ticks_per_unit", std::to_string(r));
    }
    while (r % 10 == 0) {
        r /= 10;
    }
    if (r != 1) {
        return make_error(ErrorCode::ValidationError, "ticks_per_unit must be a power of ten")
            .with("ticks_per_unit", std::to_string(base.ticks_per_unit));
    }
    return ok_status();
}

int fractional_digits(TimeBase base) noexcept {
    int digits = 0;
    for (std::int64_t r = base.ticks_per_unit; r > 1; r /= 10) {
        ++digits;
    }
    return digits;
}

Result<Ticks> units_to_ticks(std::int64_t units, TimeBase base) {
    if (units > kMaxModelConstant || units < -kMaxModelConstant) {
        return make_error(ErrorCode::ArithmeticOverflow, "model constant out of range")
            .with("value", std::to_string(units));
    }
    Ticks result = 0;
    if (__builtin_mul_overflow(units, base.ticks_per_unit, &result)) {
        return make_error(ErrorCode::ArithmeticOverflow, "model constant overflows when scaled")
            .with("value", std::to_string(units));
    }
    return result;
}

Result<Ticks> parse_time(std::string_view text, TimeBase base) {
    if (Status s = validate_time_base(base); !s) {
        return s.error();
    }
    if (text.empty()) {
        return make_error(ErrorCode::ParseError, "empty time value");
    }
    const std::size_t dot = text.find('.');
    const std::string_view int_part = text.substr(0, dot);
    std::string_view frac_part =
        dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
    if (int_part.empty() || (dot != std::string_view::npos && frac_part.empty())) {
        return make_error(ErrorCode::ParseError, "expected digits[.digits]")
            .with("value", std::string(text));
    }
    for (char c : int_part) {
        if (!is_digit(c)) {
            return make_error(ErrorCode::ParseError, "expected digits[.digits]")
                .with("value", std::string(text));
        }
    }
    for (char c : frac_part) {
        if (!is_digit(c)) {
            return make_error(ErrorCode::ParseError, "expected digits[.digits]")
                .with("value", std::string(text));
        }
    }
    // Trailing zeros of the fraction never affect the value.
    while (!frac_part.empty() && frac_part.back() == '0') {
        frac_part.remove_suffix(1);
    }
    const int grid_digits = fractional_digits(base);
    if (static_cast<int>(frac_part.size()) > grid_digits) {
        return make_error(ErrorCode::TimeNotRepresentable,
                          "timestamp has more fractional digits than the logical-time grid")
            .with("value", std::string(text))
            .with("ticks_per_unit", std::to_string(base.ticks_per_unit));
    }
    Ticks whole = 0;
    for (char c : int_part) {
        if (__builtin_mul_overflow(whole, Ticks{10}, &whole) ||
            __builtin_add_overflow(whole, Ticks{c - '0'}, &whole)) {
            return make_error(ErrorCode::ArithmeticOverflow, "time value too large")
                .with("value", std::string(text));
        }
    }
    Ticks ticks = 0;
    if (__builtin_mul_overflow(whole, base.ticks_per_unit, &ticks)) {
        return make_error(ErrorCode::ArithmeticOverflow, "time value too large")
            .with("value", std::string(text));
    }
    // Fraction 0.d1..dk contributes d1..dk * 10^(grid_digits - k) ticks.
    Ticks frac = 0;
    for (char c : frac_part) {
        frac = frac * 10 + (c - '0');
    }
    for (int i = static_cast<int>(frac_part.size()); i < grid_digits; ++i) {
        frac *= 10;
    }
    if (__builtin_add_overflow(ticks, frac, &ticks) || ticks > kMaxTicks) {
        return make_error(ErrorCode::ArithmeticOverflow, "time value exceeds the execution horizon")
            .with("value", std::string(text));
    }
    return ticks;
}

std::string format_time(Ticks ticks, TimeBase base) {
    const std::int64_t r = base.ticks_per_unit;
    const bool negative = ticks < 0;
    // Work with the magnitude in unsigned arithmetic (ticks may be INT64_MIN in theory).
    const auto magnitude = negative ? static_cast<std::uint64_t>(-(ticks + 1)) + 1U
                                    : static_cast<std::uint64_t>(ticks);
    const auto ur = static_cast<std::uint64_t>(r);
    std::string out = negative ? "-" : "";
    out += std::to_string(magnitude / ur);
    std::uint64_t frac = magnitude % ur;
    if (frac != 0) {
        std::string digits = std::to_string(frac);
        const auto width = static_cast<std::size_t>(fractional_digits(base));
        digits.insert(0, width - digits.size(), '0');
        while (!digits.empty() && digits.back() == '0') {
            digits.pop_back();
        }
        out += '.';
        out += digits;
    }
    return out;
}

Result<Ticks> checked_add(Ticks a, Ticks b) {
    Ticks sum = 0;
    if (__builtin_add_overflow(a, b, &sum)) {
        return make_error(ErrorCode::ArithmeticOverflow, "tick addition overflows")
            .with("lhs", std::to_string(a))
            .with("rhs", std::to_string(b));
    }
    return sum;
}

}  // namespace twin
