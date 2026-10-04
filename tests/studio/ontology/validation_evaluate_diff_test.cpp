// Tests for ontology/interpretation validation, 3-valued evaluation and structural diff.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "twin/ontology/diff.hpp"
#include "twin/ontology/evaluate.hpp"
#include "twin/ontology/validation.hpp"

namespace fs = std::filesystem;
using namespace twin::ontology;

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has_code(const std::vector<SourceDiagnostic>& diags, const std::string& code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

const std::string kOnt =
    "; process pump\n"
    "sort Temperature\n"
    "fun bearing_temp : Temperature\n"
    "fun temp_limit : Temperature\n"
    "fun vibration : Real\n"
    "fun vib_limit : Real\n"
    "rel maintenance_due :\n"
    "axiom limit_val : (= temp_limit 90)\n"
    "axiom vib_val : (= vib_limit 7.1)\n"
    "axiom nn_temp : (>= bearing_temp 0)\n";

const std::string kInterp =
    "NORMAL : (and (<= bearing_temp temp_limit) (<= vibration vib_limit))\n"
    "DEGRADED : (or (> bearing_temp temp_limit) (> vibration vib_limit))\n"
    "high_temperature! : (> bearing_temp temp_limit)\n";

const EntryEvaluation& entry(const EvaluationReport& r, const std::string& key) {
    for (const auto& e : r.entries) {
        if (e.key == key) return e;
    }
    throw std::runtime_error("missing " + key);
}

}  // namespace

TEST(Validation, AlignerCorpusOntologyIsValidAndConsistent) {
    const auto text = read_file(fs::path(TWIN_SOURCE_DIR) / "SemPTDTAlignmentICSE/assets/CS6_Pump/domain.ont");
    const auto v = validate_ontology(text);
    EXPECT_TRUE(v.valid);
    EXPECT_TRUE(v.consistency_checked);
    EXPECT_EQ(v.consistent, Verdict3::True);
    EXPECT_EQ(v.content_sha256.size(), 64U);
    EXPECT_FALSE(v.structure.axioms.empty());
}

TEST(Validation, InconsistentAxiomsAreAnError) {
    const auto v = validate_ontology(kOnt + "axiom bad : (< temp_limit 0)\n");
    EXPECT_FALSE(v.valid);
    EXPECT_EQ(v.consistent, Verdict3::False);
    EXPECT_TRUE(has_code(v.diagnostics, "ONT110"));
}

TEST(Validation, StructuralErrorsStopBeforeTheSolver) {
    const auto v = validate_ontology(kOnt + "axiom bad : (> nope 0)\n");
    EXPECT_FALSE(v.valid);
    EXPECT_TRUE(has_code(v.diagnostics, "ONT007"));
    EXPECT_FALSE(v.consistency_checked);
}

TEST(Validation, InterpretationFindingsUnsatisfiableAndTautological) {
    const auto v = validate_interpretation(kOnt, kInterp +
                                                     "IMPOSSIBLE : (< bearing_temp 0)\n"
                                                     "always! : (>= bearing_temp 0)\n");
    EXPECT_TRUE(v.valid) << "findings are warnings";
    EXPECT_TRUE(has_code(v.diagnostics, "INT110"));
    EXPECT_TRUE(has_code(v.diagnostics, "INT111"));
}

TEST(Validation, InterpretationWithUndeclaredSymbolIsInvalid) {
    const auto v = validate_interpretation(kOnt, "NORMAL : (< rpm 100)\n");
    EXPECT_FALSE(v.valid);
    EXPECT_TRUE(has_code(v.diagnostics, "INT004"));
}

TEST(Evaluate, ThreeValuedTruthFromObservations) {
    const std::vector<Observation> hot{{"bearing_temp", "94.1"}};
    const auto r = evaluate_interpretation(kOnt, kInterp, hot);
    ASSERT_TRUE(r.ok()) << r.error().to_string();
    EXPECT_EQ(r.value().observations_consistent, Verdict3::True);
    EXPECT_EQ(entry(r.value(), "high_temperature!").truth, Truth::True);
    EXPECT_EQ(entry(r.value(), "DEGRADED").truth, Truth::True);  // one disjunct suffices
    EXPECT_EQ(entry(r.value(), "NORMAL").truth, Truth::False);

    const std::vector<Observation> cool{{"bearing_temp", "60"}};
    const auto c = evaluate_interpretation(kOnt, kInterp, cool);
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(entry(c.value(), "high_temperature!").truth, Truth::False);
    // Vibration unobserved: NORMAL cannot be decided, and the report says why.
    const auto& normal = entry(c.value(), "NORMAL");
    EXPECT_EQ(normal.truth, Truth::Unknown);
    EXPECT_EQ(normal.unobserved, (std::vector<std::string>{"temp_limit", "vib_limit", "vibration"}));

    const auto none = evaluate_interpretation(kOnt, kInterp, {});
    ASSERT_TRUE(none.ok());
    EXPECT_EQ(entry(none.value(), "high_temperature!").truth, Truth::Unknown);
}

TEST(Evaluate, ContradictoryObservationsAreReportedNotGuessed) {
    const std::vector<Observation> obs{{"bearing_temp", "-5"}};  // violates nn_temp
    const auto r = evaluate_interpretation(kOnt, kInterp, obs);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().observations_consistent, Verdict3::False);
    for (const auto& e : r.value().entries) EXPECT_EQ(e.truth, Truth::InconsistentObservation);
}

TEST(Evaluate, RejectsInexactValuesAndUnknownSymbols) {
    const std::vector<Observation> sci{{"bearing_temp", "9.4e1"}};
    EXPECT_FALSE(evaluate_interpretation(kOnt, kInterp, sci).ok());
    const std::vector<Observation> unknown{{"rpm", "10"}};
    EXPECT_FALSE(evaluate_interpretation(kOnt, kInterp, unknown).ok());
    const std::vector<Observation> rel{{"maintenance_due", "true"}};
    EXPECT_TRUE(evaluate_interpretation(kOnt, kInterp, rel).ok());
    EXPECT_FALSE(evaluate_interpretation(kOnt, kInterp, {}, {"NOPE"}).ok());
}

TEST(Diff, ReportsElementChangesAndAffectedInterpretations) {
    const auto from = parse_ontology(kOnt).source;
    const std::string to_text =
        "sort Temperature\n"
        "sort Speed\n"                                  // added sort
        "fun bearing_temp : Temperature\n"
        "fun temp_limit : Temperature\n"
        "fun vibration : Real\n"
        "fun vib_limit : Real\n"
        "fun rpm : Speed\n"                             // added function
        "axiom limit_val : (=   temp_limit   85)\n"     // modified
        "axiom vib_limit_value : (= vib_limit 7.1)\n"  // renamed
        "axiom nn_temp : (>= bearing_temp 0)  ; whitespace/comment only\n";
    const auto to = parse_ontology(to_text).source;
    const auto d = diff_ontologies(from, to);

    auto find = [&](const std::string& element, const std::string& name) -> const StructuralChange* {
        for (const auto& c : d.changes) {
            if (c.element == element && c.name == name) return &c;
        }
        return nullptr;
    };
    ASSERT_NE(find("sort", "Speed"), nullptr);
    EXPECT_EQ(find("sort", "Speed")->kind, ChangeKind::Added);
    ASSERT_NE(find("function", "rpm"), nullptr);
    ASSERT_NE(find("relation", "maintenance_due"), nullptr);
    EXPECT_EQ(find("relation", "maintenance_due")->kind, ChangeKind::Removed);
    ASSERT_NE(find("axiom", "limit_val"), nullptr);
    EXPECT_EQ(find("axiom", "limit_val")->kind, ChangeKind::Modified);
    ASSERT_NE(find("axiom", "vib_limit_value"), nullptr);
    EXPECT_EQ(find("axiom", "vib_limit_value")->kind, ChangeKind::Renamed);
    EXPECT_EQ(find("axiom", "vib_limit_value")->previous_name, "vib_val");
    EXPECT_EQ(find("axiom", "nn_temp"), nullptr) << "whitespace and comments are not changes";

    EXPECT_TRUE(d.affected_symbols.count("temp_limit"));
    EXPECT_FALSE(d.affected_symbols.count("vib_limit")) << "a rename does not change meaning";
    const auto interp = parse_interpretation(kInterp).source;
    EXPECT_EQ(entries_using(interp, d.affected_symbols),
              (std::vector<std::string>{"NORMAL", "DEGRADED", "high_temperature!"}));
}

TEST(Diff, InterpretationEntries) {
    const auto a = parse_interpretation(kInterp).source;
    const auto b = parse_interpretation(
                       "NORMAL : (and (<= bearing_temp temp_limit) (<= vibration vib_limit))\n"
                       "DEGRADED : (> bearing_temp temp_limit)\n"
                       "cooling! : (<= bearing_temp temp_limit)\n")
                       .source;
    const auto d = diff_interpretations(a, b);
    ASSERT_EQ(d.changes.size(), 3U);
    EXPECT_EQ(d.changes[0].element, "location");
    EXPECT_EQ(d.changes[0].kind, ChangeKind::Modified);
    EXPECT_EQ(d.changes[1].kind, ChangeKind::Removed);  // high_temperature!
    EXPECT_EQ(d.changes[2].kind, ChangeKind::Added);    // cooling!
}
