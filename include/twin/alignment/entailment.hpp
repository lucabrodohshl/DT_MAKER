/**
 * @file entailment.hpp
 * @brief Ontology entailment queries with the aligner's own ontology (Z3), for property checks.
 * @ingroup alignment
 *
 * The same machinery that computes the label equivalence E (dtpta::Ontology over
 * the parsed domain theory Δ) answers the questions Studio asks about semantic
 * properties: is a formula well formed over the ontology's symbols, and does the
 * meaning of a location entail a formula (Δ ⊨ I(L) → φ)?
 */
#pragma once

#include <map>
#include <optional>
#include <string>

#include "twin/core/result.hpp"

namespace twin::alignment {

/// @brief Check that the SMT-LIB2 @p formula parses over the symbols of @p ontology_text (ParseError otherwise).
[[nodiscard]] Status check_formula(const std::string& ontology_text, const std::string& formula);

/**
 * @brief For each location L of @p formula_by_location: does Δ ⊨ I(L) → φ_L hold?
 * @return per location: true/false, or std::nullopt if the interpretation does not interpret L.
 */
[[nodiscard]] Result<std::map<std::string, std::optional<bool>>> entailment_by_location(
    const std::string& ontology_text, const std::string& interpretation_text,
    const std::map<std::string, std::string>& formula_by_location);

}  // namespace twin::alignment
