/**
 * @file monitor_test.cpp
 * @brief Monitor-mode host, recorded executions, package registry and context records.
 *
 * Uses the industrial-pump package (a second, unrelated aligned model) to show
 * that the runtime's monitoring path is generic: PT events are translated only
 * through the label equivalence E of the package's alignment evidence.
 */
#include <gtest/gtest.h>

#include <fstream>

#include "support/drone_package.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/runtime/executions.hpp"
#include "twin/runtime/monitor_host.hpp"

namespace twin::runtime {
namespace {

using json::Json;

package::BuildInputs pump_inputs() {
    const std::filesystem::path m = std::filesystem::path(TWIN_SOURCE_DIR) / "examples" / "industrial-pump" / "models";
    package::BuildInputs in;
    in.pt_model = m / "pump_pt.xml";
    in.dt_model = m / "pump_dt.xml";
    in.ontology = m / "process-pump.ont";
    in.pt_interpretation = m / "pt.interp";
    in.dt_interpretation = m / "dt.interp";
    in.model_id = "process-pump-p101";
    in.model_version = "1.0.0";
    return in;
}

class Monitor : public ::testing::Test {
public:
    static void SetUpTestSuite() {
        dir_ = new test::TempDir();  // NOLINT(cppcoreguidelines-owning-memory)
        Result<package::BuildResult> built = package::build_package(pump_inputs(), dir_->path() / "pump.twinpkg");
        ASSERT_TRUE(built.ok()) << built.error().to_string();
        pkg_ = new package::LoadedPackage(built.value().package);  // NOLINT
    }
    static void TearDownTestSuite() {
        delete pkg_;  // NOLINT
        delete dir_;  // NOLINT
    }

protected:
    void SetUp() override {
        Result<std::unique_ptr<MonitorHost>> h = MonitorHost::create(*pkg_, hub_, MonitorConfig{ledgers_.path(), false});
        ASSERT_TRUE(h.ok()) << h.error().to_string();
        host_ = std::move(h).value();
    }

    static inline test::TempDir* dir_ = nullptr;
    static inline package::LoadedPackage* pkg_ = nullptr;
    test::TempDir ledgers_;
    EventHub hub_{100000};
    std::unique_ptr<MonitorHost> host_;
};

TEST_F(Monitor, PtEventsAreTranslatedThroughEAndDecidedByTheKernel) {
    Result<Json> started = host_->pt_event(Json{{"label", "start_cmd!"}, {"ticks", 5000}});
    ASSERT_TRUE(started.ok()) << started.error().to_string();
    EXPECT_TRUE(started.value().value("accepted", false));
    EXPECT_EQ(started.value().value("label", std::string()), "restart!");  // E: start_cmd! ~ restart!
    EXPECT_EQ(started.value().value("to", std::string()), "NORMAL");
    // A PT event the model cannot explain now (cooling done without cooling): rejected, recorded.
    Result<Json> wrong = host_->pt_event(Json{{"label", "aux_cooling_done!"}, {"ticks", 6000}});
    ASSERT_TRUE(wrong.ok());
    EXPECT_FALSE(wrong.value().value("accepted", true));
    const Snapshot snap = host_->session()->snapshot();
    EXPECT_FALSE(snap.monitoring.conformant());
    EXPECT_EQ(snap.monitoring.observations_rejected, 1U);
    // Labels E does not translate are refused before reaching the kernel.
    EXPECT_FALSE(host_->pt_event(Json{{"label", "warp_drive!"}, {"ticks", 7000}}).ok());
    // Floating-point times are never accepted.
    EXPECT_FALSE(host_->pt_event(Json{{"label", "duty_up!"}, {"time", 7.5}}).ok());
}

TEST_F(Monitor, TelemetryIsLoggedAndAnchoredToTheLedger) {
    ASSERT_FALSE(host_->telemetry(Json{{"at", 1.5}}).ok());
    ASSERT_TRUE(host_->telemetry(Json{{"at", 1000}, {"bearing_temp", 72.5}}).ok());
    ASSERT_TRUE(host_->pt_event(Json{{"label", "start_cmd!"}, {"ticks", 2000}}).ok());
    ASSERT_TRUE(host_->telemetry(Json{{"at", 3000}, {"bearing_temp", 73.0}}).ok());
    ExecutionStore store(ledgers_.path());
    const std::vector<ExecutionInfo> all = store.list();
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all.front().package_hash, pkg_->package_hash);
    Result<Json> t = store.telemetry(all.front(), 10);
    ASSERT_TRUE(t.ok());
    ASSERT_EQ(t.value().at("samples").size(), 2U);
    EXPECT_LT(t.value().at("samples")[0].value("ledger_seq", 0), t.value().at("samples")[1].value("ledger_seq", 0));
}

TEST_F(Monitor, ExecutionsReplayOnlyWithTheirRecordedPackage) {
    ASSERT_TRUE(host_->pt_event(Json{{"label", "start_cmd!"}, {"ticks", 5000}}).ok());
    ASSERT_TRUE(host_->session()->record_context("note", 5000, Json{{"text", "operator remark"}}).ok());
    // A malformed context (floating point) is refused without fail-stopping the session.
    EXPECT_FALSE(host_->session()->record_context("note", 5000, Json{{"value", 0.5}}).ok());
    EXPECT_FALSE(host_->session()->snapshot().failed);
    ExecutionStore store(ledgers_.path());
    Result<ExecutionInfo> e = store.find(host_->session()->session_id());
    ASSERT_TRUE(e.ok());
    PackageRegistry registry(*pkg_, {});
    Result<package::LoadedPackage> p = registry.get(e.value().package_hash);
    ASSERT_TRUE(p.ok());
    EXPECT_EQ(registry.get(std::string(64, 'f')).error().code, ErrorCode::NotFound);
    Result<std::string> text = store.ledger_text(e.value());
    ASSERT_TRUE(text.ok());
    Result<ledger::ReplayReport> r = ledger::replay_text(p.value(), text.value(), ledger::ReplayOptions{true});
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().identical);
    EXPECT_EQ(r.value().contexts, 1U);
    EXPECT_EQ(r.value().frames.size(), e.value().records);
}

TEST_F(Monitor, ResetStartsANewSessionAndKeepsTheOldExecution) {
    const std::string first = host_->session()->session_id();
    ASSERT_TRUE(host_->reset().ok());
    EXPECT_NE(host_->session()->session_id(), first);
    EXPECT_EQ(ExecutionStore(ledgers_.path()).list().size(), 2U);
}

}  // namespace
}  // namespace twin::runtime
