/**
 * @file state_set_test.cpp
 * @brief Monitoring semantics: observations, rejection, nondeterministic belief sets.
 */
#include <gtest/gtest.h>

#include "support/model_builder.hpp"
#include "twin/kernel/semantics.hpp"
#include "twin/kernel/state_set.hpp"

namespace twin::kernel {
namespace {

using test::ModelBuilder;
using B = ModelBuilder;

std::shared_ptr<const Model> make(const ir::Model& m) { return Model::create(m).value(); }

TEST(StateSet, CanonicalisesMembers) {
    Configuration a{0, {1}, 5};
    Configuration b{1, {0}, 5};
    StateSet s = StateSet::of({b, a, b}).value();
    ASSERT_EQ(s.members().size(), 2U);
    EXPECT_EQ(s.members()[0], a);
    EXPECT_EQ(s.time(), 5);
    EXPECT_FALSE(StateSet::of(std::vector<Configuration>{}).ok());
    EXPECT_FALSE(StateSet::of({Configuration{0, {0}, 1}, Configuration{0, {0}, 2}}).ok());
}

TEST(Monitor, AcceptsCompatibleObservation) {
    auto m = make(ModelBuilder("m")
                      .clock("x")
                      .location("A")
                      .location("B")
                      .transition("A", "go!", "B", {B::c("x", ">=", 2)})
                      .build());
    StateSet s = StateSet::of(initial_configuration(*m).value());
    Result<ObservationOutcome> o = observe(*m, s, 3000, Selector::label(*m->label_id("go!")));
    ASSERT_TRUE(o.ok()) << o.error().to_string();
    EXPECT_EQ(o.value().delay, 3000);
    ASSERT_TRUE(o.value().after.is_singleton());
    EXPECT_EQ(o.value().after.members()[0].location, 1U);
    EXPECT_EQ(o.value().after.members()[0].time, 3000);
    ASSERT_EQ(o.value().branches.size(), 1U);
}

TEST(Monitor, RejectsObservationTooEarly) {
    auto m = make(ModelBuilder("m")
                      .clock("x")
                      .location("A")
                      .location("B")
                      .transition("A", "go!", "B", {B::c("x", ">=", 2)})
                      .build());
    StateSet s = StateSet::of(initial_configuration(*m).value());
    Result<ObservationOutcome> o = observe(*m, s, 1000, Selector::label(*m->label_id("go!")));
    ASSERT_FALSE(o.ok());
    EXPECT_EQ(o.error().code, ErrorCode::IncompatibleObservation);
}

TEST(Monitor, RejectsObservationRequiringInvalidTimeEvolution) {
    // In A at most 5 time units may pass; an event at t = 6 cannot be explained.
    auto m = make(ModelBuilder("m")
                      .clock("x")
                      .location("A", {B::c("x", "<=", 5)})
                      .location("B")
                      .transition("A", "go!", "B")
                      .build());
    StateSet s = StateSet::of(initial_configuration(*m).value());
    Result<ObservationOutcome> o = observe(*m, s, 6000, Selector::label(*m->label_id("go!")));
    ASSERT_FALSE(o.ok());
    EXPECT_EQ(o.error().code, ErrorCode::IncompatibleObservation);
    EXPECT_EQ(o.error().context_value("max_delay"), "5");
}

TEST(Monitor, TimeRegressionIsRejected) {
    auto m = make(ModelBuilder("m").clock("x").location("A").build());
    StateSet s = StateSet::of(initial_configuration(*m).value());
    s = advance_to(*m, s, 4000).value();
    EXPECT_EQ(advance_to(*m, s, 3999).error().code, ErrorCode::TimeRegression);
}

TEST(Monitor, NondeterminismIsRetainedNotResolved) {
    // A --a!--> B (x <= 3 allowed later) and A --a!--> C (inv x <= 1).
    auto m = make(ModelBuilder("nd")
                      .clock("x")
                      .location("A")
                      .location("B")
                      .location("C", {B::c("x", "<=", 1)})
                      .transition("A", "a!", "B")
                      .transition("A", "a!", "C")
                      .transition("B", "b!", "A")
                      .transition("C", "c!", "A")
                      .build());
    StateSet s = StateSet::of(initial_configuration(*m).value());
    Result<ObservationOutcome> o = observe(*m, s, 0, Selector::label(*m->label_id("a!")));
    ASSERT_TRUE(o.ok());
    EXPECT_EQ(o.value().after.members().size(), 2U);  // both B and C are possible
    EXPECT_EQ(o.value().branches.size(), 2U);
    // Two time units later C is impossible (invariant x <= 1): the time observation prunes it.
    StateSet later = advance_to(*m, o.value().after, 2000).value();
    ASSERT_TRUE(later.is_singleton());
    EXPECT_EQ(later.members()[0].location, 1U);
}

TEST(Monitor, ObserveSpecificTransition) {
    auto m = make(ModelBuilder("t")
                      .location("A")
                      .location("B")
                      .location("C")
                      .transition("A", "tau", "B")
                      .transition("A", "tau", "C")
                      .build());
    StateSet s = StateSet::of(initial_configuration(*m).value());
    Result<ObservationOutcome> o = observe(*m, s, 0, Selector::transition(1));
    ASSERT_TRUE(o.ok());
    ASSERT_TRUE(o.value().after.is_singleton());
    EXPECT_EQ(o.value().after.members()[0].location, 2U);
}

TEST(Monitor, SameTimestampEventsAreOrderedByArrival) {
    auto m = make(ModelBuilder("seq")
                      .location("A")
                      .location("B")
                      .location("C")
                      .transition("A", "first!", "B")
                      .transition("B", "second!", "C")
                      .build());
    StateSet s = StateSet::of(initial_configuration(*m).value());
    s = observe(*m, s, 7000, Selector::label(*m->label_id("first!"))).value().after;
    s = observe(*m, s, 7000, Selector::label(*m->label_id("second!"))).value().after;
    EXPECT_EQ(s.members()[0].location, 2U);
    EXPECT_EQ(s.time(), 7000);
    // The reverse order is not explainable.
    StateSet s2 = StateSet::of(initial_configuration(*m).value());
    EXPECT_FALSE(observe(*m, s2, 7000, Selector::label(*m->label_id("second!"))).ok());
}

}  // namespace
}  // namespace twin::kernel
