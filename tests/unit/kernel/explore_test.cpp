/**
 * @file explore_test.cpp
 * @brief Prediction: timed successors, bounded exploration, simulation on copies.
 */
#include <gtest/gtest.h>

#include "support/model_builder.hpp"
#include "twin/kernel/explore.hpp"

namespace twin::kernel {
namespace {

using test::ModelBuilder;
using B = ModelBuilder;

std::shared_ptr<const Model> cycle() {
    return Model::create(ModelBuilder("cycle")
                             .clock("x")
                             .location("A", {B::c("x", "<=", 4)})
                             .location("B", {B::c("x", "<=", 3)})
                             .transition("A", "ab!", "B", {B::c("x", ">=", 1)}, {"x"})
                             .transition("B", "ba!", "A", {B::c("x", ">=", 2)}, {"x"})
                             .build())
        .value();
}

TEST(Explore, TimedSuccessorsCarryExactWindows) {
    auto m = cycle();
    Configuration c = initial_configuration(*m).value();
    std::vector<TimedSuccessor> s = timed_successors(*m, c);
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].window.earliest, 1000);
    EXPECT_EQ(s[0].window.latest.value(), 4000);
    EXPECT_EQ(s[0].earliest.location, 1U);
    EXPECT_EQ(s[0].earliest.time, 1000);
}

TEST(Explore, BoundedByDepthAndHorizon) {
    auto m = cycle();
    Configuration c = initial_configuration(*m).value();
    ExplorationResult r = explore(*m, c, ExplorationLimits{3, std::nullopt, 100});
    ASSERT_EQ(r.nodes.size(), 4U);  // root + 3 steps along the single cycle
    EXPECT_EQ(r.nodes[3].config.time, 1000 + 2000 + 1000);
    std::vector<TrajectoryStep> path = trajectory_to(r, 3);
    ASSERT_EQ(path.size(), 3U);
    EXPECT_EQ(path[0].transition, 0U);
    EXPECT_EQ(path[1].transition, 1U);

    ExplorationResult h = explore(*m, c, ExplorationLimits{10, Ticks{2500}, 100});
    EXPECT_TRUE(h.truncated);
    for (const ExplorationNode& n : h.nodes) EXPECT_LE(n.config.time, 2500);
}

TEST(Explore, SimulationNeverTouchesTheInputState) {
    auto m = cycle();
    const StateSet start = StateSet::of(initial_configuration(*m).value());
    const StateSet copy = start;
    std::vector<ScheduledObservation> plan{{1000, Selector::label(*m->label_id("ab!"))},
                                           {3000, Selector::label(*m->label_id("ba!"))}};
    Result<std::vector<ObservationOutcome>> r = simulate(*m, start, plan);
    ASSERT_TRUE(r.ok()) << r.error().to_string();
    EXPECT_EQ(r.value().back().after.members()[0].location, 0U);
    EXPECT_EQ(start, copy);
}

TEST(Explore, SimulationReportsTheFailingStep) {
    auto m = cycle();
    const StateSet start = StateSet::of(initial_configuration(*m).value());
    std::vector<ScheduledObservation> plan{{1000, Selector::label(*m->label_id("ab!"))},
                                           {1500, Selector::label(*m->label_id("ba!"))}};
    Result<std::vector<ObservationOutcome>> r = simulate(*m, start, plan);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context_value("step"), "1");
}

}  // namespace
}  // namespace twin::kernel
