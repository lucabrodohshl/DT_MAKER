// Integration tests for twin::studio: Services use cases on the seeded industrial-pump
// example (real validation, refinement, alignment, compilation, packaging) and the HTTP API.
#include <gtest/gtest.h>
#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

#include "twin/studio/seed.hpp"
#include "twin/studio/server.hpp"
#include "twin/studio/services.hpp"

namespace fs = std::filesystem;
using namespace twin;
using namespace twin::studio;
using json::Json;

namespace {

const fs::path kExample = fs::path(TWIN_SOURCE_DIR) / "examples" / "industrial-pump";

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// One seeded Studio shared by the suite (seeding runs the real toolchain).
class StudioTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dir_ = new fs::path(fs::temp_directory_path() / ("twin-studio-test-" + std::to_string(std::random_device{}())));
        StudioConfig config;
        config.data_dir = *dir_;
        auto s = Services::open(config);
        ASSERT_TRUE(s.ok()) << s.error().to_string();
        services_ = s.value().release();
        SeedOptions options;
        options.telemetry_history = true;
        auto r = seed_example(*services_, kExample, options);
        ASSERT_TRUE(r.ok()) << r.error().to_string();
        seed_ = new Json(r.value());
    }
    static void TearDownTestSuite() {
        delete services_;
        delete seed_;
        std::error_code ec;
        fs::remove_all(*dir_, ec);
        delete dir_;
    }
    static Services& s() { return *services_; }
    static inline Services* services_ = nullptr;
    static inline Json* seed_ = nullptr;
    static inline fs::path* dir_ = nullptr;
    Actor alice{"alice"};
};

}  // namespace

TEST_F(StudioTest, SeedDeploysAnAlignedVerifiedTwin) {
    auto t = s().twin("pump-p101-dt");
    ASSERT_TRUE(t.ok()) << t.error().to_string();
    const Json& trust = t.value()["trust"];
    EXPECT_EQ(trust["alignment"]["state"], "pass");
    EXPECT_EQ(trust["packageIntegrity"]["state"], "pass");
    EXPECT_EQ(trust["compilation"]["state"], "pass");
    EXPECT_EQ(trust["runtimeCompatibility"]["state"], "pass");
    EXPECT_EQ(trust["ontologyRefinement"]["state"], "not_applicable");
    EXPECT_EQ(t.value()["deployment"]["packageId"], "PKG-0001");
    EXPECT_EQ(t.value()["bindings"].size(), 5U);
    // The seeded maintenance history contains real NOT-A-REFINEMENT evidence.
    ASSERT_EQ(seed_->at("maintenanceHistory").size(), 1U);
    EXPECT_EQ(seed_->at("maintenanceHistory")[0]["verdict"], "not_a_refinement");
    EXPECT_GT(seed_->at("telemetrySamples").get<std::int64_t>(), 100000);
}

TEST_F(StudioTest, PublishedVersionsAreImmutableAndPublishRequiresValidation) {
    auto saved = s().save_draft({"process-pump", 1}, "sort X\n", std::nullopt, std::nullopt, alice);
    ASSERT_FALSE(saved.ok());
    EXPECT_EQ(saved.error().code, ErrorCode::StateError);
    auto change = s().create_change("pump-p101-dt", "unvalidated draft", "", alice);
    ASSERT_TRUE(change.ok());
    auto draft = s().add_to_change(change.value()["id"].get<std::string>(), "p101-dt-semantics", "x", alice);
    ASSERT_TRUE(draft.ok()) << draft.error().to_string();
    const platform::ArtifactRef ref{"p101-dt-semantics", draft.value()["version"].get<std::int64_t>()};
    auto pub = s().publish(ref, alice);
    ASSERT_FALSE(pub.ok());
    EXPECT_EQ(pub.error().code, ErrorCode::StateError);
    // Cleanup: abandon (rejects the draft) so other tests can create drafts of this artefact.
    ASSERT_TRUE(s().abandon_change(change.value()["id"].get<std::string>(), "test cleanup", alice).ok());
    EXPECT_EQ(s().version(ref).value()["state"], "rejected");
}

TEST_F(StudioTest, ValidationReportsDiagnosticsAndKeepsDraftOnFailure) {
    auto change = s().create_change("pump-p101-dt", "broken edit", "", alice);
    ASSERT_TRUE(change.ok());
    const std::string cid = change.value()["id"].get<std::string>();
    auto draft = s().add_to_change(cid, "p101-pt-semantics", "x", alice);
    ASSERT_TRUE(draft.ok()) << draft.error().to_string();
    const platform::ArtifactRef ref{"p101-pt-semantics", draft.value()["version"].get<std::int64_t>()};
    auto saved = s().save_draft(ref, "RUN_NORMAL : (> unknown_symbol 0)\n", std::nullopt, std::nullopt, alice);
    ASSERT_TRUE(saved.ok());
    EXPECT_FALSE(saved.value()["diagnostics"].empty()) << "strict diagnostics are returned on save";
    auto v = s().validate(ref, alice);
    ASSERT_TRUE(v.ok()) << v.error().to_string();
    EXPECT_EQ(v.value()["state"], "draft");
    EXPECT_EQ(v.value()["validation"]["outcome"], "fail");
    ASSERT_TRUE(s().abandon_change(cid, "test cleanup", alice).ok());
}

TEST_F(StudioTest, OntologyEvolutionFullLifecycle) {
    // 1. Change workspace + draft from the deployed ontology.
    auto change = s().create_change("pump-p101-dt", "Datasheet rev C", "rated speed fixed, lube oil supervision", alice);
    ASSERT_TRUE(change.ok());
    const std::string cid = change.value()["id"].get<std::string>();
    auto draft = s().add_to_change(cid, "process-pump", "rev C", alice);
    ASSERT_TRUE(draft.ok()) << draft.error().to_string();
    const platform::ArtifactRef ref{"process-pump", draft.value()["version"].get<std::int64_t>()};
    ASSERT_TRUE(s().save_draft(ref, read_file(kExample / "evolution/process-pump-v2.ont"), std::nullopt, std::nullopt, alice).ok());

    // 2. Before checks: release is blocked, alignment is not established, evidence is stale for the change.
    auto p0 = s().pipeline(cid);
    ASSERT_TRUE(p0.ok()) << p0.error().to_string();
    EXPECT_FALSE(p0.value()["releaseReady"].get<bool>());
    auto rel0 = s().release(cid, alice);
    ASSERT_FALSE(rel0.ok());
    EXPECT_EQ(rel0.error().code, ErrorCode::StateError);

    // 3. Impact: alignment requires verification (no refinement evidence yet), package definitely stale.
    auto impact = s().impact(ref);
    ASSERT_TRUE(impact.ok()) << impact.error().to_string();
    std::map<std::string, std::string> cls;
    for (const auto& n : impact.value()["nodes"]) cls[n["type"].get<std::string>()] = n["classification"].get<std::string>();
    EXPECT_EQ(cls["alignment"], "requires_verification");
    EXPECT_EQ(cls["package"], "definitely_stale");
    EXPECT_EQ(cls["compilation"], "unaffected") << "the IR does not depend on the ontology";

    // 4. Diff shows the structural changes.
    auto d = s().diff({"process-pump", 1}, ref);
    ASSERT_TRUE(d.ok());
    bool modified_speed = false;
    bool added_lube = false;
    for (const auto& c : d.value()["structural"]["changes"]) {
        modified_speed = modified_speed || (c["name"] == "rated_speed_bound" && c["kind"] == "modified");
        added_lube = added_lube || (c["name"] == "lube_oil_temp" && c["kind"] == "added");
    }
    EXPECT_TRUE(modified_speed);
    EXPECT_TRUE(added_lube);

    // 5. Validate + refinement (valid) -> alignment preserved by Theorem 3.
    ASSERT_TRUE(s().run_stage(cid, "validate", alice).ok());
    auto refinement = s().run_stage(cid, "refinement", alice);
    ASSERT_TRUE(refinement.ok()) << refinement.error().to_string();
    EXPECT_EQ(refinement.value()["verdict"], "valid_refinement");
    auto p1 = s().pipeline(cid).value();
    for (const auto& st : p1["stages"]) {
        if (st["id"] == "alignment") {
            EXPECT_EQ(st["state"], "pass");
            EXPECT_EQ(st["route"], "theorem3");
        }
    }
    auto impact2 = s().impact(ref).value();
    for (const auto& n : impact2["nodes"]) {
        if (n["type"] == "alignment") EXPECT_EQ(n["classification"], "preserved");
    }

    // 6. Compile, package, verify -> ready; release publishes; deploy; history retains old deployment.
    for (const char* st : {"compile", "package"}) {
        auto r = s().run_stage(cid, st, alice);
        ASSERT_TRUE(r.ok()) << st << ": " << r.error().to_string();
        EXPECT_EQ(r.value()["outcome"], "pass") << st << ": " << r.value().dump();
    }
    auto p2 = s().pipeline(cid).value();
    EXPECT_TRUE(p2["releaseReady"].get<bool>()) << p2.dump(2);
    auto rel = s().release(cid, alice);
    ASSERT_TRUE(rel.ok()) << rel.error().to_string();
    EXPECT_EQ(s().version(ref).value()["state"], "published");
    EXPECT_EQ(s().version({"process-pump", 1}).value()["state"], "superseded");
    const std::string pkg = p2["packageId"].get<std::string>();
    ASSERT_TRUE(s().deploy("pump-p101-dt", pkg, "rev C", alice).ok());

    auto deps = s().deployments("pump-p101-dt").value();
    ASSERT_GE(deps.size(), 2U);
    EXPECT_EQ(deps[0]["packageId"], pkg);
    EXPECT_EQ(deps[1]["artifacts"]["ontology"], "process-pump@1") << "history keeps the historical ontology version";

    // 7. Rollback: preview shows the ontology difference; requires a reason; nothing deleted.
    auto preview = s().rollback_preview("pump-p101-dt", "PKG-0001").value();
    EXPECT_TRUE(preview["allowed"].get<bool>());
    EXPECT_FALSE(s().rollback("pump-p101-dt", "PKG-0001", "", alice).ok());
    ASSERT_TRUE(s().rollback("pump-p101-dt", "PKG-0001", "verification of rev C in the field pending", alice).ok());
    EXPECT_EQ(s().twin("pump-p101-dt").value()["deployment"]["packageId"], "PKG-0001");
    EXPECT_EQ(s().package(pkg).value()["state"], "released");

    // 8. The engineering audit chain is intact.
    auto audit = s().verify_audit().value();
    EXPECT_TRUE(audit["valid"].get<bool>()) << audit.dump();
}

TEST_F(StudioTest, EvaluationExplainsMeaningFromTelemetry) {
    auto r = s().evaluate({"p101-dt-semantics", 1}, {{"bearing_temp", "94.1"}, {"vibration_rms", "3.2"}}, std::nullopt,
                          {"DEGRADED", "condition_degraded!"});
    ASSERT_TRUE(r.ok()) << r.error().to_string();
    for (const auto& e : r.value()["entries"]) {
        if (e["key"] == "condition_degraded!") EXPECT_EQ(e["truth"], "true");
    }
    // From the asset's latest telemetry (symbol-bound channels), with provenance of every observation.
    auto live = s().evaluate({"p101-dt-semantics", 1}, {}, std::string("pump-p101"), {});
    ASSERT_TRUE(live.ok()) << live.error().to_string();
    EXPECT_FALSE(live.value()["observations"].empty());
    for (const auto& o : live.value()["observations"]) {
        EXPECT_TRUE(o.contains("observedAt"));
        EXPECT_TRUE(o.contains("channelId"));
    }
}

TEST_F(StudioTest, TelemetryAndGraphQueries) {
    auto ch = s().telemetry_channels("pump-p101", true);
    ASSERT_TRUE(ch.ok());
    EXPECT_GE(ch.value()["channels"].size(), 6U);
    const std::int64_t now = s().clock().now_ms();
    auto series = s().telemetry_series("pump-p101.bearing_temp", now - 7 * 86400000LL, now, 500);
    ASSERT_TRUE(series.ok());
    EXPECT_TRUE(series.value()["downsampled"].get<bool>());
    EXPECT_LE(series.value()["buckets"].size(), 500U);
    auto n = s().neighborhood("pump-p101", 1, {}, 50);
    ASSERT_TRUE(n.ok());
    EXPECT_GE(n.value()["nodes"].size(), 5U);
    auto hits = s().search("bearing_temp", 20).value()["hits"];
    bool symbol = false;
    for (const auto& h : hits) symbol = symbol || h["kind"] == "ontology_symbol";
    EXPECT_TRUE(symbol);
}

TEST_F(StudioTest, HttpApiErrorsProxyAndStream) {
    ServerOptions options;
    options.port = 0;
    StudioServer server(s(), options);
    auto port = server.bind();
    ASSERT_TRUE(port.ok()) << port.error().to_string();
    std::thread t([&] { (void)server.listen(); });
    httplib::Client cli("127.0.0.1", port.value());
    cli.set_read_timeout(60, 0);

    auto about = cli.Get("/api/v1/about");
    ASSERT_TRUE(about);
    EXPECT_EQ(about->status, 200);
    EXPECT_FALSE(about->get_header_value("X-Request-Id").empty());

    auto missing = cli.Get("/api/v1/assets/nope");
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 404);
    EXPECT_EQ(json::parse(missing->body).value()["error"]["code"], "not_found");

    auto immut = cli.Put("/api/v1/artifacts/p101-pt-view/versions/1", R"({"content":"x"})", "application/json");
    ASSERT_TRUE(immut);
    EXPECT_EQ(immut->status, 409);

    auto bad = cli.Post("/api/v1/changes", "{not json", "application/json");
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->status, 400);

    auto runtime = cli.Get("/api/v1/twins/pump-p101-dt/runtime/state");
    ASSERT_TRUE(runtime);
    EXPECT_EQ(runtime->status, 503);
    EXPECT_EQ(json::parse(runtime->body).value()["error"]["code"], "runtime_not_connected");

    // SSE: a hello event first, then a published event with an id.
    std::string received;
    std::thread reader([&] {
        httplib::Client sse("127.0.0.1", port.value());
        sse.set_read_timeout(5, 0);
        (void)sse.Get("/api/v1/stream", [&](const char* data, std::size_t len) {
            received.append(data, len);
            return received.find("event: test") == std::string::npos;
        });
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    s().events().publish("test", {{"x", 1}}, "2026-01-01T00:00:00.000Z");
    reader.join();
    EXPECT_NE(received.find("event: hello"), std::string::npos);
    EXPECT_NE(received.find("event: test"), std::string::npos);
    EXPECT_NE(received.find("id: "), std::string::npos);

    server.stop();
    t.join();
}
