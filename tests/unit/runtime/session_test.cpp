/**
 * @file session_test.cpp
 * @brief TwinSession: commit protocol, rejection, isolation of prediction, fail-stop.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "support/drone_package.hpp"
#include "support/mission_script.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/ledger/verifier.hpp"
#include "twin/runtime/session.hpp"

namespace twin::runtime {
namespace {

using test::DronePackageSuite;
using test::TempDir;

class SessionTest : public DronePackageSuite {
protected:
    std::unique_ptr<TwinSession> start(const TempDir& dir, bool deterministic = true,
                                       std::optional<std::uint64_t> fault = std::nullopt) {
        SessionOptions o;
        o.ledger_path = dir / "run.ledger.jsonl";
        o.deterministic = deterministic;
        o.fsync = false;
        o.ledger_fault_after_records = fault;
        Result<std::unique_ptr<TwinSession>> s = TwinSession::start(pkg(), o);
        EXPECT_TRUE(s.ok()) << (s.ok() ? "" : s.error().to_string());
        return s.ok() ? std::move(s).value() : nullptr;
    }

    std::string location(const TwinSession& s) {
        const Snapshot snap = s.snapshot();
        EXPECT_TRUE(snap.state.is_singleton());
        return s.model().ir().locations.at(snap.state.members().front().location).id;
    }
};

TEST_F(SessionTest, StartsInTheFormalInitialState) {
    TempDir dir;
    auto s = start(dir);
    ASSERT_TRUE(s);
    EXPECT_EQ(location(*s), "INITIALIZING");
    const Snapshot snap = s->snapshot();
    EXPECT_EQ(snap.state.time(), 0);
    EXPECT_EQ(snap.ledger_records, 1U);  // genesis
    ASSERT_TRUE(snap.deadline.has_value());
    EXPECT_EQ(*snap.deadline, 30000);  // INITIALIZING: t_mode <= 30
}

TEST_F(SessionTest, ScriptedMissionFollowsTheKernel) {
    TempDir dir;
    auto s = start(dir);
    ASSERT_TRUE(s);
    for (const test::ScriptedInput& step : test::mission_script()) {
        const std::string before = location(*s);
        Result<SubmitResult> r = s->submit(step.input);
        ASSERT_TRUE(r.ok()) << r.error().to_string();
        EXPECT_EQ(r.value().accepted, step.expect_accepted) << step.input.name << " @" << step.input.at;
        EXPECT_EQ(location(*s), step.expect_location) << step.input.name;
        if (!step.expect_accepted) {
            EXPECT_EQ(location(*s), before);
            ASSERT_TRUE(r.value().rejection.has_value());
        }
    }
    ASSERT_TRUE(s->close("mission complete").ok());
    EXPECT_FALSE(s->submit(test::label("start_mission!", 50000)).ok());  // closed
}

TEST_F(SessionTest, LedgerOfAMissionVerifiesAndReplaysIdentically) {
    TempDir dir;
    auto s = start(dir);
    ASSERT_TRUE(s);
    for (const test::ScriptedInput& step : test::mission_script()) ASSERT_TRUE(s->submit(step.input).ok());
    ASSERT_TRUE(s->close("mission complete").ok());

    const ledger::VerificationReport v =
        ledger::verify_file(s->ledger_path(), ledger::VerifyOptions{pkg().package_hash, std::nullopt, true});
    EXPECT_TRUE(v.valid);
    EXPECT_EQ(v.records, 1U + test::mission_script().size() + 1U);
    Result<ledger::ReplayReport> replay = ledger::replay_file(pkg(), s->ledger_path());
    ASSERT_TRUE(replay.ok()) << replay.error().to_string();
    EXPECT_TRUE(replay.value().identical);
    for (const ledger::ReplayMismatch& m : replay.value().mismatches) ADD_FAILURE() << m.seq << ": " << m.detail;
    EXPECT_EQ(replay.value().rejections, 4U);
    EXPECT_EQ(replay.value().delays, 1U);
}

TEST_F(SessionTest, DeterministicModeYieldsByteIdenticalLedgers) {
    TempDir a;
    TempDir b;
    auto s1 = start(a);
    auto s2 = start(b);
    for (const test::ScriptedInput& step : test::mission_script()) {
        ASSERT_TRUE(s1->submit(step.input).ok());
        ASSERT_TRUE(s2->submit(step.input).ok());
    }
    std::ifstream f1(s1->ledger_path()), f2(s2->ledger_path());
    std::stringstream b1, b2;
    b1 << f1.rdbuf();
    b2 << f2.rdbuf();
    EXPECT_EQ(b1.str(), b2.str());
}

TEST_F(SessionTest, PredictionAndProjectionDoNotMutateLiveState) {
    TempDir dir;
    auto s = start(dir);
    ASSERT_TRUE(s->submit(test::label("mission_loaded!", 1000)).ok());
    const Snapshot before = s->snapshot();
    (void)s->predict(kernel::ExplorationLimits{6, std::nullopt, 500});
    (void)s->projected(5000);
    std::vector<kernel::ScheduledObservation> plan{
        {2000, kernel::Selector::label(*s->model().label_id("start_mission!"))}};
    ASSERT_TRUE(s->simulate(plan).ok());
    const Snapshot after = s->snapshot();
    EXPECT_EQ(before.state, after.state);
    EXPECT_EQ(before.ledger_records, after.ledger_records);
}

TEST_F(SessionTest, MissedDeadlineIsVisibleInProjection) {
    TempDir dir;
    auto s = start(dir);
    EXPECT_TRUE(s->projected(30000).ok());
    Result<kernel::StateSet> late = s->projected(30001);  // INITIALIZING allows at most 30 s
    ASSERT_FALSE(late.ok());
    EXPECT_EQ(late.error().code, ErrorCode::IncompatibleObservation);
}

TEST_F(SessionTest, FailStopWhenTheLedgerBecomesUnavailable) {
    TempDir dir;
    auto s = start(dir, true, /*fault after*/ 3);  // genesis + 2 records succeed
    ASSERT_TRUE(s->submit(test::label("mission_loaded!", 1000)).ok());
    ASSERT_TRUE(s->submit(test::label("start_mission!", 2000)).ok());
    const Snapshot before = s->snapshot();
    Result<SubmitResult> r = s->submit(test::label("takeoff_complete!", 5000));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Unavailable);
    const Snapshot after = s->snapshot();
    EXPECT_TRUE(after.failed);
    EXPECT_EQ(after.state, before.state);  // the unrecorded step was NOT committed
    EXPECT_FALSE(s->submit(test::label("takeoff_complete!", 6000)).ok());
    EXPECT_TRUE(ledger::verify_file(s->ledger_path()).valid);  // what was written is intact
}

TEST_F(SessionTest, ListenersSeeEveryCommittedRecord) {
    TempDir dir;
    auto s = start(dir);
    std::vector<std::string> kinds;
    s->subscribe([&kinds](const SessionEvent& e) { kinds.push_back(e.kind); });
    ASSERT_TRUE(s->submit(test::label("mission_loaded!", 1000)).ok());
    ASSERT_TRUE(s->submit(test::label("bogus!", 1500)).ok());
    ASSERT_TRUE(s->submit(test::advance(2000)).ok());
    EXPECT_EQ(kinds, (std::vector<std::string>{"step", "reject", "delay"}));
}

}  // namespace
}  // namespace twin::runtime
