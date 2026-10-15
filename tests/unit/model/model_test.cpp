// The twin modelling language: decoding, validation codes, the behaviour compiler
// (instantiation, groups, timeouts, links, ignore, product), the domain theory, the
// semantic web outputs, the derived Blueprint sections, trace and determinism.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "twin/authoring/model.hpp"
#include "twin/model/compile.hpp"
#include "twin/model/lift.hpp"
#include "twin/monitoring/monitors.hpp"

namespace twin::model {
namespace {

namespace fs = std::filesystem;
using json::Json;

Json load(const std::string& relative) {
    std::ifstream in(fs::path(TWIN_SOURCE_DIR) / relative, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return Json::parse(b.str());
}

Compiled compiled(const Json& doc) {
    Compiled c = compile(doc);
    EXPECT_TRUE(c.artifacts.has_value()) << to_json(c.diagnostics).dump(2);
    return c;
}

bool has_code(const std::vector<Diagnostic>& diags, const std::string& code, const std::string& severity = "error") {
    return std::any_of(diags.begin(), diags.end(), [&](const Diagnostic& d) { return d.code == code && d.severity == severity; });
}

/// A small one-type model to mutate in tests.
Json lamp() {
    return Json::parse(R"({
      "format": "twin-model/1", "name": "lamp", "root": "Lamp",
      "types": [{
        "id": "t", "name": "Lamp",
        "properties": [
          {"id": "p1", "name": "on", "kind": "measured", "type": "boolean"},
          {"id": "p2", "name": "temperature", "kind": "measured", "type": "real", "unit": "degC", "range": {"min": "-20", "max": "120"}},
          {"id": "p3", "name": "limit", "kind": "fixed", "type": "real", "unit": "degC", "value": "70"}
        ],
        "events": [
          {"id": "e1", "name": "lit", "origin": "reported", "meaning": "on"},
          {"id": "e2", "name": "hot", "origin": "condition", "when": "temperature > limit"}
        ],
        "actions": [{"id": "a1", "name": "switch_off"}],
        "behavior": {
          "timers": [{"id": "x", "name": "t"}],
          "states": [
            {"id": "s1", "name": "Off", "initial": true, "meaning": "not on"},
            {"id": "s2", "name": "On", "meaning": "on"},
            {"id": "s3", "name": "Cooling"}
          ],
          "transitions": [
            {"id": "r1", "from": ["Off"], "to": "On", "trigger": {"event": "lit"}},
            {"id": "r2", "from": ["On"], "to": "Off", "trigger": {"action": "switch_off"}},
            {"id": "r3", "from": ["On"], "to": "Cooling", "trigger": {"event": "hot"}},
            {"id": "r4", "from": ["Cooling"], "to": "Off", "trigger": {"timeout": 20}}
          ]
        }
      }]
    })");
}

Json& lamp_behavior(Json& doc) { return doc["types"][0]["behavior"]; }

// ------------------------------------------------------------------ document

TEST(ModelDocument, DecodeEncodeRoundTripsTheExamples) {
    for (const char* f : {"examples/thermal-chamber/model.json", "examples/pump-station/model.json"}) {
        SCOPED_TRACE(f);
        const Decoded d = decode(load(f));
        ASSERT_FALSE(has_errors(d.diagnostics)) << to_json(d.diagnostics).dump(2);
        const Decoded again = decode(encode(d.document));
        ASSERT_FALSE(has_errors(again.diagnostics));
        EXPECT_EQ(again.document, d.document);
    }
}

TEST(ModelDocument, UnknownFieldsAndWrongTypesAreReported) {
    Json doc = lamp();
    doc["types"][0]["bogus"] = 1;
    doc["types"][0]["properties"][1]["range"]["min"] = 1.5;  // a float is not exact
    const Decoded d = decode(doc);
    ASSERT_TRUE(has_errors(d.diagnostics));
    EXPECT_TRUE(std::any_of(d.diagnostics.begin(), d.diagnostics.end(), [](const Diagnostic& x) { return x.path.find("bogus") != std::string::npos; }));
    EXPECT_TRUE(std::any_of(d.diagnostics.begin(), d.diagnostics.end(), [](const Diagnostic& x) { return x.path.find("range.min") != std::string::npos; }));
}

TEST(ModelDocument, SemanticDigestIgnoresPresentation) {
    Json a = lamp();
    Json b = lamp();
    b["types"][0]["label"] = "A lamp";
    b["types"][0]["behavior"]["states"][0]["tone"] = "warning";
    b["layout"] = {{"x", 1}};
    EXPECT_EQ(semantic_digest(decode(a).document), semantic_digest(decode(b).document));
    b["types"][0]["properties"][2]["value"] = "71";
    EXPECT_NE(semantic_digest(decode(a).document), semantic_digest(decode(b).document));
}

// ------------------------------------------------------------------ validation

TEST(ModelValidation, ReportsTheDocumentedCodes) {
    {
        Json d = lamp();
        lamp_behavior(d)["states"][0].erase("initial");
        EXPECT_TRUE(has_code(check(d), "TML050"));
    }
    {
        Json d = lamp();
        lamp_behavior(d)["transitions"][0]["trigger"] = {{"event", "nothing"}};
        EXPECT_TRUE(has_code(check(d), "TML004"));
    }
    {
        Json d = lamp();
        d["types"][0]["events"][1]["when"] = "pressure > limit";
        EXPECT_TRUE(has_code(check(d), "TML022"));
    }
    {
        Json d = lamp();
        d["types"][0]["properties"][2].erase("value");
        EXPECT_TRUE(has_code(check(d), "TML010"));
    }
    {
        Json d = lamp();
        d["types"][0]["properties"][2]["value"] = "200";  // above no range: fine; give it a range it violates
        d["types"][0]["properties"][2]["range"] = {{"max", "100"}};
        EXPECT_TRUE(has_code(check(d), "TML010"));
    }
    {
        Json d = lamp();
        d["types"][0]["name"] = "and";
        EXPECT_TRUE(has_code(check(d), "TML002"));
    }
    {
        Json d = lamp();
        d["types"][0]["events"][0]["when"] = "on";
        EXPECT_TRUE(has_code(check(d), "TML030"));
    }
    {
        Json d = lamp();
        d["types"][0]["parts"] = Json::array({{{"id", "pp"}, {"name", "inner"}, {"type", "Lamp"}}});
        EXPECT_TRUE(has_code(compile(d).diagnostics, "TML005"));
    }
}

TEST(ModelValidation, OverlappingTransitionsAreNondeterministic) {
    Json d = lamp();
    // A second transition on 'hot' from On whose window overlaps the first (no window = always).
    lamp_behavior(d)["transitions"].push_back(Json::parse(
        R"({"id": "r5", "from": ["On"], "to": "Off", "trigger": {"event": "hot"}, "window": [{"timer": "t", "op": ">=", "bound": 5}]})"));
    EXPECT_TRUE(has_code(compile(d).diagnostics, "TML053"));

    // Disjoint windows are deterministic.
    lamp_behavior(d)["transitions"][2]["window"] = Json::array({{{"timer", "t"}, {"op", "<"}, {"bound", 5}}});
    EXPECT_FALSE(has_code(compile(d).diagnostics, "TML053"));

    // Overlap again, but allowed: a warning, and the model compiles.
    lamp_behavior(d)["transitions"][2].erase("window");
    d["allowNondeterminism"] = true;
    const Compiled c = compile(d);
    EXPECT_TRUE(has_code(c.diagnostics, "TML053", "warning"));
    EXPECT_TRUE(c.artifacts.has_value());
}

TEST(ModelValidation, TimeoutsAreCheckedAgainstLimits) {
    Json d = lamp();
    lamp_behavior(d)["states"][2]["limits"] = Json::array({{{"timer", "t"}, {"op", "<="}, {"bound", 10}}});
    EXPECT_TRUE(has_code(compile(d).diagnostics, "TML054"));  // the limit ends before the timeout
    lamp_behavior(d)["states"][2]["limits"][0]["bound"] = 20;
    EXPECT_TRUE(has_code(compile(d).diagnostics, "TML056", "warning"));  // the limit repeats the timeout
    lamp_behavior(d)["states"][2].erase("limits");
    lamp_behavior(d)["transitions"].push_back(Json::parse(R"({"id": "r6", "from": ["Cooling"], "to": "On", "trigger": {"timeout": 30}})"));
    EXPECT_TRUE(has_code(compile(d).diagnostics, "TML054"));  // two timeouts from one state
}

// ------------------------------------------------------------------ behaviour compiler

const authoring::LocationDecl* location(const authoring::Model& m, const std::string& name) {
    for (const auto& l : m.locations) {
        if (l.name == name) return &l;
    }
    return nullptr;
}

std::vector<const authoring::EdgeDecl*> edges(const authoring::Model& m, const std::string& source, const std::string& label) {
    std::vector<const authoring::EdgeDecl*> out;
    for (const auto& e : m.edges) {
        if (e.source == source && authoring::edge_label(e) == label) out.push_back(&e);
    }
    return out;
}

TEST(ModelBehavior, SingleMachineMapsOneToOne) {
    const Compiled c = compiled(lamp());
    const authoring::Model& m = *c.artifacts->automaton;
    EXPECT_EQ(m.name, "Lamp");
    ASSERT_EQ(m.locations.size(), 3U);
    EXPECT_TRUE(location(m, "Off")->initial);
    // The timeout: guard t >= 20, invariant t <= 20 on Cooling, label Cooling_timeout!
    const auto timeout = edges(m, "Cooling", "Cooling_timeout!");
    ASSERT_EQ(timeout.size(), 1U);
    ASSERT_EQ(timeout.front()->guard.size(), 1U);
    EXPECT_EQ(timeout.front()->guard.front().op, ir::Comparison::GreaterEqual);
    ASSERT_EQ(location(m, "Cooling")->invariant.size(), 1U);
    EXPECT_EQ(location(m, "Cooling")->invariant.front().op, ir::Comparison::LessEqual);
    // State-change transitions reset the state timer.
    EXPECT_EQ(edges(m, "Off", "lit!").front()->resets, std::vector<std::string>{"t"});
}

TEST(ModelBehavior, PumpStationIsTheSynchronousProductWithLinks) {
    const Compiled c = compiled(load("examples/pump-station/model.json"));
    const authoring::Model& m = *c.artifacts->automaton;
    EXPECT_EQ(m.locations.size(), 15U);
    EXPECT_EQ(m.clocks.size(), 1U);
    EXPECT_EQ(m.locations.front().name, "pump_01_Idle__tank_01_Normal");
    EXPECT_TRUE(m.locations.front().initial);
    // level_high moves both: the tank goes High and the pump stops (link to pump_01.stop).
    const auto full = edges(m, "pump_01_Running__tank_01_Normal", "tank_01__level_high!");
    ASSERT_EQ(full.size(), 1U);
    EXPECT_EQ(full.front()->target, "pump_01_Stopping__tank_01_High");
    // An idle pump ignores the stop request (stop is declared unexpected: ignore).
    const auto idle = edges(m, "pump_01_Idle__tank_01_Normal", "tank_01__level_high!");
    ASSERT_EQ(idle.size(), 1U);
    EXPECT_EQ(idle.front()->target, "pump_01_Idle__tank_01_High");
    // The group Energized expands: over-current leads to Fault from Starting, Running and Stopping.
    for (const char* s : {"Starting", "Running", "Stopping"}) {
        const auto oc = edges(m, std::string("pump_01_") + s + "__tank_01_Normal", "pump_01__motor__overcurrent!");
        ASSERT_EQ(oc.size(), 1U) << s;
        EXPECT_EQ(oc.front()->target, "pump_01_Fault__tank_01_Normal");
    }
    // Timeouts bound their states in every product location.
    for (const auto& l : m.locations) {
        if (l.name.rfind("pump_01_Starting", 0) == 0) {
            ASSERT_EQ(l.invariant.size(), 1U) << l.name;
            EXPECT_EQ(std::get<std::int64_t>(l.invariant.front().bound.value), 10);
        }
    }
}

TEST(ModelBehavior, CompilationIsDeterministic) {
    for (const char* f : {"examples/thermal-chamber/model.json", "examples/pump-station/model.json"}) {
        SCOPED_TRACE(f);
        const Json doc = load(f);
        const Compiled a = compiled(doc);
        const Compiled b = compiled(encode(decode(doc).document));
        EXPECT_EQ(to_json(*a.artifacts).dump(), to_json(*b.artifacts).dump());
        EXPECT_EQ(a.semantic_digest, b.semantic_digest);
    }
}

TEST(ModelBehavior, ThermalChamberCompilesToTheTutorialAutomaton) {
    const Compiled c = compiled(load("examples/thermal-chamber/model.json"));
    auto expected = authoring::model_from_json(load("examples/thermal-chamber/models/chamber_dt.tta.json"));
    ASSERT_TRUE(expected) << expected.error().to_string();
    authoring::Model hand = std::move(expected).value();
    hand.name = "ThermalChamber";  // the template is named after the root type
    std::string why;
    EXPECT_TRUE(same_automaton(*c.artifacts->automaton, hand, &why)) << why;
}

// ------------------------------------------------------------------ domain theory

TEST(ModelTheory, ChamberTheoryAndInterpretation) {
    const Compiled c = compiled(load("examples/thermal-chamber/model.json"));
    const Artifacts& a = *c.artifacts;
    EXPECT_NE(a.ontology.find("fun probe__temperature : Temperature"), std::string::npos);
    EXPECT_NE(a.ontology.find("rel heater__on :"), std::string::npos);
    EXPECT_NE(a.ontology.find("axiom temperature_limit_value : (= temperature_limit 80)"), std::string::npos);
    EXPECT_NE(a.ontology.find("axiom probe__temperature_min : (>= probe__temperature (- 40))"), std::string::npos);
    EXPECT_NE(a.ontology.find("axiom assume_setpoint_below_limit : (< setpoint temperature_limit)"), std::string::npos);
    EXPECT_NE(a.dt_interpretation.find("HEATING : (and heater__on (not door__open) (<= probe__temperature temperature_limit))"), std::string::npos);
    EXPECT_NE(a.dt_interpretation.find("cooled_down! : (and cooled_down_requested (<= probe__temperature temperature_limit))"), std::string::npos);
    // Per-twin and text properties never enter the theory.
    EXPECT_EQ(a.ontology.find("serial"), std::string::npos);
}

TEST(ModelTheory, GeneratedRelationsForActionsReportsAndTimeouts) {
    const Compiled c = compiled(lamp());
    const Artifacts& a = *c.artifacts;
    EXPECT_NE(a.ontology.find("rel switch_off_requested :"), std::string::npos);
    EXPECT_NE(a.ontology.find("rel Cooling_timeout_elapsed :"), std::string::npos);
    EXPECT_NE(a.dt_interpretation.find("hot! : (> temperature limit)"), std::string::npos);
    EXPECT_NE(a.dt_interpretation.find("lit! : on"), std::string::npos);
    // A state without a meaning has no entry (the aligner reads it as true).
    EXPECT_EQ(a.dt_interpretation.find("Cooling :"), std::string::npos);
}

// ------------------------------------------------------------------ semantic web, sections, trace

TEST(ModelRdf, SchemaAndGraph) {
    const Compiled c = compiled(load("examples/pump-station/model.json"));
    const Artifacts& a = *c.artifacts;
    EXPECT_NE(a.schema_turtle.find(":CentrifugalPump a owl:Class"), std::string::npos);
    EXPECT_NE(a.schema_turtle.find(":feeds a owl:ObjectProperty"), std::string::npos);
    EXPECT_NE(a.schema_turtle.find("sh:path :CentrifugalPump.temp_limit"), std::string::npos);
    EXPECT_NE(a.schema_turtle.find("qudt:hasUnit unit:DEG_C"), std::string::npos);
    EXPECT_NE(a.graph_turtle.find(":feeds asset:pump_station.tank_01"), std::string::npos);
    EXPECT_NE(a.graph_turtle.find(":CentrifugalPump.temp_limit \"90\"^^xsd:decimal"), std::string::npos);
    EXPECT_NE(a.graph_turtle.find("tw:references asset:pump_station.site"), std::string::npos);
    EXPECT_TRUE(a.graph_jsonld.contains("@context"));
    EXPECT_EQ(a.graph_turtle.find("_:"), std::string::npos);  // no blank nodes
}

TEST(ModelSections, DerivedSectionsAreValidBlueprintSections) {
    const Compiled c = compiled(load("examples/pump-station/model.json"));
    const Json& s = c.artifacts->sections;
    EXPECT_EQ(s["structure"]["root"], "pump_station");
    const auto has_id = [](const Json& arr, const std::string& id) {
        return std::any_of(arr.begin(), arr.end(), [&](const Json& x) { return x["id"] == id; });
    };
    EXPECT_TRUE(has_id(s["data"]["telemetry"], "pump_01__bearing_temp"));
    EXPECT_TRUE(has_id(s["data"]["commands"], "pump_01__reset"));
    const auto ev = std::find_if(s["data"]["events"].begin(), s["data"]["events"].end(), [](const Json& x) { return x["id"] == "tank_01__level_high"; });
    ASSERT_NE(ev, s["data"]["events"].end());
    EXPECT_EQ((*ev)["formal"]["dt"], "tank_01__level_high!");
    EXPECT_EQ((*ev)["condition"]["when"], "(>= tank_01__level tank_01__high_level)");
    // The monitors document is a valid twin-monitors/1 document.
    auto monitors = monitoring::monitors_from_json(s["assurance"]);
    ASSERT_TRUE(monitors) << monitors.error().to_string();
    EXPECT_TRUE(monitoring::validate_document(monitors.value()).empty()) << monitoring::to_json(monitoring::validate_document(monitors.value())).dump(2);
    ASSERT_NE(monitoring::find_monitor(monitors.value(), "req-pump_never_faults"), nullptr);
    EXPECT_NE(monitoring::find_monitor(monitors.value(), "pump_01__bearing_temp-fresh"), nullptr);
}

TEST(ModelTrace, EveryGeneratedLocationAndEdgeIsTraced) {
    const Compiled c = compiled(load("examples/pump-station/model.json"));
    std::set<std::string> traced;
    for (const Json& e : c.artifacts->trace) {
        EXPECT_FALSE(e["sources"].empty()) << e.dump();
        traced.insert(e["kind"].get<std::string>() + ":" + e["name"].get<std::string>());
    }
    for (const auto& l : c.artifacts->automaton->locations) EXPECT_TRUE(traced.contains("location:" + l.name)) << l.name;
    for (const auto& e : c.artifacts->automaton->edges) EXPECT_TRUE(traced.contains("edge:" + e.id)) << e.id;
}

}  // namespace
}  // namespace twin::model
