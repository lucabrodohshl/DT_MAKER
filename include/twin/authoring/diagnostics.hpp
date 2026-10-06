/**
 * @file diagnostics.hpp
 * @brief Authoring diagnostics: located, coded messages shared by validation, text parsing and import.
 * @ingroup authoring
 *
 * Codes are stable and documented in docs/authoring/:
 *  - TWM0xx  structural validation of a canonical model (validate.hpp);
 *  - TWT0xx  TwinTA text syntax (text.hpp);
 *  - TWI0xx  import (import.hpp); compiler codes TWC0xx are passed through
 *            unchanged when the strict UPPAAL reader refuses a construct.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "twin/json/canonical.hpp"

namespace twin::authoring {

/// @brief A range in a source text, 1-based; 0 means unknown.
struct SourceRange {
    std::uint32_t line{0};        ///< First line.
    std::uint32_t column{0};      ///< First column.
    std::uint32_t end_line{0};    ///< Last line.
    std::uint32_t end_column{0};  ///< Column after the last character.
    /// @brief Member-wise equality.
    friend bool operator==(const SourceRange&, const SourceRange&) = default;
};

/// @brief The model element a diagnostic is about.
struct ElementRef {
    std::string kind;  ///< "model", "clock", "constant", "channel", "location", "edge" or "document".
    std::string name;  ///< Name or edge id.
    std::string part;  ///< "invariant", "guard", "sync", "reset", "source", "target", "name" or "".
    /// @brief Member-wise equality.
    friend bool operator==(const ElementRef&, const ElementRef&) = default;
};

/// @brief One diagnostic.
struct Diagnostic {
    std::string severity;               ///< "error", "warning" or "info".
    std::string code;                   ///< Stable code (TWM/TWT/TWI/TWC).
    std::string message;                ///< Explanation, including the offending text.
    std::string hint;                   ///< How to fix it (may be empty).
    ElementRef element;                 ///< Element concerned.
    std::optional<SourceRange> range;   ///< Position in the text or imported document, if known.
    /// @brief Member-wise equality.
    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

/// @brief API form: {severity, code, message, hint, element:{kind,name,part}, range?}.
[[nodiscard]] json::Json to_json(const Diagnostic& diagnostic);
/// @brief API form of a list.
[[nodiscard]] json::Json to_json(const std::vector<Diagnostic>& diagnostics);
/// @brief API form of a range: {line, column, endLine, endColumn}.
[[nodiscard]] json::Json to_json(const SourceRange& range);
/// @brief True iff any diagnostic has severity "error".
[[nodiscard]] bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept;

}  // namespace twin::authoring
