/**
 * @file interpretation.hpp
 * @brief Internal: textual reader for the aligner's `.interp` interpretation files.
 *
 * Syntax (identical to dtpta::InterpFileParser, src/semalign/domain_parser.cpp):
 *   ; comment
 *   LocationName : smt2-formula
 *   event_label! : smt2-formula
 * A ';' outside parentheses starts a comment; the first ':' separates key and
 * formula. Formulas are stored verbatim (whitespace-trimmed); their
 * well-typedness against the ontology is checked by `twin align` (Z3).
 */
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "twin/compiler/diagnostics.hpp"

namespace twin::compiler::detail {

/// @brief Entries of an interpretation file, split as the aligner splits them.
struct InterpretationEntries {
    std::map<std::string, std::string> locations;  ///< location name -> formula
    std::map<std::string, std::string> events;     ///< "a!" -> formula
    std::string sha256;                            ///< SHA-256 of the file bytes.
};

/// @brief Read an interpretation file; problems are appended to @p diagnostics.
[[nodiscard]] std::optional<InterpretationEntries> read_interpretation(
    const std::filesystem::path& path, std::vector<Diagnostic>& diagnostics);

}  // namespace twin::compiler::detail
