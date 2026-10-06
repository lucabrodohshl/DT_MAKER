/**
 * @file runtime_monitors_test.cpp
 * @brief Monitors shipped in packages and evaluated by the runtime: behavioural properties on
 * committed state sets (verdict changes ledgered), conformance options, instance identity.
 */
#include <gtest/gtest.h>

#include <fstream>

#include "support/drone_package.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/runtime/monitor_host.hpp"
#include "twin/runtime/monitors.hpp"

namespace twin::runtime {
namespace {

using json::Json;

constexpr const char* kMonitors = R"json({
  "format": "twin-monitors/1",
  "requirements": [{"id": "REQ-S1", "title": "No trip", "category": "safety", "severity": "critical",
                    "monitors": ["never-fault"]}],
  "monitors": [
    {"id": "conformance", "kind": "conformance", "name": "Behavioural conformance", "severity": "critical",
     "events": "all", "unmatchedEvents": "record"},
    {"id": "never-fault", "kind": "property", "name": "Never tripped", "severity": "critical", "property": "A[] !FAULT"},
    {"id": "cooling-bound", "kind": "property", "name": "Cooling bound", "severity": "warning",
     "property": "A[] COOLING -> t <= 300"},
    {"id": "envelope", "kind": "property", "name": "Bearing envelope", "severity": "warning",
     "property": "A[] sem((<= bearing_temp bearing_temp_trip))"},
    {"id": "fresh", "kind": "data_quality", "name": "Freshness", "severity": "warning",
     "field": "bearing_temp", "check": "stale", "maxAgeSeconds": 30}
  ],
  "alerts": [{"id": "AL-1", "monitor": "never-fault", "on": "violated", "severity": "critical", "message": "Pump tripped"}]
})json";

package::BuildInputs pump_inputs(const std::filesystem::path& monitors) {
    const std::filesystem::path m = std::filesystem::path(TWIN_SOURCE_DIR) / "examples" / "industrial-pump" / "models";
    package::BuildInputs in;
    in.pt_model = m / "pump_pt.xml";
    in.dt_model = m / "pump_dt.xml";
    in.ontology = m / "process-pump.ont";
    in.pt_interpretation = m / "pt.interp";
    in.dt_interpretation = m / "dt.interp";
    in.model_id = "process-pump-p101";
    in.model_version = "1.0.0";
    if (!monitors.empty()) in.monitors = monitors;
    return in;
}

std::vector<Json> ledger_lines(const std::filesystem::path& ledger) {
    std::vector<Json> out;
    std::ifstream in(ledger);
    std::string line;
    while (std::getline(in, line)) {
        Result<Json> j = json::parse(line);
        if (j) out.push_back(j.value());
    }
    return out;
}

class RuntimeMonitors : public ::testing::Test {
public:
    static void SetUpTestSuite() {
        dir_ = new test::TempDir();  // NOLINT(cppcoreguidelines-owning-memory)
        const std::filesystem::path doc = dir_->path() / "monitors.json";
        std::ofstream(doc) << kMonitors;
        Result<package::BuildResult> built = package::build_package(pump_inputs(doc), dir_->path() / "pump.twinpkg");
        ASSERT_TRUE(built.ok()) << built.error().to_string();
        pkg_ = new package::LoadedPackage(built.value().package);  // NOLINT
        Result<package::BuildResult> plain = package::build_package(pump_inputs({}), dir_->path() / "plain.twinpkg");
        ASSERT_TRUE(plain.ok()) << plain.error().to_string();
        plain_ = new package::LoadedPackage(plain.value().package);  // NOLINT
    }
    static void TearDownTestSuite() {
        delete pkg_;    // NOLINT
        delete plain_;  // NOLINT
        delete dir_;    // NOLINT
    }

protected:
    std::unique_ptr<MonitorHost> host(const package::LoadedPackage& p, std::string instance = {}) {
        MonitorConfig config{ledgers_.path(), false};
        config.instance_id = std::move(instance);
        Result<std::unique_ptr<MonitorHost>> h = MonitorHost::create(p, hub_, config);
        EXPECT_TRUE(h.ok()) << h.error().to_string();
        return h ? std::move(h).value() : nullptr;
    }
    static Json find(const Json& view, const std::string& id) {
        for (const Json& m : view.at("monitors")) {
            if (m.at("id") == id) return m;
        }
        return Json();
    }

    static inline test::TempDir* dir_ = nullptr;
    static inline package::LoadedPackage* pkg_ = nullptr;
    static inline package::LoadedPackage* plain_ = nullptr;
    test::TempDir ledgers_;
    EventHub hub_{100000};
};

TEST_F(RuntimeMonitors, PackageCarriesTheMonitorDocument) {
    ASSERT_TRUE(pkg_->monitors.has_value());
    EXPECT_EQ(pkg_->monitors->at("format"), "twin-monitors/1");
    bool listed = false;
    for (const package::PackageFile& f : pkg_->manifest.files) listed = listed || (f.role == "monitors");
    EXPECT_TRUE(listed);
    EXPECT_FALSE(plain_->monitors.has_value());
}

TEST_F(RuntimeMonitors, ViewListsEveryMonitorWithItsEvaluator) {
    auto h = host(*pkg_);
    ASSERT_TRUE(h);
    const Json view = h->monitors().view();
    EXPECT_EQ(view.at("package_hash"), pkg_->package_hash);
    EXPECT_EQ(view.at("monitors").size(), 5u);
    EXPECT_EQ(find(view, "conformance").at("evaluator"), "runtime");
    EXPECT_EQ(find(view, "conformance").at("status"), "satisfied");
    EXPECT_EQ(find(view, "never-fault").at("evaluator"), "runtime");
    EXPECT_EQ(find(view, "never-fault").at("status"), "satisfied");
    EXPECT_EQ(find(view, "envelope").at("evaluator"), "studio");
    EXPECT_EQ(find(view, "envelope").at("status"), "external");
    EXPECT_EQ(find(view, "fresh").at("evaluator"), "ingest");
    EXPECT_EQ(view.at("requirements").size(), 1u);
    EXPECT_EQ(view.at("alerts").size(), 1u);
}

TEST_F(RuntimeMonitors, VerdictChangesAreRecordedInTheLedger) {
    auto h = host(*pkg_);
    ASSERT_TRUE(h);
    ASSERT_TRUE(h->pt_event(Json{{"label", "start_cmd!"}, {"time", "1"}}));
    ASSERT_TRUE(h->pt_event(Json{{"label", "emergency_trip!"}, {"time", "2"}}));
    const Json view = h->monitors().view();
    EXPECT_EQ(find(view, "never-fault").at("status"), "violated");
    EXPECT_EQ(find(view, "never-fault").at("since").at("ticks"), 2000);
    EXPECT_EQ(find(view, "cooling-bound").at("status"), "satisfied");
    bool recorded = false;
    for (const Json& line : ledger_lines(h->session()->ledger_path())) {
        const Json& body = line.at("body");
        if (body.at("kind") == "context" && body.at("fields").at("topic") == "monitor") {
            const Json& data = body.at("fields").at("data");
            recorded = recorded || (data.at("monitor") == "never-fault" && data.at("verdict") == "violated" &&
                                    data.at("previous") == "satisfied");
        }
    }
    EXPECT_TRUE(recorded);
    // The ledger still verifies and replays with the monitor records in it.
    Result<ledger::ReplayReport> replay = ledger::replay_file(*pkg_, h->session()->ledger_path());
    ASSERT_TRUE(replay) << replay.error().to_string();
    EXPECT_TRUE(replay.value().identical);
    EXPECT_GE(replay.value().contexts, 1u);
}

TEST_F(RuntimeMonitors, UnmatchedEventsAreRecordedWhenThePolicySaysSo) {
    auto h = host(*pkg_);
    ASSERT_TRUE(h);
    Result<Json> r = h->pt_event(Json{{"label", "unknown_signal!"}, {"time", "1"}});
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_FALSE(r.value().at("accepted").get<bool>());
    EXPECT_EQ(r.value().at("reason"), "unmatched_event");
    bool alarm = false;
    for (const Json& line : ledger_lines(h->session()->ledger_path())) {
        alarm = alarm || line.at("body").at("kind") == "alarm";
    }
    EXPECT_TRUE(alarm);
    // Without the policy (no monitor document) the same event is refused as before.
    auto plain = host(*plain_);
    ASSERT_TRUE(plain);
    EXPECT_FALSE(plain->pt_event(Json{{"label", "unknown_signal!"}, {"time", "1"}}));
}

TEST_F(RuntimeMonitors, DefaultConformanceMonitorWithoutADocument) {
    auto h = host(*plain_);
    ASSERT_TRUE(h);
    const Json view = h->monitors().view();
    ASSERT_EQ(view.at("monitors").size(), 1u);
    EXPECT_EQ(view.at("monitors")[0].at("kind"), "conformance");
    EXPECT_TRUE(view.at("monitors_sha256").is_null());
}

TEST_F(RuntimeMonitors, InstanceIdentityIsRecorded) {
    auto h = host(*plain_, "pump-017");
    ASSERT_TRUE(h);
    EXPECT_EQ(h->status().at("instance"), "pump-017");
    bool recorded = false;
    for (const Json& line : ledger_lines(h->session()->ledger_path())) {
        const Json& body = line.at("body");
        recorded = recorded || (body.at("kind") == "context" && body.at("fields").at("topic") == "instance" &&
                                body.at("fields").at("data").at("id") == "pump-017");
    }
    EXPECT_TRUE(recorded);
}

}  // namespace
}  // namespace twin::runtime
