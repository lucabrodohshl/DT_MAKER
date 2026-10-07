/**
 * @file scene_test.cpp
 * @brief World & Layout documents (twin-world/1) and the mobile-robot simulation adapter.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "twin/scene/robot_sim.hpp"
#include "twin/scene/world.hpp"
#include "twin/world/scenario.hpp"

namespace twin::scene {
namespace {

using json::Json;
namespace fs = std::filesystem;

Json read_json(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream s;
    s << in.rdbuf();
    auto j = json::parse(s.str());
    EXPECT_TRUE(j.ok()) << p;
    return j.ok() ? j.value() : Json();
}

Json small_world() {
    Json w = empty_world(5000, 3000);  // 10 x 6 cells of 500 mm
    w["layers"] = Json::array({Json{{"id", "s"}, {"name", "Shared"}, {"role", "shared"}},
                               Json{{"id", "gt"}, {"name", "Truth"}, {"role", "ground-truth"}},
                               Json{{"id", "k"}, {"name", "Plan"}, {"role", "knowledge"}},
                               Json{{"id", "n"}, {"name", "Notes"}, {"role", "annotation"}}});
    w["objects"] = Json::array({
        Json{{"id", "floor"}, {"layer", "s"}, {"kind", "rect"}, {"semanticType", "floor"},
             {"geometry", {{"x", 500}, {"y", 500}, {"w", 4000}, {"h", 2000}}}},
        // A stroked wall line along the centres of row 3 (y = 1750), columns 1..8.
        Json{{"id", "wall"}, {"layer", "s"}, {"kind", "line"}, {"semanticType", "wall"},
             {"geometry", {{"points", Json::array({Json::array({750, 1750}), Json::array({4250, 1750})})}, {"width", 100}}}},
        Json{{"id", "door"}, {"layer", "gt"}, {"kind", "rect"}, {"semanticType", "door"},
             {"geometry", {{"x", 2000}, {"y", 1500}, {"w", 500}, {"h", 500}}}, {"properties", {{"state", "closed"}}}},
        Json{{"id", "door-plan"}, {"layer", "k"}, {"kind", "rect"}, {"semanticType", "door"},
             {"geometry", {{"x", 2000}, {"y", 1500}, {"w", 500}, {"h", 500}}}, {"properties", {{"state", "open"}}}},
        // A triangle obstacle covering the centres of cells (6,1), (7,1) and (6,2).
        Json{{"id", "tri"}, {"layer", "s"}, {"kind", "polygon"}, {"semanticType", "obstacle"},
             {"geometry", {{"points", Json::array({Json::array({3000, 500}), Json::array({4000, 500}), Json::array({3000, 1500})})}}}},
        Json{{"id", "unknown"}, {"layer", "k"}, {"kind", "rect"}, {"semanticType", "unknown"},
             {"geometry", {{"x", 500}, {"y", 2000}, {"w", 1000}, {"h", 500}}}},
        Json{{"id", "start"}, {"layer", "n"}, {"kind", "point"}, {"semanticType", "start"}, {"name", "Pad"},
             {"geometry", {{"x", 750}, {"y", 750}}}},
        Json{{"id", "goal"}, {"layer", "n"}, {"kind", "point"}, {"semanticType", "target"}, {"name", "Valve"},
             {"geometry", {{"x", 4250}, {"y", 750}}}, {"properties", {{"targetId", "T1"}}}},
        Json{{"id", "label"}, {"layer", "n"}, {"kind", "zone"}, {"semanticType", "room"}, {"name", "Room"},
             {"geometry", {{"x", 500}, {"y", 500}, {"w", 4000}, {"h", 2000}}}},
    });
    return w;
}

TEST(SceneWorld, DecodesAndRefusesMalformedDocuments) {
    auto ok = world_from_json(small_world());
    ASSERT_TRUE(ok) << ok.error().to_string();
    EXPECT_EQ(ok.value().objects.size(), 9U);
    EXPECT_EQ(ok.value().layer("gt")->role, LayerRole::GroundTruth);
    EXPECT_TRUE(validate(ok.value()).empty()) << to_json(validate(ok.value())).dump(1);

    Json w = small_world();
    w["format"] = "twin-world/0";
    EXPECT_FALSE(world_from_json(w));
    w = small_world();
    w["objects"][0]["geometry"]["x"] = 500.5;
    auto floats = world_from_json(w);
    ASSERT_FALSE(floats);
    EXPECT_NE(floats.error().message.find("floating-point"), std::string::npos);
    w = small_world();
    w["objects"][0]["kind"] = "sphere";
    EXPECT_FALSE(world_from_json(w));
    w = small_world();
    w["layers"][0]["role"] = "secret";
    EXPECT_FALSE(world_from_json(w));
}

TEST(SceneWorld, ValidationFindsReferenceAndGeometryProblems) {
    Json w = small_world();
    w["objects"].push_back(Json{{"id", "floor"}, {"layer", "s"}, {"kind", "point"}, {"geometry", {{"x", 1}, {"y", 1}}}});
    w["objects"].push_back(Json{{"id", "lost"}, {"layer", "nowhere"}, {"kind", "point"}, {"geometry", {{"x", 1}, {"y", 1}}}});
    w["objects"].push_back(Json{{"id", "flat"}, {"layer", "s"}, {"kind", "rect"}, {"geometry", {{"x", 0}, {"y", 0}, {"w", 0}, {"h", 10}}}});
    w["objects"].push_back(Json{{"id", "pipe"}, {"layer", "s"}, {"kind", "edge"}, {"geometry", {{"from", "floor"}, {"to", "ghost"}}}});
    w["objects"].push_back(Json{{"id", "plan"}, {"layer", "s"}, {"kind", "image"}, {"geometry", {{"x", 0}, {"y", 0}, {"w", 10}, {"h", 10}}}});
    w["objects"].push_back(Json{{"id", "far"}, {"layer", "s"}, {"kind", "point"}, {"geometry", {{"x", 99999}, {"y", 1}}}});
    auto world = world_from_json(w);
    ASSERT_TRUE(world);
    std::set<std::string> codes;
    for (const Finding& f : validate(world.value())) codes.insert(f.code);
    for (const char* c : {"TWW001", "TWW002", "TWW003", "TWW004", "TWW007", "TWW008"}) EXPECT_TRUE(codes.count(c)) << c;
}

TEST(SceneRaster, ComposesGroundTruthAndKnowledgeFromLayerRoles) {
    auto world = world_from_json(small_world());
    ASSERT_TRUE(world);
    auto r = rasterize(world.value(), 500);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_TRUE(r.value().findings.empty()) << to_json(r.value().findings).dump(1);
    const std::vector<std::string> truth = {
        "##########",
        "#.....oo.#",
        "#.....o..#",
        "#WWWdWWWW#",
        "#........#",
        "##########",
    };
    std::vector<std::string> expected_truth = truth;
    for (auto& row : expected_truth) {
        for (char& c : row) c = c == 'W' ? '#' : c;
    }
    EXPECT_EQ(r.value().ground_truth.to_rows(), expected_truth);
    const std::vector<std::string> knowledge = {
        "##########",
        "#.....oo.#",
        "#.....o..#",
        "####D#####",
        "#??......#",
        "##########",
    };
    EXPECT_EQ(r.value().knowledge.to_rows(), knowledge);
}

TEST(SceneRaster, UnknownGroundTruthAndBadCellSizesAreReported) {
    Json w = small_world();
    w["objects"][5]["layer"] = "s";  // unknown area on a shared layer reaches the ground truth
    auto world = world_from_json(w);
    ASSERT_TRUE(world);
    auto r = rasterize(world.value(), 500);
    ASSERT_TRUE(r);
    ASSERT_FALSE(r.value().findings.empty());
    EXPECT_EQ(r.value().findings.front().code, "TWS002");
    auto bad = rasterize(world.value(), 700);
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad.value().findings.front().code, "TWS001");
    w = small_world();
    w["objects"][2]["properties"]["state"] = "ajar";
    auto ajar = rasterize(world_from_json(w).value(), 500);
    ASSERT_TRUE(ajar);
    EXPECT_EQ(ajar.value().findings.front().code, "TWS004");
}

TEST(SceneRaster, SimulatorScenarioCarriesMissionObservationAndTimeline) {
    Json w = small_world();
    w["layers"].push_back(Json{{"id", "ev"}, {"name", "Events"}, {"role", "event"}});
    w["objects"].push_back(Json{{"id", "nofly"}, {"layer", "ev"}, {"kind", "rect"}, {"semanticType", "hazard"},
                                {"geometry", {{"x", 3500}, {"y", 2000}, {"w", 1000}, {"h", 500}}}});
    const Json sim = {{"kind", "mobile-robot"},
                      {"cellSize", 500},
                      {"seed", 7},
                      {"robot", {{"maxSpeed", "0.8"}}},
                      {"observation", {{"sensorRange", "2.5"}, {"updateIntervalMs", 200}, {"observes", Json::array({"wall", "door"})}}},
                      {"mission", {{"start", "start"}, {"targets", Json::array({"goal"})}}},
                      {"timeline", Json::array({Json{{"id", "e1"}, {"at", "12.5"}, {"kind", "hazard"}, {"object", "nofly"}, {"description", "Crane works"}}})}};
    auto world = world_from_json(w);
    ASSERT_TRUE(world);
    auto s = simulator_scenario(world.value(), sim, "Small", "A small world");
    ASSERT_TRUE(s) << s.error().to_string();
    // The output is exactly what twin-world loads.
    auto parsed = world::scenario_from_json(s.value());
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    EXPECT_EQ(parsed.value().home, (geo::Cell{1, 1}));
    ASSERT_EQ(parsed.value().targets.size(), 1U);
    EXPECT_EQ(parsed.value().targets[0].id, "T1");
    EXPECT_EQ(parsed.value().targets[0].cell, (geo::Cell{8, 1}));
    EXPECT_DOUBLE_EQ(parsed.value().drone.max_speed_mps, 0.8);
    EXPECT_DOUBLE_EQ(parsed.value().drone.sensor_range_m, 2.5);
    ASSERT_EQ(parsed.value().events.size(), 1U);
    EXPECT_EQ(parsed.value().events[0].cells.size(), 2U);
    EXPECT_EQ(s.value()["observation"]["update_interval_ms"], 200);

    // A start marker on a wall and an unsupported noise model are refused, never approximated.
    Json bad_sim = sim;
    bad_sim["observation"]["noise"] = "gaussian";
    auto refused = simulator_scenario(world.value(), bad_sim, "Small", "");
    ASSERT_FALSE(refused);
    EXPECT_NE(refused.error().context_value("TWS012").find("not supported"), std::string::npos);
    w["objects"][6]["geometry"] = {{"x", 250}, {"y", 250}};
    auto on_wall = simulator_scenario(world_from_json(w).value(), sim, "Small", "");
    ASSERT_FALSE(on_wall);
    EXPECT_FALSE(on_wall.error().context_value("TWS023").empty());
}

/// The indoor-drone Blueprint's authored world rasterises to the map the demo used before Studio
/// authored it: every cell of the ground truth and of the twin's initial knowledge, the home pad,
/// the targets and the facility timeline are identical.
TEST(SceneRaster, DroneWorldReproducesTheLegacyMap) {
    const fs::path root = TWIN_SOURCE_DIR;
    const Json world_doc = read_json(root / "examples/indoor-drone/world.json");
    const Json sim = read_json(root / "examples/indoor-drone/simulation.json");
    auto world = world_from_json(world_doc);
    ASSERT_TRUE(world) << world.error().to_string();
    EXPECT_FALSE(has_errors(validate(world.value()))) << to_json(validate(world.value())).dump(1);
    auto generated = simulator_scenario(world.value(), sim, sim.value("name", std::string()), sim.value("description", std::string()));
    ASSERT_TRUE(generated) << generated.error().to_string();
    auto mine = world::scenario_from_json(generated.value());
    auto legacy = world::load_scenario(root / "tests/fixtures/legacy/inspection_default.json");
    ASSERT_TRUE(mine) << mine.error().to_string();
    ASSERT_TRUE(legacy) << legacy.error().to_string();
    EXPECT_EQ(mine.value().ground_truth.to_rows(), legacy.value().ground_truth.to_rows());
    EXPECT_EQ(mine.value().prior_knowledge.to_rows(), legacy.value().prior_knowledge.to_rows());
    EXPECT_EQ(mine.value().home, legacy.value().home);
    ASSERT_EQ(mine.value().targets.size(), legacy.value().targets.size());
    for (std::size_t i = 0; i < mine.value().targets.size(); ++i) {
        EXPECT_EQ(mine.value().targets[i].id, legacy.value().targets[i].id);
        EXPECT_EQ(mine.value().targets[i].name, legacy.value().targets[i].name);
        EXPECT_EQ(mine.value().targets[i].cell, legacy.value().targets[i].cell);
    }
    ASSERT_EQ(mine.value().events.size(), legacy.value().events.size());
    for (std::size_t i = 0; i < mine.value().events.size(); ++i) {
        EXPECT_EQ(mine.value().events[i].at, legacy.value().events[i].at);
        EXPECT_EQ(mine.value().events[i].occupancy, legacy.value().events[i].occupancy);
        EXPECT_EQ(mine.value().events[i].description, legacy.value().events[i].description);
        std::set<geo::Cell> a(mine.value().events[i].cells.begin(), mine.value().events[i].cells.end());
        std::set<geo::Cell> b(legacy.value().events[i].cells.begin(), legacy.value().events[i].cells.end());
        EXPECT_EQ(a, b);
    }
    EXPECT_DOUBLE_EQ(mine.value().drone.max_speed_mps, legacy.value().drone.max_speed_mps);
    EXPECT_DOUBLE_EQ(mine.value().drone.battery_capacity_wh, legacy.value().drone.battery_capacity_wh);
    EXPECT_DOUBLE_EQ(mine.value().drone.reserve_pct, legacy.value().drone.reserve_pct);
    EXPECT_DOUBLE_EQ(mine.value().drone.sensor_range_m, legacy.value().drone.sensor_range_m);
    EXPECT_EQ(mine.value().seed, legacy.value().seed);
}

}  // namespace
}  // namespace twin::scene
