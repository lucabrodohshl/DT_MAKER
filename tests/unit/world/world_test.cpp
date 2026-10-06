/**
 * @file world_test.cpp
 * @brief The Physical Twin world through its JSON API (the same code twin-world serves).
 */
#include <gtest/gtest.h>

#include <string>

#include "twin/world/service.hpp"

namespace twin::world {
namespace {

using json::Json;

std::unique_ptr<WorldService> open_world() {
    Result<std::unique_ptr<WorldService>> s =
        WorldService::open(std::filesystem::path(TWIN_SOURCE_DIR) / "tests" / "fixtures" / "legacy" / "inspection_default.json");
    EXPECT_TRUE(s.ok()) << (s ? "" : s.error().to_string());
    return std::move(s).value();
}

char cell(const Json& map, std::size_t x, std::size_t y) { return map.at("rows").at(y).get<std::string>().at(x); }

TEST(WorldApi, RoutingAndValidation) {
    auto w = open_world();
    EXPECT_EQ(w->get("/health").status, 200);
    EXPECT_EQ(w->get("/nope").status, 404);
    EXPECT_EQ(w->post("/env/map/known").status, 405);
    EXPECT_EQ(w->post("/pt/step", Json{{"dt", 1.5}}).status, 400);   // floats are never accepted
    EXPECT_EQ(w->post("/pt/step", Json{{"dt", 0}}).status, 400);
    EXPECT_EQ(w->post("/pt/step", Json{{"dt", 100}}).status, 200);
    EXPECT_EQ(w->get("/env/map/updates", {{"since", "x"}}).status, 400);
    // A command that the flight controller refuses in its current mode: 409.
    const ApiReply refused = w->post("/pt/command", Json{{"kind", "inspect"}, {"target_id", "T1"}});
    EXPECT_EQ(refused.status, 409);
    EXPECT_EQ(refused.body.at("error").value("code", std::string()), "state_error");
    EXPECT_EQ(w->post("/pt/command", Json{{"kind", "dance"}}).status, 400);
}

TEST(WorldApi, PriorKnowledgeDiffersFromGroundTruth) {
    auto w = open_world();
    const Json known = w->get("/env/map/known").body.at("map");
    const Json truth = w->get("/observer/ground-truth").body.at("map");
    // Fire door FD-2 (column 31): open in the facility plan, closed in reality.
    EXPECT_EQ(cell(known, 31, 14), 'D');
    EXPECT_EQ(cell(truth, 31, 14), 'd');
    // The refurbished office is unknown to the facility plan.
    EXPECT_EQ(cell(known, 20, 23), '?');
    EXPECT_NE(cell(truth, 20, 23), '?');
}

TEST(WorldApi, SensingRevealsTheClosedDoorAndNeverHazards) {
    auto w = open_world();
    ASSERT_EQ(w->post("/pt/command", Json{{"kind", "upload_mission"}}).status, 200);
    ASSERT_EQ(w->post("/pt/command", Json{{"kind", "arm_takeoff"}}).status, 200);
    for (int i = 0; i < 60; ++i) ASSERT_EQ(w->post("/pt/step", Json{{"dt", 100}}).status, 200);
    // Fly east along the corridor towards FD-2.
    const Json route{{"kind", "follow_route"}, {"goal", "waypoint"}, {"route_id", 1},
                     {"waypoints_mm", Json::array({Json::array({3250, 7250}), Json::array({14250, 7250})})}};
    ASSERT_EQ(w->post("/pt/command", route).status, 200) << w->post("/pt/command", route).body.dump();
    bool saw_door = false;
    for (int i = 0; i < 300 && !saw_door; ++i) {
        ASSERT_EQ(w->post("/pt/step", Json{{"dt", 100}}).status, 200);
        const ApiReply updates = w->get("/env/map/updates");  // keep the reply alive while iterating
        for (const Json& u : updates.body.at("updates")) {
            for (const Json& c : u.at("cells")) {
                EXPECT_NE(c.value("occupancy", std::string()), "hazard") << "the lidar cannot see hazards";
                if (c.at("cell")[0] == 31 && c.value("occupancy", std::string()) == "door_closed") saw_door = true;
            }
        }
    }
    EXPECT_TRUE(saw_door);
}

TEST(WorldApi, FacilityNoticeIsPublishedAtItsTime) {
    auto w = open_world();
    for (int i = 0; i < 530; ++i) ASSERT_EQ(w->post("/pt/step", Json{{"dt", 100}}).status, 200);  // t = 53 s
    bool notice = false;
    const ApiReply updates = w->get("/env/map/updates");  // keep the reply alive while iterating
    for (const Json& u : updates.body.at("updates")) {
        notice = notice || u.value("kind", std::string()) == "facility_notice";
    }
    EXPECT_TRUE(notice);
    EXPECT_FALSE(w->get("/env/hazards").body.at("hazard_cells").empty());
}

TEST(WorldApi, IsDeterministic) {
    auto a = open_world();
    auto b = open_world();
    for (auto* w : {a.get(), b.get()}) {
        ASSERT_EQ(w->post("/pt/command", Json{{"kind", "upload_mission"}}).status, 200);
        ASSERT_EQ(w->post("/pt/command", Json{{"kind", "arm_takeoff"}}).status, 200);
    }
    for (int i = 0; i < 80; ++i) {
        EXPECT_EQ(a->post("/pt/step", Json{{"dt", 100}}).body.dump(), b->post("/pt/step", Json{{"dt", 100}}).body.dump());
    }
}

TEST(WorldApi, OperatorCannotBlockTheHomePad) {
    auto w = open_world();
    const ApiReply r = w->post("/observer/inject", Json{{"cells", Json::array({Json::array({6, 23})})}});
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(w->post("/admin/reset").status, 200);
}

TEST(WorldApi, OperatorDefinedMissionIsValidatedAndSurvivesResets) {
    auto w = open_world();
    auto target = [](const std::string& name, int x, int y) { return Json{{"name", name}, {"cell", Json::array({x, y})}}; };
    // Walls, cells outside the building, duplicates and the home pad are refused; nothing changes.
    EXPECT_EQ(w->post("/scenario/mission", Json{{"targets", Json::array({target("wall", 0, 0)})}}).status, 400);
    EXPECT_EQ(w->post("/scenario/mission", Json{{"targets", Json::array({target("out", 500, 2)})}}).status, 400);
    EXPECT_EQ(w->post("/scenario/mission", Json{{"targets", Json::array({target("a", 41, 5), target("b", 41, 5)})}}).status, 400);
    EXPECT_EQ(w->post("/scenario/mission", Json{{"targets", Json::array({target("home", 6, 23)})}}).status, 400);
    EXPECT_EQ(w->post("/scenario/mission", Json{{"targets", Json::array()}}).status, 400);
    EXPECT_EQ(w->get("/scenario").body.at("targets").size(), 2U);

    const ApiReply set = w->post("/scenario/mission", Json{{"targets", Json::array({target("Valve", 41, 5)})}});
    ASSERT_EQ(set.status, 200) << set.body.dump();
    EXPECT_TRUE(set.body.at("custom_mission").get<bool>());
    EXPECT_EQ(set.body.at("targets").size(), 1U);
    EXPECT_EQ(set.body.at("targets")[0].at("id"), "T1");
    EXPECT_EQ(w->post("/admin/reset").status, 200);
    EXPECT_EQ(w->get("/scenario").body.at("targets")[0].at("name"), "Valve");

    const ApiReply restored = w->post("/scenario/mission/default");
    ASSERT_EQ(restored.status, 200);
    EXPECT_EQ(restored.body.at("targets").size(), 2U);
}

}  // namespace
}  // namespace twin::world
