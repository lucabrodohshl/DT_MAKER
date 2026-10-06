/**
 * @file ontology_json.hpp
 * @brief (private) API JSON renderings of twin::ontology results.
 * @ingroup studio
 */
#pragma once

#include "twin/json/canonical.hpp"
#include "twin/ontology/diff.hpp"
#include "twin/ontology/evaluate.hpp"
#include "twin/ontology/refinement.hpp"
#include "twin/ontology/source.hpp"
#include "twin/ontology/validation.hpp"

namespace twin::studio {

/// @brief API form of a source span {line, column, length}.
[[nodiscard]] json::Json to_json(const ontology::Span& span);
/// @brief API form of a diagnostic {code, severity, message, span}.
[[nodiscard]] json::Json to_json(const ontology::SourceDiagnostic& d);
/// @brief API form of a list of diagnostics.
[[nodiscard]] json::Json to_json(const std::vector<ontology::SourceDiagnostic>& diagnostics);
/// @brief Sorts/functions/relations/axioms with spans; each axiom lists the symbols it uses.
[[nodiscard]] json::Json to_json(const ontology::OntologySource& source);
/// @brief Entries with spans and used symbols.
[[nodiscard]] json::Json to_json(const ontology::InterpretationSource& source);
/// @brief API form of a structural diff with the affected symbols.
[[nodiscard]] json::Json to_json(const ontology::StructuralDiff& diff);
/// @brief Canonical-safe (no floats) rendering of a refinement report.
[[nodiscard]] json::Json to_json(const ontology::RefinementReport& report);
/// @brief API form of a three-valued interpretation evaluation.
[[nodiscard]] json::Json to_json(const ontology::EvaluationReport& report);
/// @brief API form of an ontology validation result.
[[nodiscard]] json::Json to_json(const ontology::OntologyValidation& v);
/// @brief API form of an interpretation validation result.
[[nodiscard]] json::Json to_json(const ontology::InterpretationValidation& v);

}  // namespace twin::studio
