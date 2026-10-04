/**
 * @file refinement.cpp
 * @brief Def. 4 refinement checker and Theorem 3 preservation (see refinement.hpp).
 */
#include "twin/ontology/refinement.hpp"

#include <algorithm>
#include <set>

#include "theory.hpp"
#include "twin/core/sha256.hpp"
#include "twin/ontology/diff.hpp"
#include "twin/ontology/source.hpp"

namespace twin::ontology {

const char* to_string(RefinementVerdict v) noexcept {
    switch (v) {
        case RefinementVerdict::ValidRefinement: return "valid_refinement";
        case RefinementVerdict::NotARefinement: return "not_a_refinement";
        case RefinementVerdict::Unknown: return "unknown";
        case RefinementVerdict::CheckFailed: return "check_failed";
    }
    return "check_failed";
}

const char* to_string(ConditionStatus s) noexcept {
    switch (s) {
        case ConditionStatus::Holds: return "holds";
        case ConditionStatus::Violated: return "violated";
        case ConditionStatus::Unknown: return "unknown";
        case ConditionStatus::NotEvaluated: return "not_evaluated";
    }
    return "not_evaluated";
}

const char* to_string(PreservationVerdict v) noexcept {
    switch (v) {
        case PreservationVerdict::PreservedByRefinement: return "preserved_by_refinement";
        case PreservationVerdict::RealignmentRequired: return "realignment_required";
    }
    return "realignment_required";
}

namespace {

ConditionStatus from_verdict(Verdict3 v) {
    switch (v) {
        case Verdict3::True: return ConditionStatus::Holds;
        case Verdict3::False: return ConditionStatus::Violated;
        case Verdict3::Unknown: return ConditionStatus::Unknown;
    }
    return ConditionStatus::Unknown;
}

std::string first_error(const std::vector<SourceDiagnostic>& diags) {
    for (const auto& d : diags) {
        if (d.severity == Severity::Error) {
            return d.code + " (line " + std::to_string(d.span.line) + "): " + d.message;
        }
    }
    return "invalid";
}

/// @brief Fold obligation statuses of one condition into the condition status.
void fold(ConditionResult& cond, const std::vector<Obligation>& obligations) {
    bool any = false;
    bool violated = false;
    bool unknown = false;
    for (const auto& o : obligations) {
        if (o.condition != cond.condition) continue;
        any = true;
        violated = violated || o.status == ConditionStatus::Violated;
        unknown = unknown || o.status == ConditionStatus::Unknown;
    }
    if (!any) {
        if (cond.status != ConditionStatus::NotEvaluated) cond.status = ConditionStatus::Holds;
        return;
    }
    cond.status = violated ? ConditionStatus::Violated : unknown ? ConditionStatus::Unknown : ConditionStatus::Holds;
}

class Checker {
public:
    Checker(const DomainKnowledgeText& base, const DomainKnowledgeText& candidate, const SolverConfig& config)
        : base_(base), candidate_(candidate), config_(config) {}

    RefinementReport run() {
        init();
        if (!prepare()) return std::move(report_);
        check_signature();
        check_axioms();
        if (!check_interpretation("c.P", "I_P", base_.pt_interpretation, candidate_.pt_interpretation)) {
            return std::move(report_);
        }
        if (!check_interpretation("c.D", "I_D", base_.dt_interpretation, candidate_.dt_interpretation)) {
            return std::move(report_);
        }
        conclude();
        return std::move(report_);
    }

private:
    void init() {
        report_.checker = checker_identity();
        report_.timeout_ms = config_.timeout_ms;
        report_.base_ontology_sha256 = sha256_hex(base_.ontology);
        report_.candidate_ontology_sha256 = sha256_hex(candidate_.ontology);
        auto effective = [](const std::optional<std::string>& c, const std::optional<std::string>& b) {
            return c ? c : b;
        };
        if (base_.pt_interpretation) report_.base_pt_sha256 = sha256_hex(*base_.pt_interpretation);
        if (base_.dt_interpretation) report_.base_dt_sha256 = sha256_hex(*base_.dt_interpretation);
        if (auto c = effective(candidate_.pt_interpretation, base_.pt_interpretation)) {
            report_.candidate_pt_sha256 = sha256_hex(*c);
        }
        if (auto c = effective(candidate_.dt_interpretation, base_.dt_interpretation)) {
            report_.candidate_dt_sha256 = sha256_hex(*c);
        }
        report_.conditions = {
            {"a", "Signature inclusion: S, F, R of the base are kept, shared symbols keep their sorts",
             ConditionStatus::NotEvaluated, {}},
            {"b", "Axiom entailment: the candidate axioms entail every base axiom (Δ_candidate ⊨ Δ_base)",
             ConditionStatus::NotEvaluated, {}},
            {"c.P", "PT interpretation preserved: Δ_candidate ⊨ I'_P(x) ↔ I_P(x) for every label, τ-status kept",
             ConditionStatus::NotEvaluated, {}},
            {"c.D", "DT interpretation preserved: Δ_candidate ⊨ I'_D(x) ↔ I_D(x) for every label, τ-status kept",
             ConditionStatus::NotEvaluated, {}},
        };
        report_.assumptions = {
            "Definition 4 of the semantic-alignment framework (ICSE_DT_final.pdf, Section V) with the base as Φ₂ "
            "and the candidate as Φ₁.",
            "Formulas are read exactly as the semantic aligner reads them (SemPTDTAlignmentICSE OntFileParser / "
            "InterpFileParser); every user sort is interpreted as Real by that reading.",
            "Condition (a) compares declared sort names, which is stricter than the aligner's Real-sorted reading.",
            "Entailment is decided by Z3 modulo linear/nonlinear real arithmetic with a per-query timeout of " +
                std::to_string(config_.timeout_ms) + " ms; undecided queries are reported as unknown, never as "
                "violations.",
        };
    }

    void fail(const std::string& reason) {
        report_.verdict = RefinementVerdict::CheckFailed;
        report_.failure_reasons.push_back(reason);
        report_.summary = "The refinement check could not be carried out: " + reason;
    }

    ConditionResult& condition(const std::string& id) {
        return *std::find_if(report_.conditions.begin(), report_.conditions.end(),
                             [&](const ConditionResult& c) { return c.condition == id; });
    }

    bool prepare() {
        base_parsed_ = parse_ontology(base_.ontology);
        if (!base_parsed_.ok()) {
            fail("the base ontology is not valid (" + first_error(base_parsed_.diagnostics) + ")");
            return false;
        }
        candidate_parsed_ = parse_ontology(candidate_.ontology);
        if (!candidate_parsed_.ok()) {
            fail("the candidate ontology is not valid; validate the draft first (" +
                 first_error(candidate_parsed_.diagnostics) + ")");
            return false;
        }
        auto loaded = detail::Theory::load(candidate_.ontology);
        if (!loaded) {
            fail("the aligner could not load the candidate ontology: " + loaded.error().message);
            return false;
        }
        theory_ = std::move(loaded).value();
        std::vector<std::string> axioms;
        for (const auto& a : candidate_parsed_.source.axioms) axioms.push_back(a.formula);
        if (auto st = theory_->set_axioms(axioms); !st) {
            fail("the candidate axioms could not be read: " + st.error().message);
            return false;
        }
        std::string reason;
        const Verdict3 consistent = theory_->satisfiable({}, config_.timeout_ms, &reason);
        if (consistent == Verdict3::False) {
            fail("the candidate axioms are inconsistent, so every entailment would hold vacuously; fix the "
                 "candidate before checking refinement");
            return false;
        }
        if (consistent == Verdict3::Unknown) {
            consistency_undecided_ = true;
            report_.assumptions.push_back("Z3 could not decide whether the candidate axioms are consistent (" + reason +
                                          "); a valid verdict would be vacuous if they were not.");
        } else {
            report_.assumptions.push_back("The candidate axioms were checked to be consistent (satisfiable).");
        }
        return true;
    }

    void structural(const std::string& subject, const std::string& statement, bool holds, const std::string& note) {
        if (holds) return;  // only violations are listed for (a); the summary counts the rest
        report_.obligations.push_back({"a", subject, statement, ConditionStatus::Violated, {}, note});
    }

    void check_signature() {
        const OntologySource& b = base_parsed_.source;
        const OntologySource& c = candidate_parsed_.source;
        std::size_t checked = 0;
        for (const auto& s : b.sorts) {
            ++checked;
            structural("sort " + s.name, "S_candidate ⊇ S_base", c.find_sort(s.name) != nullptr,
                       "sort '" + s.name + "' is removed in the candidate");
        }
        for (const auto& f : b.functions) {
            ++checked;
            const FunctionDecl* nf = c.find_function(f.name);
            if (nf == nullptr) {
                structural("function " + f.name, "F_candidate ⊇ F_base", false,
                           "function '" + f.name + "' is removed in the candidate");
            } else {
                structural("function " + f.name, "shared symbols keep their sorts", signature(*nf) == signature(f),
                           "signature changed from '" + signature(f) + "' to '" + signature(*nf) + "'");
            }
        }
        for (const auto& r : b.relations) {
            ++checked;
            const RelationDecl* nr = c.find_relation(r.name);
            if (nr == nullptr) {
                structural("relation " + r.name, "R_candidate ⊇ R_base", false,
                           "relation '" + r.name + "' is removed in the candidate");
            } else {
                structural("relation " + r.name, "shared symbols keep their sorts", signature(*nr) == signature(r),
                           "signature changed from '" + signature(r) + "' to '" + signature(*nr) + "'");
            }
        }
        ConditionResult& a = condition("a");
        a.status = ConditionStatus::Holds;
        const std::size_t added = c.sorts.size() + c.functions.size() + c.relations.size() -
                                  std::min(c.sorts.size() + c.functions.size() + c.relations.size(), checked);
        a.details.push_back(std::to_string(checked) + " base symbols checked; candidate declares " +
                            std::to_string(c.sorts.size() + c.functions.size() + c.relations.size()) + " (" +
                            std::to_string(added) + " more).");
        fold(a, report_.obligations);
    }

    void check_axioms() {
        ConditionResult& b = condition("b");
        b.status = ConditionStatus::Holds;
        for (const auto& ax : base_parsed_.source.axioms) {
            Obligation o{"b", "axiom " + ax.id, "Δ_candidate ⊨ " + ax.formula, ConditionStatus::Unknown, {}, {}};
            auto parsed = theory_->parse_formula(ax.formula);
            if (!parsed) {
                o.status = ConditionStatus::Violated;
                o.note = "the base axiom is not expressible over the candidate signature (" + parsed.error().message +
                         ")";
            } else {
                const auto e = theory_->entails(parsed.value(), config_.timeout_ms);
                o.status = from_verdict(e.verdict);
                if (e.counter_model) o.counter_model = *e.counter_model;
                if (e.verdict == Verdict3::False) {
                    o.note = "a model of the candidate axioms violates this base axiom (counter-model shown)";
                } else if (e.verdict == Verdict3::Unknown) {
                    o.note = "Z3 could not decide: " + e.reason;
                }
            }
            report_.obligations.push_back(std::move(o));
        }
        b.details.push_back(std::to_string(base_parsed_.source.axioms.size()) + " base axioms checked against " +
                            std::to_string(candidate_parsed_.source.axioms.size()) + " candidate axioms.");
        fold(b, report_.obligations);
    }

    /// @return false if the check had to be aborted (CheckFailed).
    bool check_interpretation(const std::string& id, const std::string& name, const std::optional<std::string>& base,
                              const std::optional<std::string>& candidate) {
        ConditionResult& cond = condition(id);
        if (!base) {
            cond.status = ConditionStatus::NotEvaluated;
            cond.details.push_back(candidate ? "no base " + name + " was supplied, so (c) cannot be evaluated for it"
                                             : name + " not part of this check (ontology-only refinement K ⊑ K)");
            return true;
        }
        const std::string& candidate_text = candidate ? *candidate : *base;
        if (!candidate) {
            cond.details.push_back("candidate keeps the base " + name + " text unchanged; it is re-checked under the "
                                   "candidate axioms");
        }
        const ParsedInterpretation old_i = parse_interpretation(*base, &base_parsed_.source);
        if (!old_i.ok()) {
            fail("the base " + name + " is not valid (" + first_error(old_i.diagnostics) + ")");
            return false;
        }
        const ParsedInterpretation new_i = parse_interpretation(candidate_text, &candidate_parsed_.source);
        if (!new_i.ok()) {
            // Symbols removed from the signature make an *unchanged* interpretation invalid:
            // that is a refinement violation (a, c), not a checker failure.
            if (!candidate) {
                cond.status = ConditionStatus::Violated;
                for (const auto& d : new_i.diagnostics) {
                    if (d.severity == Severity::Error) {
                        report_.obligations.push_back({id, name, "I_X is expressible over the candidate signature",
                                                       ConditionStatus::Violated, {}, d.message});
                    }
                }
                fold(cond, report_.obligations);
                return true;
            }
            fail("the candidate " + name + " is not valid; validate it first (" + first_error(new_i.diagnostics) + ")");
            return false;
        }
        cond.status = ConditionStatus::Holds;
        std::set<std::string> keys;
        for (const auto& e : old_i.source.entries) keys.insert(e.key);
        for (const auto& e : new_i.source.entries) keys.insert(e.key);
        for (const auto& key : keys) {
            const InterpretationEntry* o = old_i.source.find(key);
            const InterpretationEntry* n = new_i.source.find(key);
            const std::string subject = name + "(" + key + ")";
            if (o == nullptr || n == nullptr) {
                report_.obligations.push_back(
                    {id, subject, "I'(x) = τ ⇔ I(x) = τ", ConditionStatus::Violated, {},
                     o == nullptr ? "'" + key + "' is interpreted in the candidate but internal (τ) in the base"
                                  : "'" + key + "' is interpreted in the base but internal (τ) in the candidate"});
                continue;
            }
            Obligation ob{id, subject, "Δ_candidate ⊨ (" + n->formula + ") ↔ (" + o->formula + ")",
                          ConditionStatus::Unknown, {}, {}};
            auto old_f = theory_->parse_formula(o->formula);
            auto new_f = theory_->parse_formula(n->formula);
            if (!new_f) {
                fail("the candidate " + name + " entry '" + key + "' could not be read: " + new_f.error().message);
                return false;
            }
            if (!old_f) {
                ob.status = ConditionStatus::Violated;
                ob.note = "the base meaning is not expressible over the candidate signature (" +
                          old_f.error().message + ")";
            } else {
                const z3::expr& l = new_f.value();
                const z3::expr& r = old_f.value();
                const auto e = theory_->entails(z3::implies(l, r) && z3::implies(r, l), config_.timeout_ms);
                ob.status = from_verdict(e.verdict);
                if (e.counter_model) ob.counter_model = *e.counter_model;
                if (e.verdict == Verdict3::False) {
                    ob.note = "the meaning of '" + key + "' changed: a model of the candidate axioms distinguishes "
                              "the two formulas (counter-model shown)";
                } else if (e.verdict == Verdict3::Unknown) {
                    ob.note = "Z3 could not decide: " + e.reason;
                }
            }
            report_.obligations.push_back(std::move(ob));
        }
        cond.details.push_back(std::to_string(keys.size()) + " labels checked.");
        fold(cond, report_.obligations);
        return true;
    }

    void conclude() {
        bool violated = false;
        bool unknown = false;
        for (const auto& c : report_.conditions) {
            violated = violated || c.status == ConditionStatus::Violated;
            unknown = unknown || c.status == ConditionStatus::Unknown;
        }
        std::size_t holds = 0;
        std::size_t failed = 0;
        std::size_t open = 0;
        for (const auto& o : report_.obligations) {
            if (o.status == ConditionStatus::Holds) ++holds;
            else if (o.status == ConditionStatus::Violated) ++failed;
            else if (o.status == ConditionStatus::Unknown) ++open;
        }
        if (violated) {
            report_.verdict = RefinementVerdict::NotARefinement;
            std::string which;
            for (const auto& c : report_.conditions) {
                if (c.status == ConditionStatus::Violated) which += (which.empty() ? "" : ", ") + c.condition;
            }
            report_.summary = "The candidate is not a refinement of the base: condition(s) " + which + " of "
                              "Definition 4 are violated (" + std::to_string(failed) + " failing obligation(s)).";
        } else if (unknown || consistency_undecided_) {
            report_.verdict = RefinementVerdict::Unknown;
            report_.summary = "Inconclusive: no obligation is violated, but " + std::to_string(open) +
                              " obligation(s) could not be decided" +
                              (consistency_undecided_ ? " and the consistency of the candidate is undecided." : ".");
        } else {
            report_.verdict = RefinementVerdict::ValidRefinement;
            const bool full = condition("c.P").status == ConditionStatus::Holds &&
                              condition("c.D").status == ConditionStatus::Holds;
            report_.summary = "The candidate is a valid refinement of the base (" + std::to_string(holds) +
                              " entailment obligation(s) discharged by Z3)" +
                              (full ? ", including both interpretations (Φ′ ⊑ Φ)."
                                    : "; condition (c) was not evaluated for every interpretation, so this establishes "
                                      "K′ ⊑ K only for the interpretations supplied.");
        }
    }

    const DomainKnowledgeText& base_;
    const DomainKnowledgeText& candidate_;
    SolverConfig config_;
    RefinementReport report_;
    ParsedOntology base_parsed_;
    ParsedOntology candidate_parsed_;
    std::unique_ptr<detail::Theory> theory_;
    bool consistency_undecided_{false};
};

}  // namespace

RefinementReport check_refinement(const DomainKnowledgeText& base, const DomainKnowledgeText& candidate,
                                  const SolverConfig& config) {
    try {
        return Checker(base, candidate, config).run();
    } catch (const std::exception& e) {
        RefinementReport r;
        r.verdict = RefinementVerdict::CheckFailed;
        r.checker = checker_identity();
        r.failure_reasons.push_back(std::string("internal error: ") + e.what());
        r.summary = "The refinement check failed with an internal error; no conclusion about refinement was reached.";
        return r;
    }
}

PreservationAssessment assess_preservation(const AlignmentFacts& alignment, const RefinementReport& refinement) {
    PreservationAssessment a;
    if (!alignment.aligned) a.reasons.push_back("the recorded alignment result is not ALIGNED");
    if (refinement.verdict != RefinementVerdict::ValidRefinement) {
        a.reasons.push_back(std::string("the refinement check is not a valid refinement (verdict: ") +
                            to_string(refinement.verdict) + ")");
    }
    if (refinement.base_ontology_sha256 != alignment.ontology_sha256) {
        a.reasons.push_back("the refinement check does not start from the ontology the alignment was decided under");
    }
    if (refinement.base_pt_sha256.empty() || refinement.base_dt_sha256.empty()) {
        a.reasons.push_back("the refinement check did not include both interpretations, so condition (c) of "
                            "Definition 4 is not established for Φ");
    } else {
        if (refinement.base_pt_sha256 != alignment.pt_interpretation_sha256) {
            a.reasons.push_back("the refinement check's base PT interpretation differs from the one aligned");
        }
        if (refinement.base_dt_sha256 != alignment.dt_interpretation_sha256) {
            a.reasons.push_back("the refinement check's base DT interpretation differs from the one aligned");
        }
    }
    if (a.reasons.empty()) {
        a.verdict = PreservationVerdict::PreservedByRefinement;
        a.justification = "Theorem 3: Φ′ ⊑ Φ (refinement evidence, Definition 4 conditions a, b, c.P, c.D hold) and "
                          "V_P ∼Φ V_D (alignment evidence) imply V_P ∼Φ′ V_D for the candidate domain knowledge Φ′.";
    }
    return a;
}

}  // namespace twin::ontology
