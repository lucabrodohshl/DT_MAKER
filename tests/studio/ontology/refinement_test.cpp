// Tests for the Def. 4 refinement checker and Theorem 3 preservation (twin/ontology/refinement.hpp).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "twin/ontology/refinement.hpp"

namespace fs = std::filesystem;
using namespace twin::ontology;

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path asset(const std::string& cs) { return fs::path(TWIN_SOURCE_DIR) / "SemPTDTAlignmentICSE" / "assets" / cs; }

DomainKnowledgeText v1(const std::string& cs) {
    const auto d = asset(cs);
    return {read_file(d / "domain.ont"), read_file(d / "pt.interp"), read_file(d / "dt.interp")};
}
DomainKnowledgeText v2(const std::string& cs) {
    const auto d = asset(cs);
    return {read_file(d / "domain_v2.ont"), read_file(d / "pt_v2.interp"), read_file(d / "dt_v2.interp")};
}

const ConditionResult& cond(const RefinementReport& r, const std::string& id) {
    for (const auto& c : r.conditions) {
        if (c.condition == id) return c;
    }
    throw std::runtime_error("no condition " + id);
}

const Obligation* failing(const RefinementReport& r, const std::string& subject) {
    for (const auto& o : r.obligations) {
        if (o.subject == subject && o.status == ConditionStatus::Violated) return &o;
    }
    return nullptr;
}

std::string dump(const RefinementReport& r) {
    std::string s = std::string(to_string(r.verdict)) + ": " + r.summary + "\n";
    for (const auto& o : r.obligations) {
        if (o.status != ConditionStatus::Holds) s += "  [" + o.condition + "] " + o.subject + " " + o.note + "\n";
    }
    for (const auto& f : r.failure_reasons) s += "  FAIL " + f + "\n";
    return s;
}

const std::string kBase =
    "sort Temperature\n"
    "fun bearing_temp : Temperature\n"
    "fun temp_limit : Temperature\n"
    "axiom limit_bound : (<= temp_limit 95)\n"
    "axiom nn_temp : (>= bearing_temp 0)\n";
const std::string kBaseDt =
    "NORMAL : (<= bearing_temp temp_limit)\n"
    "HOT : (> bearing_temp temp_limit)\n"
    "overheat! : (> bearing_temp temp_limit)\n";

}  // namespace

// --- Synthetic cases: one per clause of Definition 4 -------------------------

TEST(Refinement, AddingSymbolsAndTighteningAnInequalityIsAValidRefinement) {
    const std::string candidate = kBase +
                                  "sort Vibration\n"
                                  "fun vibration : Vibration\n"
                                  "axiom limit_val : (= temp_limit 90)\n"
                                  "axiom nn_vib : (>= vibration 0)\n";
    const auto r = check_refinement({kBase, {}, kBaseDt}, {candidate, {}, kBaseDt});
    EXPECT_EQ(r.verdict, RefinementVerdict::ValidRefinement) << dump(r);
    EXPECT_EQ(cond(r, "a").status, ConditionStatus::Holds);
    EXPECT_EQ(cond(r, "b").status, ConditionStatus::Holds);
    EXPECT_EQ(cond(r, "c.D").status, ConditionStatus::Holds);
    EXPECT_EQ(cond(r, "c.P").status, ConditionStatus::NotEvaluated);
    EXPECT_FALSE(r.checker.empty());
    EXPECT_EQ(r.base_ontology_sha256.size(), 64U);
}

TEST(Refinement, RelaxingAnAxiomViolatesConditionBWithCounterModel) {
    std::string candidate = kBase;
    candidate.replace(candidate.find("95"), 2, "110");
    const auto r = check_refinement({kBase, {}, {}}, {candidate, {}, {}});
    ASSERT_EQ(r.verdict, RefinementVerdict::NotARefinement) << dump(r);
    EXPECT_EQ(cond(r, "b").status, ConditionStatus::Violated);
    const Obligation* o = failing(r, "axiom limit_bound");
    ASSERT_NE(o, nullptr);
    ASSERT_FALSE(o->counter_model.empty());
    bool has_limit = false;
    for (const auto& [sym, val] : o->counter_model) has_limit = has_limit || sym == "temp_limit";
    EXPECT_TRUE(has_limit);
}

TEST(Refinement, RemovedSymbolIsConditionANotACheckFailure) {
    const std::string candidate =
        "sort Temperature\n"
        "fun temp_limit : Temperature\n"
        "axiom limit_bound : (<= temp_limit 95)\n";
    const auto r = check_refinement({kBase, {}, kBaseDt}, {candidate, {}, {}});
    ASSERT_EQ(r.verdict, RefinementVerdict::NotARefinement) << dump(r);
    EXPECT_EQ(cond(r, "a").status, ConditionStatus::Violated);
    EXPECT_NE(failing(r, "function bearing_temp"), nullptr);
    // The base axiom nn_temp is no longer expressible: (b) fails as well, still not CheckFailed.
    EXPECT_EQ(cond(r, "b").status, ConditionStatus::Violated);
    EXPECT_EQ(cond(r, "c.D").status, ConditionStatus::Violated);
}

TEST(Refinement, ChangedSignatureViolatesConditionA) {
    std::string candidate = kBase;
    candidate.replace(candidate.find("fun bearing_temp : Temperature"), 30, "fun bearing_temp : Real");
    const auto r = check_refinement({kBase, {}, {}}, {candidate, {}, {}});
    EXPECT_EQ(r.verdict, RefinementVerdict::NotARefinement) << dump(r);
    EXPECT_EQ(cond(r, "a").status, ConditionStatus::Violated);
}

TEST(Refinement, ChangedInterpretationMeaningViolatesConditionC) {
    const std::string new_dt =
        "NORMAL : (<= bearing_temp temp_limit)\n"
        "HOT : (> bearing_temp (+ temp_limit 5))\n"
        "overheat! : (> bearing_temp temp_limit)\n";
    const auto r = check_refinement({kBase, {}, kBaseDt}, {kBase, {}, new_dt});
    ASSERT_EQ(r.verdict, RefinementVerdict::NotARefinement) << dump(r);
    EXPECT_EQ(cond(r, "c.D").status, ConditionStatus::Violated);
    const Obligation* o = failing(r, "I_D(HOT)");
    ASSERT_NE(o, nullptr);
    EXPECT_FALSE(o->counter_model.empty());
}

TEST(Refinement, EquivalentInterpretationUnderNewAxiomsIsPreserved) {
    // The paper's example: once Δ' fixes the limit, a literal and the symbolic form are equivalent.
    const std::string candidate = kBase + "axiom limit_val : (= temp_limit 90)\n";
    const std::string new_dt =
        "NORMAL : (<= bearing_temp 90)\n"
        "HOT : (> bearing_temp 90)\n"
        "overheat! : (> bearing_temp temp_limit)\n";
    const auto r = check_refinement({kBase, {}, kBaseDt}, {candidate, {}, new_dt});
    EXPECT_EQ(r.verdict, RefinementVerdict::ValidRefinement) << dump(r);
}

TEST(Refinement, TauStatusChangeViolatesConditionC) {
    const std::string new_dt = kBaseDt + "cooldown! : (<= bearing_temp temp_limit)\n";
    const auto r = check_refinement({kBase, {}, kBaseDt}, {kBase, {}, new_dt});
    ASSERT_EQ(r.verdict, RefinementVerdict::NotARefinement) << dump(r);
    const Obligation* o = failing(r, "I_D(cooldown!)");
    ASSERT_NE(o, nullptr);
    EXPECT_NE(o->note.find("internal"), std::string::npos);
}

TEST(Refinement, UndecidedObligationIsUnknownNeverNotARefinement) {
    // Nonlinear integer arithmetic (cubes): undecidable in general; a 1 ms budget forces 'unknown'.
    const std::string base =
        "fun x : Int\nfun y : Int\nfun z : Int\n"
        "axiom no_cubes : (not (and (> x 0) (> y 0) (> z 0) (= (+ (* x x x) (* y y y)) (* z z z))))\n";
    const std::string candidate = "fun x : Int\nfun y : Int\nfun z : Int\naxiom pos : (> x 0)\n";
    const auto r = check_refinement({base, {}, {}}, {candidate, {}, {}}, SolverConfig{1});
    EXPECT_NE(r.verdict, RefinementVerdict::ValidRefinement) << dump(r);
    // Z3 may refute it (it is not entailed); what it must never do is call an undecided query a violation.
    for (const auto& o : r.obligations) {
        if (o.status == ConditionStatus::Unknown) {
            EXPECT_EQ(r.verdict, RefinementVerdict::Unknown) << dump(r);
        }
    }
    if (r.verdict == RefinementVerdict::NotARefinement) {
        EXPECT_FALSE(failing(r, "axiom no_cubes")->counter_model.empty()) << "a violation must carry a witness";
    }
}

TEST(Refinement, InvalidBaseOrCandidateIsCheckFailedNotAVerdict) {
    const auto r1 = check_refinement({"sort A\nbogus line\n", {}, {}}, {kBase, {}, {}});
    EXPECT_EQ(r1.verdict, RefinementVerdict::CheckFailed);
    EXPECT_FALSE(r1.failure_reasons.empty());
    const auto r2 = check_refinement({kBase, {}, {}}, {kBase + "axiom broken : (> temp_limit\n", {}, {}});
    EXPECT_EQ(r2.verdict, RefinementVerdict::CheckFailed);
}

TEST(Refinement, InconsistentCandidateIsCheckFailedNotVacuouslyValid) {
    const auto r = check_refinement({kBase, {}, {}}, {kBase + "axiom contra : (> temp_limit 100)\n", {}, {}});
    EXPECT_EQ(r.verdict, RefinementVerdict::CheckFailed) << dump(r);
    EXPECT_NE(r.summary.find("inconsistent"), std::string::npos);
}

TEST(Refinement, IsDeterministic) {
    const std::string candidate = kBase + "axiom limit_val : (= temp_limit 90)\n";
    const auto a = check_refinement({kBase, {}, kBaseDt}, {candidate, {}, {}});
    const auto b = check_refinement({kBase, {}, kBaseDt}, {candidate, {}, {}});
    EXPECT_EQ(a.summary, b.summary);
    ASSERT_EQ(a.obligations.size(), b.obligations.size());
    for (std::size_t i = 0; i < a.obligations.size(); ++i) EXPECT_EQ(a.obligations[i].subject, b.obligations[i].subject);
}

// --- The aligner corpus (v1 -> v2) ---------------------------------------------
// Recorded as findings in docs/studio/aligner-findings.md (S2): most shipped "v2"
// evolutions change constant values or the τ-status of events, so they are NOT
// Def. 4 refinements; the paper's benchmark re-runs alignment under v2 instead.

TEST(RefinementCorpus, ThreeTankAndGraspingAreValidRefinementsOfPhi) {
    for (const char* cs : {"CS1_ThreeTank", "CS5_Grasping"}) {
        const auto r = check_refinement(v1(cs), v2(cs));
        EXPECT_EQ(r.verdict, RefinementVerdict::ValidRefinement) << cs << "\n" << dump(r);
        EXPECT_EQ(cond(r, "c.P").status, ConditionStatus::Holds) << cs;
        EXPECT_EQ(cond(r, "c.D").status, ConditionStatus::Holds) << cs;
    }
}

TEST(RefinementCorpus, ChangedConstantsAreNotRefinements) {
    const auto pump = check_refinement(v1("CS6_Pump"), v2("CS6_Pump"));
    EXPECT_EQ(pump.verdict, RefinementVerdict::NotARefinement) << dump(pump);
    EXPECT_NE(failing(pump, "axiom tolerance_val"), nullptr);  // (= dose_tolerance 5) vs (= dose_tolerance 3)

    const auto drone = check_refinement(v1("UseCase_Drone"), v2("UseCase_Drone"));
    EXPECT_EQ(drone.verdict, RefinementVerdict::NotARefinement) << dump(drone);
    EXPECT_NE(failing(drone, "axiom bat_cap"), nullptr);  // battery capacity 500 -> 450
}

TEST(RefinementCorpus, NewlyInterpretedEventsViolateTauClause) {
    const auto crane = check_refinement(v1("CS2_Crane"), v2("CS2_Crane"));
    EXPECT_EQ(crane.verdict, RefinementVerdict::NotARefinement) << dump(crane);
    EXPECT_EQ(cond(crane, "b").status, ConditionStatus::Holds);
    EXPECT_NE(failing(crane, "I_P(reset_crane!)"), nullptr);
    // The ontology alone is a refinement (K' ⊑ K):
    const auto k_only = check_refinement({v1("CS2_Crane").ontology, {}, {}}, {v2("CS2_Crane").ontology, {}, {}});
    EXPECT_EQ(k_only.verdict, RefinementVerdict::ValidRefinement) << dump(k_only);
}

TEST(RefinementCorpus, RoverBaseInterpretationDefectIsCheckFailed) {
    const auto r = check_refinement(v1("CS3_Rover"), v2("CS3_Rover"));
    EXPECT_EQ(r.verdict, RefinementVerdict::CheckFailed) << dump(r);
}

// --- Theorem 3 -------------------------------------------------------------------

TEST(Preservation, AppliesOnlyWhenAllPremisesHoldForTheExactArtefacts) {
    const std::string candidate = kBase + "axiom limit_val : (= temp_limit 90)\n";
    const std::string pt = "RUNNING : (<= bearing_temp temp_limit)\nalarm! : (> bearing_temp temp_limit)\n";
    const auto r = check_refinement({kBase, pt, kBaseDt}, {candidate, {}, {}});
    ASSERT_EQ(r.verdict, RefinementVerdict::ValidRefinement) << dump(r);

    AlignmentFacts facts{true, r.base_ontology_sha256, r.base_pt_sha256, r.base_dt_sha256};
    const auto ok = assess_preservation(facts, r);
    EXPECT_EQ(ok.verdict, PreservationVerdict::PreservedByRefinement);
    EXPECT_TRUE(ok.reasons.empty());
    EXPECT_NE(ok.justification.find("Theorem 3"), std::string::npos);

    AlignmentFacts other_ontology = facts;
    other_ontology.ontology_sha256 = std::string(64, '0');
    EXPECT_EQ(assess_preservation(other_ontology, r).verdict, PreservationVerdict::RealignmentRequired);

    AlignmentFacts not_aligned = facts;
    not_aligned.aligned = false;
    EXPECT_EQ(assess_preservation(not_aligned, r).verdict, PreservationVerdict::RealignmentRequired);

    // A K-only check does not establish (c): no preservation.
    const auto k_only = check_refinement({kBase, {}, {}}, {candidate, {}, {}});
    EXPECT_EQ(assess_preservation(facts, k_only).verdict, PreservationVerdict::RealignmentRequired);

    // NotARefinement: no preservation.
    const auto bad = check_refinement({kBase, pt, kBaseDt}, {kBase + "axiom x : (> temp_limit 0)\n", {}, {}});
    ASSERT_EQ(bad.verdict, RefinementVerdict::ValidRefinement) << "adding an axiom is a refinement";
    std::string relaxed = kBase;
    relaxed.replace(relaxed.find("95"), 2, "99");
    const auto not_ref = check_refinement({kBase, pt, kBaseDt}, {relaxed, {}, {}});
    EXPECT_EQ(assess_preservation(facts, not_ref).verdict, PreservationVerdict::RealignmentRequired);
}
