/**
 * @file validation.hpp
 * @brief Validation of ontologies and interpretations (structure + the aligner's theory + Z3).
 * @ingroup ontology
 *
 * Validation answers "is this draft a well-formed artefact the aligner will
 * read exactly as I wrote it, and is it meaningful?". It combines:
 *
 *  1. the strict structural parse (source.hpp; ONT0xx / INT0xx);
 *  2. the authoritative aligner parse (ONT100 / INT100 when it rejects the text);
 *  3. a cross-check that both readings have the same symbol tables (ONT101);
 *  4. Z3 checks:
 *     - ontology: Δ is satisfiable (ONT110 error if not; ONT111 warning if Z3
 *       cannot decide). An inconsistent Δ entails everything, which would make
 *       every alignment and refinement check vacuously true.
 *     - interpretation: every entry is satisfiable under Δ (INT110 warning:
 *       the label can never hold) and not valid under Δ (INT111 warning:
 *       a tautological event interpretation is skipped by the aligner, see
 *       docs/existing-aligner-integration.md finding 13).
 *
 * Validation never publishes anything and never asserts refinement or alignment.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "twin/ontology/source.hpp"
#include "twin/ontology/verdict.hpp"

namespace twin::ontology {

/// @brief Outcome of validating an ontology.
struct OntologyValidation {
    bool valid{false};                          ///< No Error diagnostics (consistency included).
    std::vector<SourceDiagnostic> diagnostics;  ///< All findings, located where possible.
    Verdict3 consistent{Verdict3::Unknown};     ///< Δ satisfiable? (Unknown if not checked).
    bool consistency_checked{false};            ///< False if earlier errors prevented the Z3 check.
    OntologySource structure;                   ///< Strict structural reading.
    std::string content_sha256;                 ///< SHA-256 of the exact text validated.
    std::string checker;                        ///< checker_identity().
};

/// @brief Outcome of validating an interpretation against an ontology.
struct InterpretationValidation {
    bool valid{false};                          ///< No Error diagnostics.
    std::vector<SourceDiagnostic> diagnostics;  ///< All findings.
    InterpretationSource structure;             ///< Strict structural reading.
    std::string content_sha256;                 ///< SHA-256 of the interpretation text.
    std::string ontology_sha256;                ///< SHA-256 of the ontology text it was checked against.
    std::string checker;                        ///< checker_identity().
};

/// @brief Validate an ontology document (see file documentation).
[[nodiscard]] OntologyValidation validate_ontology(std::string_view text, const SolverConfig& config = {});

/**
 * @brief Validate an interpretation document against an ontology.
 *
 * If the ontology itself does not load, a single INT100 error says so (the
 * interpretation cannot be judged against an invalid signature).
 */
[[nodiscard]] InterpretationValidation validate_interpretation(std::string_view ontology_text,
                                                               std::string_view interpretation_text,
                                                               const SolverConfig& config = {});

}  // namespace twin::ontology
