/**
 * @file logical_time.hpp
 * @brief Exact logical (model) time on a fixed decimal grid.
 * @ingroup core
 *
 * Logical time is *model time*, not wall-clock time (see docs/logical-time-model.md).
 * It is represented exactly as an integer number of **ticks**, where a model
 * declares its time base R = ticks per model time unit (a power of ten, e.g.
 * R = 1000 makes 1 tick = 0.001 time units). The semantic time domain is
 * therefore the grid  T_R = { k / R | k in N, k <= kMaxTicks }.
 *
 * No floating-point number ever enters semantic computations: decimal
 * timestamps such as "31.5" are parsed exactly, and timestamps that are not on
 * the grid are rejected (ErrorCode::TimeNotRepresentable) instead of rounded.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "twin/core/result.hpp"

namespace twin {

/// @brief Logical time / clock value / delay, in ticks of the model's TimeBase.
using Ticks = std::int64_t;

/**
 * @brief Execution horizon: the largest admissible logical time, 2^62 - 1 ticks.
 *
 * Keeping all clock values in [0, 2^62) guarantees that the difference of two
 * clock values and every scaled model constant fit in int64 without overflow.
 * At R = 1000 the horizon is about 4.6e15 time units (> 146 million years if
 * one unit is a second).
 */
inline constexpr Ticks kMaxTicks = (Ticks{1} << 62) - 1;

/// @brief Largest supported time base (ticks per model time unit).
inline constexpr std::int64_t kMaxTicksPerUnit = 1'000'000'000;

/// @brief Largest absolute value of an integer model constant (same as the aligner: int32).
inline constexpr std::int64_t kMaxModelConstant = 2'147'483'647;

/**
 * @brief The logical-time resolution of a model: R ticks per model time unit.
 *
 * R must be a power of ten in [1, kMaxTicksPerUnit] so that the grid consists
 * exactly of the decimal numbers with at most log10(R) fractional digits.
 */
struct TimeBase {
    std::int64_t ticks_per_unit{1000};  ///< R

    /// @brief Member-wise equality.
    friend bool operator==(const TimeBase&, const TimeBase&) = default;
};

/// @brief Check that @p base is a power of ten in [1, kMaxTicksPerUnit].
[[nodiscard]] Status validate_time_base(TimeBase base);

/// @brief Number of fractional decimal digits of the grid (log10 R).
[[nodiscard]] int fractional_digits(TimeBase base) noexcept;

/**
 * @brief Scale an integer model constant (in time units) to ticks: c * R.
 * @return ErrorCode::ArithmeticOverflow if |c| > kMaxModelConstant or the product overflows.
 */
[[nodiscard]] Result<Ticks> units_to_ticks(std::int64_t units, TimeBase base);

/**
 * @brief Parse a non-negative decimal time ("31.5", "0.001", "12") exactly into ticks.
 *
 * Accepted syntax: `digits [ "." digits ]`. Signs, exponents and whitespace are
 * rejected. Trailing fractional zeros beyond the grid precision are accepted.
 * @return ErrorCode::TimeNotRepresentable if the value is not on the grid;
 *         ErrorCode::ParseError on bad syntax; ErrorCode::ArithmeticOverflow
 *         if the value exceeds kMaxTicks.
 */
[[nodiscard]] Result<Ticks> parse_time(std::string_view text, TimeBase base);

/**
 * @brief Render ticks as an exact decimal number of time units, e.g. 31500 -> "31.5".
 *
 * Trailing fractional zeros are removed; integral values have no decimal point.
 * The rendering is exact and parse_time(format_time(t)) == t.
 */
[[nodiscard]] std::string format_time(Ticks ticks, TimeBase base);

/// @brief a + b with overflow detection (ErrorCode::ArithmeticOverflow).
[[nodiscard]] Result<Ticks> checked_add(Ticks a, Ticks b);

}  // namespace twin
