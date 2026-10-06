/**
 * @file compiler_corpus_test.cpp
 * @brief Every timed-automaton view of the SemPTDTAlignmentICSE corpus compiles,
 * passes translation validation against the aligner's own reading, and compiles
 * to byte-identical IR twice (determinism).
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <variant>

#include "twin/compiler/compiler.hpp"

namespace twin::compiler {
namespace {

std::vector<std::filesystem::path> corpus() {
    std::vector<std::filesystem::path> out;
    const auto root = std::filesystem::path(TWIN_SOURCE_DIR) / "SemPTDTAlignmentICSE" / "assets";
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (e.is_regular_file() && e.path().extension() == ".xml") out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

TEST(CompilerCorpus, EveryViewCompilesValidatesAndIsDeterministic) {
    const std::vector<std::filesystem::path> files = corpus();
    ASSERT_GE(files.size(), 30U);
    for (const std::filesystem::path& f : files) {
        CompileOptions o;
        o.model_id = f.parent_path().filename().string() + "-" + f.stem().string();
        o.legacy_system_declaration = true;  // the corpus' <system> blocks are invalid UPPAAL (see docs)
        const auto a = compile_file(f, o);
        const auto b = compile_file(f, o);
        ASSERT_TRUE(std::holds_alternative<CompileResult>(a)) << f;
        ASSERT_TRUE(std::holds_alternative<CompileResult>(b)) << f;
        const CompileResult& r = std::get<CompileResult>(a);
        EXPECT_TRUE(r.manifest.translation_validation.passed) << f;
        EXPECT_EQ(r.canonical_ir, std::get<CompileResult>(b).canonical_ir) << f;
    }
}

}  // namespace
}  // namespace twin::compiler
