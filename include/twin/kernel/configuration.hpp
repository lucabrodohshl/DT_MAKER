/**
 * @file configuration.hpp
 * @brief Kernel semantic state: a configuration (l, v, theta).
 * @ingroup kernel
 *
 * A Configuration is a plain value. It is designed so that the abstraction
 * function of the correctness proof,
 *
 *     alpha(k) = ( L[k.location],  x_i |-> k.clocks[i-1] / R,  k.time / R ),
 *
 * is a bijection between kernel configurations and IR configurations (over the
 * logical-time grid): every field corresponds to exactly one component of the
 * formal configuration, and nothing else is stored (no caches, no history, no
 * bookkeeping). See proof/sections/06-kernel-isomorphism.tex.
 */
#pragma once

#include <compare>
#include <string>
#include <vector>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"

namespace twin::kernel {

class Model;

/**
 * @brief Formal configuration (l, v, theta) of the DT view.
 *
 * - @c location : the current location l;
 * - @c clocks   : the clock valuation v, clocks[k] = value of clock k+1 in ticks;
 * - @c time     : the global logical time theta in ticks (an implicit clock that
 *                 is never reset; it orders events and timestamps ledger records).
 */
struct Configuration {
    ir::LocationIndex location{0};  ///< l
    std::vector<Ticks> clocks;      ///< v (ticks), one entry per model clock
    Ticks time{0};                  ///< theta (ticks)

    /// @brief Member-wise equality (configurations are values).
    friend bool operator==(const Configuration&, const Configuration&) = default;
    /// @brief Lexicographic total order; gives sets of configurations a canonical order.
    friend auto operator<=>(const Configuration&, const Configuration&) = default;
};

/// @brief Value of clock @p clock in @p c (the reference clock 0 has value 0).
[[nodiscard]] Ticks clock_value(const Configuration& c, ir::ClockIndex clock) noexcept;

/**
 * @brief Check that @p c is a well-formed, *valid* configuration of @p model:
 * location in range, one value per clock, 0 <= v(x) <= theta <= kMaxTicks, and
 * the location invariant holds. Used when restoring externally supplied snapshots.
 */
[[nodiscard]] Status check_configuration(const Model& model, const Configuration& c);

/// @brief Human-readable rendering, e.g. "NAVIGATING{t_flight=12.5, t_mode=3}@12.5".
[[nodiscard]] std::string describe(const Model& model, const Configuration& c);

}  // namespace twin::kernel
