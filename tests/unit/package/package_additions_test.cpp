/**
 * @file package_additions_test.cpp
 * @brief Studio additions to the Verified Twin Package: canonical source models (whose toolchain
 * rendering must be the shipped view, byte for byte), monitors, property evidence, type metadata.
 */
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "support/drone_package.hpp"
#include "twin/authoring/import.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/package/builder.hpp"

namespace twin::package {
namespace {

namespace fs = std::filesystem;

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

class PackageAdditions : public ::testing::Test {
protected:
    void SetUp() override {
        const fs::path m = fs::path(TWIN_SOURCE_DIR) / "examples/industrial-pump/models";
        for (const char* view : {"pump_pt.xml", "pump_dt.xml"}) {
            const authoring::ImportResult r = authoring::import_any(read(m / view), authoring::ImportOptions{view, false});
            ASSERT_TRUE(r.model) << view;
            const std::string stem = fs::path(view).stem().string();
            std::ofstream(dir_.path() / (stem + ".xml"), std::ios::binary) << authoring::render_toolchain_xml(*r.model);
            std::ofstream(dir_.path() / (stem + ".tta.json"), std::ios::binary) << authoring::to_json(*r.model).dump(2);
        }
        std::ofstream(dir_.path() / "monitors.json") << R"json({"format": "twin-monitors/1", "monitors": [
            {"id": "never-fault", "kind": "property", "name": "Never tripped", "severity": "critical", "property": "A[] !FAULT"}]})json";
        std::ofstream(dir_.path() / "properties.json") << R"json({"format": "twin-property-evidence-set/1", "items": []})json";
        in_.pt_model = dir_.path() / "pump_pt.xml";
        in_.dt_model = dir_.path() / "pump_dt.xml";
        in_.ontology = m / "process-pump.ont";
        in_.pt_interpretation = m / "pt.interp";
        in_.dt_interpretation = m / "dt.interp";
        in_.model_id = "process-pump";
        in_.model_version = "4.0.0";
        in_.pt_source_model = dir_.path() / "pump_pt.tta.json";
        in_.dt_source_model = dir_.path() / "pump_dt.tta.json";
        in_.monitors = dir_.path() / "monitors.json";
        in_.property_evidence = dir_.path() / "properties.json";
        in_.type_metadata = json::Json{{"typeId", "centrifugal-pump"}, {"typeVersion", 4}, {"name", "Centrifugal pump"},
                                       {"runtimeMode", "monitor"}};
    }

    std::set<std::string> roles(const LoadedPackage& p) {
        std::set<std::string> out;
        for (const PackageFile& f : p.manifest.files) out.insert(f.role);
        return out;
    }

    test::TempDir dir_;
    BuildInputs in_;
};

TEST_F(PackageAdditions, BuildsAndVerifiesWithEveryAddition) {
    Result<BuildResult> b = build_package(in_, dir_.path() / "pkg");
    ASSERT_TRUE(b) << b.error().to_string();
    const std::set<std::string> r = roles(b.value().package);
    for (const char* role : {"dt_source_model", "pt_source_model", "monitors", "property_evidence", "type_metadata"}) {
        EXPECT_TRUE(r.contains(role)) << role;
    }
    Result<LoadedPackage> again = load_and_verify(dir_.path() / "pkg");
    ASSERT_TRUE(again) << again.error().to_string();
    ASSERT_TRUE(again.value().monitors.has_value());
    EXPECT_EQ(again.value().monitors->at("monitors").size(), 1u);
    ASSERT_TRUE(again.value().type_metadata.has_value());
    EXPECT_EQ(again.value().type_metadata->at("typeId"), "centrifugal-pump");
}

TEST_F(PackageAdditions, SourceModelMustRenderToTheShippedView) {
    std::ofstream(dir_.path() / "pump_dt.xml", std::ios::binary | std::ios::app) << "\n<!-- edited by hand -->\n";
    Result<BuildResult> b = build_package(in_, dir_.path() / "pkg");
    ASSERT_FALSE(b);
    EXPECT_NE(b.error().to_string().find("source model"), std::string::npos) << b.error().to_string();
}

TEST_F(PackageAdditions, InvalidMonitorsAreRefusedAtBuildTime) {
    std::ofstream(dir_.path() / "monitors.json", std::ios::trunc) << R"json({"format": "twin-monitors/1", "monitors": [
        {"id": "x", "kind": "property", "name": "X", "severity": "fatal", "property": "A[] !FAULT"}]})json";
    Result<BuildResult> b = build_package(in_, dir_.path() / "pkg");
    ASSERT_FALSE(b);
    EXPECT_NE(b.error().to_string().find("TWN003"), std::string::npos) << b.error().to_string();
}

TEST_F(PackageAdditions, TamperedMonitorsFailVerification) {
    Result<BuildResult> b = build_package(in_, dir_.path() / "pkg");
    ASSERT_TRUE(b) << b.error().to_string();
    std::ofstream(dir_.path() / "pkg/monitors/monitors.json", std::ios::binary | std::ios::app) << " ";
    EXPECT_FALSE(load_and_verify(dir_.path() / "pkg"));
}

}  // namespace
}  // namespace twin::package
