/**
 * @file alignment.hpp
 * @brief Running the existing semantic aligner and recording hash-bound evidence.
 * @ingroup alignment
 *
 * @defgroup alignment Semantic-alignment evidence
 * @brief `V_P ~Phi V_D` as decided by the unmodified SemPTDTAlignmentICSE aligner.
 *
 * check_alignment() runs SemanticAlignmentChecker::check_semantic_alignment
 * (Algorithm 1, Z3) on the PT view, the DT view, the ontology and the two
 * interpretations, and produces an AlignmentEvidence document that binds the
 * verdict to the SHA-256 of every input and to the identity of the aligner
 * build. In addition it
 *  - compiles BOTH views with the strict twin compiler (translation-validated),
 *    so that the aligner's reading of each view is known to be faithful;
 *  - lints for constructs the aligner silently skips (synchronised labels
 *    without interpretation, tautological interpretations, receive labels);
 *  - recomputes the label-equivalence relation E and a location-consistency
 *    table (Condition I, informational) with the aligner's own Ontology.
 *
 * The evidence contains no timings, so it is reproducible byte for byte.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::alignment {

/// @brief Inputs of an alignment check.
struct AlignmentInputs {
    std::filesystem::path pt_model;           ///< V_P (UPPAAL XML).
    std::filesystem::path dt_model;           ///< V_D (UPPAAL XML).
    std::filesystem::path ontology;           ///< Domain ontology K (.ont).
    std::filesystem::path pt_interpretation;  ///< I_P (.interp).
    std::filesystem::path dt_interpretation;  ///< I_D (.interp).
    bool legacy_system_declaration{false};    ///< See compiler::CompileOptions.
};

/// @brief A lint finding.
struct LintFinding {
    std::string severity;  ///< "error" | "warning"
    std::string code;      ///< e.g. "TWA010"
    std::string message;   ///< Explanation.
};

/// @brief A row of the label-equivalence relation E.
struct LabelEquivalence {
    std::string pt_label;                ///< Label of V_P.
    std::vector<std::string> dt_labels;  ///< Equivalent labels of V_D (may be empty).
};

/// @brief A row of the location-consistency table (informational).
struct LocationCorrespondence {
    std::string dt_location;                    ///< Location of V_D.
    std::vector<std::string> pt_equivalents;    ///< PT locations with Delta |= I_P <-> I_D.
};

/// @brief The alignment evidence document (see file documentation).
struct AlignmentEvidence {
    bool aligned{false};                       ///< The aligner's verdict.
    bool lint_clean{false};                    ///< No lint errors.
    std::string counterexample_pt;             ///< First mismatching PT label (if not aligned).
    std::string counterexample_dt;             ///< First mismatching DT label (if not aligned).
    std::size_t label_pairs{0};                ///< |E|.
    std::size_t final_relation_size{0};        ///< |~Phi| after the fixpoint.
    std::size_t smt_calls{0};                  ///< Z3 invocations.
    std::size_t fixpoint_iterations{0};        ///< Worklist iterations.
    std::size_t pt_zones{0};                   ///< Zone-graph size of V_P.
    std::size_t dt_zones{0};                   ///< Zone-graph size of V_D.
    std::size_t pt_internal_transitions{0};    ///< Internal (tau) transitions of V_P.
    std::size_t dt_internal_transitions{0};    ///< Internal (tau) transitions of V_D.
    bool syntactic_baseline_aligned{false};    ///< Classical WTB with syntactic labels.
    std::vector<LabelEquivalence> label_equivalence;      ///< E.
    std::vector<LocationCorrespondence> location_consistency;  ///< Condition I table.
    std::vector<LintFinding> lint;             ///< Lint findings.
    json::Json inputs;                         ///< {role: {file, sha256}}.
    std::string pt_ir_sha256;                  ///< IR hash of V_P (strict compilation).
    std::string dt_ir_sha256;                  ///< IR hash of V_D (strict compilation).
};

/// @brief Run the aligner and build the evidence (see file documentation).
[[nodiscard]] Result<AlignmentEvidence> check_alignment(const AlignmentInputs& inputs);

/// @brief Serialise evidence (format "twin-alignment-evidence/1"; integers and strings only).
[[nodiscard]] json::Json to_json(const AlignmentEvidence& evidence);

}  // namespace twin::alignment
