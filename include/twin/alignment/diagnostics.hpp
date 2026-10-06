/**
 * @file diagnostics.hpp
 * @brief Alignment workspace support: failure categories linked to model elements, and
 * explanations of label pairs — derived from the aligner's evidence and its own ontology.
 * @ingroup alignment
 *
 * The aligner returns a verdict, a first mismatching label pair and (through the
 * evidence) the label-equivalence relation E, the location-consistency table
 * and lint findings. diagnose() turns these into actionable findings, each
 * linked to the PT/DT edges, locations and interpretation entries involved:
 *
 * | category                    | source                                   | severity |
 * |-----------------------------|------------------------------------------|----------|
 * | outside_fragment            | lint TWA001                              | error    |
 * | missing_interpretation      | lint TWA010                              | error    |
 * | receive_label               | lint TWA011                              | error    |
 * | tautological_interpretation | lint TWA012                              | error    |
 * | unused_interpretation       | lint TWA020                              | warning  |
 * | unmatched_pt_event          | E row of a PT label with no DT label     | error if not aligned, else warning |
 * | unmatched_dt_event          | DT label in no E row                     | error if not aligned, else warning |
 * | ontology_equivalence        | counterexample pair not equivalent under Δ | error  |
 * | timing_or_branching         | counterexample pair equivalent under Δ (the views disagree on when or after which steps it occurs) | error |
 * | state_inconsistency         | DT location with no PT location of equivalent meaning (Condition I, informational) | info |
 *
 * Nothing here decides alignment: the verdict is the aligner's. The categories
 * explain it; they are not a second checker.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/alignment/alignment.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::alignment {

/// @brief A model element a finding refers to.
struct ElementLink {
    std::string view;                  ///< "pt", "dt", "pt_interpretation", "dt_interpretation" or "ontology".
    std::string kind;                  ///< "edge", "location", "label", "entry" or "document".
    std::string name;                  ///< Element name; edges as "SOURCE -label-> TARGET".
    std::optional<std::size_t> index;  ///< Edge index in document order (= canonical edge order).
};

/// @brief One finding of the alignment workspace.
struct AlignmentDiagnostic {
    std::string category;            ///< See the table in the file documentation.
    std::string severity;            ///< "error", "warning" or "info".
    std::string message;             ///< Explanation.
    std::vector<ElementLink> links;  ///< Elements involved.
};

/**
 * @brief Findings for an alignment evidence document (twin-alignment-evidence/1).
 * @param evidence The evidence as stored (to_json(AlignmentEvidence)).
 * @param pt_xml   The PT view that was aligned (toolchain bytes), for edge links.
 * @param dt_xml   The DT view that was aligned (toolchain bytes), for edge links.
 */
[[nodiscard]] std::vector<AlignmentDiagnostic> diagnose(const json::Json& evidence, std::string_view pt_xml,
                                                        std::string_view dt_xml);

/// @brief API form: {category, severity, message, links:[{view, kind, name, index?}]}.
[[nodiscard]] json::Json to_json(const AlignmentDiagnostic& diagnostic);
/// @brief API form of a list.
[[nodiscard]] json::Json to_json(const std::vector<AlignmentDiagnostic>& diagnostics);

/**
 * @brief Alignment modes of an evidence document: {weak, strong, strong_reason}.
 *
 * The aligner decides weak semantic alignment. Strong alignment is reported
 * "aligned" only when the views have no internal (tau) transitions (then weak
 * and strong timed bisimulation coincide), "not_aligned" when weak alignment
 * fails (strong implies weak), and "not_decidable" otherwise.
 */
[[nodiscard]] json::Json alignment_modes(bool aligned, std::size_t pt_internal, std::size_t dt_internal);

/// @brief How two labels (or two locations) relate under the ontology.
struct PairExplanation {
    bool equivalent{false};     ///< Δ ⊨ I_P(a) ↔ I_D(b) (the aligner's E criterion).
    bool pt_implies_dt{false};  ///< Δ ⊨ I_P(a) → I_D(b).
    bool dt_implies_pt{false};  ///< Δ ⊨ I_D(b) → I_P(a).
};

/**
 * @brief Explain a PT/DT pair with the aligner's own ontology (Z3).
 * @param kind "event" (labels such as "start!") or "location" (location names).
 * @return NotFound if either side has no interpretation.
 */
[[nodiscard]] Result<PairExplanation> explain_pair(const AlignmentInputs& inputs, std::string_view pt,
                                                   std::string_view dt, std::string_view kind);

/// @brief API form: {equivalent, ptImpliesDt, dtImpliesPt}.
[[nodiscard]] json::Json to_json(const PairExplanation& explanation);

}  // namespace twin::alignment
