/**
 * @file authoring_corpus_test.cpp
 * @brief Every view of the SemPTDTAlignmentICSE corpus imports into the canonical model with the
 * preservation check passing, survives the TwinTA round trip, and exports back to UPPAAL with the
 * same semantic digest. Plus the repository's own example views.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "twin/authoring/import.hpp"
#include "twin/authoring/text.hpp"
#include "twin/authoring/uppaal.hpp"

namespace twin::authoring {
namespace {

namespace fs = std::filesystem;

std::vector<fs::path> views() {
    std::vector<fs::path> out;
    const fs::path root(TWIN_SOURCE_DIR);
    for (const auto& e : fs::recursive_directory_iterator(root / "SemPTDTAlignmentICSE" / "assets")) {
        if (e.is_regular_file() && e.path().extension() == ".xml") out.push_back(e.path());
    }
    for (const char* f : {"models/indoor_drone/V_D_mission_supervisor.xml", "models/indoor_drone/V_P_flight_controller.xml",
                          "examples/industrial-pump/models/pump_dt.xml", "examples/industrial-pump/models/pump_pt.xml"}) {
        out.push_back(root / f);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string read(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

TEST(AuthoringCorpus, EveryViewImportsPreservedAndRoundTrips) {
    const std::vector<fs::path> files = views();
    ASSERT_GE(files.size(), 34U);
    for (const fs::path& f : files) {
        SCOPED_TRACE(f.string());
        const bool legacy = f.string().find("SemPTDTAlignmentICSE") != std::string::npos;
        const ImportResult r = import_any(read(f), ImportOptions{f.filename().string(), legacy});
        ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump(2);
        ASSERT_TRUE(r.provenance.has_value());
        EXPECT_TRUE(r.provenance->preserved);

        const ParseResult text = parse_text(print_text(*r.model));
        ASSERT_TRUE(text.model.has_value()) << to_json(text.diagnostics).dump(2);
        EXPECT_EQ(*text.model, *r.model);

        Result<ExportResult> exported = export_model(*r.model, r.layout, "uppaal");
        ASSERT_TRUE(exported) << exported.error().to_string();
        EXPECT_TRUE(exported.value().round_trip_verified);
    }
}

}  // namespace
}  // namespace twin::authoring
