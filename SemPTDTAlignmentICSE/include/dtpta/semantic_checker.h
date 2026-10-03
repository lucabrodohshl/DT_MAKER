#ifndef DTPTA_SEMANTIC_CHECKER_H
#define DTPTA_SEMANTIC_CHECKER_H

#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include "dtpta/core.h"
#include "dtpta/timedautomaton.h"
#include "dtpta/interpretation.h"
#include "dtpta/ontology.h"

namespace dtpta {

/**
 * @struct SemanticAlignmentResult
 * @brief Collects metrics produced by SemanticAlignmentChecker (Algorithm 1).
 *
 * Mirrors the layout of CheckStatistics for easy CSV aggregation.
 */
struct SemanticAlignmentResult {
    bool   aligned             = false;  ///< true iff semantic alignment holds
    size_t label_pairs_in_E    = 0;      ///< |E|: number of semantically equivalent label pairs
    size_t initial_state_pairs = 0;      ///< |K~| before fixpoint
    size_t final_relation_size = 0;      ///< |K~| after fixpoint
    size_t smt_calls_total     = 0;      ///< total Z3 solver invocations
    size_t fixpoint_iterations = 0;      ///< number of fixpoint passes
    double time_ms             = 0.0;    ///< wall-clock time in milliseconds
    long   peak_memory_kb      = 0;      ///< peak RSS delta during alignment (kB, Linux only)

    /// First mismatched label pair discovered (if not aligned).
    std::optional<std::pair<std::string, std::string>> counterexample_labels;

    /** Print a human-readable summary to stdout. */
    void print() const;

    /** Write a CSV header line. */
    static void write_csv_header(std::ofstream& file);

    /** Append one result row to an open CSV file. */
    void append_to_csv(std::ofstream& file, const std::string& model_name) const;
};

// -------------------------------------------------------------------------- //

/**
 * @class SemanticAlignmentChecker
 * @brief Implements Algorithm 1 from the ICSE_DT paper: K-bisimulation fixpoint check.
 *
 * Inherits from DTPTAChecker to reuse tau_closure_cached() and
 * weak_observable_successors_cached().
 *
 * Usage:
 * @code
 *   SemanticAlignmentChecker checker;
 *   SemanticAlignmentResult  result;
 *   bool ok = checker.check_semantic_alignment(pt, dt, knowledge, result);
 * @endcode
 *
 * The zone graphs of both automata must be constructed before calling
 * check_semantic_alignment(), or they will be constructed lazily.
 *
 * Thread safety: NOT thread-safe (sequential only).
 */
class SemanticAlignmentChecker : public DTPTAChecker {
public:
    SemanticAlignmentChecker() = default;

    /**
     * @brief Run semantic alignment check (Algorithm 1).
     *
     * @param pt_view  Physical Twin zone-graph automaton (zone graph pre-built or lazy).
     * @param dt_view  Digital Twin zone-graph automaton.
     * @param phi      Ontology + interpretation pair {Δ, IP, ID}.
     * @param result   Output: detailed metrics and verdict.
     * @return true iff (s0_PT, s0_DT) ∈ K~ after fixpoint convergence.
     */
    bool check_semantic_alignment(
        const TimedAutomaton& pt_view,
        const TimedAutomaton& dt_view,
        const DomainKnowledge& phi,
        SemanticAlignmentResult& result);

    /**
     * @brief Standard weak timed bisimulation check (syntactic label matching only).
     *
     * Labels are matched syntactically: label `a` in PT can only be matched by
     * the identical label `a` in DT.  No ontology or interpretation is used.
     * This is the classical WTB check and serves as a comparison baseline for
     * the semantics-enriched DTPTA result.
     *
     * @param pt_view  Physical Twin zone-graph automaton.
     * @param dt_view  Digital Twin zone-graph automaton.
     * @param result   Output: metrics and verdict (smt_calls_total will be 0).
     * @return true iff (s0_PT, s0_DT) ∈ K~ after fixpoint convergence.
     */
    bool check_weak_timed_bisimulation(
        const TimedAutomaton& pt_view,
        const TimedAutomaton& dt_view,
        SemanticAlignmentResult& result);

private:
    // ------------------------------------------------------------------ //
    //  Internal types
    // ------------------------------------------------------------------ //

    using ZonePair = std::pair<const ZoneState*, const ZoneState*>;

    struct ZonePairHash {
        size_t operator()(const ZonePair& p) const noexcept {
            // Use raw pointer addresses for uniform distribution.
            // Content-based hash_value causes clustering for same-location states.
            size_t h1 = reinterpret_cast<uintptr_t>(p.first);
            size_t h2 = reinterpret_cast<uintptr_t>(p.second);
            return h1 ^ (h2 * 0x9e3779b9 + 0x6c62272e + (h1 << 6) + (h1 >> 2));
        }
    };

    using RelationSet = std::unordered_set<ZonePair, ZonePairHash>;

    // ------------------------------------------------------------------ //
    //  Algorithm helpers
    // ------------------------------------------------------------------ //

    /**
     * @brief Step 1 of Algorithm 1: build semantic label equivalence relation E.
     *
     * E = {(a, a') ∈ A × B | Δ ⊨ IP(a) ↔ ID(a')}
     *
     * Actions without an explicit interpretation are treated as having formula ⊤.
     * Two labels without interpretations are equivalent iff they are syntactically equal.
     *
     * @param pt      PT automaton (provides A = observable labels).
     * @param dt      DT automaton (provides B = observable labels).
     * @param phi     Domain knowledge with ontology and interpretations.
     * @param[out] E  Result: for each PT label a, the set of equivalent DT labels.
     */
    void build_label_equivalence(
        const TimedAutomaton& pt,
        const TimedAutomaton& dt,
        const DomainKnowledge& phi,
        std::unordered_map<std::string, std::unordered_set<std::string>>& E);

    /**
     * @brief Step 2 of Algorithm 1: seed the candidate K-bisimulation relation.
     *
     * K~ = {(p, d) ∈ SP × SD | Δ ⊨ IP(loc(p)) ↔ ID(loc(d))}
     *
     * When no explicit state interpretations are provided the pair is always
     * included (trivially consistent under ⊤).
     *
     * @param pt   PT automaton.
     * @param dt   DT automaton.
     * @param phi  Domain knowledge.
     * @param[out] kbisim Seeded relation set.
     */
    void seed_relation(
        const TimedAutomaton& pt,
        const TimedAutomaton& dt,
        const DomainKnowledge& phi,
        RelationSet& kbisim);

    /**
     * @brief Step 3 of Algorithm 1: greatest-fixed-point refinement.
     *
     * Repeatedly removes pairs (p, d) from K~ that violate:
     *  Condition II  — every PT observable move has a matching DT move under E.
     *  Condition III — every DT observable move has a matching PT move under E.
     *
     * Continues until no more pairs are removed.
     *
     * @param pt       PT automaton.
     * @param dt       DT automaton.
     * @param E        Label equivalence relation (from build_label_equivalence).
     * @param[in,out] kbisim Relation being refined.
     * @param[out] result   Updated with fixpoint_iterations and counterexample.
     */
    void fixpoint_refine(
        const TimedAutomaton& pt,
        const TimedAutomaton& dt,
        const std::unordered_map<std::string, std::unordered_set<std::string>>& E,
        const std::unordered_set<std::string>& dt_observable,
        RelationSet& kbisim,
        SemanticAlignmentResult& result);

    /**
     * @brief Check a single pair (zp, zd) against all K-bisimulation conditions.
     *
     * Newly discovered successor pairs not yet in kbisim are appended to new_pairs.
     * Returns true if the pair is valid, false if it should be rejected.
     */
    bool check_pair(
        const TimedAutomaton& pt,
        const TimedAutomaton& dt,
        const std::unordered_map<std::string, std::unordered_set<std::string>>& E,
        const std::unordered_set<std::string>& dt_observable,
        const std::unordered_map<std::string, std::vector<std::string>>& pt_equiv_of,
        const std::unordered_set<std::string>& a_unmatched,
        const std::unordered_set<std::string>& b_unmatched,
        const ZoneState* zp,
        const ZoneState* zd,
        const RelationSet& kbisim,
        const RelationSet& rejected,
        std::vector<ZonePair>& new_pairs,
        SemanticAlignmentResult& result);

    /**
     * @brief Collect all non-tau observable action labels from a TimedAutomaton.
     */
    static std::unordered_set<std::string> observable_labels(const TimedAutomaton& ta);

    /**
     * @brief Helper: return the formula for an event label, defaulting to ⊤.
     */
    static z3::expr formula_for_event(
        const InterpretationMap& imap,
        const std::string& label,
        z3::context& ctx);

    /**
     * @brief Helper: return the formula for a state (location name), defaulting to ⊤.
     */
    static z3::expr formula_for_state(
        const InterpretationMap& imap,
        const std::string& location_name,
        z3::context& ctx);
};

} // namespace dtpta

#endif // DTPTA_SEMANTIC_CHECKER_H
