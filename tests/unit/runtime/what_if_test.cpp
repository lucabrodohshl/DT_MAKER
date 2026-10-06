/**
 * @file what_if_test.cpp
 * @brief What-if scenarios and timed action availability: windows with their derivation,
 *        non-convex event windows, nondeterministic branches, and precise refusals.
 */
#include <gtest/gtest.h>

#include "support/model_builder.hpp"
#include "twin/kernel/semantics.hpp"
#include "twin/runtime/what_if.hpp"

namespace twin::runtime {
namespace {

using json::Json;
using test::ModelBuilder;
using B = ModelBuilder;

std::shared_ptr<const kernel::Model> make(const ir::Model& m) {
    auto r = kernel::Model::create(m);
    if (!r) throw std::logic_error(r.error().to_string());
    return r.value();
}

kernel::StateSet initial(const kernel::Model& m) { return kernel::StateSet::of(kernel::initial_configuration(m).value()); }

/// Idle --start! [y>=2] {x:=0}--> Busy (inv x<=10) --done! [x>=4] {x,y}--> Idle
std::shared_ptr<const kernel::Model> machine() {
    return make(ModelBuilder("machine")
                    .clock("x")
                    .clock("y")
                    .location("Idle")
                    .location("Busy", {B::c("x", "<=", 10)})
                    .transition("Idle", "start!", "Busy", {B::c("y", ">=", 2)}, {"x"})
                    .transition("Busy", "done!", "Idle", {B::c("x", ">=", 4)}, {"x", "y"})
                    .build());
}

const Json* event(const Json& final, const std::string& label) {
    for (const Json& a : final["availability"]) {
        if (a["label"] == label) return &a;
    }
    return nullptr;
}

TEST(WhatIf, AvailabilityCarriesWindowAndItsDerivation) {
    auto m = machine();
    Json r = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}}, {"steps", Json::array()}}).value();
    const Json* start = event(r["final"], "start!");
    ASSERT_NE(start, nullptr);
    EXPECT_EQ((*start)["status"], "later");                        // y >= 2 needs 2 time units
    EXPECT_EQ((*start)["intervals"][0]["earliest"]["text"], "2");
    EXPECT_TRUE((*start)["intervals"][0]["latest"].is_null());     // no upper bound
    const Json& factors = (*start)["alternatives"][0]["factors"];
    bool guard_cited = false;
    for (const Json& f : factors) {
        if (f["origin"] == "guard" && f["min_delay"]["text"] == "2") guard_cited = true;
    }
    EXPECT_TRUE(guard_cited) << factors.dump();
    EXPECT_TRUE(r["final"]["max_delay"].is_null()) << "Idle has no invariant: unbounded";
    ASSERT_EQ(r["final"]["unavailable"].size(), 1U);
    EXPECT_EQ(r["final"]["unavailable"][0]["label"], "done!");
}

TEST(WhatIf, ScenarioStepsAndDeadline) {
    auto m = machine();
    Json r = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}},
                                           {"steps", {{{"kind", "event"}, {"label", "start!"}, {"delay", "3"}}}}}).value();
    ASSERT_EQ(r["steps"][0]["status"], "ok");
    EXPECT_EQ(r["steps"][0]["branches"][0]["target"], "Busy");
    EXPECT_EQ(r["final"]["max_delay"]["text"], "10");           // Busy: x <= 10, x reset
    EXPECT_EQ(r["final"]["max_delay_at"]["text"], "13");
    const Json* done = event(r["final"], "done!");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ((*done)["intervals"][0]["earliest"]["text"], "4");
    EXPECT_EQ((*done)["intervals"][0]["latest"]["text"], "10");
    EXPECT_EQ((*done)["intervals"][0]["earliest_at"]["text"], "7");
}

TEST(WhatIf, TooEarlyIsExplainedByTheResponsibleAtom) {
    auto m = machine();
    Json r = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}},
                                           {"steps", {{{"kind", "event"}, {"label", "start!"}, {"delay", "1"}},
                                                      {{"kind", "delay"}, {"delay", "1"}}}}}).value();
    EXPECT_EQ(r["first_invalid"], 0);
    EXPECT_EQ(r["steps"][0]["status"], "invalid");
    const std::string why = r["steps"][0]["explanation"]["reasons"][0]["reason"];
    EXPECT_NE(why.find("too early: the earliest permitted time is t = 2"), std::string::npos) << why;
    EXPECT_NE(why.find("y >= 2"), std::string::npos) << why;
    EXPECT_EQ(r["steps"][1]["status"], "not_evaluated");
}

TEST(WhatIf, DelayBeyondInvariantIsRefusedWithTheDeadline) {
    auto m = machine();
    Json r = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}},
                                           {"steps", {{{"kind", "event"}, {"label", "start!"}, {"delay", "2"}},
                                                      {{"kind", "delay"}, {"delay", "11"}}}}}).value();
    EXPECT_EQ(r["first_invalid"], 1);
    EXPECT_EQ(r["steps"][1]["explanation"]["max_delay"]["text"], "10");
    EXPECT_EQ(r["steps"][1]["explanation"]["invariants"][0]["invariant"], "x <= 10");
}

TEST(WhatIf, EventWindowsAreTheExactUnionOfAlternatives) {
    // Two transitions with the same label from L, enabled in [1,2] and [4,5]: non-convex.
    auto m = make(ModelBuilder("gaps")
                      .clock("x")
                      .location("L", {B::c("x", "<=", 5)})
                      .location("A")
                      .location("C")
                      .transition("L", "go!", "A", {B::c("x", ">=", 1), B::c("x", "<=", 2)}, {})
                      .transition("L", "go!", "C", {B::c("x", ">=", 4)}, {})
                      .build());
    Json r = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}}}).value();
    const Json* go = event(r["final"], "go!");
    ASSERT_NE(go, nullptr);
    ASSERT_EQ((*go)["intervals"].size(), 2U) << go->dump();
    EXPECT_EQ((*go)["intervals"][0]["earliest"]["text"], "1");
    EXPECT_EQ((*go)["intervals"][0]["latest"]["text"], "2");
    EXPECT_EQ((*go)["intervals"][1]["earliest"]["text"], "4");
    EXPECT_EQ((*go)["intervals"][1]["latest"]["text"], "5");
    EXPECT_EQ((*go)["alternatives"].size(), 2U);
}

TEST(WhatIf, NondeterministicEventKeepsEveryBranchAndAChosenTransitionSelectsOne) {
    auto m = make(ModelBuilder("branch")
                      .clock("x")
                      .location("L")
                      .location("A")
                      .location("C")
                      .transition("L", "go!", "A", {}, {})
                      .transition("L", "go!", "C", {}, {})
                      .build());
    Json both = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}},
                                              {"steps", {{{"kind", "event"}, {"label", "go!"}, {"delay", "0"}}}}}).value();
    EXPECT_EQ(both["steps"][0]["branches"].size(), 2U);
    EXPECT_FALSE(both["final"]["state"]["deterministic"].get<bool>());
    const std::string chosen = both["steps"][0]["branches"][1]["transition"];
    Json one = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}},
                                             {"steps", {{{"kind", "event"}, {"transition", chosen}, {"delay", "0"}}}}}).value();
    EXPECT_EQ(one["steps"][0]["branches"].size(), 1U);
    EXPECT_TRUE(one["final"]["state"]["deterministic"].get<bool>());
}

TEST(WhatIf, StartsFromARecordedConfigurationAndRejectsInvalidOnes) {
    auto m = machine();
    Json ok = what_if(*m, initial(*m), Json{{"start", {{"kind", "configurations"},
                                                        {"configurations", {{{"location", "Busy"}, {"clocks", {{"x", "3"}, {"y", "7"}}}, {"time", "20"}}}}}}}).value();
    const Json* done = event(ok["final"], "done!");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ((*done)["intervals"][0]["earliest"]["text"], "1");   // x = 3, guard x >= 4
    EXPECT_EQ((*done)["intervals"][0]["latest_at"]["text"], "27");  // x <= 10 → 7 more units
    auto bad = what_if(*m, initial(*m), Json{{"start", {{"kind", "configurations"},
                                                        {"configurations", {{{"location", "Busy"}, {"clocks", {{"x", "12"}}}, {"time", "20"}}}}}}});
    EXPECT_FALSE(bad.ok());
}

TEST(WhatIf, FloatingPointTimesAreRefused) {
    auto m = machine();
    auto r = what_if(*m, initial(*m), Json{{"start", {{"kind", "initial"}}}, {"steps", {{{"kind", "delay"}, {"delay", 1.5}}}}});
    EXPECT_FALSE(r.ok());
}

}  // namespace
}  // namespace twin::runtime
