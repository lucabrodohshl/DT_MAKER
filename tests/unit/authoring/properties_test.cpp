/**
 * @file properties_test.cpp
 * @brief Property analysis (monitorability decided by the backend) and design-time checks
 * (exact zone-graph reachability; semantic guarantees by ontology entailment).
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "authoring_fixtures.hpp"
#include "twin/authoring/import.hpp"
#include "twin/authoring/properties.hpp"
#include "twin/authoring/text.hpp"
#include "twin/authoring/uppaal.hpp"

namespace twin::authoring {
namespace {

namespace fs = std::filesystem;

std::string read(const std::string& relative) {
    std::ifstream in(fs::path(TWIN_SOURCE_DIR) / relative, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

Model pump_dt() {
    const ParseResult r = parse_text(test::kPumpDtText);
    EXPECT_TRUE(r.model);
    return r.model.value_or(Model{});
}

PropertyAnalysis analyse(const std::string& text) {
    return analyse_property(text, pump_dt(), read("examples/industrial-pump/models/process-pump.ont"));
}

TEST(PropertyAnalysis, ClassificationFollowsBackendCapability) {
    const PropertyAnalysis loc = analyse("A[] !FAULT");
    EXPECT_TRUE(loc.valid);
    EXPECT_EQ(loc.design_time, "checkable");
    EXPECT_EQ(loc.runtime, "monitorable");
    EXPECT_EQ(loc.evaluator, "runtime");

    const PropertyAnalysis clocks = analyse("A[] DEGRADED -> t <= 600");
    EXPECT_EQ(clocks.design_time, "checkable");
    EXPECT_EQ(clocks.runtime, "monitorable");
    EXPECT_EQ(clocks.clocks, (std::vector<std::string>{"t"}));

    const PropertyAnalysis reach = analyse("E<> COOLING");
    EXPECT_EQ(reach.design_time, "checkable");
    EXPECT_EQ(reach.runtime, "not_monitorable");

    const PropertyAnalysis sem = analyse("A[] (NORMAL || DEGRADED) -> sem((<= bearing_temp bearing_temp_trip))");
    EXPECT_TRUE(sem.valid);
    EXPECT_EQ(sem.design_time, "guarantee_check");
    EXPECT_EQ(sem.runtime, "monitorable");
    EXPECT_EQ(sem.evaluator, "studio");

    for (const char* unsupported : {"A<> STOPPED", "E[] NORMAL", "DEGRADED --> NORMAL", "A[] t <= 5 -> sem(motor_running)"}) {
        const PropertyAnalysis a = analyse(unsupported);
        EXPECT_TRUE(a.valid) << unsupported;
        EXPECT_EQ(a.design_time, "unsupported") << unsupported;
        EXPECT_EQ(a.runtime, "unsupported") << unsupported;
        EXPECT_FALSE(a.reasons.empty()) << unsupported;
    }
}

TEST(PropertyAnalysis, NamesAreResolvedAgainstTheModelAndTheOntology) {
    const PropertyAnalysis unknown = analyse("A[] !MELTDOWN");
    EXPECT_FALSE(unknown.valid);
    ASSERT_FALSE(unknown.diagnostics.empty());
    EXPECT_EQ(unknown.diagnostics[0].code, "TWP010");
    EXPECT_FALSE(analyse("A[] zz < 3").valid);
    const PropertyAnalysis badsym = analyse("A[] sem((> no_such_symbol 3))");
    EXPECT_FALSE(badsym.valid);
    EXPECT_EQ(badsym.diagnostics.at(0).code, "TWP012");
    const PropertyAnalysis syntax = analyse("A[] (NORMAL");
    EXPECT_FALSE(syntax.valid);
    EXPECT_EQ(syntax.diagnostics.at(0).code, "TWP001");
}

PropertyCheckInputs inputs(const std::string& property) {
    PropertyCheckInputs in;
    in.property = property;
    in.dt_xml = render_toolchain_xml(pump_dt());
    in.ontology_text = read("examples/industrial-pump/models/process-pump.ont");
    in.dt_interpretation_text = read("examples/industrial-pump/models/dt.interp");
    return in;
}

TEST(PropertyCheck, SafetyViolationHasAWitnessPath) {
    Result<json::Json> e = check_property(inputs("A[] !FAULT"));
    ASSERT_TRUE(e) << e.error().to_string();
    EXPECT_EQ(e.value().at("format"), "twin-property-evidence/1");
    EXPECT_EQ(e.value().at("verdict"), "violated");
    EXPECT_EQ(e.value().at("witness").at("path"), json::Json::array({"STOPPED", "NORMAL", "FAULT"}));
    EXPECT_GT(e.value().at("states").get<std::int64_t>(), 0);
    EXPECT_EQ(e.value().at("inputs").at("dt_sha256").get<std::string>().size(), 64u);
}

TEST(PropertyCheck, InvariantsMakeClockBoundsHold) {
    EXPECT_EQ(check_property(inputs("A[] STOPPING -> t <= 60")).value().at("verdict"), "holds");
    EXPECT_EQ(check_property(inputs("A[] COOLING -> t <= 300")).value().at("verdict"), "holds");
    Result<json::Json> tighter = check_property(inputs("A[] COOLING -> t <= 200"));
    ASSERT_TRUE(tighter);
    EXPECT_EQ(tighter.value().at("verdict"), "violated");
    EXPECT_EQ(tighter.value().at("witness").at("path").back(), "COOLING");
}

TEST(PropertyCheck, ReachabilityWithWitness) {
    Result<json::Json> e = check_property(inputs("E<> COOLING"));
    ASSERT_TRUE(e) << e.error().to_string();
    EXPECT_EQ(e.value().at("verdict"), "holds");
    EXPECT_EQ(e.value().at("witness").at("path"), json::Json::array({"STOPPED", "NORMAL", "DEGRADED", "COOLING"}));
    EXPECT_EQ(check_property(inputs("E<> COOLING && t > 300")).value().at("verdict"), "does_not_hold");
}

TEST(PropertyCheck, SemanticGuaranteeByEntailmentPerReachableLocation) {
    Result<json::Json> all = check_property(inputs("A[] sem((<= bearing_temp bearing_temp_trip))"));
    ASSERT_TRUE(all) << all.error().to_string();
    EXPECT_EQ(all.value().at("verdict"), "not_guaranteed");
    std::set<std::string> not_guaranteed;
    for (const json::Json& l : all.value().at("locations")) {
        if (!l.at("guaranteed").get<bool>()) not_guaranteed.insert(l.at("location").get<std::string>());
    }
    EXPECT_EQ(not_guaranteed, (std::set<std::string>{"FAULT", "STOPPED", "STOPPING"}));

    Result<json::Json> scoped = check_property(inputs("A[] (NORMAL || DEGRADED || COOLING) -> sem((<= bearing_temp bearing_temp_trip))"));
    ASSERT_TRUE(scoped) << scoped.error().to_string();
    EXPECT_EQ(scoped.value().at("verdict"), "guaranteed");
}

TEST(PropertyCheck, UnsupportedFormsAreReportedNotGuessed) {
    Result<json::Json> e = check_property(inputs("A<> STOPPED"));
    ASSERT_TRUE(e);
    EXPECT_EQ(e.value().at("verdict"), "unsupported");
    EXPECT_FALSE(e.value().at("reasons").empty());
}

TEST(MonitorValidation, ReferencesAreResolvedAgainstModelsAndSchema) {
    const Model dt = pump_dt();
    const ImportResult pt = import_any(read("examples/industrial-pump/models/pump_pt.xml"), ImportOptions{"pt.xml", false});
    ASSERT_TRUE(pt.model);
    const json::Json schema = json::Json::array({{{"id", "bearing_temp"}, {"type", "real"}, {"unit", "degC"}}});
    Result<monitoring::MonitorsDocument> doc = monitoring::monitors_from_json(json::Json::parse(R"json({
      "format": "twin-monitors/1",
      "monitors": [
        {"id": "c", "kind": "conformance", "name": "Conformance", "severity": "critical", "events": ["start_cmd!", "nope!"]},
        {"id": "p", "kind": "property", "name": "P", "severity": "warning", "property": "A[] !MELTDOWN"},
        {"id": "q", "kind": "data_quality", "name": "Q", "severity": "warning", "field": "pressure", "check": "missing"}
      ]})json"));
    ASSERT_TRUE(doc) << doc.error().to_string();
    const std::vector<Diagnostic> ds = validate_monitors(doc.value(), &*pt.model, &dt, schema, std::nullopt);
    std::set<std::string> codes;
    for (const Diagnostic& d : ds) codes.insert(d.code);
    EXPECT_TRUE(codes.contains("TWN012")) << to_json(ds).dump(2);  // unknown PT label
    EXPECT_TRUE(codes.contains("TWP010")) << to_json(ds).dump(2);  // unknown location in the property
    EXPECT_TRUE(codes.contains("TWN021")) << to_json(ds).dump(2);  // unknown telemetry field
}

}  // namespace
}  // namespace twin::authoring
