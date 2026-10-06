/**
 * @file validate_test.cpp
 * @brief Structural validation of canonical models: one test per TWM code.
 *
 * VALID (no error) is structural correctness only; it never implies any
 * formal property of the model.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "authoring_fixtures.hpp"
#include "twin/authoring/validate.hpp"

namespace twin::authoring {
namespace {

using ir::Comparison;

/// The diagnostics with @p code.
std::vector<Diagnostic> with_code(const std::vector<Diagnostic>& ds, const std::string& code) {
    std::vector<Diagnostic> out;
    std::copy_if(ds.begin(), ds.end(), std::back_inserter(out), [&](const Diagnostic& d) { return d.code == code; });
    return out;
}

/// Exactly one diagnostic with @p code, of @p severity, about @p kind/@p name.
void expect_one(const Model& m, const std::string& code, const std::string& severity, const std::string& kind,
                const std::string& name) {
    const std::vector<Diagnostic> ds = validate(m);
    const std::vector<Diagnostic> hits = with_code(ds, code);
    ASSERT_EQ(hits.size(), 1u) << code << " expected once; got " << to_json(ds).dump();
    EXPECT_EQ(hits[0].severity, severity);
    EXPECT_EQ(hits[0].element.kind, kind);
    EXPECT_EQ(hits[0].element.name, name);
    EXPECT_FALSE(hits[0].message.empty());
}

TEST(Validate, ValidModelHasNoDiagnostics) {
    const std::vector<Diagnostic> ds = validate(test::pump_like_model());
    EXPECT_TRUE(ds.empty()) << to_json(ds).dump();
    EXPECT_FALSE(has_errors(ds));
}

TEST(Validate, TWM001_InvalidIdentifierOrKeyword) {
    Model m = test::pump_like_model();
    m.locations.push_back(LocationDecl{"int", false, {}, ""});
    m.edges.push_back(EdgeDecl{"e9", "STOPPED", "int", std::nullopt, {}, {}, ""});
    expect_one(m, "TWM001", "error", "location", "int");

    Model bad_name = test::pump_like_model();
    bad_name.name = "9lives";
    expect_one(bad_name, "TWM001", "error", "model", "9lives");
}

TEST(Validate, QueryLanguageKeywordsAreOrdinaryNames) {
    // A, E, M and Pr are keywords of UPPAAL's property language only, not of models.
    for (const char* name : {"A", "E", "M", "Pr", "control", "inf", "sup"}) {
        EXPECT_TRUE(is_identifier(name)) << name;
    }
    EXPECT_FALSE(is_identifier("clock"));
    EXPECT_FALSE(is_identifier("urgent"));
}

TEST(Validate, TWM002_SharedNamespaceClash) {
    Model m = test::pump_like_model();
    m.locations.push_back(LocationDecl{"t", false, {}, ""});
    m.edges.push_back(EdgeDecl{"e9", "STOPPED", "t", std::nullopt, {}, {}, ""});
    expect_one(m, "TWM002", "error", "location", "t");
}

TEST(Validate, TWM003_NoInitialLocation) {
    Model m = test::pump_like_model();
    m.locations[0].initial = false;
    expect_one(m, "TWM003", "error", "model", "PumpLike");
}

TEST(Validate, TWM004_SeveralInitialLocations) {
    Model m = test::pump_like_model();
    m.locations[1].initial = true;
    expect_one(m, "TWM004", "error", "location", "DEGRADED");
}

TEST(Validate, TWM005_UnknownEdgeEndpoint) {
    Model m = test::pump_like_model();
    m.edges[0].target = "NOWHERE";
    expect_one(m, "TWM005", "error", "edge", "e1");
}

TEST(Validate, TWM006_UndefinedClock) {
    Model m = test::pump_like_model();
    m.edges[2].guard.push_back(test::atom("zz", Comparison::Less, 3));
    expect_one(m, "TWM006", "error", "edge", "e3");
    Model r = test::pump_like_model();
    r.edges[0].resets.push_back("zz");
    expect_one(r, "TWM006", "error", "edge", "e1");
}

TEST(Validate, TWM007_UndefinedConstant) {
    Model m = test::pump_like_model();
    m.locations[1].invariant[0].bound = Bound{std::string("NOPE")};
    expect_one(m, "TWM007", "error", "location", "DEGRADED");
}

TEST(Validate, TWM008_UndefinedChannel) {
    Model m = test::pump_like_model();
    m.edges[0].sync = Sync{"nochan", '!'};
    expect_one(m, "TWM008", "error", "edge", "e1");
}

TEST(Validate, TWM009_DiagonalInGuard) {
    Model m = test::pump_like_model();
    m.edges[2].guard.push_back(Atom{"t", std::string("u"), Comparison::Less, Bound{std::int64_t{3}}});
    expect_one(m, "TWM009", "error", "edge", "e3");
}

TEST(Validate, TWM010_BoundOutOfRange) {
    Model m = test::pump_like_model();
    m.locations[1].invariant[0].bound = Bound{std::int64_t{2000000000}};
    expect_one(m, "TWM010", "error", "location", "DEGRADED");
    Model c = test::pump_like_model();
    c.constants[0].value = -2000000000;
    expect_one(c, "TWM010", "error", "edge", "e2");
}

TEST(Validate, TWM011_DuplicateOrInvalidEdgeId) {
    Model m = test::pump_like_model();
    m.edges[3].id = "e1";
    expect_one(m, "TWM011", "error", "edge", "e1");
    Model b = test::pump_like_model();
    b.edges[3].id = "e 4";
    expect_one(b, "TWM011", "error", "edge", "e 4");
}

TEST(Validate, TWM012_NoLocations) {
    Model m;
    m.name = "Empty";
    const std::vector<Diagnostic> ds = validate(m);
    EXPECT_EQ(with_code(ds, "TWM012").size(), 1u) << to_json(ds).dump();
}

TEST(Validate, TWM013_ClockResetTwice) {
    Model m = test::pump_like_model();
    m.edges[0].resets.push_back("t");
    expect_one(m, "TWM013", "warning", "edge", "e1");
}

TEST(Validate, TWM020_UnusedClock) {
    Model m = test::pump_like_model();
    m.clocks.push_back(ClockDecl{"w", ""});
    expect_one(m, "TWM020", "warning", "clock", "w");
}

TEST(Validate, TWM021_UnusedChannel) {
    Model m = test::pump_like_model();
    m.channels.push_back(ChannelDecl{"spare", ""});
    expect_one(m, "TWM021", "warning", "channel", "spare");
}

TEST(Validate, TWM022_UnusedConstant) {
    Model m = test::pump_like_model();
    m.constants.push_back(ConstantDecl{"SPARE", 3, ""});
    expect_one(m, "TWM022", "warning", "constant", "SPARE");
}

TEST(Validate, TWM023_UnreachableLocation) {
    Model m = test::pump_like_model();
    m.locations.push_back(LocationDecl{"ISOLATED", false, {}, ""});
    m.edges.push_back(EdgeDecl{"e9", "ISOLATED", "STOPPED", std::nullopt, {}, {}, ""});
    expect_one(m, "TWM023", "warning", "location", "ISOLATED");
}

TEST(Validate, TWM024_ReceiveLabel) {
    Model m = test::pump_like_model();
    m.edges[0].sync = Sync{"restart", '?'};
    expect_one(m, "TWM024", "warning", "edge", "e1");
}

TEST(Validate, ErrorsMakeTheModelInvalidWarningsDoNot) {
    Model w = test::pump_like_model();
    w.clocks.push_back(ClockDecl{"w", ""});
    EXPECT_FALSE(has_errors(validate(w)));
    Model e = test::pump_like_model();
    e.edges[0].target = "NOWHERE";
    EXPECT_TRUE(has_errors(validate(e)));
}

TEST(Validate, DiagnosticsSerialiseWithElementAndPart) {
    Model m = test::pump_like_model();
    m.edges[1].guard[0].clock = "zz";
    const json::Json j = to_json(validate(m));
    ASSERT_TRUE(j.is_array());
    ASSERT_FALSE(j.empty());
    const json::Json& d = j.at(0);
    EXPECT_EQ(d.at("code"), "TWM006");
    EXPECT_EQ(d.at("severity"), "error");
    EXPECT_EQ(d.at("element").at("kind"), "edge");
    EXPECT_EQ(d.at("element").at("name"), "e2");
    EXPECT_EQ(d.at("element").at("part"), "guard");
}

}  // namespace
}  // namespace twin::authoring
