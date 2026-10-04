/**
 * @file instance_test.cpp
 * @brief KernelInstance: transactional updates, snapshots, isolation of prediction.
 */
#include <gtest/gtest.h>

#include "support/model_builder.hpp"
#include "twin/kernel/instance.hpp"

namespace twin::kernel {
namespace {

using test::ModelBuilder;
using B = ModelBuilder;

KernelInstance make() {
    auto model = Model::create(ModelBuilder("inst")
                                   .clock("x")
                                   .location("A", {B::c("x", "<=", 5)})
                                   .location("B")
                                   .transition("A", "go!", "B", {B::c("x", ">=", 1)}, {"x"})
                                   .build())
                     .value();
    return KernelInstance::initialize(model).value();
}

TEST(KernelInstance, RefusedStepsLeaveStateUnchanged) {
    KernelInstance k = make();
    const StateSet before = k.current_state();
    EXPECT_FALSE(k.apply_event("go!", 500).ok());     // guard x >= 1 false
    EXPECT_EQ(k.current_state(), before);
    EXPECT_FALSE(k.advance_to(6000).ok());            // invariant x <= 5
    EXPECT_EQ(k.current_state(), before);
    EXPECT_FALSE(k.apply_event("unknown!", 1000).ok());
    EXPECT_EQ(k.current_state(), before);
}

TEST(KernelInstance, AcceptedStepsCommit) {
    KernelInstance k = make();
    ASSERT_TRUE(k.apply_event("go!", 1500).ok());
    EXPECT_EQ(k.current_state().members()[0].location, 1U);
    EXPECT_EQ(k.current_state().time(), 1500);
}

TEST(KernelInstance, CloneAndRestore) {
    KernelInstance k = make();
    const StateSet snap = k.clone_state();
    ASSERT_TRUE(k.apply_event("go!", 2000).ok());
    ASSERT_TRUE(k.restore_state(snap).ok());
    EXPECT_EQ(k.current_state(), snap);
}

TEST(KernelInstance, RestoreRejectsInvalidSnapshots) {
    KernelInstance k = make();
    StateSet bad = StateSet::of(Configuration{0, {9000}, 9000});  // violates x <= 5 in A
    EXPECT_FALSE(k.restore_state(bad).ok());
    StateSet bad2 = StateSet::of(Configuration{0, {10}, 5});  // clock exceeds logical time
    EXPECT_FALSE(k.restore_state(bad2).ok());
}

TEST(KernelInstance, PredictionDoesNotMutateLiveState) {
    KernelInstance k = make();
    const StateSet before = k.current_state();
    (void)k.predict(ExplorationLimits{5, std::nullopt, 100});
    std::vector<ScheduledObservation> plan{{1000, Selector::label(*k.model().label_id("go!"))}};
    ASSERT_TRUE(k.simulate(plan).ok());
    (void)k.successors();
    (void)k.enabled_transitions();
    EXPECT_EQ(k.current_state(), before);
}

TEST(KernelInstance, EnabledTransitionsReportWindows) {
    KernelInstance k = make();
    std::vector<EnabledTransition> e = k.enabled_transitions();
    ASSERT_EQ(e.size(), 1U);
    EXPECT_FALSE(e[0].enabled_now);
    EXPECT_EQ(e[0].window.earliest, 1000);
    EXPECT_EQ(e[0].window.latest.value(), 5000);
}

TEST(KernelInstance, PropositionsCertainAndPossible) {
    KernelInstance k = make();
    PropositionStatus p = k.evaluate_propositions();
    EXPECT_EQ(p.certain, (std::vector<ir::PropositionIndex>{0}));
    EXPECT_EQ(p.possible, (std::vector<ir::PropositionIndex>{0}));
}

}  // namespace
}  // namespace twin::kernel
