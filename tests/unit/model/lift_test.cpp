// Lifting: timed automaton (+ ontology + interpretation) -> model, and the round-trip guarantee.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "twin/model/compile.hpp"
#include "twin/model/expression.hpp"
#include "twin/model/lift.hpp"

namespace twin::model {
namespace {

namespace fs = std::filesystem;
using json::Json;

std::string read(const std::string& relative) {
    std::ifstream in(fs::path(TWIN_SOURCE_DIR) / relative, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

authoring::Model automaton(const std::string& relative) {
    auto m = authoring::model_from_json(Json::parse(read(relative)));
    EXPECT_TRUE(m) << m.error().to_string();
    return std::move(m).value();
}

/// key -> normalised formula of an interpretation text.
std::map<std::string, std::string> entries(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t semi = line.find(';');
        if (semi == 0) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        key.erase(key.find_last_not_of(' ') + 1);
        std::string f = line.substr(colon + 1);
        // strip a trailing comment outside parentheses
        int depth = 0;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (f[i] == '(') ++depth;
            if (f[i] == ')') --depth;
            if (f[i] == ';' && depth == 0) {
                f = f.substr(0, i);
                break;
            }
        }
        out[key] = expr::normalize_smt(f);
    }
    return out;
}

TEST(ModelLift, TutorialAutomatonLiftsAndCompilesBack) {
    const authoring::Model ta = automaton("examples/thermal-chamber/models/chamber_dt.tta.json");
    const Lifted l = lift(LiftInput{ta, "", "", "chamber", ""});
    ASSERT_TRUE(l.document.has_value()) << to_json(l.diagnostics).dump(2);
    const Compiled c = compile(*l.document);
    ASSERT_TRUE(c.artifacts.has_value()) << to_json(c.diagnostics).dump(2);
    std::string why;
    EXPECT_TRUE(same_automaton(*c.artifacts->automaton, ta, &why)) << why;
    // The state timer is recognised (t is reset on every state change).
    const Behavior& b = *l.document->types.front().behavior;
    ASSERT_EQ(b.timers.size(), 1U);
    EXPECT_EQ(b.timers.front().restart, Restart::OnStateChange);
    // Without an interpretation, channels stay invisible to the aligner.
    for (const Event& e : l.document->types.front().events) EXPECT_TRUE(e.uninterpreted) << e.name;
}

TEST(ModelLift, OntologyAndInterpretationBecomeProperties) {
    const authoring::Model ta = automaton("examples/thermal-chamber/models/chamber_dt.tta.json");
    const std::string ont = read("examples/thermal-chamber/models/thermal-chamber.ont");
    const std::string interp = read("examples/thermal-chamber/models/dt.interp");
    const Lifted l = lift(LiftInput{ta, ont, interp, "chamber", ""});
    ASSERT_TRUE(l.document.has_value()) << to_json(l.diagnostics).dump(2);
    const AssetType& root = l.document->types.front();
    const auto prop = [&](const std::string& n) -> const Property* {
        for (const Property& p : root.properties) {
            if (p.name == n) return &p;
        }
        return nullptr;
    };
    ASSERT_NE(prop("temperature_limit"), nullptr);
    EXPECT_EQ(prop("temperature_limit")->kind, PropertyKind::Fixed);
    EXPECT_EQ(prop("temperature_limit")->value, "80");
    ASSERT_NE(prop("temperature"), nullptr);
    EXPECT_EQ(prop("temperature")->kind, PropertyKind::Measured);
    EXPECT_EQ(prop("temperature")->min, "-40");
    ASSERT_NE(prop("heater_on"), nullptr);
    EXPECT_EQ(prop("heater_on")->type, DataType::Boolean);

    const Compiled c = compile(*l.document);
    ASSERT_TRUE(c.artifacts.has_value()) << to_json(c.diagnostics).dump(2);
    std::string why;
    EXPECT_TRUE(same_automaton(*c.artifacts->automaton, ta, &why)) << why;
    // Every interpretation entry comes back formula by formula.
    EXPECT_EQ(entries(c.artifacts->dt_interpretation), entries(interp));
}

TEST(ModelLift, LiftOfACompiledModelIsTheModel) {
    // A one-type model using every construct whose generated form lifting recognises.
    const Json doc = Json::parse(R"({
      "format": "twin-model/1", "name": "kiln", "root": "Kiln",
      "types": [{
        "id": "t", "name": "Kiln",
        "properties": [
          {"id": "p1", "name": "temperature", "kind": "measured", "type": "real", "range": {"min": "0", "max": "1400"}},
          {"id": "p2", "name": "limit", "kind": "fixed", "type": "real", "value": "1200"},
          {"id": "p3", "name": "door_closed", "kind": "measured", "type": "boolean"}
        ],
        "events": [
          {"id": "e1", "name": "too_hot", "origin": "condition", "when": "temperature > limit"},
          {"id": "e2", "name": "door_opened", "origin": "reported"}
        ],
        "actions": [
          {"id": "a1", "name": "fire", "meaning": "door_closed and temperature < limit"},
          {"id": "a2", "name": "abort"}
        ],
        "behavior": {
          "timers": [{"id": "x1", "name": "t"}],
          "states": [
            {"id": "s1", "name": "Cold", "initial": true, "meaning": "not door_closed or temperature < 100"},
            {"id": "s2", "name": "Firing", "meaning": "door_closed", "limits": [{"timer": "t", "op": "<=", "bound": 36000}]},
            {"id": "s3", "name": "Soaking"},
            {"id": "s4", "name": "Alarm"}
          ],
          "transitions": [
            {"id": "r1", "from": ["Cold"], "to": "Firing", "trigger": {"action": "fire"}},
            {"id": "r2", "from": ["Firing"], "to": "Soaking", "trigger": {"timeout": 7200}},
            {"id": "r3", "from": ["Firing", "Soaking"], "to": "Alarm", "trigger": {"event": "too_hot"}},
            {"id": "r4", "from": ["Firing", "Soaking"], "to": "Cold", "trigger": {"action": "abort"}},
            {"id": "r5", "from": ["Soaking"], "to": "Cold", "trigger": {"event": "door_opened"}, "window": [{"timer": "t", "op": ">=", "bound": 60}]},
            {"id": "r6", "from": ["Alarm"], "to": "Cold", "trigger": {"action": "abort"}, "window": [{"timer": "t", "op": ">=", "bound": 300}]}
          ]
        }
      }]
    })");
    const Compiled first = compile(doc);
    ASSERT_TRUE(first.artifacts.has_value()) << to_json(first.diagnostics).dump(2);
    const Lifted l = lift(LiftInput{*first.artifacts->automaton, first.artifacts->ontology, first.artifacts->dt_interpretation, "kiln", ""});
    ASSERT_TRUE(l.document.has_value()) << to_json(l.diagnostics).dump(2);
    const AssetType& root = l.document->types.front();
    // Actions, timeouts and groups are recovered.
    ASSERT_EQ(root.actions.size(), 2U);
    EXPECT_EQ(root.actions.front().meaning, "door_closed and temperature < limit");
    const auto& transitions = root.behavior->transitions;
    EXPECT_TRUE(std::any_of(transitions.begin(), transitions.end(), [](const Transition& t) { return t.trigger.kind == TriggerKind::Timeout; }));
    EXPECT_TRUE(std::any_of(transitions.begin(), transitions.end(), [](const Transition& t) { return t.from.size() == 2; }));

    const Compiled second = compile(*l.document);
    ASSERT_TRUE(second.artifacts.has_value()) << to_json(second.diagnostics).dump(2);
    std::string why;
    EXPECT_TRUE(same_automaton(*second.artifacts->automaton, *first.artifacts->automaton, &why)) << why;
    EXPECT_EQ(entries(second.artifacts->dt_interpretation), entries(first.artifacts->dt_interpretation));
    // The lifted model is in normal form: lifting its compilation again changes nothing.
    const Lifted again = lift(LiftInput{*second.artifacts->automaton, second.artifacts->ontology, second.artifacts->dt_interpretation, "kiln", ""});
    ASSERT_TRUE(again.document.has_value());
    EXPECT_EQ(encode(*again.document), encode(*l.document));
}

TEST(ModelLift, ReferenceSuggestionMapsEqualNamesOnly) {
    const Json doc = json::Json::parse(read("examples/thermal-chamber/model.json"));
    const Decoded d = decode(doc);
    ASSERT_FALSE(has_errors(d.diagnostics));
    authoring::Model pt = automaton("examples/thermal-chamber/models/chamber_dt.tta.json");
    pt.channels.push_back({"latch_reset", ""});
    const Reference ref = reference_suggestion(d.document, pt);
    EXPECT_EQ(ref.states.at("HEATING").states, std::vector<std::string>{"HEATING"});
    EXPECT_EQ(ref.labels.at("start_heating").trigger, "start_heating");
    EXPECT_FALSE(ref.labels.contains("latch_reset"));  // never guessed
}

}  // namespace
}  // namespace twin::model
