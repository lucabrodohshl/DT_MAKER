// Tests for the strict .ont/.interp source parser (twin/ontology/source.hpp).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "twin/ontology/source.hpp"

namespace fs = std::filesystem;
using namespace twin::ontology;

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string codes(const std::vector<SourceDiagnostic>& diags) {
    std::string out;
    for (const auto& d : diags) out += d.code + "@" + std::to_string(d.span.line) + ":" + d.message + "\n";
    return out;
}

bool has_code(const std::vector<SourceDiagnostic>& diags, const std::string& code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

const fs::path kAssets = fs::path(TWIN_SOURCE_DIR) / "SemPTDTAlignmentICSE" / "assets";

}  // namespace

TEST(OntologySourceParser, ParsesDeclarationsWithSpansAndComments) {
    const auto parsed = parse_ontology(
        "; Process pump domain\n"
        "; second header line\n"
        "\n"
        "; === SORTS ===\n"
        "sort Temperature ; degC\n"
        "fun bearing_temp : Temperature\n"
        "fun power : Real Real -> Real ; kW\n"
        "rel overheating :\n"
        "rel at_risk : Temperature\n"
        "axiom max_t : (<= bearing_temp 120) ; datasheet\n");
    ASSERT_TRUE(parsed.ok()) << codes(parsed.diagnostics);
    const auto& s = parsed.source;
    EXPECT_EQ(s.header_comment, "Process pump domain\nsecond header line");
    ASSERT_EQ(s.sorts.size(), 1U);
    EXPECT_EQ(s.sorts[0].comment, "degC");
    EXPECT_EQ(s.sorts[0].span.line, 5U);
    EXPECT_EQ(s.sorts[0].span.column, 6U);
    ASSERT_EQ(s.functions.size(), 2U);
    EXPECT_EQ(s.functions[1].arg_sorts, (std::vector<std::string>{"Real", "Real"}));
    EXPECT_EQ(s.functions[1].return_sort, "Real");
    ASSERT_EQ(s.relations.size(), 2U);
    EXPECT_TRUE(s.relations[0].arg_sorts.empty());
    ASSERT_EQ(s.axioms.size(), 1U);
    EXPECT_EQ(s.axioms[0].formula, "(<= bearing_temp 120)");
    EXPECT_EQ(s.axioms[0].comment, "datasheet");
    EXPECT_EQ(s.axioms[0].formula_span.line, 10U);
}

TEST(OntologySourceParser, UnknownKeywordIsAnErrorTheAlignerWouldIgnore) {
    const auto parsed = parse_ontology("sort A\nconst x : A\n");
    ASSERT_FALSE(parsed.ok());
    ASSERT_EQ(parsed.diagnostics.size(), 1U);
    EXPECT_EQ(parsed.diagnostics[0].code, "ONT001");
    EXPECT_EQ(parsed.diagnostics[0].span.line, 2U);
    EXPECT_EQ(parsed.diagnostics[0].span.column, 1U);
    EXPECT_EQ(parsed.diagnostics[0].span.length, 5U);
}

TEST(OntologySourceParser, CrlfParsesIdenticallyToLf) {
    const std::string lf = "sort A\nfun x : A ; c\naxiom p : (> x 0)\n";
    std::string crlf;
    for (char c : lf) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const auto a = parse_ontology(lf);
    const auto b = parse_ontology(crlf);
    ASSERT_TRUE(b.ok()) << codes(b.diagnostics);
    EXPECT_EQ(a.source.axioms[0].formula, b.source.axioms[0].formula);
    EXPECT_EQ(a.source.functions[0].comment, b.source.functions[0].comment);
}

TEST(OntologySourceParser, ReportsDuplicatesMissingColonUndeclaredSymbolsAndBadFormulas) {
    const auto parsed = parse_ontology(
        "sort A\n"
        "sort A\n"
        "fun x A\n"
        "fun y : A\n"
        "axiom p : (> y z)\n"
        "axiom p : (> y 0)\n"
        "axiom q : (> y 0\n"
        "fun w : Undeclared\n");
    EXPECT_TRUE(has_code(parsed.diagnostics, "ONT004")) << codes(parsed.diagnostics);
    EXPECT_TRUE(has_code(parsed.diagnostics, "ONT002"));
    EXPECT_TRUE(has_code(parsed.diagnostics, "ONT007"));
    EXPECT_TRUE(has_code(parsed.diagnostics, "ONT008"));
    EXPECT_TRUE(has_code(parsed.diagnostics, "ONT006"));
    EXPECT_TRUE(has_code(parsed.diagnostics, "ONT005"));
    for (const auto& d : parsed.diagnostics) {
        if (d.code == "ONT005") EXPECT_EQ(d.severity, Severity::Warning);
    }
}

TEST(OntologySourceParser, SemicolonInsideParenthesesIsNotAComment) {
    const auto parsed = parse_ontology("fun x : Real\naxiom a : (> x 0) ; real comment\n");
    ASSERT_TRUE(parsed.ok()) << codes(parsed.diagnostics);
    EXPECT_EQ(parsed.source.axioms[0].comment, "real comment");
}

TEST(FormulaSymbols, ExcludesBuiltinsNumeralsAndBoundVariables) {
    EXPECT_EQ(formula_symbols("(and (> a b) (f c))").value(), (std::vector<std::string>{"a", "b", "c", "f"}));
    EXPECT_EQ(formula_symbols("(forall ((h Hec)) (>= (cons h) 0.5))").value(), (std::vector<std::string>{"cons"}));
    EXPECT_EQ(formula_symbols("(let ((v (+ x 1))) (> v y))").value(), (std::vector<std::string>{"x", "y"}));
    EXPECT_EQ(formula_symbols("f(x, g(y))").value(), (std::vector<std::string>{"f", "g", "x", "y"}));
    EXPECT_EQ(formula_symbols("true").value(), std::vector<std::string>{});
    EXPECT_FALSE(formula_symbols("(and a").has_value());
    EXPECT_FALSE(formula_symbols(")").has_value());
    EXPECT_FALSE(formula_symbols("").has_value());
}

TEST(InterpretationSourceParser, ParsesLocationsAndEvents) {
    const auto ont = parse_ontology("fun t : Real\nfun t_max : Real\n");
    const auto parsed = parse_interpretation(
        "; DT interpretation\n"
        "NORMAL : (<= t t_max) ; nominal\n"
        "overheat! : (> t t_max)\n",
        &ont.source);
    ASSERT_TRUE(parsed.ok()) << codes(parsed.diagnostics);
    ASSERT_EQ(parsed.source.entries.size(), 2U);
    EXPECT_FALSE(parsed.source.entries[0].is_event);
    EXPECT_TRUE(parsed.source.entries[1].is_event);
    EXPECT_EQ(parsed.source.entries[1].key, "overheat!");
    EXPECT_EQ(parsed.source.header_comment, "DT interpretation");
}

TEST(InterpretationSourceParser, MalformedDuplicateAndUndeclared) {
    const auto ont = parse_ontology("fun t : Real\n");
    const auto parsed = parse_interpretation(
        "NORMAL (<= t 3)\n"
        "A : (> t 0)\n"
        "A : (> t 1)\n"
        "B : (> speed 0)\n"
        "bad name! : true\n",
        &ont.source);
    EXPECT_TRUE(has_code(parsed.diagnostics, "INT001")) << codes(parsed.diagnostics);
    EXPECT_TRUE(has_code(parsed.diagnostics, "INT002"));
    EXPECT_TRUE(has_code(parsed.diagnostics, "INT004"));
    EXPECT_TRUE(has_code(parsed.diagnostics, "INT005"));
}

// Every ontology and interpretation shipped with the aligner must be accepted
// by the strict parser (no errors); otherwise Studio could not open them.
TEST(OntologySourceParser, AcceptsTheWholeAlignerCorpus) {
    std::size_t ontologies = 0;
    std::size_t interpretations = 0;
    for (const auto& dir : fs::directory_iterator(kAssets)) {
        if (!dir.is_directory()) continue;
        std::map<std::string, OntologySource> onts;
        for (const auto& f : fs::directory_iterator(dir.path())) {
            if (f.path().extension() != ".ont") continue;
            const auto parsed = parse_ontology(read_file(f.path()));
            EXPECT_TRUE(parsed.ok()) << f.path() << "\n" << codes(parsed.diagnostics);
            onts[f.path().stem().string()] = parsed.source;
            ++ontologies;
        }
        for (const auto& f : fs::directory_iterator(dir.path())) {
            if (f.path().extension() != ".interp") continue;
            const bool v2 = f.path().stem().string().find("_v2") != std::string::npos;
            const auto it = onts.find(v2 ? "domain_v2" : "domain");
            const auto parsed =
                parse_interpretation(read_file(f.path()), it == onts.end() ? nullptr : &it->second);
            // Known corpus defect (docs/studio/aligner-findings.md, finding S1): line 12 of
            // CS3_Rover/dt.interp is a comment continuation without ';' that the aligner skips
            // silently. The strict parser must report it -- and nothing else.
            if (f.path().parent_path().filename() == "CS3_Rover" && f.path().filename() == "dt.interp") {
                ASSERT_EQ(parsed.diagnostics.size(), 1U) << codes(parsed.diagnostics);
                EXPECT_EQ(parsed.diagnostics[0].code, "INT001");
                EXPECT_EQ(parsed.diagnostics[0].span.line, 12U);
                ++interpretations;
                continue;
            }
            EXPECT_TRUE(parsed.ok()) << f.path() << "\n" << codes(parsed.diagnostics);
            ++interpretations;
        }
    }
    EXPECT_GE(ontologies, 18U);
    EXPECT_GE(interpretations, 40U);
}
