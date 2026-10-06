/**
 * @file architecture_test.cpp
 * @brief Machine-checked architecture rules (the TCB boundary and the
 *        "single semantic authority" discipline), checked on the sources.
 *
 * These checks guard the properties the correctness argument relies on
 * (docs/trusted-computing-base.md):
 *  1. the semantic kernel depends only on the IR model and core utilities;
 *  2. the runtime never reaches the simulator's ground truth;
 *  3. neither the mission controller, the planner nor the drone UI plugin
 *     embeds the twin's state machine (no location names in their logic);
 *  4. the world simulator and the PT feed are never linked into the runtime.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace twin {
namespace {

namespace fs = std::filesystem;

const fs::path kRoot = TWIN_SOURCE_DIR;

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}

/// Source files under @p dirs with one of the @p extensions.
std::vector<fs::path> sources(const std::vector<fs::path>& dirs, const std::vector<std::string>& extensions) {
    std::vector<fs::path> out;
    for (const fs::path& d : dirs) {
        if (!fs::exists(kRoot / d)) continue;
        for (const auto& e : fs::recursive_directory_iterator(kRoot / d)) {
            if (!e.is_regular_file()) continue;
            for (const std::string& ext : extensions) {
                if (e.path().extension() == ext) out.push_back(e.path());
            }
        }
    }
    return out;
}

/// CMake text without comments.
std::string cmake_code(const fs::path& p) { return std::regex_replace(read(p), std::regex(R"(#[^\n]*)"), " "); }

/// Source text without comments (rules apply to code, not to explanations).
std::string code_of(const fs::path& p) {
    std::string s = read(p);
    s = std::regex_replace(s, std::regex(R"(/\*[\s\S]*?\*/)"), " ");
    s = std::regex_replace(s, std::regex(R"(//[^\n]*)"), " ");
    return s;
}

const std::vector<std::string> kCpp = {".cpp", ".hpp"};
const std::vector<std::string> kDtLocations = {"INITIALIZING", "TAKING_OFF", "NAVIGATING", "REPLANNING", "HOVERING",
                                               "INSPECTING", "RETURNING", "RETURN_REPLANNING", "LANDING",
                                               "LANDED", "MISSION_FAILED"};

TEST(Architecture, KernelDependsOnlyOnTheIrModelAndCore) {
    const std::regex include(R"(#include\s*[<"]([^>"]+)[>"])");
    for (const fs::path& f : sources({"src/kernel", "include/twin/kernel"}, kCpp)) {
        const std::string s = read(f);
        for (auto it = std::sregex_iterator(s.begin(), s.end(), include); it != std::sregex_iterator(); ++it) {
            const std::string h = (*it)[1];
            const bool project = h.rfind("twin/", 0) == 0;
            // twin::ir_model = the IR data model and its validator (no codec, no JSON).
            const bool allowed = !project || h.rfind("twin/kernel/", 0) == 0 || h.rfind("twin/core/", 0) == 0 ||
                                 h == "twin/ir/model.hpp" || h == "twin/ir/validate.hpp";
            const bool third_party = h.find("httplib") != std::string::npos || h.find("nlohmann") != std::string::npos ||
                                     h.find("z3") != std::string::npos || h.find("dtpta") != std::string::npos ||
                                     h.find("sqlite") != std::string::npos || h.find("utap") != std::string::npos;
            EXPECT_TRUE(allowed && !third_party) << f << " includes " << h;
        }
    }
    const std::string cmake = cmake_code(kRoot / "src/kernel/CMakeLists.txt");
    for (const char* forbidden : {"twin::json", "twin::runtime", "twin::world", "httplib", "aligner::"}) {
        EXPECT_EQ(cmake.find(forbidden), std::string::npos) << "kernel links " << forbidden;
    }
}

TEST(Architecture, RuntimeNeverReachesTheGroundTruth) {
    for (const fs::path& f : sources({"src/runtime", "include/twin/runtime"}, kCpp)) {
        const std::string s = code_of(f);
        EXPECT_EQ(s.find("twin/world/"), std::string::npos) << f << " includes the world simulator";
        EXPECT_EQ(s.find("twin/ptfeed/"), std::string::npos) << f << " includes the PT feed";
        EXPECT_EQ(s.find("\"/observer"), std::string::npos) << f << " calls the observer (ground-truth) API";
    }
    const std::string cmake = cmake_code(kRoot / "src/runtime/CMakeLists.txt");
    EXPECT_EQ(cmake.find("twin::world"), std::string::npos);
    EXPECT_EQ(cmake.find("twin::ptfeed"), std::string::npos);
    const std::string app = cmake_code(kRoot / "apps/twin-runtime/CMakeLists.txt");
    EXPECT_EQ(app.find("twin::world"), std::string::npos);
}

TEST(Architecture, NoShadowStateMachineOutsideTheKernel) {
    std::vector<fs::path> files = {kRoot / "src/runtime/mission_controller.cpp",
                                   kRoot / "include/twin/runtime/mission_controller.hpp"};
    for (const fs::path& f : sources({"src/planner", "include/twin/planner"}, kCpp)) files.push_back(f);
    for (const fs::path& f : sources({"web/studio/src/plugins/drone"}, {".ts", ".tsx"})) files.push_back(f);
    ASSERT_GT(files.size(), 4U);
    for (const fs::path& f : files) {
        const std::string s = code_of(f);
        for (const std::string& loc : kDtLocations) {
            const std::regex literal("[\"'`]" + loc + "[\"'`]");
            EXPECT_FALSE(std::regex_search(s, literal)) << f << " branches on the twin location " << loc;
        }
    }
}

TEST(Architecture, OnlyTheSessionMutatesSemanticState) {
    // In the production runtime, the only assignments to the authoritative state are in
    // session.cpp, from kernel results. (The kernel's own instance and the replayer hold
    // their own copies by design; they are not the runtime's state.)
    for (const fs::path& f : sources({"src/runtime", "include/twin/runtime", "apps"}, kCpp)) {
        if (f.filename() == "session.cpp" || f.filename() == "session.hpp") continue;
        EXPECT_EQ(code_of(f).find("state_ ="), std::string::npos) << f;
    }
    const std::string session = code_of(kRoot / "src/runtime/session.cpp");
    const std::regex assign(R"(state_\s*=\s*([^;]+);)");
    for (auto it = std::sregex_iterator(session.begin(), session.end(), assign); it != std::sregex_iterator(); ++it) {
        const std::string rhs = (*it)[1];
        EXPECT_NE(rhs.find("effect"), std::string::npos) << "state_ assigned from a non-kernel value: " << rhs;
    }
}

}  // namespace
}  // namespace twin
