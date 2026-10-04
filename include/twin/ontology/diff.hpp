/**
 * @file diff.hpp
 * @brief Structural (semantic-element) diff of ontologies and interpretations.
 * @ingroup ontology
 *
 * A source (line) diff shows *text*; this diff shows *theory elements*:
 * sorts, function and relation symbols (with signatures) and axioms (by id),
 * and interpretation entries (by key). Comments are documentation and are
 * ignored; formulas are compared modulo whitespace.
 *
 * It also computes the **affected symbols** of a change — every symbol whose
 * declaration changed or that occurs in an added, removed or modified axiom —
 * and the interpretation keys whose formulas mention them. Those entries'
 * meaning under Δ may have changed; whether it actually did is decided by the
 * refinement checker (refinement.hpp), never by the diff.
 */
#pragma once

#include <set>
#include <string>
#include <vector>

#include "twin/ontology/source.hpp"

namespace twin::ontology {

/// @brief Kind of a structural change.
enum class ChangeKind { Added, Removed, Modified, Renamed };

/// @brief "added" / "removed" / "modified" / "renamed".
[[nodiscard]] const char* to_string(ChangeKind kind) noexcept;

/// @brief One element-level change.
struct StructuralChange {
    ChangeKind kind{ChangeKind::Modified};  ///< What happened.
    std::string element;                    ///< "sort" | "function" | "relation" | "axiom" | "location" | "event".
    std::string name;                       ///< Element name (new name for Renamed).
    std::string previous_name;              ///< Old name (Renamed only).
    std::string before;                     ///< Old signature / formula (empty when Added).
    std::string after;                      ///< New signature / formula (empty when Removed).
};

/// @brief Result of a structural diff.
struct StructuralDiff {
    std::vector<StructuralChange> changes;   ///< Ordered: sorts, functions, relations, axioms / entries.
    std::set<std::string> affected_symbols;  ///< Symbols whose meaning may have changed (ontology diff).
};

/// @brief Render a function signature, e.g. "Real Real -> Real" or "Volume".
[[nodiscard]] std::string signature(const FunctionDecl& f);
/// @brief Render a relation signature, e.g. "Hec" or "" for 0-ary.
[[nodiscard]] std::string signature(const RelationDecl& r);
/// @brief Collapse whitespace runs to one space (formula comparison).
[[nodiscard]] std::string normalize_formula(std::string_view formula);

/// @brief Element-level diff of two ontologies.
[[nodiscard]] StructuralDiff diff_ontologies(const OntologySource& from, const OntologySource& to);

/// @brief Entry-level diff of two interpretations.
[[nodiscard]] StructuralDiff diff_interpretations(const InterpretationSource& from, const InterpretationSource& to);

/// @brief Keys of @p interpretation whose formulas mention any of @p symbols (document order).
[[nodiscard]] std::vector<std::string> entries_using(const InterpretationSource& interpretation,
                                                     const std::set<std::string>& symbols);

}  // namespace twin::ontology
