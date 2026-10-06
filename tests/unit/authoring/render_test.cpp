/**
 * @file render_test.cpp
 * @brief Toolchain/exchange renderings, semantic digest and compilation from the canonical model.
 *
 * The central property: the toolchain rendering of a canonical model compiles to
 * exactly the IR of the hand-written UPPAAL document it mirrors, so authoring
 * in TwinTA or in the diagram produces the same executable model as UPPAAL.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <variant>

#include "authoring_fixtures.hpp"
#include "twin/authoring/text.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/authoring/validate.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/core/large_stack.hpp"

namespace twin::authoring {
namespace {

namespace fs = std::filesystem;

Model pump_dt() {
    const ParseResult r = parse_text(test::kPumpDtText);
    EXPECT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump();
    return r.model.value_or(Model{});
}

fs::path work_dir(const std::string& name) {
    const fs::path d = fs::temp_directory_path() / ("twin-render-test-" + name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

compiler::CompileOptions options() {
    compiler::CompileOptions o;
    o.model_id = "pump-dt";
    return o;
}

/// IR with the source hash cleared (the only field that depends on the document bytes).
ir::Model normalised(ir::Model m) {
    m.info.source_sha256.clear();
    return m;
}

ir::Model compile_xml(const std::string& xml, const std::string& name) {
    const fs::path f = work_dir(name) / "model.xml";
    std::ofstream(f) << xml;
    auto out = compiler::compile_file(f, options());
    if (auto* failure = std::get_if<compiler::CompileFailure>(&out)) {
        std::string all;
        for (const auto& d : failure->diagnostics) all += compiler::render(d) + "\n";
        ADD_FAILURE() << "compilation failed:\n" << all << "\n" << xml;
        return {};
    }
    return std::get<compiler::CompileResult>(out).model;
}

TEST(Render, ToolchainRenderingCompilesToTheSameIrAsTheUppaalOriginal) {
    auto original = compiler::compile_file(fs::path(TWIN_SOURCE_DIR) / "examples/industrial-pump/models/pump_dt.xml",
                                           options());
    ASSERT_TRUE(std::holds_alternative<compiler::CompileResult>(original));
    const ir::Model from_uppaal = std::get<compiler::CompileResult>(original).model;
    const ir::Model from_canonical = compile_xml(render_toolchain_xml(pump_dt()), "pump");
    EXPECT_EQ(normalised(from_canonical), normalised(from_uppaal));
}

TEST(Render, DigestIgnoresNotesAndLayout) {
    const Model m = test::pump_like_model();
    Model bare = m;
    bare.note.clear();
    for (auto& c : bare.clocks) c.note.clear();
    for (auto& c : bare.constants) c.note.clear();
    for (auto& l : bare.locations) l.note.clear();
    for (auto& e : bare.edges) e.note.clear();
    EXPECT_EQ(render_toolchain_xml(m), render_toolchain_xml(bare));
    EXPECT_EQ(semantic_digest(m), semantic_digest(bare));
    EXPECT_NE(content_sha256(m), content_sha256(bare));
    Model changed = m;
    changed.locations[1].invariant[0].bound = Bound{std::int64_t{601}};
    EXPECT_NE(semantic_digest(changed), semantic_digest(m));
}

TEST(Render, InitialLocationIsEmittedFirst) {
    Model m = test::pump_like_model();
    std::rotate(m.locations.begin(), m.locations.begin() + 1, m.locations.end());  // STOPPED now last
    ASSERT_FALSE(m.locations.front().initial);
    const ir::Model ir = compile_xml(render_toolchain_xml(m), "initial");
    ASSERT_FALSE(ir.locations.empty());
    EXPECT_EQ(ir.locations[ir.initial].id, "STOPPED");
    EXPECT_EQ(ir.initial, 0u);
}

TEST(Render, EscapesXmlSpecialCharactersInLabels) {
    const std::string xml = render_toolchain_xml(test::pump_like_model());
    EXPECT_NE(xml.find("t &gt;= COOL_MIN"), std::string::npos) << xml;
    EXPECT_NE(xml.find("t - u &lt;= 5"), std::string::npos) << xml;
    EXPECT_EQ(xml.find(">= COOL_MIN"), std::string::npos);
    const ir::Model ir = compile_xml(xml, "escape");
    EXPECT_EQ(ir.transitions.size(), 4u);
}

TEST(Render, ExchangeRenderingHasTheSameSemantics) {
    const Model m = test::pump_like_model();
    Layout l;
    l.locations["STOPPED"] = LocationLayout{Point{0, 0}, Point{-10, -30}};
    l.locations["DEGRADED"] = LocationLayout{Point{300, 0}, std::nullopt};
    l.edges["e2"] = EdgeLayout{{Point{450, 80}}, Point{420, 60}};
    const std::string exchange = render_exchange_xml(m, l);
    EXPECT_NE(exchange.find("x=\"300\""), std::string::npos);
    EXPECT_NE(exchange.find("<nail x=\"450\" y=\"80\"/>"), std::string::npos);
    EXPECT_NE(exchange.find("start the cooler"), std::string::npos);  // edge note as comments label
    EXPECT_EQ(normalised(compile_xml(exchange, "exchange")), normalised(compile_xml(render_toolchain_xml(m), "tc")));
}

TEST(Render, ToolchainSourceAcceptsCanonicalAndLegacyContent) {
    std::ifstream in(fs::path(TWIN_SOURCE_DIR) / "examples/industrial-pump/models/pump_dt.xml");
    std::stringstream xml;
    xml << in.rdbuf();
    EXPECT_EQ(content_format(xml.str()), "uppaal-xml");
    Result<std::string> legacy = toolchain_source(xml.str());
    ASSERT_TRUE(legacy);
    EXPECT_EQ(legacy.value(), xml.str());

    const std::string canonical = to_json(test::pump_like_model()).dump(2);
    EXPECT_EQ(content_format(canonical), "twin-ta/1");
    Result<std::string> rendered = toolchain_source(canonical);
    ASSERT_TRUE(rendered) << rendered.error().to_string();
    EXPECT_EQ(rendered.value(), render_toolchain_xml(test::pump_like_model()));

    EXPECT_EQ(content_format("hello"), "unknown");
    EXPECT_FALSE(toolchain_source("hello"));
    Model broken = test::pump_like_model();
    broken.edges[0].target = "NOWHERE";
    EXPECT_FALSE(toolchain_source(to_json(broken).dump()));
}

TEST(Render, CompileModelProducesASourceMap) {
    const Model m = pump_dt();
    auto out = compile_model(m, options(), work_dir("compile"));
    ASSERT_TRUE(std::holds_alternative<CompiledModel>(out));
    const CompiledModel& c = std::get<CompiledModel>(out);
    EXPECT_EQ(c.semantic_digest, semantic_digest(m));
    EXPECT_EQ(c.result.manifest.source_sha256, c.semantic_digest);
    const json::Json& transitions = c.source_map.at("transitions");
    ASSERT_EQ(transitions.size(), m.edges.size());
    EXPECT_EQ(transitions.at("STOPPED.restart!.NORMAL"), "e1");
    EXPECT_EQ(transitions.at("FAULT.fault_reset!.STOPPED"), "e14");
    EXPECT_EQ(c.source_map.at("edges").at("e6"), "COOLING.cooling_complete!.NORMAL");
    EXPECT_EQ(c.source_map.at("locations").at("DEGRADED"), "DEGRADED");
}

TEST(Render, CompileModelRefusesStructurallyInvalidModels) {
    Model m = test::pump_like_model();
    m.edges[0].sync = Sync{"nochan", '!'};
    auto out = compile_model(m, options(), work_dir("invalid"));
    ASSERT_TRUE(std::holds_alternative<compiler::CompileFailure>(out));
    const auto& ds = std::get<compiler::CompileFailure>(out).diagnostics;
    ASSERT_FALSE(ds.empty());
    EXPECT_EQ(ds.front().code, "TWM008");
}

TEST(Render, FormattingKeepsTheSemanticDigest) {
    const std::string messy = "// pump\nautomaton ProcessPumpDT{clock t; channel restart, trip; /* x */ initial location A;"
                              "location B{invariant t<=5;} edge A->B{sync restart!;reset t;} edge B->A{guard t>=1; sync trip!;}}";
    const ParseResult before = parse_text(messy);
    ASSERT_TRUE(before.model.has_value()) << to_json(before.diagnostics).dump();
    Result<std::string> formatted = format_text(messy);
    ASSERT_TRUE(formatted) << formatted.error().to_string();
    const ParseResult after = parse_text(formatted.value());
    ASSERT_TRUE(after.model.has_value());
    EXPECT_EQ(semantic_digest(*after.model), semantic_digest(*before.model));
    EXPECT_EQ(*after.model, *before.model);
}

TEST(Render, CompileModelIsSafeUnderConcurrency) {
    // Studio serves requests on several threads: concurrent compilations of the same model
    // share one work directory and one rendering file name (the semantic digest).
    const Model m = pump_dt();
    const fs::path dir = work_dir("concurrent");
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 6; ++i) {
                // Formal tools need a large stack off the main thread (as in Studio's services).
                const bool ok = run_with_large_stack(
                    [&] { return std::holds_alternative<CompiledModel>(compile_model(m, options(), dir)); });
                if (!ok) ++failures;
            }
        });
    }
    for (std::thread& t : threads) t.join();
    EXPECT_EQ(failures.load(), 0);
}

TEST(Render, MaxBoundMatchesTheCompiler) { EXPECT_EQ(max_bound(), compiler::max_clock_bound()); }

}  // namespace
}  // namespace twin::authoring
