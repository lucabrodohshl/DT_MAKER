/**
 * @file diagnostics.hpp
 * @brief Compiler diagnostics: precise, located, machine-readable messages.
 * @ingroup compiler
 */
#pragma once

#include <string>
#include <vector>

namespace twin::compiler {

/// @brief Severity of a diagnostic. Any Error aborts compilation.
enum class Severity { Error, Warning, Note };

/**
 * @brief One diagnostic message.
 *
 * `code` is a stable identifier (e.g. "TWC012") documented in
 * docs/compiler-diagnostics.md; `where` names the model element
 * ("template EnergyBudgetDT, edge #3 (EM_TRANSIT -> EM_RETURNING), guard").
 */
struct Diagnostic {
    Severity severity{Severity::Error};  ///< Severity.
    std::string code;                    ///< Stable diagnostic code.
    std::string message;                 ///< Explanation including the offending text.
    std::string where;                   ///< Model element the message refers to.
    std::string hint;                    ///< How to fix it (may be empty).
};

/// @brief "error"/"warning"/"note".
[[nodiscard]] const char* to_string(Severity severity) noexcept;

/// @brief Render "error[TWC012] where: message (hint: ...)".
[[nodiscard]] std::string render(const Diagnostic& diagnostic);

/// @brief True iff any diagnostic is an error.
[[nodiscard]] bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept;

}  // namespace twin::compiler
