/**
 * @file compiler_test.cpp
 * @brief Compiler: one malformed fixture per diagnostic code, and determinism.
 *
 * Every fixture is a minimal UPPAAL document that is valid except for one
 * construct outside the supported fragment (docs/supported-model-fragment.md).
 * The compiler must refuse it with that construct's stable diagnostic code
 * (docs/compiler-diagnostics.md) — never compile it silently, as the aligner
 * would by ignoring the construct.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include "support/drone_package.hpp"
#include "twin/compiler/compiler.hpp"

namespace twin::compiler {
namespace {

/// A minimal one-template document; parts are spliced in verbatim.
struct Doc {
    std::string globals = "clock x, y;\nchan a;";
    std::string tmpl_decl;
    std::string parameter;
    std::string loc0 = R"(<location id="id0"><name>L0</name><label kind="invariant">x &lt;= 5</label></location>)";
    std::string loc1 = R"(<location id="id1"><name>L1</name></location>)";
    std::string extra_locations;
    std::string edge_labels = R"(<label kind="guard">x &gt;= 2</label><label kind="synchronisation">a!</label><label kind="assignment">x := 0</label>)";
    std::string extra_templates;
    std::string system = "P = T();\nsystem P;";

    [[nodiscard]] std::string xml() const {
        return R"(<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE nta PUBLIC '-//Uppaal Team//DTD Flat System 1.6//EN' 'http://www.it.uu.se/research/group/darts/uppaal/flat-1_6.dtd'>
<nta><declaration>)" + globals + R"(</declaration>
<template><name>T</name>)" + (parameter.empty() ? "" : "<parameter>" + parameter + "</parameter>") +
               "<declaration>" + tmpl_decl + "</declaration>" + loc0 + loc1 + extra_locations +
               R"(<init ref="id0"/><transition><source ref="id0"/><target ref="id1"/>)" + edge_labels +
               "</transition></template>" + extra_templates + "<system>" + system + "</system></nta>";
    }
};

class CompilerFixtures : public ::testing::Test {
protected:
    std::variant<CompileResult, CompileFailure> compile(const Doc& d, CompileOptions o = {}) {
        const std::filesystem::path p = dir_ / ("m" + std::to_string(counter_++) + ".xml");
        std::ofstream(p) << d.xml();
        if (o.model_id.empty()) o.model_id = "fixture";
        return compile_file(p, o);
    }
    static std::vector<std::string> codes(const std::variant<CompileResult, CompileFailure>& r) {
        std::vector<std::string> out;
        const auto& diags = std::holds_alternative<CompileFailure>(r) ? std::get<CompileFailure>(r).diagnostics
                                                                     : std::get<CompileResult>(r).manifest.diagnostics;
        for (const Diagnostic& d : diags) out.push_back(d.code);
        return out;
    }
    void expect_refused(const Doc& d, const std::string& code, CompileOptions o = {}) {
        const auto r = compile(d, o);
        ASSERT_TRUE(std::holds_alternative<CompileFailure>(r)) << "compiled although it uses a construct for " << code;
        const auto c = codes(r);
        EXPECT_NE(std::find(c.begin(), c.end(), code), c.end())
            << "expected " << code << ", got: " << testing::PrintToString(c);
    }

    test::TempDir tmp_;
    std::filesystem::path dir_ = tmp_.path();
    int counter_{0};
};

TEST_F(CompilerFixtures, BaselineFixtureCompiles) {
    const auto r = compile(Doc{});
    ASSERT_TRUE(std::holds_alternative<CompileResult>(r)) << testing::PrintToString(codes(r));
    const CompileResult& ok = std::get<CompileResult>(r);
    EXPECT_EQ(ok.model.locations.size(), 2U);
    EXPECT_EQ(ok.model.transitions.size(), 1U);
    EXPECT_TRUE(ok.manifest.translation_validation.passed);
}

TEST_F(CompilerFixtures, TWC000_MissingSource) {
    CompileOptions o;
    o.model_id = "m";
    const auto r = compile_file(dir_ / "does-not-exist.xml", o);
    ASSERT_TRUE(std::holds_alternative<CompileFailure>(r));
    EXPECT_EQ(codes(r).front(), "TWC000");
}

TEST_F(CompilerFixtures, TWC001_SyntaxError) {
    Doc d;
    d.edge_labels = R"(<label kind="guard">x &gt;&lt; 2</label>)";
    expect_refused(d, "TWC001");
}

TEST_F(CompilerFixtures, TWC003_TwoTemplates) {
    Doc d;
    d.extra_templates = R"(<template><name>U</name><location id="u0"><name>M</name></location><init ref="u0"/></template>)";
    expect_refused(d, "TWC003");
}

TEST_F(CompilerFixtures, TWC005_TwoInstances) {
    Doc d;
    d.system = "P1 = T();\nP2 = T();\nsystem P1, P2;";
    expect_refused(d, "TWC005");
}

TEST_F(CompilerFixtures, TWC007_TemplateParameter) {
    Doc d;
    d.parameter = "const int n";
    d.system = "P = T(1);\nsystem P;";
    expect_refused(d, "TWC007");
}

TEST_F(CompilerFixtures, TWC008_UserFunction) {
    Doc d;
    d.globals += "\nint f() { return 1; }";
    expect_refused(d, "TWC008");
}

TEST_F(CompilerFixtures, TWC011_ClockArray) {
    Doc d;
    d.globals = "clock x, y;\nclock zs[2];\nchan a;";
    expect_refused(d, "TWC011");
}

TEST_F(CompilerFixtures, TWC012_UrgentChannel) {
    Doc d;
    d.globals = "clock x, y;\nurgent chan a;";
    expect_refused(d, "TWC012");
}

TEST_F(CompilerFixtures, TWC014_DataVariable) {
    Doc d;
    d.globals += "\nint counter;";
    expect_refused(d, "TWC014");
}

TEST_F(CompilerFixtures, TWC022_BoundOutOfRange) {
    Doc d;
    d.edge_labels = R"(<label kind="guard">x &gt;= 2000000000</label><label kind="synchronisation">a!</label>)";
    expect_refused(d, "TWC022");
}

// UTAP 2.1 already rejects clock disjunctions and negations in guards (TWC001);
// TWC024/TWC025 are the compiler's own second barrier for documents UTAP accepts.
TEST_F(CompilerFixtures, TWC024_DisjunctionIsNeverCompiled) {
    Doc d;
    d.edge_labels = R"(<label kind="guard">x &lt; 2 || x &gt; 4</label><label kind="synchronisation">a!</label>)";
    const auto r = compile(d);
    ASSERT_TRUE(std::holds_alternative<CompileFailure>(r));
    const auto c = codes(r);
    EXPECT_TRUE(std::find(c.begin(), c.end(), "TWC024") != c.end() || std::find(c.begin(), c.end(), "TWC001") != c.end());
}

TEST_F(CompilerFixtures, TWC025_NegationIsNeverCompiled) {
    Doc d;
    d.edge_labels = R"(<label kind="guard">!(x &lt; 2)</label><label kind="synchronisation">a!</label>)";
    const auto r = compile(d);
    ASSERT_TRUE(std::holds_alternative<CompileFailure>(r));
    const auto c = codes(r);
    EXPECT_TRUE(std::find(c.begin(), c.end(), "TWC025") != c.end() || std::find(c.begin(), c.end(), "TWC001") != c.end());
}

TEST_F(CompilerFixtures, TWC026_FalseGuard) {
    Doc d;
    d.edge_labels = R"(<label kind="guard">false</label><label kind="synchronisation">a!</label>)";
    expect_refused(d, "TWC026");
}

TEST_F(CompilerFixtures, TWC031_UrgentLocation) {
    Doc d;
    d.loc1 = R"(<location id="id1"><name>L1</name><urgent/></location>)";
    expect_refused(d, "TWC031");
}

TEST_F(CompilerFixtures, TWC041_SelectBinding) {
    Doc d;
    d.edge_labels = R"(<label kind="select">i : int[0,1]</label><label kind="synchronisation">a!</label>)";
    expect_refused(d, "TWC041");
}

TEST_F(CompilerFixtures, TWC052_ResetToNonZero) {
    Doc d;
    d.edge_labels = R"(<label kind="synchronisation">a!</label><label kind="assignment">x := 3</label>)";
    expect_refused(d, "TWC052");
}

TEST_F(CompilerFixtures, TWC080_MissingModelId) {
    const std::filesystem::path p = dir_ / "id.xml";
    std::ofstream(p) << Doc{}.xml();
    const auto r = compile_file(p, CompileOptions{});
    ASSERT_TRUE(std::holds_alternative<CompileFailure>(r));
    EXPECT_EQ(codes(r).front(), "TWC080");
}

TEST_F(CompilerFixtures, TWC081_InvalidTimeBase) {
    CompileOptions o;
    o.ticks_per_unit = 3;  // the logical-time grid needs a power of ten
    expect_refused(Doc{}, "TWC081", o);
}

TEST_F(CompilerFixtures, TWC015_LegacySystemDeclarationIsAWarningOnlyWhenRequested) {
    Doc d;
    d.system = "system Undeclared;";
    expect_refused(d, "TWC001");  // strict by default
    CompileOptions legacy;
    legacy.legacy_system_declaration = true;
    const auto r = compile(d, legacy);
    ASSERT_TRUE(std::holds_alternative<CompileResult>(r)) << testing::PrintToString(codes(r));
    const auto c = codes(r);
    EXPECT_NE(std::find(c.begin(), c.end(), "TWC015"), c.end());
}

TEST_F(CompilerFixtures, CompilationIsDeterministic) {
    const auto a = compile(Doc{});
    const auto b = compile(Doc{});
    ASSERT_TRUE(std::holds_alternative<CompileResult>(a));
    ASSERT_TRUE(std::holds_alternative<CompileResult>(b));
    EXPECT_EQ(std::get<CompileResult>(a).canonical_ir, std::get<CompileResult>(b).canonical_ir);
    EXPECT_EQ(std::get<CompileResult>(a).manifest.ir_sha256, std::get<CompileResult>(b).manifest.ir_sha256);
}

TEST(CompilerModels, DroneSupervisorCompilesDeterministicallyWithTranslationValidation) {
    const package::BuildInputs in = test::drone_build_inputs();
    CompileOptions o;
    o.model_id = "indoor-drone-dt";
    o.interpretation = in.dt_interpretation;
    const auto a = compile_file(in.dt_model, o);
    const auto b = compile_file(in.dt_model, o);
    ASSERT_TRUE(std::holds_alternative<CompileResult>(a));
    ASSERT_TRUE(std::holds_alternative<CompileResult>(b));
    const CompileResult& r = std::get<CompileResult>(a);
    EXPECT_EQ(r.canonical_ir, std::get<CompileResult>(b).canonical_ir);
    EXPECT_TRUE(r.manifest.translation_validation.passed);
    EXPECT_GT(r.manifest.translation_validation.checks, 0U);
    EXPECT_TRUE(r.manifest.determinism.event_deterministic);
    EXPECT_EQ(r.model.locations.size(), 12U);
    EXPECT_EQ(r.model.transitions.size(), 25U);
}

}  // namespace
}  // namespace twin::compiler
