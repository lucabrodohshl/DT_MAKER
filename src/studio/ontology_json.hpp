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

[[nodiscard]] json::Json to_json(const ontology::Span& span);
[[nodiscard]] json::Json to_json(const ontology::SourceDiagnostic& d);
[[nodiscard]] json::Json to_json(const std::vector<ontology::SourceDiagnostic>& diagnostics);
/// @brief Sorts/functions/relations/axioms with spans; each axiom lists the symbols it uses.
[[nodiscard]] json::Json to_json(const ontology::OntologySource& source);
/// @brief Entries with spans and used symbols.
[[nodiscard]] json::Json to_json(const ontology::InterpretationSource& source);
[[nodiscard]] json::Json to_json(const ontology::StructuralDiff& diff);
/// @brief Canonical-safe (no floats) rendering of a refinement report.
[[nodiscard]] json::Json to_json(const ontology::RefinementReport& report);
[[nodiscard]] json::Json to_json(const ontology::EvaluationReport& report);
[[nodiscard]] json::Json to_json(const ontology::OntologyValidation& v);
[[nodiscard]] json::Json to_json(const ontology::InterpretationValidation& v);

}  // namespace twin::studio
