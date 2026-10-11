// Acceptance tests of Blueprint Studio (twin::studio::BlueprintService): a twin built from scratch
// through the same use cases the Studio UI calls (the "Build Your First Twin" thermal chamber:
// sections, PT/DT views, ontology and interpretations, alignment, compilation, timing windows,
// scenario tests, package, publish, instance), UPPAAL import refusals, impact analysis, search,
// and a real deployment through the supervisor.
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <thread>

#include <httplib.h>

#include <array>

#include "twin/ingest/mqtt.hpp"
#include "twin/ingest/net.hpp"
#include "twin/studio/blueprints.hpp"
#include "twin/studio/seed.hpp"
#include "twin/studio/services.hpp"
#include "twin/studio/supervisor.hpp"

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

/// One Studio shared by the suite; every test uses its own Blueprint ids.
class BlueprintTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dir_ = new fs::path(fs::temp_directory_path() / ("twin-blueprint-test-" + std::to_string(std::random_device{}())));
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
    static Services& s() { return *services_; }
    static BlueprintService& bp() { return services_->blueprints(); }
    static inline Services* services_ = nullptr;
    static inline fs::path* dir_ = nullptr;
    Actor alice{"alice"};

    /// Revision of a version (sections are saved against it).
    static std::int64_t revision(const std::string& id, std::int64_t v) {
        return bp().version(id, v).value()["revision"].get<std::int64_t>();
    }

    /// Build the thermal chamber from scratch into Blueprint @p id (draft v1), step by step.
    void build_chamber(const std::string& id) {
        auto created = bp().create(Json{{"mode", "blank"}, {"id", id}, {"name", "Simple Thermal Chamber"}, {"domain", "thermal"}}, alice);
        ASSERT_TRUE(created.ok()) << created.error().to_string();
        const Json doc = read_json(kChamber / "blueprint.json");
        for (const char* section : {"identity", "structure", "world", "data", "connectivity", "presentation", "assurance",
                                    "simulation", "scenarios"}) {
            auto saved = bp().save_section(id, 1, section, revision(id, 1), doc.at(section), alice);
            ASSERT_TRUE(saved.ok()) << section << ": " << saved.error().to_string();
        }
        // PT view imported from UPPAAL; DT view saved as a canonical model (what the TA editor sends).
        auto pt = bp().import_model(id, 1, "pt", revision(id, 1), "chamber_pt.xml", read_file(kChamber / "models/chamber_pt.xml"), alice);
        ASSERT_TRUE(pt.ok()) << pt.error().to_string();
        ASSERT_TRUE(pt.value()["imported"].get<bool>()) << pt.value().dump(2);
        auto dt = bp().save_model(id, 1, "dt", revision(id, 1), read_json(kChamber / "models/chamber_dt.tta.json"), Json(nullptr), alice);
        ASSERT_TRUE(dt.ok()) << dt.error().to_string();
        for (const auto& [role, file] : std::vector<std::pair<std::string, std::string>>{
                 {"ontology", "models/thermal-chamber.ont"}, {"pt_interpretation", "models/pt.interp"}, {"dt_interpretation", "models/dt.interp"}}) {
            auto saved = bp().save_semantics(id, 1, role, revision(id, 1), Json{{"content", read_file(kChamber / file)}}, alice);
            ASSERT_TRUE(saved.ok()) << role << ": " << saved.error().to_string();
        }
    }
};

}  // namespace

TEST_F(BlueprintTest, TemplatesAndPalettesAreData) {
    auto t = bp().templates();
    ASSERT_TRUE(t.ok()) << t.error().to_string();
    std::set<std::string> ids;
    for (const Json& x : t.value()) ids.insert(x.value("id", std::string()));
    EXPECT_TRUE(ids.count("empty") && ids.count("mobile-robot") && ids.count("process-equipment")) << t.value().dump();
    auto p = bp().palettes();
    ASSERT_TRUE(p.ok()) << p.error().to_string();
    EXPECT_TRUE(p.value()["palettes"].contains("thermal"));
}

TEST_F(BlueprintTest, ThermalChamberFromScratchToInstance) {
    const std::string id = "chamber-scratch";
    build_chamber(id);

    // A stale revision is refused (two editors never overwrite each other silently).
    auto stale = bp().save_section(id, 1, "presentation", revision(id, 1) - 1, read_json(kChamber / "blueprint.json")["presentation"], alice);
    ASSERT_FALSE(stale.ok());
    EXPECT_EQ(stale.error().code, ErrorCode::StateError);

    // Every section is valid.
    auto val = bp().validate(id, 1);
    ASSERT_TRUE(val.ok()) << val.error().to_string();
    for (const Json& f : val.value()["findings"]) EXPECT_NE(f.value("severity", std::string()), "error") << f.dump();

    // Assurance runs the real tools: artefact validation, aligner, compiler, kernel scenario tests, package build.
    auto formal = bp().run_check(id, 1, "formal", Json::object(), alice);
    ASSERT_TRUE(formal.ok()) << formal.error().to_string();
    EXPECT_EQ(formal.value()["outcome"], "pass") << formal.value().dump(2).substr(0, 2000);
    auto align = bp().run_check(id, 1, "alignment", Json::object(), alice);
    ASSERT_TRUE(align.ok()) << align.error().to_string();
    EXPECT_EQ(align.value()["outcome"], "pass") << align.value().dump(2).substr(0, 2000);
    auto compile = bp().run_check(id, 1, "compile", Json::object(), alice);
    ASSERT_TRUE(compile.ok()) << compile.error().to_string();
    EXPECT_EQ(compile.value()["outcome"], "pass");
    auto tests = bp().run_check(id, 1, "scenarios", Json::object(), alice);
    ASSERT_TRUE(tests.ok()) << tests.error().to_string();
    EXPECT_EQ(tests.value()["outcome"], "pass") << tests.value().dump(2).substr(0, 4000);
    // The negative test passes because the kernel refuses the early reset, with an explanation.
    for (const Json& r : tests.value()["results"]) {
        if (r["id"] != "reset-too-early") continue;
        const Json& refused = r["steps"][2];
        EXPECT_EQ(refused["status"], "pass");
        EXPECT_FALSE(refused["explanation"].is_null()) << refused.dump(2);
    }

    // Timing windows come from the kernel: after an over-temperature, the reset is LATER, from 30 s.
    auto timing = bp().timing(id, 1, Json{{"start", {{"kind", "initial"}}},
                                          {"steps", Json::array({Json{{"kind", "event"}, {"label", "start_heating!"}, {"at", "5"}},
                                                                 Json{{"kind", "event"}, {"label", "high_temperature!"}, {"at", "100"}}})}});
    ASSERT_TRUE(timing.ok()) << timing.error().to_string();
    bool reset_later = false;
    for (const Json& a : timing.value()["final"]["availability"]) {
        if (a["label"] == "cooled_down!") {
            reset_later = a["status"] == "later" && a["intervals"][0]["earliest"]["text"] == "30" && a["intervals"][0]["latest"].is_null();
        }
    }
    EXPECT_TRUE(reset_later) << timing.value()["final"].dump(2);

    // Scenario Builder timing: a scenario's own steps. The negative-test step leaves the state as is
    // and the expectation at t = 110 lets time pass; origins name each formal step's source.
    Json early;
    const Json chamber_doc = read_json(kChamber / "blueprint.json");
    for (const Json& sc : chamber_doc["scenarios"]) {
        if (sc["id"] == "reset-too-early") early = sc;
    }
    ASSERT_FALSE(early.is_null());
    auto sct = bp().timing(id, 1, Json{{"scenarioSteps", early["steps"]}});
    ASSERT_TRUE(sct.ok()) << sct.error().to_string();
    EXPECT_EQ(sct.value()["origins"], Json::array({"s1", "s2", "s4"})) << sct.value()["origins"].dump();
    EXPECT_TRUE(sct.value()["first_invalid"].is_null());
    EXPECT_EQ(sct.value()["final"]["state"]["time"]["text"], "110");
    // The same reset as an ordinary step is refused with the earliest legal time the kernel reports.
    Json steps = early["steps"];
    steps[2].erase("expectRefused");
    auto refused = bp().timing(id, 1, Json{{"scenarioSteps", steps}});
    ASSERT_TRUE(refused.ok()) << refused.error().to_string();
    ASSERT_EQ(refused.value()["first_invalid"], 2) << refused.value().dump(2).substr(0, 2000);
    const Json& why = refused.value()["steps"][2]["explanation"];
    ASSERT_FALSE(why["alternatives"].empty()) << why.dump(2);
    EXPECT_EQ(why["alternatives"][0]["window"]["earliest_at"]["text"], "130") << why.dump(2);
    EXPECT_NE(why["reasons"][0]["reason"].get<std::string>().find("too early"), std::string::npos) << why.dump(2);

    // The release gate is computed from evidence; packaging makes it ready.
    auto pkg = bp().run_check(id, 1, "package", Json::object(), alice);
    ASSERT_TRUE(pkg.ok()) << pkg.error().to_string();
    EXPECT_EQ(pkg.value()["outcome"], "pass") << pkg.value().dump(2).substr(0, 2000);
    auto status = bp().status(id, 1);
    ASSERT_TRUE(status.ok()) << status.error().to_string();
    EXPECT_EQ(status.value()["readiness"]["verdict"], "ready") << status.value()["readiness"].dump(2);

    // The package view keeps the verified core apart from the deployment content.
    auto view = bp().package(id, 1);
    ASSERT_TRUE(view.ok()) << view.error().to_string();
    EXPECT_EQ(view.value()["verifiedCoreInputs"].size(), 5U);
    EXPECT_FALSE(view.value()["deploymentContent"].empty());
    EXPECT_FALSE(view.value()["package"].is_null());
    EXPECT_TRUE(view.value()["bundle"]["filesIntact"].get<bool>());

    // A build describes exactly one content of the draft. Saving the same content keeps it...
    const Json same_pres = read_json(kChamber / "blueprint.json")["presentation"];
    ASSERT_TRUE(bp().save_section(id, 1, "presentation", revision(id, 1), same_pres, alice).ok());
    EXPECT_EQ(bp().status(id, 1).value()["readiness"]["verdict"], "ready");
    // ...an edit detaches it: the gate asks for a rebuild and publishing is refused, so a stale
    // package or bundle can never be released.
    Json edited_pres = same_pres;
    edited_pres["states"]["IDLE"]["label"] = "Idle (ready)";
    ASSERT_TRUE(bp().save_section(id, 1, "presentation", revision(id, 1), edited_pres, alice).ok());
    EXPECT_TRUE(bp().package(id, 1).value()["package"].is_null());
    const Json detached = bp().status(id, 1).value()["readiness"];
    EXPECT_NE(detached["verdict"], "ready");
    bool rebuild_asked = false;
    for (const Json& item : detached["items"]) {
        if (item["id"] == "package") {
            EXPECT_EQ(item["state"], "not_run");
            rebuild_asked = item["detail"].get<std::string>().find("changed after its last package build") != std::string::npos;
        }
    }
    EXPECT_TRUE(rebuild_asked) << detached.dump(2);
    auto refused_pub = bp().publish(id, 1, alice);
    ASSERT_FALSE(refused_pub.ok());
    EXPECT_EQ(refused_pub.error().code, ErrorCode::StateError);
    // Scenario evidence is keyed on the document too: re-run, rebuild, ready again.
    ASSERT_EQ(bp().run_check(id, 1, "scenarios", Json::object(), alice).value()["outcome"], "pass");
    ASSERT_EQ(bp().run_check(id, 1, "package", Json::object(), alice).value()["outcome"], "pass");
    EXPECT_EQ(bp().status(id, 1).value()["readiness"]["verdict"], "ready");

    // Publish: the version becomes immutable.
    auto pub = bp().publish(id, 1, alice);
    ASSERT_TRUE(pub.ok()) << pub.error().to_string();
    auto frozen = bp().save_section(id, 1, "presentation", revision(id, 1), read_json(kChamber / "blueprint.json")["presentation"], alice);
    ASSERT_FALSE(frozen.ok());
    EXPECT_EQ(frozen.error().code, ErrorCode::StateError);

    // An instance gets its own assets and channels; the context asset binds to an estate asset.
    auto inst = bp().create_instance(Json{{"blueprintId", id}, {"version", 1}, {"id", "tc-01-dt"}, {"name", "TC-01"},
                                          {"assetIds", {{"chamber", "chamber-01"}, {"lab", "lab-m2"}}}},
                                     alice);
    ASSERT_TRUE(inst.ok()) << inst.error().to_string();
    auto twin = s().twin_record("tc-01-dt");
    ASSERT_TRUE(twin.ok());
    EXPECT_EQ(twin.value().blueprint_id.value_or(""), id);
    EXPECT_EQ(twin.value().asset_id.value_or(""), "chamber-01");
    std::set<std::string> channels;
    for (const Json& c : twin.value().instance_config["channels"]) channels.insert(c.get<std::string>());
    EXPECT_TRUE(channels.count("chamber-01.temperature")) << twin.value().instance_config.dump();
    auto chamber = s().asset("chamber-01");
    ASSERT_TRUE(chamber.ok()) << chamber.error().to_string();
    EXPECT_EQ(chamber.value()["parentId"], "lab-m2");

    // v2: a presentation-only change invalidates no formal evidence.
    auto draft = bp().create_draft(id, 1, "friendlier state names", alice);
    ASSERT_TRUE(draft.ok()) << draft.error().to_string();
    // The library shows the draft's release gate ("Verification required" until evidence covers it).
    bool listed = false;
    const Json library = bp().list().value();
    for (const Json& b : library) {
        if (b["id"] != id) continue;
        listed = true;
        EXPECT_EQ(b["draft"]["version"], 2);
        EXPECT_TRUE(b.contains("draftReadiness")) << b.dump(2);
        EXPECT_EQ(b["published"]["version"], 1);
    }
    EXPECT_TRUE(listed);
    Json pres = read_json(kChamber / "blueprint.json")["presentation"];
    pres["states"]["HEATING"]["label"] = "Heating up";
    ASSERT_TRUE(bp().save_section(id, 2, "presentation", revision(id, 2), pres, alice).ok());
    auto impact = bp().impact(id, 2, std::nullopt);
    ASSERT_TRUE(impact.ok()) << impact.error().to_string();
    EXPECT_FALSE(impact.value()["formal"]["alignmentStale"].get<bool>());
    EXPECT_FALSE(impact.value()["formal"]["irStale"].get<bool>());
    EXPECT_EQ(impact.value()["instances"].size(), 1U);
    auto status2 = bp().status(id, 2).value();
    for (const Json& item : status2["readiness"]["items"]) {
        if (item["id"] == "alignment") EXPECT_EQ(item["state"], "pass") << "evidence of the unchanged artefacts still applies";
    }

    // Editing the DT view in v2 makes the alignment stale.
    Json dt = read_json(kChamber / "models/chamber_dt.tta.json");
    dt["edges"][3]["guard"][0]["bound"] = 20;
    ASSERT_TRUE(bp().save_model(id, 2, "dt", revision(id, 2), dt, Json(nullptr), alice).ok());
    auto impact2 = bp().impact(id, 2, std::nullopt).value();
    EXPECT_TRUE(impact2["formal"]["alignmentStale"].get<bool>());
    EXPECT_TRUE(impact2["formal"]["irStale"].get<bool>());

    // Search reaches Blueprint elements: states, telemetry, monitors.
    auto hits = s().search("OVERHEATED", 50).value()["hits"];
    bool state = false;
    for (const Json& h : hits) state = state || (h["kind"] == "state" && h["route"].get<std::string>().find("/studio/blueprints/chamber-scratch/") == 0);
    EXPECT_TRUE(state) << hits.dump(2);
    auto monitors = s().search("never-overheated", 10).value()["hits"];
    ASSERT_FALSE(monitors.empty());
    EXPECT_EQ(monitors[0]["kind"], "monitor");
}

TEST_F(BlueprintTest, MonitorsAreValidatedAgainstThePinnedViews) {
    const std::string id = "chamber-monitor-views";
    build_chamber(id);
    // A property over a state the Digital Twin View does not have, and a conformance monitor on a
    // label the Physical System View never emits: both are release-blocking assurance errors.
    Json assurance = read_json(kChamber / "blueprint.json")["assurance"];
    for (Json& m : assurance["monitors"]) {
        if (m["id"] == "never-overheated") m["property"] = "A[] !MELTDOWN";
        if (m["id"] == "conformance") m["events"] = Json::array({"heater_on!", "door_slammed!"});
    }
    ASSERT_TRUE(bp().save_section(id, 1, "assurance", revision(id, 1), assurance, alice).ok());
    const Json val = bp().validate(id, 1).value();
    std::set<std::string> flagged;
    for (const Json& f : val["findings"]) {
        if (f["section"] == "assurance" && f["severity"] == "error") flagged.insert(f["target"].get<std::string>());
    }
    EXPECT_TRUE(flagged.count("never-overheated")) << val.dump(2);
    EXPECT_TRUE(flagged.count("conformance")) << val.dump(2);
    bool gate_fails = false;
    const Json readiness = bp().status(id, 1).value()["readiness"];
    for (const Json& item : readiness["items"]) {
        if (item["id"] == "monitors") gate_fails = item["state"] == "fail";
    }
    EXPECT_TRUE(gate_fails) << readiness.dump(2);
}

TEST_F(BlueprintTest, CreationTakesIdentityAndRasterNeedsASpatialWorld) {
    auto created = bp().create(Json{{"mode", "blank"}, {"id", "ident-check"}, {"name", "Identity check"},
                                    {"identity", {{"runtimeMode", "cosimulation"}, {"timeUnit", "ms"}, {"ticksPerUnit", 1}, {"modelId", "ignored"}}}},
                               alice);
    ASSERT_TRUE(created.ok()) << created.error().to_string();
    const Json identity = bp().version("ident-check", 1).value()["document"]["identity"];
    EXPECT_EQ(identity["runtimeMode"], "cosimulation");
    EXPECT_EQ(identity["timeUnit"], "ms");
    EXPECT_EQ(identity["ticksPerUnit"], 1);
    EXPECT_EQ(identity["modelId"], "ident-check") << "the model id is the Blueprint's, not a client choice";
    Json world = bp().version("ident-check", 1).value()["document"]["world"];
    world["mode"] = "topology";
    ASSERT_TRUE(bp().save_section("ident-check", 1, "world", revision("ident-check", 1), world, alice).ok());
    auto raster = bp().world_raster("ident-check", 1);
    ASSERT_FALSE(raster.ok());
    EXPECT_EQ(raster.error().code, ErrorCode::StateError);
}

TEST_F(BlueprintTest, UnsupportedUppaalConstructsAreReportedNeverApproximated) {
    ASSERT_TRUE(bp().create(Json{{"mode", "blank"}, {"id", "bad-import"}, {"name", "Bad import"}}, alice).ok());
    const std::int64_t before = revision("bad-import", 1);
    const std::string xml = R"(<?xml version="1.0" encoding="utf-8"?>
<nta>
  <declaration>clock x; int counter; chan go;</declaration>
  <template>
    <name>Counter</name>
    <location id="A"><name>A</name><committed/></location>
    <location id="B"><name>B</name></location>
    <init ref="A"/>
    <transition><source ref="A"/><target ref="B"/>
      <label kind="synchronisation">go!</label>
      <label kind="assignment">counter := counter + 1</label>
    </transition>
  </template>
  <system>system Counter;</system>
</nta>)";
    auto r = bp().import_model("bad-import", 1, "dt", before, "counter.xml", xml, alice);
    ASSERT_TRUE(r.ok()) << r.error().to_string();
    EXPECT_FALSE(r.value()["imported"].get<bool>());
    bool unsupported = false;
    for (const Json& d : r.value()["report"]["diagnostics"]) unsupported = unsupported || d["severity"] == "error";
    EXPECT_TRUE(unsupported) << r.value().dump(2);
    EXPECT_EQ(revision("bad-import", 1), before) << "nothing is saved when the import is refused";
}

TEST_F(BlueprintTest, SeededInstanceDeploysRealProcesses) {
    if (!s().supervisor().available()) GTEST_SKIP() << "runtime binaries not built next to the tests";
    SeedOptions options;
    options.telemetry_history = false;
    auto seeded = seed_example(s(), kChamber, options);
    ASSERT_TRUE(seeded.ok()) << seeded.error().to_string();
    for (const char* check : {"alignment", "compile", "scenarios", "package"}) {
        EXPECT_EQ(seeded.value()["checks"][check], "pass") << check;
    }
    auto deployed = bp().deploy_instance("chamber-tc1-dt", Json::object(), alice);
    ASSERT_TRUE(deployed.ok()) << deployed.error().to_string();
    EXPECT_EQ(deployed.value()["runtime"]["state"], "running") << deployed.value().dump(2);
    const std::string v1_package = bp().version("simple-thermal-chamber", 1).value()["packageId"].get<std::string>();
    EXPECT_EQ(deployed.value()["runtime"]["packageId"], v1_package) << "the supervisor reports the package it runs";
    // The feed drives the runtime; live telemetry reaches the instance's channels through the bridge.
    bool live = false;
    for (int i = 0; i < 100 && !live; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto ch = s().telemetry_channels("chamber-tc1", true);
        if (!ch) continue;
        for (const Json& c : ch.value()["channels"]) {
            if (c["id"] == "chamber-tc1.temperature" && !c.value("latest", Json()).is_null()) live = true;
        }
    }
    EXPECT_TRUE(live) << "no live temperature sample arrived";
    // Monitors are evaluated live by their authorities.
    auto mon = bp().instance_monitors("chamber-tc1-dt");
    ASSERT_TRUE(mon.ok()) << mon.error().to_string();
    EXPECT_TRUE(mon.value()["runtime"]["available"].get<bool>()) << mon.value()["runtime"].dump();
    std::map<std::string, Json> by_id;
    for (const Json& m : mon.value()["monitors"]) by_id[m["id"].get<std::string>()] = m;
    EXPECT_EQ(by_id["conformance"]["evaluator"], "runtime");
    EXPECT_TRUE(by_id["conformance"]["status"] == "satisfied" || by_id["conformance"]["status"] == "violated") << by_id["conformance"].dump();
    EXPECT_EQ(by_id["never-overheated"]["evaluator"], "property evaluator");
    EXPECT_TRUE(by_id["never-overheated"]["status"] == "satisfied" || by_id["never-overheated"]["status"] == "violated") << by_id["never-overheated"].dump();
    EXPECT_EQ(by_id["temperature-fresh"]["status"], "satisfied") << by_id["temperature-fresh"].dump();
    EXPECT_EQ(mon.value()["alerts"].size(), 3U);
    auto stopped = bp().control_instance("chamber-tc1-dt", "stop", alice);
    ASSERT_TRUE(stopped.ok()) << stopped.error().to_string();
    EXPECT_EQ(bp().instance("chamber-tc1-dt").value()["runtime"]["state"], "stopped");
    auto offline = bp().instance_monitors("chamber-tc1-dt").value();
    EXPECT_FALSE(offline["runtime"]["available"].get<bool>());
    for (const Json& m : offline["monitors"]) {
        if (m["id"] == "conformance") EXPECT_EQ(m["status"], "unknown") << "never a guessed verdict without the runtime";
    }

    // A draft previews in isolation: sandbox build of its core, real runtime and feed, nothing recorded.
    const std::size_t packages_before = s().packages("").value().size();
    ASSERT_TRUE(bp().create_draft("simple-thermal-chamber", 1, "preview", alice).ok());
    auto preview = bp().start_preview("simple-thermal-chamber", 2, Json::object(), alice);
    ASSERT_TRUE(preview.ok()) << preview.error().to_string();
    EXPECT_EQ(preview.value()["previewId"], "preview~simple-thermal-chamber~v2");
    EXPECT_EQ(preview.value()["runtime"]["state"], "running");
    EXPECT_EQ(preview.value()["core"]["checksFailed"].size(), 0U);
    EXPECT_EQ(s().packages("").value().size(), packages_before) << "a preview records no package";
    EXPECT_FALSE(s().twin_record("preview~simple-thermal-chamber~v2").ok()) << "a preview is not a twin";
    auto ended = bp().stop_preview("simple-thermal-chamber", 2, alice);
    ASSERT_TRUE(ended.ok());
    EXPECT_EQ(ended.value()["runtime"]["state"], "stopped");

    // What is recorded as deployed is what runs. Publish v2 (its own package), then deploy that
    // package through the deployment API (as a change release does): the running instance restarts
    // on it; a plain start resumes it; an explicit version deploys that version's package again.
    for (const char* check : {"scenarios", "package"}) {
        auto r = bp().run_check("simple-thermal-chamber", 2, check, Json::object(), alice);
        ASSERT_TRUE(r.ok()) << check << ": " << r.error().to_string();
        ASSERT_EQ(r.value()["outcome"], "pass") << check << ": " << r.value().dump(2).substr(0, 1500);
    }
    ASSERT_TRUE(bp().publish("simple-thermal-chamber", 2, alice).ok());
    const std::string v2_package = bp().version("simple-thermal-chamber", 2).value()["packageId"].get<std::string>();
    ASSERT_NE(v2_package, v1_package);
    auto restarted = bp().control_instance("chamber-tc1-dt", "start", alice);
    ASSERT_TRUE(restarted.ok()) << restarted.error().to_string();
    EXPECT_EQ(restarted.value()["runtime"]["packageId"], v1_package) << "start resumes the current deployment";
    auto via_api = s().deploy("chamber-tc1-dt", v2_package, "deployed through the deployment API", alice);
    ASSERT_TRUE(via_api.ok()) << via_api.error().to_string();
    EXPECT_TRUE(via_api.value()["instance"]["restarted"].get<bool>()) << via_api.value().dump(2);
    EXPECT_EQ(bp().instance("chamber-tc1-dt").value()["runtime"]["packageId"], v2_package);
    auto resumed = bp().control_instance("chamber-tc1-dt", "start", alice);
    ASSERT_TRUE(resumed.ok()) << resumed.error().to_string();
    EXPECT_EQ(resumed.value()["runtime"]["packageId"], v2_package) << "a start never silently replaces the deployed package";
    auto back = bp().deploy_instance("chamber-tc1-dt", Json{{"version", 1}, {"reason", "back to v1"}}, alice);
    ASSERT_TRUE(back.ok()) << back.error().to_string();
    EXPECT_EQ(back.value()["runtime"]["packageId"], v1_package);
    ASSERT_TRUE(bp().control_instance("chamber-tc1-dt", "stop", alice).ok());
}

TEST_F(BlueprintTest, PlantConnectionFeedsADeployedInstance) {
    if (!s().supervisor().available()) GTEST_SKIP() << "runtime binaries not built next to the tests";
    SeedOptions options;
    options.telemetry_history = false;
    auto seeded = seed_example(s(), fs::path(TWIN_SOURCE_DIR) / "examples" / "industrial-pump", options);
    ASSERT_TRUE(seeded.ok()) << seeded.error().to_string();

    // A plant-facing port inside the runtimes' range is refused before anything is created.
    auto bad = bp().create_instance(Json{{"blueprintId", "centrifugal-pump"}, {"version", 1}, {"id", "pump-p199-dt"},
                                         {"connection", {{"rest", {{"enabled", true}, {"port", 18500}}}}}}, alice);
    EXPECT_FALSE(bad.ok());
    EXPECT_FALSE(s().twin_record("pump-p199-dt").ok());

    // REST and MQTT endpoints on automatic ports; the simulator no longer feeds this instance.
    const Json connection{{"rest", {{"enabled", true}}}, {"mqtt", {{"enabled", true}}}, {"timestamp", {{"field", "ts"}, {"unit", "ms"}}}};
    auto inst = bp().create_instance(Json{{"blueprintId", "centrifugal-pump"}, {"version", 1}, {"id", "pump-p102-dt"}, {"name", "Pump P-102"},
                                          {"connection", connection}}, alice);
    ASSERT_TRUE(inst.ok()) << inst.error().to_string();
    const Json cfg = inst.value()["instanceConfig"];
    ASSERT_TRUE(cfg["connection"]["rest"]["port"].is_number_integer());
    EXPECT_NE(cfg["connection"]["rest"]["port"], cfg["connection"]["mqtt"]["port"]);
    EXPECT_EQ(cfg["connection"]["token"].get<std::string>().size(), 48U);
    for (const Json& src : cfg["connectivity"]["sources"]) EXPECT_NE(src["kind"], "simulator");

    // The plant's own vocabulary, bound for this instance: tag TT101_degC, event code START.
    Json bindings = Json::array();
    for (Json b : cfg["connectivity"]["bindings"]) {
        if (b["target"]["id"] == "bearing_temp") b["select"] = Json{{"field", "TT101_degC"}};
        bindings.push_back(b);
    }
    bindings.push_back(Json{{"id", "b-start"}, {"target", {{"kind", "event"}, {"id", "start_command"}}}, {"source", "plc-mqtt"},
                            {"select", {{"field", "event"}, {"value", "START"}}}});
    Json update = connection;
    update["bindings"] = bindings;
    auto updated = bp().update_connection("pump-p102-dt", update, alice);
    ASSERT_TRUE(updated.ok()) << updated.error().to_string();
    const Json info = updated.value();
    EXPECT_TRUE(info["configured"].get<bool>());
    EXPECT_EQ(info["rest"]["port"], cfg["connection"]["rest"]["port"]) << "ports are kept across edits";
    EXPECT_NE(info["examples"]["curl"].get<std::string>().find("Authorization: Bearer"), std::string::npos);
    EXPECT_NE(info["examples"]["mosquittoPub"].get<std::string>().find("-t twins/pump-p102-dt/data"), std::string::npos);
    bool bound = false;
    for (const Json& t : info["contract"]["telemetry"]) bound = bound || (t["id"] == "bearing_temp" && t["field"] == "TT101_degC");
    EXPECT_TRUE(bound) << info["contract"].dump();

    // Plant names, as the UI sends them: codes per event (the START binding above is kept).
    Json named = connection;
    named["names"] = Json{{"events", {{"halted", Json::array({"STOPPED"})}}}};
    auto renamed = bp().update_connection("pump-p102-dt", named, alice);
    ASSERT_TRUE(renamed.ok()) << renamed.error().to_string();
    std::map<std::string, Json> codes;
    for (const Json& e : renamed.value()["contract"]["events"]) codes[e["id"].get<std::string>()] = e["codes"];
    EXPECT_EQ(codes["halted"], Json::array({"STOPPED"}));
    EXPECT_EQ(codes["start_command"], Json::array({"START"}));

    // Deploy: the runtime and the plant connection start together.
    auto deployed = bp().deploy_instance("pump-p102-dt", Json::object(), alice);
    ASSERT_TRUE(deployed.ok()) << deployed.error().to_string();
    std::set<std::string> names;
    for (const Json& p : deployed.value()["runtime"]["processes"]) names.insert(p["name"].get<std::string>());
    EXPECT_EQ(names, (std::set<std::string>{"twin-runtime", "twin-ingest"}));

    // REST: the plant posts in its own vocabulary; the kernel decides.
    const std::string token = info["token"].get<std::string>();
    httplib::Client rest("127.0.0.1", info["rest"]["port"].get<int>());
    rest.set_read_timeout(10, 0);
    EXPECT_EQ(rest.Post("/v1/messages", R"({"event": "START"})", "application/json")->status, 401);
    auto r = rest.Post("/v1/messages", httplib::Headers{{"Authorization", "Bearer " + token}},
                       R"({"ts": 1000, "event": "START", "TT101_degC": 71.5})", "application/json");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->status, 200) << r->body;
    const Json verdict = json::parse(r->body).value()["results"][0];
    ASSERT_EQ(verdict["events"].size(), 1U) << verdict.dump();
    EXPECT_TRUE(verdict["events"][0]["accepted"].get<bool>()) << verdict.dump();
    EXPECT_EQ(verdict["events"][0]["to"], "NORMAL");

    // MQTT: the same twin through its own endpoint (password = token).
    auto sock = ingest::net::connect_tcp("127.0.0.1", info["mqtt"]["port"].get<int>(), std::chrono::milliseconds(3000));
    ASSERT_TRUE(sock.ok()) << sock.error().to_string();
    ingest::mqtt::Connect hello;
    hello.client_id = "plc-p102";
    hello.username = "pump-p102-dt";
    hello.password = token;
    ASSERT_TRUE(ingest::net::write_all(sock.value().fd(), ingest::mqtt::connect(hello)).ok());
    ASSERT_TRUE(ingest::net::write_all(sock.value().fd(), ingest::mqtt::publish({"twins/pump-p102-dt/data", R"({"ts": 2000, "TT101_degC": 72.0})", 1, false, false, 1})).ok());
    ingest::mqtt::FrameReader reader;
    std::vector<ingest::mqtt::Type> got;
    std::array<char, 512> buf{};
    for (int i = 0; i < 50 && got.size() < 2; ++i) {
        auto rd = ingest::net::read_for(sock.value().fd(), buf.data(), buf.size(), std::chrono::milliseconds(100));
        if (rd.ok() && rd.value().kind == ingest::net::ReadResult::Kind::Data) reader.feed(std::string_view(buf.data(), rd.value().bytes));
        for (auto p = reader.next(); p.ok() && p.value(); p = reader.next()) got.push_back(p.value()->type);
    }
    EXPECT_EQ(got, (std::vector<ingest::mqtt::Type>{ingest::mqtt::Type::Connack, ingest::mqtt::Type::Puback}));

    // Both samples reach the instance's channel; Studio shows the connection's live status.
    bool stored = false;
    for (int i = 0; i < 100 && !stored; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto series = s().telemetry_series("pump-p102-dt.bearing_temp", 0, std::int64_t{1} << 50, 1000);
        stored = series.ok() && series.value()["samples"].size() >= 2;
    }
    EXPECT_TRUE(stored) << "the plant's samples did not reach the channel";
    const Json status = bp().connection("pump-p102-dt").value()["status"];
    ASSERT_TRUE(status.is_object());
    EXPECT_EQ(status["counters"]["eventsAccepted"], 1);
    EXPECT_GE(status["counters"]["telemetrySamples"].get<int>(), 2);

    // A new token locks the old one out; the runtime keeps running.
    auto rotated = bp().rotate_connection_token("pump-p102-dt", alice);
    ASSERT_TRUE(rotated.ok()) << rotated.error().to_string();
    EXPECT_NE(rotated.value()["token"], token);
    EXPECT_EQ(rest.Post("/v1/messages", httplib::Headers{{"Authorization", "Bearer " + token}}, R"({"TT101_degC": 1})", "application/json")->status, 401);
    EXPECT_EQ(bp().instance("pump-p102-dt").value()["runtime"]["state"], "running");
    ASSERT_TRUE(bp().control_instance("pump-p102-dt", "stop", alice).ok());
}
