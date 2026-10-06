/**
 * @file engine_contract_test.cpp
 * @brief Records one real response per engine route (and the route table) for the contract check.
 *
 * scripts/openapi/check_engine_contract.py validates every recorded body against
 * api/studio-engine.openapi.yaml and checks that the route table and the contract
 * list the same operations (`make api-check`).
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "twin/alignment/alignment.hpp"
#include "twin/authoring/text.hpp"
#include "twin/studio_engine/engine.hpp"

namespace twin::studio_engine {
namespace {

namespace fs = std::filesystem;

std::string read(const std::string& relative) {
    std::ifstream in(fs::path(TWIN_SOURCE_DIR) / relative, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

TEST(EngineContract, RecordOneResponsePerRoute) {
    Engine engine(EngineConfig{fs::temp_directory_path() / "twin-engine-contract"});
    const std::vector<Route> routes = engine.routes();
    const authoring::ParseResult small = authoring::parse_text(
        "automaton Small { clock x; channel go; initial location A { invariant x <= 5; } location B;"
        " edge e1: A -> B { guard x >= 2; sync go!; reset x; } }");
    ASSERT_TRUE(small.model);
    const json::Json model = authoring::to_json(*small.model);

    const fs::path pump = fs::path(TWIN_SOURCE_DIR) / "examples/industrial-pump/models";
    Result<alignment::AlignmentEvidence> evidence = alignment::check_alignment(alignment::AlignmentInputs{
        pump / "pump_pt.xml", pump / "pump_dt.xml", pump / "process-pump.ont", pump / "pt.interp", pump / "dt.interp", false});
    ASSERT_TRUE(evidence) << evidence.error().to_string();
    const json::Json explain{{"ontology", read("examples/industrial-pump/models/process-pump.ont")},
                             {"ptInterpretation", read("examples/industrial-pump/models/pt.interp")},
                             {"dtInterpretation", read("examples/industrial-pump/models/dt.interp")},
                             {"pt", "alarm_raise!"},
                             {"dt", "condition_degraded!"}};

    const json::Json pump_dt_source{{"dtSource", read("examples/industrial-pump/models/pump_dt.xml")}};
    const json::Json monitors = json::Json::parse(R"json({"format": "twin-monitors/1", "monitors": [
        {"id": "c", "kind": "conformance", "name": "Conformance", "severity": "critical", "events": "all"},
        {"id": "p", "kind": "property", "name": "P", "severity": "warning", "property": "A[] !B"}]})json");

    const std::map<std::string, std::vector<json::Json>> requests = {
        {"POST /api/v1/authoring/properties/analyse",
         {{{"property", "A[] !B"}, {"dtModel", model}}, {{"property", "A<> B"}, {"dtModel", model}}, {{"property", "A[] ("}, {"dtModel", model}}}},
        {"POST /api/v1/authoring/properties/check",
         {{{"property", "A[] !FAULT"}, {"dtSource", read("examples/industrial-pump/models/pump_dt.xml")}},
          {{"property", "A[] (NORMAL || DEGRADED) -> sem((<= bearing_temp bearing_temp_trip))"},
           {"dtSource", read("examples/industrial-pump/models/pump_dt.xml")},
           {"ontology", read("examples/industrial-pump/models/process-pump.ont")},
           {"dtInterpretation", read("examples/industrial-pump/models/dt.interp")}},
          {{"property", "E[] NORMAL"}, {"dtSource", read("examples/industrial-pump/models/pump_dt.xml")}}}},
        {"POST /api/v1/authoring/monitors/validate", {{{"monitors", monitors}, {"dtModel", model}}}},
        {"POST /api/v1/authoring/alignment/diagnose",
         {{{"evidence", alignment::to_json(evidence.value())},
           {"ptSource", read("examples/industrial-pump/models/pump_pt.xml")},
           {"dtSource", read("examples/industrial-pump/models/pump_dt.xml")}}}},
        {"POST /api/v1/authoring/alignment/explain", {explain}},
        {"POST /api/v1/authoring/models/parse", {{{"source", authoring::print_text(*small.model)}}, {{"source", "automaton M {"}}}},
        {"POST /api/v1/authoring/models/validate", {{{"model", model}}}},
        {"POST /api/v1/authoring/models/format", {{{"source", "automaton M{clock x;initial location A;}"}}, {{"source", "x"}}}},
        {"POST /api/v1/authoring/models/render", {{{"model", model}, {"target", "uppaal"}}}},
        {"POST /api/v1/authoring/models/diff", {{{"from", model}, {"to", model}}}},
        {"POST /api/v1/authoring/models/compile", {{{"model", model}, {"modelId", "small"}}}},
        {"POST /api/v1/authoring/constraints/parse", {{{"text", "x >= 2"}}, {{"text", "x < 1 || x > 2"}}}},
        {"POST /api/v1/authoring/import",
         {{{"filename", "with_constants.xml"}, {"content", read("tests/fixtures/authoring/with_constants.xml")}},
          {{"filename", "mix.xml"}, {"content", read("tests/fixtures/authoring/unsupported_mix.xml")}}}},
        {"GET /api/v1/authoring/importers", {json::Json::object()}},
    };

    json::Json samples = json::Json::array();
    json::Json table = json::Json::array();
    for (const Route& r : routes) {
        const std::string key = r.method + " " + r.pattern;
        table.push_back({{"method", r.method}, {"path", r.pattern}, {"summary", r.summary}});
        const auto it = requests.find(key);
        ASSERT_NE(it, requests.end()) << "no contract sample for " << key;
        for (const json::Json& body : it->second) {
            Request req;
            req.body = body;
            Result<json::Json> out = r.handler(req);
            ASSERT_TRUE(out) << key << ": " << out.error().to_string();
            samples.push_back({{"method", r.method}, {"path", r.pattern}, {"status", r.ok_status}, {"body", out.value()}});
        }
    }
    std::ofstream(fs::path(TWIN_BINARY_DIR) / "engine-contract-samples.json")
        << json::Json{{"routes", table}, {"samples", samples}}.dump(2);
}

}  // namespace
}  // namespace twin::studio_engine
