/**
 * @file engine_routes_test.cpp
 * @brief Studio engine routes (authoring services) through the route table, and over HTTP.
 */
#include <gtest/gtest.h>
#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "twin/alignment/alignment.hpp"
#include "twin/authoring/text.hpp"
#include "twin/studio_engine/engine.hpp"
#include "twin/studio_engine/http.hpp"

namespace twin::studio_engine {
namespace {

namespace fs = std::filesystem;

constexpr const char* kSmall = R"(automaton Small {
    clock x;
    channel go;

    initial location A { invariant x <= 5; }
    location B;

    edge e1: A -> B { guard x >= 2; sync go!; reset x; }
}
)";

class EngineRoutes : public ::testing::Test {
protected:
    void SetUp() override {
        // One directory per test: ctest runs the tests of this file as parallel processes.
        dir_ = fs::temp_directory_path() /
               ("twin-engine-routes-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(dir_);
        engine_ = std::make_unique<Engine>(EngineConfig{dir_});
        for (Route& r : engine_->routes()) routes_.push_back(std::move(r));
    }

    /// Call the route registered for @p method + @p pattern.
    Result<json::Json> call(const std::string& method, const std::string& pattern, json::Json body,
                            std::map<std::string, std::string> path = {}) {
        for (const Route& r : routes_) {
            if (r.method == method && r.pattern == pattern) {
                Request req;
                req.body = std::move(body);
                req.path = std::move(path);
                return r.handler(req);
            }
        }
        ADD_FAILURE() << "no route " << method << " " << pattern;
        return make_error(ErrorCode::NotFound, "no route");
    }

    static std::string read(const std::string& relative) {
        std::ifstream in(fs::path(TWIN_SOURCE_DIR) / relative, std::ios::binary);
        std::stringstream b;
        b << in.rdbuf();
        return b.str();
    }

    fs::path dir_;
    std::unique_ptr<Engine> engine_;
    std::vector<Route> routes_;
};

TEST_F(EngineRoutes, ParseReturnsModelDiagnosticsSymbolsAndDigests) {
    Result<json::Json> r = call("POST", "/api/v1/authoring/models/parse", {{"source", kSmall}});
    ASSERT_TRUE(r) << r.error().to_string();
    const json::Json& j = r.value();
    EXPECT_TRUE(j.at("valid").get<bool>());
    EXPECT_EQ(j.at("model").at("format"), "twin-ta/1");
    EXPECT_TRUE(j.at("diagnostics").empty());
    EXPECT_FALSE(j.at("symbols").empty());
    EXPECT_EQ(j.at("semanticDigest").get<std::string>().size(), 64u);
    EXPECT_EQ(j.at("contentSha256").get<std::string>().size(), 64u);
}

TEST_F(EngineRoutes, ParseWithSyntaxErrorsHasNoModel) {
    Result<json::Json> r = call("POST", "/api/v1/authoring/models/parse", {{"source", "automaton M { clock }"}});
    ASSERT_TRUE(r);
    EXPECT_FALSE(r.value().contains("model"));
    EXPECT_FALSE(r.value().at("valid").get<bool>());
    EXPECT_EQ(r.value().at("diagnostics").at(0).at("code"), "TWT001");
}

TEST_F(EngineRoutes, ValidateReturnsTextAndDiagnostics) {
    const authoring::ParseResult p = authoring::parse_text(kSmall);
    ASSERT_TRUE(p.model);
    json::Json model = authoring::to_json(*p.model);
    model["edges"][0]["target"] = "NOWHERE";
    Result<json::Json> r = call("POST", "/api/v1/authoring/models/validate", {{"model", model}});
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_FALSE(r.value().at("valid").get<bool>());
    EXPECT_EQ(r.value().at("diagnostics").at(0).at("code"), "TWM005");
    EXPECT_NE(r.value().at("text").get<std::string>().find("automaton Small"), std::string::npos);
}

TEST_F(EngineRoutes, MalformedModelIsABadRequest) {
    Result<json::Json> r = call("POST", "/api/v1/authoring/models/validate", {{"model", {{"format", "nope"}}}});
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
    Result<json::Json> missing = call("POST", "/api/v1/authoring/models/validate", json::Json::object());
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::InvalidArgument);
}

TEST_F(EngineRoutes, FormatReturnsSourceOrDiagnostics) {
    Result<json::Json> ok = call("POST", "/api/v1/authoring/models/format", {{"source", "automaton M{clock x;initial location A;}"}});
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok.value().at("source"), "automaton M {\n    clock x;\n\n    initial location A;\n}\n");
    Result<json::Json> bad = call("POST", "/api/v1/authoring/models/format", {{"source", "automaton M { clock }"}});
    ASSERT_TRUE(bad);
    EXPECT_TRUE(bad.value().at("source").is_null());
    EXPECT_FALSE(bad.value().at("diagnostics").empty());
}

TEST_F(EngineRoutes, RenderExportsWithRoundTripVerification) {
    const authoring::ParseResult p = authoring::parse_text(kSmall);
    Result<json::Json> r = call("POST", "/api/v1/authoring/models/render",
                                {{"model", authoring::to_json(*p.model)}, {"target", "uppaal"}});
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r.value().at("mediaType"), "application/xml");
    EXPECT_TRUE(r.value().at("roundTripVerified").get<bool>());
    EXPECT_NE(r.value().at("content").get<std::string>().find("<nta>"), std::string::npos);
    Result<json::Json> unknown = call("POST", "/api/v1/authoring/models/render",
                                      {{"model", authoring::to_json(*p.model)}, {"target", "scxml"}});
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, ErrorCode::InvalidArgument);
}

TEST_F(EngineRoutes, ConstraintsParseForInspectorFields) {
    Result<json::Json> g = call("POST", "/api/v1/authoring/constraints/parse", {{"text", "x >= 2 && y < 3"}, {"kind", "guard"}});
    ASSERT_TRUE(g);
    EXPECT_EQ(g.value().at("atoms").size(), 2u);
    EXPECT_EQ(g.value().at("text"), "x >= 2 && y < 3");
    EXPECT_TRUE(g.value().at("diagnostics").empty());
    Result<json::Json> diag = call("POST", "/api/v1/authoring/constraints/parse", {{"text", "x - y < 3"}, {"kind", "guard"}});
    ASSERT_TRUE(diag);
    EXPECT_EQ(diag.value().at("diagnostics").at(0).at("code"), "TWM009");
    Result<json::Json> inv = call("POST", "/api/v1/authoring/constraints/parse", {{"text", "x - y < 3"}, {"kind", "invariant"}});
    ASSERT_TRUE(inv);
    EXPECT_TRUE(inv.value().at("diagnostics").empty());
    Result<json::Json> bad = call("POST", "/api/v1/authoring/constraints/parse", {{"text", "x < 1 || x > 3"}, {"kind", "guard"}});
    ASSERT_TRUE(bad);
    EXPECT_FALSE(bad.value().contains("atoms"));
    EXPECT_EQ(bad.value().at("diagnostics").at(0).at("code"), "TWT010");
}

TEST_F(EngineRoutes, ImportAndImporters) {
    Result<json::Json> list = call("GET", "/api/v1/authoring/importers", json::Json::object());
    ASSERT_TRUE(list);
    EXPECT_EQ(list.value().size(), 3u);
    Result<json::Json> r = call("POST", "/api/v1/authoring/import",
                                {{"filename", "pump_dt.xml"}, {"content", read("examples/industrial-pump/models/pump_dt.xml")}});
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r.value().at("format"), "uppaal-xml");
    EXPECT_TRUE(r.value().at("provenance").at("preserved").get<bool>());
    Result<json::Json> bad = call("POST", "/api/v1/authoring/import",
                                  {{"filename", "mix.xml"}, {"content", read("tests/fixtures/authoring/unsupported_mix.xml")}});
    ASSERT_TRUE(bad);
    EXPECT_FALSE(bad.value().contains("model"));
    EXPECT_GE(bad.value().at("diagnostics").size(), 4u);
}

TEST_F(EngineRoutes, DiffAndCompile) {
    const authoring::ParseResult p = authoring::parse_text(kSmall);
    json::Json changed = authoring::to_json(*p.model);
    changed["locations"][0]["invariant"][0]["bound"] = 7;
    Result<json::Json> d = call("POST", "/api/v1/authoring/models/diff", {{"from", authoring::to_json(*p.model)}, {"to", changed}});
    ASSERT_TRUE(d);
    EXPECT_TRUE(d.value().at("semanticChange").get<bool>());

    Result<json::Json> c = call("POST", "/api/v1/authoring/models/compile",
                                {{"model", authoring::to_json(*p.model)}, {"modelId", "small"}});
    ASSERT_TRUE(c) << c.error().to_string();
    EXPECT_TRUE(c.value().at("compiled").get<bool>());
    EXPECT_EQ(c.value().at("ir").at("model").at("id"), "small");
    EXPECT_EQ(c.value().at("irSha256").get<std::string>().size(), 64u);
    EXPECT_EQ(c.value().at("sourceMap").at("edges").at("e1"), "A.go!.B");
    EXPECT_TRUE(c.value().at("manifest").at("translation_validation").at("passed").get<bool>());

    json::Json broken = authoring::to_json(*p.model);
    broken["edges"][0]["sync"]["channel"] = "nochan";
    Result<json::Json> f = call("POST", "/api/v1/authoring/models/compile", {{"model", broken}, {"modelId", "small"}});
    ASSERT_TRUE(f);
    EXPECT_FALSE(f.value().at("compiled").get<bool>());
    EXPECT_EQ(f.value().at("diagnostics").at(0).at("code"), "TWM008");
}

TEST_F(EngineRoutes, AlignmentDiagnoseExplainsTheEvidence) {
    const fs::path m = fs::path(TWIN_SOURCE_DIR) / "examples/industrial-pump/models";
    Result<alignment::AlignmentEvidence> ev = alignment::check_alignment(alignment::AlignmentInputs{
        m / "pump_pt.xml", m / "pump_dt.xml", m / "process-pump.ont", m / "pt.interp", m / "dt.interp", false});
    ASSERT_TRUE(ev) << ev.error().to_string();
    json::Json evidence = alignment::to_json(ev.value());
    evidence.erase("modes");  // evidence recorded before modes existed: recomputed from the views
    Result<json::Json> r = call("POST", "/api/v1/authoring/alignment/diagnose",
                                {{"evidence", evidence},
                                 {"ptSource", read("examples/industrial-pump/models/pump_pt.xml")},
                                 {"dtSource", read("examples/industrial-pump/models/pump_dt.xml")}});
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r.value().at("modes").at("weak"), "aligned");
    EXPECT_EQ(r.value().at("modes").at("strong"), "aligned");
    EXPECT_TRUE(r.value().at("diagnostics").is_array());
    EXPECT_EQ(r.value().at("correspondences").at("labels").size(), 10u);
    EXPECT_TRUE(r.value().at("verdict").at("aligned").get<bool>());

    Result<json::Json> e = call("POST", "/api/v1/authoring/alignment/explain",
                                {{"ontology", read("examples/industrial-pump/models/process-pump.ont")},
                                 {"ptInterpretation", read("examples/industrial-pump/models/pt.interp")},
                                 {"dtInterpretation", read("examples/industrial-pump/models/dt.interp")},
                                 {"pt", "start_cmd!"},
                                 {"dt", "restart!"},
                                 {"kind", "event"}});
    ASSERT_TRUE(e) << e.error().to_string();
    EXPECT_TRUE(e.value().at("equivalent").get<bool>());
    Result<json::Json> missing = call("POST", "/api/v1/authoring/alignment/explain",
                                      {{"ontology", read("examples/industrial-pump/models/process-pump.ont")},
                                       {"ptInterpretation", read("examples/industrial-pump/models/pt.interp")},
                                       {"dtInterpretation", read("examples/industrial-pump/models/dt.interp")},
                                       {"pt", "nope!"},
                                       {"dt", "restart!"}});
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(EngineRoutes, EveryRouteIsUnderApiV1AndDocumented) {
    for (const Route& r : routes_) {
        EXPECT_EQ(r.pattern.rfind("/api/v1/", 0), 0u) << r.pattern;
        EXPECT_FALSE(r.summary.empty()) << r.pattern;
        EXPECT_TRUE(r.method == "GET" || r.method == "POST" || r.method == "DELETE") << r.method;
    }
}

TEST_F(EngineRoutes, RouteTableServesOverHttp) {
    httplib::Server srv;
    mount(srv, routes_);
    const int port = srv.bind_to_any_port("127.0.0.1");
    std::thread t([&] { srv.listen_after_bind(); });
    httplib::Client cli("127.0.0.1", port);
    auto ok = cli.Post("/api/v1/authoring/models/parse", json::Json{{"source", kSmall}}.dump(), "application/json");
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok->status, 200);
    auto bad = cli.Post("/api/v1/authoring/models/validate", "not json", "application/json");
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->status, 400);
    const auto err = json::parse(bad->body);
    ASSERT_TRUE(err);
    EXPECT_EQ(err.value().at("error").at("code"), "parse_error");
    srv.stop();
    t.join();
}

}  // namespace
}  // namespace twin::studio_engine
