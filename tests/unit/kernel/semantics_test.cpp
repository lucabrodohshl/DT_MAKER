/**
 * @file semantics_test.cpp
 * @brief Kernel transition rules: init, delay/invariants, guards, resets, labels, windows.
 */
#include <gtest/gtest.h>

#include "support/model_builder.hpp"
#include "twin/kernel/semantics.hpp"

namespace twin::kernel {
namespace {

using test::ModelBuilder;
using B = ModelBuilder;

std::shared_ptr<const Model> make(const ir::Model& m) {
    Result<std::shared_ptr<const Model>> r = Model::create(m);
    if (!r) throw std::logic_error(r.error().to_string());
    return r.value();
}

constexpr Ticks kUnit = 1000;  // ticks per time unit (default time base)

/// Idle --start! [y>=2] {x:=0}--> Busy (inv x<=10) --done! [x>=4] {x,y}--> Idle
std::shared_ptr<const Model> machine() {
    return make(ModelBuilder("machine")
                    .clock("x")
                    .clock("y")
                    .location("Idle")
                    .location("Busy", {B::c("x", "<=", 10)})
                    .transition("Idle", "start!", "Busy", {B::c("y", ">=", 2)}, {"x"})
                    .transition("Busy", "done!", "Idle", {B::c("x", ">=", 4)}, {"x", "y"})
                    .build());
}

TEST(KernelSemantics, InitialConfigurationIsZeroAtInitialLocation) {
    auto m = machine();
    Result<Configuration> c0 = initial_configuration(*m);
    ASSERT_TRUE(c0.ok());
    EXPECT_EQ(c0.value().location, 0U);
    EXPECT_EQ(c0.value().clocks, (std::vector<Ticks>{0, 0}));
    EXPECT_EQ(c0.value().time, 0);
}

TEST(KernelSemantics, InitialInvariantMustHold) {
    auto m = make(ModelBuilder("bad").clock("x").location("L", {B::c("x", ">=", 1)}).build());
    Result<Configuration> c0 = initial_configuration(*m);
    ASSERT_FALSE(c0.ok());
    EXPECT_EQ(c0.error().code, ErrorCode::InvariantViolation);
}

TEST(KernelSemantics, DelayAdvancesAllClocksAndGlobalTime) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    Configuration d = delay(*m, c, 2500).value();
    EXPECT_EQ(d.clocks, (std::vector<Ticks>{2500, 2500}));
    EXPECT_EQ(d.time, 2500);
    EXPECT_EQ(d.location, c.location);
}

TEST(KernelSemantics, DelayRespectsInvariantBoundaryExactly) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    c = timed_step(*m, c, 2 * kUnit, 0).value();  // start! at y = 2, resets x
    ASSERT_EQ(c.location, 1U);
    EXPECT_TRUE(delay(*m, c, 10 * kUnit).ok());           // x = 10 is allowed (x <= 10)
    Result<Configuration> late = delay(*m, c, 10 * kUnit + 1);  // x = 10.001 is not
    ASSERT_FALSE(late.ok());
    EXPECT_EQ(late.error().code, ErrorCode::InvariantViolation);
    EXPECT_EQ(late.error().context_value("max_delay"), "10");
}

TEST(KernelSemantics, NegativeDelayIsRejected) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    EXPECT_EQ(delay(*m, c, -1).error().code, ErrorCode::InvalidArgument);
}

TEST(KernelSemantics, GuardsAreCheckedAtTheGrid) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    EXPECT_EQ(fire(*m, delay(*m, c, 1999).value(), 0).error().code,
              ErrorCode::TransitionNotEnabled);
    EXPECT_TRUE(fire(*m, delay(*m, c, 2000).value(), 0).ok());
}

TEST(KernelSemantics, ResetsZeroExactlyTheResetClocks) {
    auto m = machine();
    Configuration c = timed_step(*m, initial_configuration(*m).value(), 3 * kUnit, 0).value();
    EXPECT_EQ(c.clocks, (std::vector<Ticks>{0, 3000}));  // x reset, y kept
    EXPECT_EQ(c.time, 3000);
}

TEST(KernelSemantics, WrongLocationIsNotEnabled) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    EXPECT_EQ(enablement(*m, c, 1), Enablement::WrongLocation);
    EXPECT_EQ(enablement(*m, c, 99), Enablement::NoSuchTransition);
    EXPECT_EQ(fire(*m, c, 99).error().code, ErrorCode::InvalidArgument);
}

TEST(KernelSemantics, TargetInvariantIsChecked) {
    // A --go!--> B with B's invariant x <= 1 and no reset: only enabled while x <= 1.
    auto m = make(ModelBuilder("t")
                      .clock("x")
                      .location("A")
                      .location("B", {B::c("x", "<=", 1)})
                      .transition("A", "go!", "B")
                      .build());
    Configuration c = initial_configuration(*m).value();
    EXPECT_TRUE(timed_step(*m, c, 1000, 0).ok());
    Result<Configuration> r = timed_step(*m, c, 1001, 0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::TransitionNotEnabled);
    EXPECT_EQ(enablement(*m, delay(*m, c, 1001).value(), 0), Enablement::TargetInvariantFalse);
}

TEST(KernelSemantics, StrictConstraints) {
    auto m = make(ModelBuilder("s")
                      .clock("x")
                      .location("A", {B::c("x", "<", 2)})
                      .location("B")
                      .transition("A", "go!", "B", {B::c("x", ">", 1)})
                      .build());
    Configuration c = initial_configuration(*m).value();
    EXPECT_FALSE(timed_step(*m, c, 1000, 0).ok());   // x = 1 is not > 1
    EXPECT_TRUE(timed_step(*m, c, 1001, 0).ok());    // x = 1.001
    EXPECT_TRUE(delay(*m, c, 1999).ok());            // x = 1.999 < 2
    EXPECT_FALSE(delay(*m, c, 2000).ok());           // x = 2 is not < 2
}

TEST(KernelSemantics, DiagonalConstraintsAreDelayInvariant) {
    auto m = make(ModelBuilder("d")
                      .clock("x")
                      .clock("y")
                      .location("A")
                      .location("B", {B::diff("x", "y", "<=", 1)})
                      .transition("A", "reset_y!", "A", {}, {"y"})
                      .transition("A", "go!", "B")
                      .build());
    Configuration c = initial_configuration(*m).value();
    c = timed_step(*m, c, 2000, 0).value();  // y := 0 at x = 2, so x - y = 2
    EXPECT_FALSE(timed_step(*m, c, 0, 1).ok());
    EXPECT_FALSE(timed_step(*m, c, 5000, 1).ok());  // still 2 after any delay
}

TEST(KernelSemantics, DiscreteSuccessorsPreserveNondeterminism) {
    auto m = make(ModelBuilder("n")
                      .location("A")
                      .location("B")
                      .location("C")
                      .transition("A", "a!", "B")
                      .transition("A", "a!", "C")
                      .build());
    Configuration c = initial_configuration(*m).value();
    std::vector<DiscreteSuccessor> succ = discrete_successors(*m, c);
    ASSERT_EQ(succ.size(), 2U);
    EXPECT_EQ(succ[0].target.location, 1U);
    EXPECT_EQ(succ[1].target.location, 2U);
}

TEST(KernelSemantics, PropositionsAreTheLocationProposition) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    EXPECT_EQ(propositions(*m, c), (std::vector<ir::PropositionIndex>{0}));
    c = timed_step(*m, c, 2000, 0).value();
    EXPECT_EQ(propositions(*m, c), (std::vector<ir::PropositionIndex>{1}));
}

TEST(KernelSemantics, EnablingWindowIsExact) {
    auto m = machine();
    Configuration c = timed_step(*m, initial_configuration(*m).value(), 2000, 0).value();
    // done! needs x >= 4 and Busy allows x <= 10  =>  window [4, 10] time units.
    std::optional<DelayWindow> w = enabling_window(*m, c, 1);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->earliest, 4000);
    ASSERT_TRUE(w->latest.has_value());
    EXPECT_EQ(*w->latest, 10000);
    // Exhaustive agreement with timed_step on the grid around the boundaries.
    for (Ticks d = 3990; d <= 4010; ++d) {
        EXPECT_EQ(timed_step(*m, c, d, 1).ok(), d >= 4000) << d;
    }
    for (Ticks d = 9990; d <= 10010; ++d) {
        EXPECT_EQ(timed_step(*m, c, d, 1).ok(), d <= 10000) << d;
    }
}

TEST(KernelSemantics, EnablingWindowUnboundedWhenNoUpperConstraint) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    std::optional<DelayWindow> w = enabling_window(*m, c, 0);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->earliest, 2000);
    EXPECT_FALSE(w->latest.has_value());
    EXPECT_FALSE(max_delay(*m, c).has_value());
}

TEST(KernelSemantics, ExplainGuardReportsEachAtom) {
    auto m = machine();
    Configuration c = delay(*m, initial_configuration(*m).value(), 1500).value();
    std::vector<AtomEvaluation> ev = explain_guard(*m, c, 0);
    ASSERT_EQ(ev.size(), 1U);
    EXPECT_EQ(ev[0].lhs_minus_rhs, 1500);
    EXPECT_EQ(ev[0].bound, 2000);
    EXPECT_FALSE(ev[0].holds);
}

TEST(KernelSemantics, HorizonOverflowIsAnErrorNotAWrap) {
    auto m = machine();
    Configuration c = initial_configuration(*m).value();
    c.time = kMaxTicks - 1;
    c.clocks = {kMaxTicks - 1, kMaxTicks - 1};
    EXPECT_EQ(delay(*m, c, 5).error().code, ErrorCode::ArithmeticOverflow);
}

}  // namespace
}  // namespace twin::kernel
