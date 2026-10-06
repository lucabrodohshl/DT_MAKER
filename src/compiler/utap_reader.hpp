/**
 * @file utap_reader.hpp
 * @brief Internal: strict extraction of a SourceModel from a UTAP document.
 */
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "source_model.hpp"
#include "twin/compiler/diagnostics.hpp"

namespace UTAP {
class Document;
}

namespace twin::compiler::detail {

using compiler::ReaderOptions;

/**
 * @brief Read @p doc strictly: accept only the aligner's formal fragment.
 *
 * Appends one diagnostic per problem to @p diagnostics (it does not stop at the
 * first error) and returns the model only if no error was found.
 */
[[nodiscard]] std::optional<SourceModel> read_strict(UTAP::Document& doc,
                                                     const ReaderOptions& options,
                                                     std::vector<Diagnostic>& diagnostics);

/// @brief Names of UTAP's built-in global declarations (INT8_MIN, DBL_MAX, ...).
[[nodiscard]] const std::set<std::string>& utap_builtin_names();

/// @brief Largest constant accepted in a clock constraint (strictly below UDBM's infinity).
[[nodiscard]] std::int64_t max_constraint_constant() noexcept;

}  // namespace twin::compiler::detail
