// Tests for twin::platform: object store, lifecycle, evidence & applicability, audit chain,
// assets graph, telemetry history, packages/deployments.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "twin/core/sha256.hpp"
#include "twin/platform/app_log.hpp"
#include "twin/platform/applicability.hpp"
#include "twin/platform/artifacts.hpp"
#include "twin/platform/assets.hpp"
#include "twin/platform/audit.hpp"
#include "twin/platform/clock.hpp"
#include "twin/platform/database.hpp"
#include "twin/platform/evidence.hpp"
#include "twin/platform/object_store.hpp"
#include "twin/platform/telemetry.hpp"
#include "twin/platform/twins.hpp"

namespace fs = std::filesystem;
using namespace twin;
using namespace twin::platform;

namespace {

constexpr std::int64_t kT0 = 1790000000000;  // 2026-09-21T...Z

class PlatformTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("twin-platform-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(dir_);
        auto db = Database::open(":memory:");
        ASSERT_TRUE(db.ok()) << db.error().to_string();
        db_ = std::move(db).value();
        auto store = ObjectStore::open(dir_);
        ASSERT_TRUE(store.ok());
        store_ = std::make_unique<ObjectStore>(std::move(store).value());
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
    std::unique_ptr<Database> db_;
    std::unique_ptr<ObjectStore> store_;
    ManualClock clock_{kT0};
};

}  // namespace

TEST(Clock, Iso8601RoundTrip) {
    EXPECT_EQ(iso8601_utc(0), "1970-01-01T00:00:00.000Z");
    EXPECT_EQ(iso8601_utc(1790000000123), "2026-09-21T14:13:20.123Z");
    EXPECT_EQ(parse_iso8601_utc("2026-09-21T14:13:20.123Z").value(), 1790000000123);
    EXPECT_EQ(parse_iso8601_utc("2026-09-21T14:13:20Z").value(), 1790000000000);
    EXPECT_FALSE(parse_iso8601_utc("2026-09-21T14:13:20+02:00").ok()) << "only UTC is accepted";
    EXPECT_FALSE(parse_iso8601_utc("yesterday").ok());
}

TEST_F(PlatformTest, ObjectStoreIsContentAddressedAndDetectsCorruption) {
    auto h = store_->put("hello");
    ASSERT_TRUE(h.ok());
    EXPECT_EQ(h.value(), sha256_hex("hello"));
    EXPECT_EQ(store_->put("hello").value(), h.value());
    EXPECT_EQ(store_->get(h.value()).value(), "hello");
    // Tamper with the stored blob.
    const fs::path blob = dir_ / "objects" / h.value().substr(0, 2) / h.value();
    { std::ofstream(blob, std::ios::trunc) << "jello"; }
    auto bad = store_->get(h.value());
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, ErrorCode::IntegrityError);
    EXPECT_EQ(store_->get(std::string(64, 'a')).error().code, ErrorCode::NotFound);
}

TEST_F(PlatformTest, LifecycleEnforcesImmutabilityOfPublishedVersions) {
    ArtifactRepository repo(*db_, *store_, clock_);
    auto v1 = repo.create(ArtifactKind::Ontology, "process-pump", "Process pump", "", "sort A\n", json::Json::object(),
                          "alice", "initial");
    ASSERT_TRUE(v1.ok()) << v1.error().to_string();
    EXPECT_EQ(v1.value().state, Lifecycle::Draft);
    // Cannot publish a draft that was never validated.
    auto early = repo.publish({"process-pump", 1}, "alice");
    ASSERT_FALSE(early.ok());
    EXPECT_EQ(early.error().code, ErrorCode::StateError);

    ASSERT_TRUE(repo.set_state({"process-pump", 1}, Lifecycle::Validating, "alice").ok());
    ASSERT_TRUE(repo.set_state({"process-pump", 1}, Lifecycle::Verified, "alice").ok());
    auto pub = repo.publish({"process-pump", 1}, "alice");
    ASSERT_TRUE(pub.ok()) << pub.error().to_string();
    EXPECT_EQ(pub.value().state, Lifecycle::Published);
    const std::string hash = pub.value().content_sha256;

    // Published content cannot be changed, and publishing twice fails.
    auto edit = repo.save({"process-pump", 1}, "sort B\n", std::nullopt, std::nullopt, "mallory");
    ASSERT_FALSE(edit.ok());
    EXPECT_EQ(edit.error().code, ErrorCode::StateError);
    EXPECT_FALSE(repo.publish({"process-pump", 1}, "alice").ok());
    EXPECT_EQ(repo.version({"process-pump", 1}).value().content_sha256, hash);

    // Edits go to a new draft derived from v1.
    auto v2 = repo.create_draft({"process-pump", 1}, "bob", "tighten");
    ASSERT_TRUE(v2.ok()) << v2.error().to_string();
    EXPECT_EQ(v2.value().version, 2);
    EXPECT_EQ(v2.value().parent, 1);
    EXPECT_EQ(v2.value().content_sha256, hash);
    EXPECT_FALSE(repo.create_draft({"process-pump", 1}, "bob", "again").ok()) << "one open draft at a time";

    // Saving changed content into a VERIFIED draft returns it to DRAFT.
    ASSERT_TRUE(repo.set_state({"process-pump", 2}, Lifecycle::Validating, "bob").ok());
    ASSERT_TRUE(repo.set_state({"process-pump", 2}, Lifecycle::Verified, "bob").ok());
    auto saved = repo.save({"process-pump", 2}, "sort A\nsort B\n", std::nullopt, std::nullopt, "bob");
    ASSERT_TRUE(saved.ok());
    EXPECT_EQ(saved.value().state, Lifecycle::Draft);
    EXPECT_EQ(repo.content({"process-pump", 2}).value(), "sort A\nsort B\n");

    // Publishing v2 supersedes v1; v1's bytes stay resolvable forever.
    ASSERT_TRUE(repo.set_state({"process-pump", 2}, Lifecycle::Validating, "bob").ok());
    ASSERT_TRUE(repo.set_state({"process-pump", 2}, Lifecycle::Verified, "bob").ok());
    ASSERT_TRUE(repo.publish({"process-pump", 2}, "bob").ok());
    EXPECT_EQ(repo.version({"process-pump", 1}).value().state, Lifecycle::Superseded);
    EXPECT_EQ(repo.content({"process-pump", 1}).value(), "sort A\n");
    EXPECT_EQ(repo.published("process-pump").value()->version, 2);
    EXPECT_FALSE(transition_allowed(Lifecycle::Superseded, Lifecycle::Published));
}

TEST_F(PlatformTest, ArtifactIdsAndRefsAreValidated) {
    ArtifactRepository repo(*db_, *store_, clock_);
    EXPECT_FALSE(repo.create(ArtifactKind::Ontology, "Bad Id", "x", "", "", json::Json::object(), "a", "").ok());
    ASSERT_TRUE(repo.create(ArtifactKind::Ontology, "ok", "x", "", "", json::Json::object(), "a", "").ok());
    EXPECT_EQ(repo.create(ArtifactKind::Ontology, "ok", "x", "", "", json::Json::object(), "a", "").error().code,
              ErrorCode::StateError);
    EXPECT_EQ(parse_ref("process-pump@12").value().version, 12);
    EXPECT_FALSE(parse_ref("process-pump").ok());
    EXPECT_FALSE(parse_ref("process-pump@0").ok());
}

TEST_F(PlatformTest, EvidenceForOldHashIsStaleNeverValid) {
    ArtifactRepository repo(*db_, *store_, clock_);
    EvidenceRepository evidence(*db_, *store_, clock_);
    auto ont1 = repo.create(ArtifactKind::Ontology, "k", "K", "", "v1", json::Json::object(), "a", "").value();
    auto interp = repo.create(ArtifactKind::Interpretation, "i-d", "I_D", "", "x", json::Json::object(), "a", "").value();
    auto ev = evidence.record(EvidenceKind::Alignment, Outcome::Pass, "aligned", "aligned", "aligner",
                              json::Json{{"aligned", true}},
                              {{"ontology", ont1.ref(), ont1.content_sha256}, {"dt_interpretation", interp.ref(), interp.content_sha256}},
                              "a");
    ASSERT_TRUE(ev.ok()) << ev.error().to_string();
    EXPECT_EQ(ev.value().document_sha256.size(), 64U);
    EXPECT_EQ(evidence.document(ev.value()).value()["aligned"], true);

    std::vector<Binding> same{{"ontology", ont1.ref(), ont1.content_sha256}, {"dt_interpretation", interp.ref(), interp.content_sha256}};
    EXPECT_EQ(applicability(ev.value(), same, repo).value().state, Applicability::Valid);

    // The deployment now uses ontology v2 (different bytes): the evidence is STALE, with the reason.
    ASSERT_TRUE(repo.set_state(ont1.ref(), Lifecycle::Validating, "a").ok());
    ASSERT_TRUE(repo.set_state(ont1.ref(), Lifecycle::Verified, "a").ok());
    ASSERT_TRUE(repo.publish(ont1.ref(), "a").ok());
    auto ont2 = repo.create_draft(ont1.ref(), "a", "").value();
    ont2 = repo.save(ont2.ref(), "v2", std::nullopt, std::nullopt, "a").value();
    std::vector<Binding> changed{{"ontology", ont2.ref(), ont2.content_sha256},
                                 {"dt_interpretation", interp.ref(), interp.content_sha256}};
    auto st = applicability(ev.value(), changed, repo).value();
    EXPECT_EQ(st.state, Applicability::Stale);
    ASSERT_EQ(st.changed.size(), 1U);
    EXPECT_EQ(st.changed[0].role, "ontology");
    EXPECT_EQ(st.changed[0].evidence_ref, "k@1");
    EXPECT_EQ(st.changed[0].current_ref, "k@2");
    EXPECT_NE(st.reasons[0].find("dependency changed"), std::string::npos);

    // Exact-input lookup finds it only for the exact hashes.
    EXPECT_TRUE(evidence.latest_for(EvidenceKind::Alignment, {{"ontology", ont1.ref(), ont1.content_sha256},
                                                              {"dt_interpretation", interp.ref(), interp.content_sha256}})
                    .value()
                    .has_value());
    EXPECT_FALSE(evidence.latest_for(EvidenceKind::Alignment, {{"ontology", ont2.ref(), ont2.content_sha256},
                                                               {"dt_interpretation", interp.ref(), interp.content_sha256}})
                     .value()
                     .has_value());

    // Rejecting an input invalidates evidence that examined it.
    auto i2 = repo.create_draft(interp.ref(), "a", "");
    ASSERT_FALSE(i2.ok()) << "interp v1 is still an open draft";
    ASSERT_TRUE(repo.set_state(interp.ref(), Lifecycle::Rejected, "a").ok());
    EXPECT_EQ(applicability(ev.value(), same, repo).value().state, Applicability::Invalidated);

    EvidenceFilter f;
    f.artifact_id = "k";
    f.version = 1;
    EXPECT_EQ(evidence.list(f).value().size(), 1U);
    f.version = 2;
    EXPECT_EQ(evidence.count(f).value(), 0);
}

TEST_F(PlatformTest, AuditChainDetectsModificationDeletionAndReordering) {
    AuditLog audit(*db_, clock_);
    for (int i = 0; i < 5; ++i) {
        clock_.advance(1000);
        ASSERT_TRUE(audit.append("ontology.save", "success", "k@" + std::to_string(i + 1),
                                 json::Json{{"n", i}}, "alice").ok());
    }
    auto v = audit.verify().value();
    EXPECT_TRUE(v.valid);
    EXPECT_EQ(v.records, 5);
    EXPECT_EQ(v.head_hash.size(), 64U);

    ASSERT_TRUE(db_->exec("UPDATE audit SET actor = 'mallory' WHERE seq = 3").ok());
    v = audit.verify().value();
    EXPECT_FALSE(v.valid);
    EXPECT_EQ(v.first_invalid, 3);
    EXPECT_NE(v.reason.find("modified"), std::string::npos);

    ASSERT_TRUE(db_->exec("UPDATE audit SET actor = 'alice' WHERE seq = 3").ok());
    EXPECT_TRUE(audit.verify().value().valid) << "restoring the content restores validity";

    ASSERT_TRUE(db_->exec("DELETE FROM audit WHERE seq = 2").ok());
    v = audit.verify().value();
    EXPECT_FALSE(v.valid);
    EXPECT_EQ(v.first_invalid, 3);
    EXPECT_NE(v.reason.find("gap"), std::string::npos);

    AuditFilter filter;
    filter.subject = "k";
    EXPECT_EQ(audit.count(filter).value(), 4);
}

TEST_F(PlatformTest, AssetGraphNeighbourhoodIsBounded) {
    AssetRepository assets(*db_);
    ASSERT_TRUE(assets.upsert({"plant", "Plant A", "Site", std::nullopt, "", json::Json::array(), json::Json::object(), std::nullopt}).ok());
    ASSERT_TRUE(assets.upsert({"line4", "Line 4", "Line", "plant", "", json::Json::array(), json::Json::object(), std::nullopt}).ok());
    ASSERT_TRUE(assets.upsert({"p101", "Pump P-101", "Pump", "line4", "", json::Json::array(), json::Json::object(), "pump-dt"}).ok());
    ASSERT_TRUE(assets.upsert({"m12", "Motor M12", "Motor", "p101", "", json::Json::array(), json::Json::object(), std::nullopt}).ok());
    ASSERT_TRUE(assets.upsert({"tt101", "TT-101", "Sensor", "line4", "", json::Json::array(), json::Json::object(), std::nullopt}).ok());
    ASSERT_TRUE(assets.relate("tt101", "monitors", "p101").ok());
    ASSERT_TRUE(assets.relate("m12", "powers", "p101").ok());
    EXPECT_FALSE(assets.relate("tt101", "contains", "p101").ok());
    EXPECT_FALSE(assets.relate("tt101", "monitors", "nope").ok());

    auto crumbs = assets.ancestors("m12").value();
    ASSERT_EQ(crumbs.size(), 3U);
    EXPECT_EQ(crumbs[0].id, "plant");
    EXPECT_EQ(crumbs[2].id, "p101");

    auto n1 = assets.neighborhood("p101", 1, {}, 50).value();
    std::set<std::string> ids;
    for (const auto& a : n1.nodes) ids.insert(a.id);
    EXPECT_EQ(ids, (std::set<std::string>{"p101", "line4", "m12", "tt101"}));
    EXPECT_EQ(n1.nodes.front().id, "p101");
    EXPECT_FALSE(n1.frontier.empty()) << "line4 has unexplored neighbours (plant)";
    for (const auto& e : n1.edges) {
        EXPECT_TRUE(e.properties.is_object()) << e.source_id << " -" << e.type << "-> " << e.target_id
                                              << ": properties must be an object (API contract), never null";
    }

    auto only_monitors = assets.neighborhood("p101", 2, {"monitors"}, 50).value();
    EXPECT_EQ(only_monitors.nodes.size(), 2U);

    auto bounded = assets.neighborhood("plant", 5, {}, 2).value();
    EXPECT_EQ(bounded.nodes.size(), 2U);
    EXPECT_TRUE(bounded.truncated);

    AssetFilter roots;
    roots.parent_id = "";
    EXPECT_EQ(assets.list(roots).value().size(), 1U);
    AssetFilter text;
    text.text = "pump";
    EXPECT_EQ(assets.count(text).value(), 1);
}

TEST_F(PlatformTest, TelemetryFreshnessAndDownsampling) {
    AssetRepository assets(*db_);
    TelemetryRepository tel(*db_);
    ASSERT_TRUE(assets.upsert({"p101", "Pump", "Pump", std::nullopt, "", json::Json::array(), json::Json::object(), std::nullopt}).ok());
    TelemetryChannel ch{"p101.temp", "p101", "bearing_temp", "number", "degC", "bearing_temp", "sim", 1000, json::Json::object()};
    ASSERT_TRUE(tel.upsert_channel(ch).ok());
    EXPECT_EQ(tel.freshness(ch, kT0).value(), Freshness::Missing);

    std::vector<TelemetrySample> samples;
    for (int i = 0; i < 1000; ++i) {
        samples.push_back({kT0 + i * 1000, kT0 + i * 1000 + 40, 60.0 + (i % 10), std::nullopt, i == 500 ? "bad" : "good", std::nullopt});
    }
    ASSERT_EQ(tel.ingest("p101.temp", samples).value(), 1000);
    EXPECT_EQ(tel.freshness(ch, kT0 + 999000 + 2000).value(), Freshness::Fresh);
    EXPECT_EQ(tel.freshness(ch, kT0 + 999000 + 10000).value(), Freshness::Stale);

    auto raw = tel.query("p101.temp", kT0, kT0 + 99000, 500).value();
    EXPECT_FALSE(raw.downsampled);
    EXPECT_EQ(raw.samples.size(), 100U);

    auto ds = tel.query("p101.temp", kT0, kT0 + 999000, 100).value();
    EXPECT_TRUE(ds.downsampled);
    EXPECT_LE(ds.buckets.size(), 100U);
    EXPECT_EQ(ds.total_samples, 1000);
    double lo = 1e9;
    double hi = -1e9;
    std::int64_t bad = 0;
    for (const auto& b : ds.buckets) {
        lo = std::min(lo, b.min);
        hi = std::max(hi, b.max);
        bad += b.bad;
    }
    EXPECT_EQ(lo, 60.0) << "downsampling keeps the true extremes";
    EXPECT_EQ(hi, 69.0);
    EXPECT_EQ(bad, 1);

    EXPECT_EQ(tel.at("p101.temp", kT0 + 1500).value()->observed_ms, kT0 + 1000);
    EXPECT_FALSE(tel.ingest("p101.temp", {{kT0, kT0, std::nullopt, std::string("hot"), "good", std::nullopt}}).ok());
    EXPECT_FALSE(tel.query("p101.temp", kT0 + 10, kT0, 10).ok());
}

TEST_F(PlatformTest, DeploymentsAreAppendOnlyAndRollbackNeedsAReason) {
    TwinRepository twins(*db_, clock_);
    Twin pump_dt;
    pump_dt.id = "pump-dt";
    pump_dt.name = "Pump DT";
    pump_dt.model_id = "pump";
    ASSERT_TRUE(twins.upsert_twin(pump_dt).ok());
    PackageRecord p;
    p.twin_id = "pump-dt";
    p.directory = "/tmp/x";
    p.package_hash = std::string(64, 'a');
    p.ir_sha256 = std::string(64, 'b');
    p.model_version = "1.0.0";
    p.created_by = "a";
    auto p1 = twins.add_package(p).value();
    auto p2 = twins.add_package(p).value();
    EXPECT_EQ(p1.id, "PKG-0001");
    EXPECT_FALSE(twins.deploy("pump-dt", p1.id, "deploy", "", "a").ok()) << "unreleased packages cannot deploy";
    ASSERT_TRUE(twins.mark_released(p1.id).ok());
    ASSERT_TRUE(twins.mark_released(p2.id).ok());
    EXPECT_FALSE(twins.mark_released(p2.id).ok());
    clock_.advance(1000);
    ASSERT_TRUE(twins.deploy("pump-dt", p1.id, "deploy", "", "a").ok());
    clock_.advance(1000);
    auto d2 = twins.deploy("pump-dt", p2.id, "deploy", "", "a").value();
    EXPECT_EQ(d2.previous_package_id, p1.id);
    EXPECT_FALSE(twins.deploy("pump-dt", p2.id, "deploy", "", "a").ok()) << "already deployed";
    EXPECT_FALSE(twins.deploy("pump-dt", p1.id, "rollback", "", "a").ok()) << "rollback requires a reason";
    auto rb = twins.deploy("pump-dt", p1.id, "rollback", "vibration alarms after v2", "a").value();
    EXPECT_EQ(rb.kind, "rollback");
    EXPECT_EQ(twins.current_deployment("pump-dt").value()->package_id, p1.id);
    EXPECT_EQ(twins.deployments("pump-dt").value().size(), 3U) << "history is kept";
    EXPECT_EQ(twins.package(p2.id).value().state, "released") << "rollback deletes nothing";
}

TEST_F(PlatformTest, AppLogRedactsSecretsAndFilters) {
    AppLog log(dir_ / "logs" / "studio.jsonl", clock_);
    log.write(LogLevel::Info, "studio.http", "GET /api/v1/assets", json::Json{{"status", 200}}, "req-1");
    clock_.advance(10);
    log.write(LogLevel::Error, "studio.runtime", "runtime unreachable",
              json::Json{{"url", "http://x"}, {"api_key", "s3cr3t"}, {"nested", {{"Password", "p"}}}}, "req-2",
              "EXE-1", "p101");
    auto all = log.read({}).value();
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(all[0].message, "runtime unreachable") << "newest first";
    EXPECT_EQ(all[0].fields["api_key"], "[redacted]");
    EXPECT_EQ(all[0].fields["nested"]["Password"], "[redacted]");
    LogFilter errors;
    errors.min_level = LogLevel::Warn;
    EXPECT_EQ(log.read(errors).value().size(), 1U);
    LogFilter by_exec;
    by_exec.execution_id = "EXE-1";
    EXPECT_EQ(log.read(by_exec).value().size(), 1U);
    LogFilter comp;
    comp.component = "studio.http";
    EXPECT_EQ(log.read(comp).value().front().correlation_id, "req-1");
}
