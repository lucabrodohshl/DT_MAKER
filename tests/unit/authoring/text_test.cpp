/**
 * @file text_test.cpp
 * @brief TwinTA text format: parsing, canonical printing, comments as notes, errors, symbols.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "authoring_fixtures.hpp"
#include "twin/authoring/text.hpp"

namespace twin::authoring {
namespace {

using ir::Comparison;

constexpr const char* kSpecExample = R"(// DT view of pump P-101 (reliability vocabulary).
automaton ProcessPumpDT {
    clock t;                       // time in the current mode
    const COOL_MIN = 30;
    channel restart, cooling_complete, controlled_stop;

    initial location STOPPED;
    location NORMAL;
    location COOLING { invariant t <= 300; }

    edge e1: STOPPED -> NORMAL { sync restart!; reset t; }
    edge e6: COOLING -> NORMAL { guard t >= COOL_MIN; sync cooling_complete!; reset t; }
    edge e9: NORMAL -> STOPPING { sync controlled_stop!; reset t; }
}
)";

/// The diagnostics with @p code.
std::vector<Diagnostic> with_code(const std::vector<Diagnostic>& ds, const std::string& code) {
    std::vector<Diagnostic> out;
    std::copy_if(ds.begin(), ds.end(), std::back_inserter(out), [&](const Diagnostic& d) { return d.code == code; });
    return out;
}

TEST(TextParse, SpecExampleGivesTheExpectedModel) {
    const ParseResult r = parse_text(kSpecExample);
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    const Model& m = *r.model;
    EXPECT_EQ(m.name, "ProcessPumpDT");
    EXPECT_EQ(m.note, "DT view of pump P-101 (reliability vocabulary).");
    ASSERT_EQ(m.clocks.size(), 1u);
    EXPECT_EQ(m.clocks[0], (ClockDecl{"t", "time in the current mode"}));
    ASSERT_EQ(m.constants.size(), 1u);
    EXPECT_EQ(m.constants[0], (ConstantDecl{"COOL_MIN", 30, ""}));
    ASSERT_EQ(m.channels.size(), 3u);
    EXPECT_EQ(m.channels[2].name, "controlled_stop");
    ASSERT_EQ(m.locations.size(), 3u);
    EXPECT_TRUE(m.locations[0].initial);
    EXPECT_EQ(m.locations[2].invariant, (Constraint{test::atom("t", Comparison::LessEqual, 300)}));
    ASSERT_EQ(m.edges.size(), 3u);
    EXPECT_EQ(m.edges[1].id, "e6");
    EXPECT_EQ(m.edges[1].guard, (Constraint{test::atom("t", Comparison::GreaterEqual, std::string("COOL_MIN"))}));
    EXPECT_EQ(m.edges[1].sync, (Sync{"cooling_complete", '!'}));
    EXPECT_EQ(m.edges[1].resets, (std::vector<std::string>{"t"}));
    // Syntax is fine; the undeclared STOPPING is a structural error from validate().
    const auto missing = with_code(r.diagnostics, "TWM005");
    ASSERT_EQ(missing.size(), 1u);
    ASSERT_TRUE(missing[0].range.has_value());
    EXPECT_EQ(missing[0].range->line, 13u);
}

TEST(TextPrint, CanonicalTextIsAFixpoint) {
    const std::string canonical = print_text(test::pump_like_model());
    const ParseResult r = parse_text(canonical);
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    EXPECT_EQ(print_text(*r.model), canonical);
}

TEST(TextPrint, ParsePrintRoundTripPreservesTheModel) {
    const Model m = test::pump_like_model();
    const ParseResult r = parse_text(print_text(m));
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    EXPECT_EQ(*r.model, m);
}

TEST(TextPrint, CanonicalLayout) {
    Model m;
    m.name = "Small";
    m.clocks = {ClockDecl{"x", ""}, ClockDecl{"y", ""}, ClockDecl{"z", "spare\nclock"}};
    m.channels = {ChannelDecl{"go", ""}};
    m.locations = {LocationDecl{"A", true, {test::atom("x", Comparison::LessEqual, 5)}, ""},
                   LocationDecl{"B", false, {}, ""}};
    m.edges = {EdgeDecl{"e1", "A", "B", Sync{"go", '!'}, {test::atom("x", Comparison::GreaterEqual, 2)}, {"x", "y"}, ""},
               EdgeDecl{"step-2", "B", "A", std::nullopt, {}, {}, ""}};
    EXPECT_EQ(print_text(m), R"(automaton Small {
    clock x, y;
    // spare
    // clock
    clock z;
    channel go;

    initial location A { invariant x <= 5; }
    location B;

    edge e1: A -> B { guard x >= 2; sync go!; reset x, y; }
    edge "step-2": B -> A;
}
)");
}

TEST(TextComments, AttachToNextDeclarationAndToTheOwnerAtBlockEnd) {
    const ParseResult r = parse_text(R"(automaton M {
    // first line
    /* second
       line */
    clock t;
    location A { invariant t <= 3; // inside the location block
    }
    initial location B;
    edge B -> A { sync a!; /* about the edge */ }
    channel a;
    // closing remark
})");
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    EXPECT_EQ(r.model->clocks[0].note, "first line\nsecond\nline");
    EXPECT_EQ(r.model->locations[0].note, "inside the location block");
    EXPECT_EQ(r.model->edges[0].note, "about the edge");
    EXPECT_EQ(r.model->note, "closing remark");
}

TEST(TextComments, FormattingKeepsEveryComment) {
    const std::string src = R"(// model remark
automaton M { clock t; /* c1 */ initial location A { invariant t <= 3; } // c2
location B; edge A -> B { reset t; } // c3
channel spare; }
)";
    Result<std::string> formatted = format_text(src);
    ASSERT_TRUE(formatted) << formatted.error().to_string();
    for (const char* c : {"model remark", "c1", "c2", "c3"}) {
        EXPECT_NE(formatted.value().find(c), std::string::npos) << c << " lost in:\n" << formatted.value();
    }
    // Formatting is idempotent.
    Result<std::string> twice = format_text(formatted.value());
    ASSERT_TRUE(twice);
    EXPECT_EQ(twice.value(), formatted.value());
}

TEST(TextEdges, MissingIdsGetTheSmallestFreeNumber) {
    const ParseResult r = parse_text(R"(automaton M {
    clock t;
    initial location A;
    location B;
    edge e2: A -> B;
    edge A -> B { reset t; }
    edge B -> A;
})");
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    EXPECT_EQ(r.model->edges[0].id, "e2");
    EXPECT_EQ(r.model->edges[1].id, "e1");
    EXPECT_EQ(r.model->edges[2].id, "e3");
}

struct SyntaxCase {
    const char* name;
    const char* source;
    const char* code;
    std::uint32_t line;
    std::uint32_t column;
};

class TextSyntaxErrors : public ::testing::TestWithParam<SyntaxCase> {};

TEST_P(TextSyntaxErrors, ReportCodeAndExactRange) {
    const SyntaxCase& c = GetParam();
    const ParseResult r = parse_text(c.source);
    EXPECT_FALSE(r.model.has_value()) << c.name;
    const auto hits = with_code(r.diagnostics, c.code);
    ASSERT_FALSE(hits.empty()) << c.name << ": " << to_json(r.diagnostics).dump();
    ASSERT_TRUE(hits[0].range.has_value());
    EXPECT_EQ(hits[0].range->line, c.line) << c.name;
    EXPECT_EQ(hits[0].range->column, c.column) << c.name;
    EXPECT_EQ(hits[0].severity, "error");
}

INSTANTIATE_TEST_SUITE_P(
    Codes, TextSyntaxErrors,
    ::testing::Values(
        SyntaxCase{"unexpected", "automaton M {\n  clock t\n  initial location A;\n}", "TWT001", 3, 3},
        SyntaxCase{"unterminated comment", "automaton M { /* never closed", "TWT002", 1, 15},
        SyntaxCase{"integer range", "automaton M {\n const C = 99999999999999999999;\n}", "TWT003", 2, 12},
        SyntaxCase{"disjunction", "automaton M { clock t;\n initial location A { invariant t < 1 || t > 2; } }",
                   "TWT010", 2, 39},
        SyntaxCase{"negation", "automaton M { clock t;\n initial location A { invariant !t < 1; } }", "TWT011", 2, 33},
        SyntaxCase{"false", "automaton M { clock t;\n initial location A { invariant false; } }", "TWT012", 2, 33},
        SyntaxCase{"nonzero reset", "automaton M { clock t; initial location A;\n edge A -> A { reset t := 4; } }",
                   "TWT013", 2, 22},
        SyntaxCase{"data variable", "automaton M {\n int counter;\n initial location A;\n}", "TWT014", 2, 2}),
    [](const ::testing::TestParamInfo<SyntaxCase>& info) {
        std::string n = info.param.name;
        std::replace(n.begin(), n.end(), ' ', '_');
        return n;
    });

TEST(TextSyntax, ResetToZeroIsAccepted) {
    const ParseResult r = parse_text("automaton M { clock t; initial location A; edge A -> A { reset t := 0; } }");
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    EXPECT_EQ(r.model->edges[0].resets, (std::vector<std::string>{"t"}));
}

TEST(TextSyntax, SeveralErrorsAreReportedTogether) {
    const ParseResult r = parse_text("automaton M {\n clock t\n location A { invariant t <; }\n edge A -> ; }");
    EXPECT_FALSE(r.model.has_value());
    EXPECT_GE(with_code(r.diagnostics, "TWT001").size(), 3u) << to_json(r.diagnostics).dump();
}

TEST(TextConstraint, ParsesConjunctionsWithDiagonalsAndConstants) {
    Result<Constraint> c = parse_constraint("t >= COOL_MIN && x - y < 3");
    ASSERT_TRUE(c) << c.error().to_string();
    ASSERT_EQ(c.value().size(), 2u);
    EXPECT_EQ(c.value()[0], test::atom("t", Comparison::GreaterEqual, std::string("COOL_MIN")));
    EXPECT_EQ(c.value()[1], (Atom{"x", std::string("y"), Comparison::Less, Bound{std::int64_t{3}}}));
    Result<Constraint> t = parse_constraint("  true ");
    ASSERT_TRUE(t);
    EXPECT_TRUE(t.value().empty());
    Result<Constraint> e = parse_constraint("");
    ASSERT_TRUE(e);
    EXPECT_TRUE(e.value().empty());
    EXPECT_FALSE(parse_constraint("t < 1 || t > 3"));
    EXPECT_FALSE(parse_constraint("t = 3"));
}

TEST(TextSymbols, DefinitionAndReferencesOfAClock) {
    const ParseResult r = parse_text(R"(automaton M {
    clock t;
    initial location A { invariant t <= 9; }
    location B;
    edge A -> B { guard t >= 2; reset t; }
})");
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    const auto it = std::find_if(r.symbols.begin(), r.symbols.end(),
                                 [](const Symbol& s) { return s.kind == "clock" && s.name == "t"; });
    ASSERT_NE(it, r.symbols.end());
    EXPECT_EQ(it->definition, (SourceRange{2, 11, 2, 12}));
    ASSERT_EQ(it->references.size(), 3u);
    EXPECT_EQ(it->references[0], (SourceRange{3, 36, 3, 37}));
    EXPECT_EQ(it->references[1], (SourceRange{5, 25, 5, 26}));
    EXPECT_EQ(it->references[2], (SourceRange{5, 39, 5, 40}));
    const auto loc = std::find_if(r.symbols.begin(), r.symbols.end(),
                                  [](const Symbol& s) { return s.kind == "location" && s.name == "B"; });
    ASSERT_NE(loc, r.symbols.end());
    EXPECT_EQ(loc->references.size(), 1u);
}

TEST(TextFormat, RefusesSyntaxErrors) {
    Result<std::string> f = format_text("automaton M { clock }");
    EXPECT_FALSE(f);
}

}  // namespace
}  // namespace twin::authoring
