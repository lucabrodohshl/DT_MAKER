/**
 * @file refinement.hpp
 * @brief Domain-knowledge refinement Φ₁ ⊑ Φ₂ (paper Def. 4) and alignment preservation (Theorem 3).
 * @ingroup ontology
 *
 * ## Definition 4 (ICSE_DT_final.pdf, Section V)
 *
 * Let Φ₁ = (K₁, I₁) and Φ₂ = (K₂, I₂). Φ₁ refines Φ₂ (Φ₁ ⊑ Φ₂) iff
 *
 *  - **(a)** S₁ ⊇ S₂, F₁ ⊇ F₂, R₁ ⊇ R₂, and shared symbols keep their sorts;
 *  - **(b)** Δ₁ ⊨ Δ₂;
 *  - **(c)** for each twin X ∈ {P, D}: Δ₁ ⊨ I₁,X(x) ↔ I₂,X(x) for every
 *    x ∈ AP ∪ Σ_E, with I₁,X(x) = τ ⇔ I₂,X(x) = τ.
 *
 * Here Φ₂ is the **base** (the published version) and Φ₁ the **candidate**.
 *
 * ## How each condition is decided
 *
 *  - (a) structurally, on the *declared* signatures (sort names as written).
 *    The aligner maps every user sort to Real, so Z3 alone could not detect a
 *    sort change; the declared-name check is the stricter, intended reading.
 *  - (b) one obligation per base axiom δ: Δ₁ ⊨ δ, decided by Z3 with δ parsed
 *    over the candidate signature by the aligner's own parser. A base axiom
 *    that does not even parse over the candidate signature fails (b) (and (a)).
 *  - (c) one obligation per interpretation key of each supplied twin
 *    interpretation. A key interpreted in one version and absent (τ) in the
 *    other violates the τ-clause. For interpretations that are *not*
 *    supplied, (c) is reported as not evaluated (ontology-only check K₁ ⊑ K₂).
 *
 * Every obligation is three-valued. The overall verdict is
 *
 *  - NotARefinement if any obligation is refuted (with a counter-model for
 *    entailment failures) or (a) fails;
 *  - otherwise Unknown if any obligation is undecided;
 *  - otherwise ValidRefinement.
 *
 * CheckFailed is reserved for the checker being unable to evaluate the
 * definition at all (base does not load, candidate invalid or inconsistent,
 * internal error). **It is never conflated with NotARefinement.**
 *
 * ## Theorem 3 (alignment preservation)
 *
 * If Φ′ ⊑ Φ and V_P ∼Φ V_D then V_P ∼Φ′ V_D (also for weak alignment).
 * assess_preservation() applies the theorem only when its premises are
 * established by evidence for *exactly* the same artefacts (by hash).
 */
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "twin/ontology/verdict.hpp"

namespace twin::ontology {

/// @brief Domain knowledge Φ = (K, I_P, I_D) as source texts. Interpretations are optional.
struct DomainKnowledgeText {
    std::string ontology;                   ///< K (.ont text).
    std::optional<std::string> pt_interpretation;  ///< I_P (.interp text), if part of the check.
    std::optional<std::string> dt_interpretation;  ///< I_D (.interp text), if part of the check.
};

/// @brief Verdict of a refinement check.
enum class RefinementVerdict { ValidRefinement, NotARefinement, Unknown, CheckFailed };

/// @brief "valid_refinement" / "not_a_refinement" / "unknown" / "check_failed".
[[nodiscard]] const char* to_string(RefinementVerdict v) noexcept;

/// @brief Status of one condition of Def. 4.
enum class ConditionStatus { Holds, Violated, Unknown, NotEvaluated };

/// @brief "holds" / "violated" / "unknown" / "not_evaluated".
[[nodiscard]] const char* to_string(ConditionStatus s) noexcept;

/// @brief Outcome of one condition: "a", "b", "c.P" or "c.D".
struct ConditionResult {
    std::string condition;                    ///< "a" | "b" | "c.P" | "c.D".
    std::string title;                        ///< Human-readable statement of the condition.
    ConditionStatus status{ConditionStatus::NotEvaluated};  ///< Status.
    std::vector<std::string> details;         ///< Violations / notes.
};

/// @brief One proof obligation discharged with Z3 (or structurally for (a)).
struct Obligation {
    std::string condition;     ///< "a" | "b" | "c.P" | "c.D".
    std::string subject;       ///< e.g. "axiom tolerance_val", "I_D(ICE_PRIMING)", "function rate".
    std::string statement;     ///< The checked statement, e.g. "Δ_candidate ⊨ (= dose_tolerance 5)".
    ConditionStatus status{ConditionStatus::NotEvaluated};  ///< Holds / Violated / Unknown.
    std::vector<std::pair<std::string, std::string>> counter_model;  ///< Witness when violated by Z3.
    std::string note;          ///< Additional explanation (solver reason, τ mismatch, ...).
};

/// @brief Full, persistable result of a refinement check.
struct RefinementReport {
    RefinementVerdict verdict{RefinementVerdict::CheckFailed};  ///< Overall verdict.
    std::string summary;                         ///< One-sentence explanation of the verdict.
    std::vector<ConditionResult> conditions;     ///< a, b, c.P, c.D (always all four).
    std::vector<Obligation> obligations;         ///< Every individual obligation.
    std::vector<std::string> assumptions;        ///< What the verdict relies on.
    std::vector<std::string> failure_reasons;    ///< For CheckFailed: why the check could not run.
    std::string checker;                         ///< checker_identity().
    std::string base_ontology_sha256;            ///< SHA-256 of K₂.
    std::string candidate_ontology_sha256;       ///< SHA-256 of K₁.
    std::string base_pt_sha256, candidate_pt_sha256;  ///< I_P hashes (empty if not part of the check).
    std::string base_dt_sha256, candidate_dt_sha256;  ///< I_D hashes (empty if not part of the check).
    unsigned timeout_ms{0};                      ///< Per-query solver timeout used.
};

/**
 * @brief Decide whether @p candidate refines @p base (Def. 4; see file documentation).
 *
 * If @p base supplies an interpretation and @p candidate does not, the
 * candidate is taken to keep the base interpretation text unchanged (the
 * interpretation is re-checked under the new axioms). Never throws.
 */
[[nodiscard]] RefinementReport check_refinement(const DomainKnowledgeText& base, const DomainKnowledgeText& candidate,
                                                const SolverConfig& config = {});

/// @brief Hash-level facts of a recorded alignment result (from alignment evidence).
struct AlignmentFacts {
    bool aligned{false};              ///< The aligner's verdict.
    std::string ontology_sha256;      ///< K the alignment was decided under.
    std::string pt_interpretation_sha256;  ///< I_P used.
    std::string dt_interpretation_sha256;  ///< I_D used.
};

/// @brief Outcome of applying Theorem 3.
enum class PreservationVerdict { PreservedByRefinement, RealignmentRequired };

/// @brief "preserved_by_refinement" / "realignment_required".
[[nodiscard]] const char* to_string(PreservationVerdict v) noexcept;

/// @brief Decision plus the reason for each unmet premise.
struct PreservationAssessment {
    PreservationVerdict verdict{PreservationVerdict::RealignmentRequired};  ///< Decision.
    std::vector<std::string> reasons;  ///< Why preservation does not follow (empty when preserved).
    std::string justification;         ///< Statement of the theorem application when preserved.
};

/**
 * @brief Apply Theorem 3 to recorded evidence.
 *
 * Preserved only if **all** hold:
 *  1. the alignment evidence says aligned;
 *  2. the refinement report's verdict is ValidRefinement;
 *  3. the report's *base* hashes equal the hashes the alignment was decided
 *     under (ontology, I_P, I_D), so the refinement starts from exactly Φ;
 *  4. the report includes both interpretations (condition (c) for P and D
 *     was actually checked), i.e. it is a check of Φ, not only of K.
 *
 * The new Φ′ is then the report's candidate (its hashes).
 */
[[nodiscard]] PreservationAssessment assess_preservation(const AlignmentFacts& alignment,
                                                         const RefinementReport& refinement);

}  // namespace twin::ontology
