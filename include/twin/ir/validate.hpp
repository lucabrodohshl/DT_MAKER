/**
 * @file validate.hpp
 * @brief Well-formedness checking of IR models.
 * @ingroup ir
 *
 * A model accepted by validate() satisfies every structural invariant that the
 * IR semantics (proof/sections/03-ir.tex, Definition "well-formed IR") and the
 * kernel rely on. The kernel re-validates every model it loads, so a model that
 * bypassed the compiler can still never be executed in a malformed state.
 */
#pragma once

#include <vector>

#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"

namespace twin::ir {

/// @brief All well-formedness violations of @p model (empty iff well-formed).
[[nodiscard]] std::vector<Error> validate_all(const Model& model);

/// @brief First well-formedness violation of @p model, or success.
[[nodiscard]] Status validate(const Model& model);

/// @brief True iff @p text is a C-style identifier [A-Za-z_][A-Za-z0-9_]*.
[[nodiscard]] bool is_identifier(std::string_view text) noexcept;

}  // namespace twin::ir
