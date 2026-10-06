/**
 * @file planner_test.cpp
 * @brief The untrusted planner (A*) and the runtime's independent route validator.
 */
#include <gtest/gtest.h>

#include <cmath>

#include "twin/planner/planner.hpp"
#include "twin/runtime/plan_validator.hpp"

namespace twin::planner {
namespace {

geo::OccupancyGrid grid(const std::vector<std::string>& rows) {
    Result<geo::OccupancyGrid> g = geo::OccupancyGrid::from_rows(rows, 0.5);
    EXPECT_TRUE(g.ok());
    return std::move(g).value();
}

Plan plan(const geo::OccupancyGrid& g, geo::Cell from, geo::Cell to, bool allow_unknown = true,
          CostWeights w = {}) {
    AStarPlanner p;
    return p.plan(PlanningProblem{&g, from, to, w, allow_unknown});
}

const std::vector<std::string> kRoom = {
    "############",
    "#..........#",
    "#..........#",
    "#..........#",
    "############",
};

TEST(AStar, StraightRouteInOpenSpaceIsOneSegment) {
    const geo::OccupancyGrid g = grid(kRoom);
    CostWeights flat;
    flat.proximity = 0.0;
    const Plan p = plan(g, {1, 2}, {10, 2}, true, flat);
    ASSERT_TRUE(p.found) << p.failure;
    ASSERT_EQ(p.waypoints.size(), 1U);  // string pulling removes intermediate cells
    EXPECT_NEAR(p.length_m, 4.5, 1e-9);
    EXPECT_NEAR(p.cost, 4.5, 1e-9);     // no penalties: cost = length
    EXPECT_DOUBLE_EQ(p.unknown_m, 0.0);
}

TEST(AStar, GoesThroughTheOnlyGap) {
    const geo::OccupancyGrid g = grid({
        "############",
        "#....#.....#",
        "#....#.....#",
        "#..........#",
        "############",
    });
    const Plan p = plan(g, {1, 1}, {10, 1});
    ASSERT_TRUE(p.found);
    // The independent validator agrees that the route never touches a wall.
    const runtime::PlanCheck check = runtime::validate_route(g, g.center({1, 1}), p.waypoints, 100.0, 0.0);
    EXPECT_TRUE(check.valid) << (check.issues.empty() ? "" : check.issues.front());
    bool through_gap = false;
    for (const geo::Cell& c : p.cells) through_gap = through_gap || (c.x == 5 && c.y == 3);
    EXPECT_TRUE(through_gap);
}

TEST(AStar, NeverCutsCorners) {
    const geo::OccupancyGrid g = grid({
        "#####",
        "#.#.#",
        "##..#",
        "#####",
    });
    // (1,1) -> (3,2) diagonally would squeeze between walls at (2,1) and (1,2).
    const Plan p = plan(g, {1, 1}, {3, 2});
    EXPECT_FALSE(p.found);
}

TEST(AStar, HazardsAndClosedDoorsAreImpassable) {
    const geo::OccupancyGrid hazard = grid({"#######", "#..!..#", "#######"});
    EXPECT_FALSE(plan(hazard, {1, 1}, {5, 1}).found);
    const geo::OccupancyGrid door = grid({"#######", "#..d..#", "#######"});
    EXPECT_FALSE(plan(door, {1, 1}, {5, 1}).found);
    const geo::OccupancyGrid open = grid({"#######", "#..D..#", "#######"});
    EXPECT_TRUE(plan(open, {1, 1}, {5, 1}).found);
}

TEST(AStar, UnknownSpaceIsOptionalAndPenalised) {
    const geo::OccupancyGrid g = grid({
        "#########",
        "#...?...#",
        "#.#####.#",
        "#.......#",
        "#########",
    });
    const Plan optimistic = plan(g, {1, 1}, {7, 1}, true);
    ASSERT_TRUE(optimistic.found);
    const Plan known_only = plan(g, {1, 1}, {7, 1}, false);
    ASSERT_TRUE(known_only.found);
    EXPECT_GT(known_only.length_m, optimistic.length_m);  // detour through known space
    EXPECT_DOUBLE_EQ(known_only.unknown_m, 0.0);
    EXPECT_GT(optimistic.unknown_m, 0.0);
}

TEST(AStar, ReportsUnreachableGoal) {
    const geo::OccupancyGrid g = grid(kRoom);
    const Plan p = plan(g, {1, 1}, {0, 0});
    EXPECT_FALSE(p.found);
    EXPECT_NE(p.failure.find("not passable"), std::string::npos);
}

TEST(RouteCost, InfiniteThroughWallsAndConsistentInOpenSpace) {
    const geo::OccupancyGrid g = grid(kRoom);
    CostWeights flat;
    flat.proximity = 0.0;
    const Plan p = plan(g, {1, 2}, {10, 2}, true, flat);
    ASSERT_TRUE(p.found);
    EXPECT_NEAR(route_cost(g, g.center({1, 2}), p.waypoints, flat), p.cost, 1e-6);
    EXPECT_TRUE(std::isinf(route_cost(g, g.center({1, 2}), {g.center({1, 2}), geo::Point{-1.0, -1.0}}, flat)));
}

TEST(Validator, ReportsBlockedRoutesAndEnergy) {
    const geo::OccupancyGrid g = grid({"#######", "#..d..#", "#######"});
    const runtime::PlanCheck blocked = runtime::validate_route(g, g.center({1, 1}), {g.center({5, 1})}, 100.0, 0.0);
    EXPECT_FALSE(blocked.valid);
    EXPECT_NE(blocked.issues.front().find("door_closed"), std::string::npos);
    const geo::OccupancyGrid open = grid(kRoom);
    const runtime::PlanCheck tired = runtime::validate_route(open, open.center({1, 2}), {open.center({10, 2})}, 0.1, 0.0);
    EXPECT_FALSE(tired.valid);
    EXPECT_NE(tired.issues.front().find("insufficient energy"), std::string::npos);
}

}  // namespace
}  // namespace twin::planner
