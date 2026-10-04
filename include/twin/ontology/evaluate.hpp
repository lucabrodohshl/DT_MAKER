/**
 * @file evaluate.hpp
 * @brief Three-valued evaluation of interpretation formulas against observations.
 * @ingroup ontology
 *
 * This is the formal step "OBSERVATION → SEMANTIC INTERPRETATION" of the
 * product: given ontology axioms Δ, observed values for some ontology
 * symbols (obs, e.g. `bearing_temp = 94.1`) and an interpretation formula
 * φ = I(x), the answer is decided by Z3:
 *
 * | Result                    | Condition                         |
 * |---------------------------|-----------------------------------|
 * | True                      | Δ ∧ obs ⊨ φ                       |
 * | False                     | Δ ∧ obs ⊨ ¬φ                      |
 * | Unknown                   | neither (insufficient observations), or the solver could not decide |
 * | InconsistentObservation   | Δ ∧ obs is unsatisfiable (observations contradict the ontology) |
 *
 * Unknown is a first-class answer: missing observations never make a
 * proposition silently false. The result lists which symbols of φ were not
 * observed, so the UI can explain *why* a fact is unknown.
 *
 * Evaluation is informational (it does not drive the kernel); the kernel's
 * propositions are location labels, whose meaning is I_D(location).
 */
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/ontology/verdict.hpp"

namespace twin::ontology {

/**
 * @brief An observed value of an ontology symbol.
 *
 * `value` is an exact decimal ("94.1", "-3", "0.25") for function symbols, or
 * "true"/"false" for 0-ary relations. Floating-point text with exponents is
 * rejected (exactness).
 */
struct Observation {
    std::string symbol;  ///< Nullary function or relation symbol of the ontology.
    std::string value;   ///< Exact decimal or Boolean literal.
};

/// @brief Truth of one interpretation formula under the observations (see file documentation).
enum class Truth { True, False, Unknown, InconsistentObservation };

/// @brief "true" / "false" / "unknown" / "inconsistent_observation".
[[nodiscard]] std::string_view to_string(Truth t) noexcept;

/// @brief Evaluation of one interpretation entry.
struct EntryEvaluation {
    std::string key;                          ///< Location name or event label ("x!").
    bool is_event{false};                     ///< Event label?
    std::string formula;                      ///< Interpretation formula text.
    Truth truth{Truth::Unknown};              ///< Result.
    std::vector<std::string> symbols;         ///< Ontology symbols the formula uses.
    std::vector<std::string> unobserved;      ///< Of those, the ones without an observation.
    std::string solver_note;                  ///< Z3 reason when the solver could not decide.
};

/// @brief Evaluation of an interpretation document.
struct EvaluationReport {
    Verdict3 observations_consistent{Verdict3::Unknown};  ///< Δ ∧ obs satisfiable?
    std::vector<EntryEvaluation> entries;                 ///< In document order.
    std::string ontology_sha256;                          ///< Inputs, for provenance.
    std::string interpretation_sha256;                    ///< Inputs, for provenance.
    std::string checker;                                  ///< checker_identity().
};

/**
 * @brief Evaluate @p keys (all entries if empty) of an interpretation.
 * @return InvalidArgument for unknown symbols / malformed values / unknown keys;
 *         ParseError if the ontology or interpretation does not load.
 */
[[nodiscard]] Result<EvaluationReport> evaluate_interpretation(std::string_view ontology_text,
                                                               std::string_view interpretation_text,
                                                               std::span<const Observation> observations,
                                                               const std::vector<std::string>& keys = {},
                                                               const SolverConfig& config = {});

}  // namespace twin::ontology
