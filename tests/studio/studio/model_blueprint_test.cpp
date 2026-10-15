// Model-driven Blueprints (twin::studio::BlueprintService with a `model` section): one twin model,
// everything formal derived and pinned by the server, the unchanged release gate on top; derived
// parts refused for direct edits; legacy Blueprints lifted into a model and adopted.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "twin/authoring/import.hpp"
#include "twin/studio/blueprints.hpp"
#include "twin/studio/services.hpp"

namespace fs = std::filesystem;
using namespace twin;
using namespace twin::studio;
using json::Json;

namespace {

const fs::path kChamber = fs::path(TWIN_SOURCE_DIR) / "examples" / "thermal-chamber";

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Json read_json(const fs::path& p) { return json::parse(read_file(p)).value(); }

/// The chamber model with the controller's UPPAAL model as its physical reference.
Json chamber_model_with_reference() {
    Json model = read_json(kChamber / "model.json");
    authoring::ImportOptions o;
    o.filename = "chamber_pt.xml";
    o.legacy_system_declaration = true;
    const auto pt = authoring::import_any(read_file(kChamber / "models/chamber_pt.xml"), o);
    EXPECT_TRUE(pt.model.has_value());
    model["reference"] = {{"automaton", authoring::to_json(*pt.model)},
                          {"states", {{"IDLE", {{"states", {"IDLE"}}}}, {"HEATING", {{"states", {"HEATING"}}}}, {"OVERHEATED", {{"states", {"OVERHEATED"}}}}}},
                          {"labels", {{"heater_on", {{"trigger", "start_heating"}}}, {"heater_off", {{"trigger", "stop_heating"}}},
                                      {"overheat", {{"trigger", "high_temperature"}}}, {"latch_reset", {{"trigger", "cooled_down"}}}}}};
    return model;
}

class ModelBlueprintTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dir_ = new fs::path(fs::temp_directory_path() / ("twin-model-blueprint-test-" + std::to_string(std::random_device{}())));
        StudioConfig config;
        config.data_dir = *dir_;
        config.templates_dir = fs::path(TWIN_SOURCE_DIR) / "examples" / "templates";
        config.bin_dir = fs::path(TWIN_BINARY_DIR) / "bin";
        auto s = Services::open(config);
        ASSERT_TRUE(s.ok()) << s.error().to_string();
        services_ = s.value().release();
    }
    static void TearDownTestSuite() {
        delete services_;
        std::error_code ec;
        fs::remove_all(*dir_, ec);
        delete dir_;
    }
    static BlueprintService& bp() { return services_->blueprints(); }
    static std::int64_t revision(const std::string& id, std::int64_t v) { return bp().version(id, v).value()["revision"].get<std::int64_t>(); }
    static inline Services* services_ = nullptr;
    static inline fs::path* dir_ = nullptr;
    Actor alice{"alice"};
};

}  // namespace

TEST_F(ModelBlueprintTest, OneModelDerivesEverythingAndReachesTheReleaseGate) {
    auto created = bp().create(Json{{"mode", "model"}, {"id", "chamber-model"}, {"name", "Chamber (model)"}, {"model", chamber_model_with_reference()}}, alice);
    ASSERT_TRUE(created.ok()) << created.error().to_string();
    ASSERT_TRUE(created.value()["model"]["derived"].get<bool>()) << created.value()["model"].dump(2);

    // Every formal role is pinned from the model; the derived sections are in the document.
    const Json version = bp().version("chamber-model", 1).value();
    for (const char* role : {"pt_model", "dt_model", "ontology", "pt_interpretation", "dt_interpretation"}) {
        EXPECT_TRUE(version["artifacts"].contains(role)) << role;
    }
    const Json& doc = version["document"];
    EXPECT_EQ(doc["structure"]["root"], "thermal_chamber");
    EXPECT_FALSE(doc["data"]["telemetry"].empty());
    EXPECT_TRUE(doc["presentation"]["states"].contains("OVERHEATED"));

    // The unchanged release machinery runs on the derived artefacts.
    for (const char* check : {"formal", "alignment", "compile", "scenarios", "package"}) {
        auto r = bp().run_check("chamber-model", 1, check, Json::object(), alice);
        ASSERT_TRUE(r.ok()) << check << ": " << r.error().to_string();
        EXPECT_EQ(r.value()["outcome"], "pass") << check << ": " << r.value().dump(2);
    }
    auto status = bp().status("chamber-model", 1);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ(status.value()["readiness"]["verdict"], "ready") << status.value()["readiness"].dump(2);

    // Generated representations are served for inspection.
    auto gen = bp().generated("chamber-model", 1);
    ASSERT_TRUE(gen.ok()) << gen.error().to_string();
    EXPECT_NE(gen.value()["artifacts"]["ontology"].get<std::string>().find("fun probe__temperature"), std::string::npos);
    EXPECT_NE(gen.value()["artifacts"]["automatonXml"].get<std::string>().find("<nta>"), std::string::npos);
}

TEST_F(ModelBlueprintTest, DerivedPartsAreNotEditedDirectly) {
    auto created = bp().create(Json{{"mode", "model"}, {"id", "lamp-model"}, {"name", "Lamp"},
                                    {"identity", {{"timeUnit", "ms"}, {"ticksPerUnit", 1}}}}, alice);
    ASSERT_TRUE(created.ok()) << created.error().to_string();
    // A new model starts with a root type named after the model and the standard relationship vocabulary.
    const Json m = bp().model_document("lamp-model", 1).value()["model"];
    EXPECT_EQ(m["root"], "Lamp");
    EXPECT_EQ(m["time"]["unit"], "ms");
    EXPECT_EQ(m["time"]["ticksPerUnit"], 1);
    EXPECT_FALSE(m["relationshipTypes"].empty());

    auto data = bp().save_section("lamp-model", 1, "data", revision("lamp-model", 1), Json::object(), alice);
    ASSERT_FALSE(data.ok());
    EXPECT_EQ(data.error().context_value("reason"), "derived_section");
    auto ont = bp().save_semantics("lamp-model", 1, "ontology", revision("lamp-model", 1), Json{{"content", "sort X"}}, alice);
    ASSERT_FALSE(ont.ok());
    EXPECT_EQ(ont.error().context_value("reason"), "derived_artifact");
    // World and connectivity stay editable.
    auto conn = bp().save_section("lamp-model", 1, "connectivity", revision("lamp-model", 1), Json{{"sources", Json::array()}, {"bindings", Json::array()}}, alice);
    EXPECT_TRUE(conn.ok()) << (conn.ok() ? "" : conn.error().to_string());
}

TEST_F(ModelBlueprintTest, AModelWithErrorsIsKeptButDerivesNothing) {
    ASSERT_TRUE(bp().create(Json{{"mode", "model"}, {"id", "broken-model"}, {"name", "Broken"}}, alice).ok());
    Json m = bp().model_document("broken-model", 1).value()["model"];
    m["types"][0]["behavior"] = Json::parse(R"({"states": [{"id": "s1", "name": "A"}], "transitions": []})");  // no initial state
    auto saved = bp().save_model_document("broken-model", 1, revision("broken-model", 1), m, alice);
    ASSERT_TRUE(saved.ok()) << saved.error().to_string();
    EXPECT_FALSE(saved.value()["derived"].get<bool>());
    const Json back = bp().model_document("broken-model", 1).value();
    EXPECT_EQ(back["model"], m);  // the user's work is stored
    bool has_tml050 = false;
    for (const Json& d : back["diagnostics"]) has_tml050 = has_tml050 || d["code"] == "TML050";
    EXPECT_TRUE(has_tml050);
}

TEST_F(ModelBlueprintTest, InvalidEditClearsPreviouslyGeneratedOutputs) {
    ASSERT_TRUE(bp().create(Json{{"mode", "model"}, {"id", "chamber-invalid-edit"}, {"name", "Chamber"},
                                 {"model", chamber_model_with_reference()}}, alice).ok());
    Json m = bp().model_document("chamber-invalid-edit", 1).value()["model"];
    for (Json& t : m["types"]) {
        if (!t.contains("behavior")) continue;
        for (Json& s : t["behavior"]["states"]) s.erase("initial");
    }
    auto saved = bp().save_model_document("chamber-invalid-edit", 1, revision("chamber-invalid-edit", 1), m, alice);
    ASSERT_TRUE(saved.ok()) << saved.error().to_string();
    EXPECT_FALSE(saved.value()["derived"].get<bool>());
    const Json v = bp().version("chamber-invalid-edit", 1).value();
    EXPECT_TRUE(v["pins"].empty()) << v["pins"].dump();
    EXPECT_TRUE(v["document"]["structure"]["assets"].empty());
    EXPECT_TRUE(v["document"]["data"]["telemetry"].empty());
    const Json status = bp().status("chamber-invalid-edit", 1).value();
    EXPECT_EQ(status["readiness"]["verdict"], "blocked");
}

TEST_F(ModelBlueprintTest, ALegacyBlueprintLiftsIntoAModelThatAlignsAgain) {
    // The tutorial chamber built the old way: sections plus five hand-written formal artefacts.
    const std::string id = "chamber-legacy";
    ASSERT_TRUE(bp().create(Json{{"mode", "blank"}, {"id", id}, {"name", "Simple Thermal Chamber"}}, alice).ok());
    const Json doc = read_json(kChamber / "blueprint.json");
    for (const char* section : {"identity", "structure", "data", "presentation", "assurance"}) {
        ASSERT_TRUE(bp().save_section(id, 1, section, revision(id, 1), doc.at(section), alice).ok()) << section;
    }
    ASSERT_TRUE(bp().import_model(id, 1, "pt", revision(id, 1), "chamber_pt.xml", read_file(kChamber / "models/chamber_pt.xml"), alice).ok());
    ASSERT_TRUE(bp().save_model(id, 1, "dt", revision(id, 1), read_json(kChamber / "models/chamber_dt.tta.json"), Json(nullptr), alice).ok());
    for (const auto& [role, file] : std::vector<std::pair<std::string, std::string>>{
             {"ontology", "models/thermal-chamber.ont"}, {"pt_interpretation", "models/pt.interp"}, {"dt_interpretation", "models/dt.interp"}}) {
        ASSERT_TRUE(bp().save_semantics(id, 1, role, revision(id, 1), Json{{"content", read_file(kChamber / file)}, {"validate", false}}, alice).ok());
    }

    auto lifted = bp().lift_model(id, 1, Json::object());
    ASSERT_TRUE(lifted.ok()) << lifted.error().to_string();
    ASSERT_TRUE(lifted.value()["roundTrip"].get<bool>()) << lifted.value().dump(2);
    const Json model = lifted.value()["model"];
    // The physical reference comes with the data contract's label pairs.
    EXPECT_EQ(model["reference"]["labels"]["heater_on"]["trigger"], "start_heating");

    // Adopting it: the Blueprint becomes model-driven and the evidence is produced again.
    auto saved = bp().save_model_document(id, 1, revision(id, 1), model, alice);
    ASSERT_TRUE(saved.ok()) << saved.error().to_string();
    ASSERT_TRUE(saved.value()["derived"].get<bool>()) << saved.value().dump(2);
    auto align = bp().run_check(id, 1, "alignment", Json::object(), alice);
    ASSERT_TRUE(align.ok()) << align.error().to_string();
    EXPECT_EQ(align.value()["outcome"], "pass") << align.value().dump(2);
}
