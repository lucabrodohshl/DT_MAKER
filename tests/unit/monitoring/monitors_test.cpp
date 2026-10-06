/**
 * @file monitors_test.cpp
 * @brief Monitor documents (twin-monitors/1): codec and structural validation.
 */
#include <gtest/gtest.h>

#include <algorithm>

#include "twin/monitoring/monitors.hpp"

namespace twin::monitoring {
namespace {

json::Json example() {
    return json::Json::parse(R"json({
      "format": "twin-monitors/1",
      "requirements": [
        {"id": "REQ-S1", "title": "No uncontrolled trip", "category": "safety", "severity": "critical",
         "description": "The pump never trips in operation.", "monitors": ["never-fault"]}
      ],
      "monitors": [
        {"id": "conformance", "kind": "conformance", "name": "Behavioural conformance", "severity": "critical",
         "events": "all", "unmatchedEvents": "record"},
        {"id": "never-fault", "kind": "property", "name": "Never tripped", "severity": "critical",
         "property": "A[] !FAULT"},
        {"id": "bearing-envelope", "kind": "property", "name": "Bearing envelope", "severity": "warning",
         "property": "A[] sem((not (> bearing_temp bearing_temp_limit)))"},
        {"id": "temp-fresh", "kind": "data_quality", "name": "Bearing temperature freshness", "severity": "warning",
         "field": "bearing_temp", "check": "stale", "maxAgeSeconds": 30},
        {"id": "temp-range", "kind": "data_quality", "name": "Bearing temperature range", "severity": "warning",
         "field": "bearing_temp", "check": "out_of_range", "min": "-40", "max": "180.5"}
      ],
      "alerts": [
        {"id": "AL-1", "monitor": "conformance", "on": "violated", "severity": "critical",
         "message": "Behaviour deviates from the verified model"}
      ]
    })json");
}

std::vector<std::string> codes(const std::vector<Finding>& fs) {
    std::vector<std::string> out;
    for (const Finding& f : fs) out.push_back(f.code);
    return out;
}

TEST(MonitorsDocument, ExampleRoundTripsAndValidates) {
    Result<MonitorsDocument> d = monitors_from_json(example());
    ASSERT_TRUE(d) << d.error().to_string();
    EXPECT_EQ(d.value().monitors.size(), 5u);
    EXPECT_EQ(d.value().monitors[0].kind, "conformance");
    EXPECT_EQ(d.value().monitors[1].config.at("property"), "A[] !FAULT");
    EXPECT_EQ(d.value().monitors[4].config.at("max"), "180.5");
    EXPECT_TRUE(validate_document(d.value()).empty()) << codes(validate_document(d.value())).size();
    Result<MonitorsDocument> again = monitors_from_json(to_json(d.value()));
    ASSERT_TRUE(again);
    EXPECT_EQ(to_json(again.value()), to_json(d.value()));
    // canonical-safe: no floating-point numbers anywhere
    EXPECT_TRUE(json::canonical_dump(to_json(d.value())));
}

TEST(MonitorsDocument, RejectsMalformedDocuments) {
    json::Json bad = example();
    bad["format"] = "twin-monitors/9";
    EXPECT_FALSE(monitors_from_json(bad));
    json::Json missing = example();
    missing["monitors"][0].erase("kind");
    EXPECT_FALSE(monitors_from_json(missing));
    json::Json floaty = example();
    floaty["monitors"][3]["maxAgeSeconds"] = 2.5;
    EXPECT_FALSE(monitors_from_json(floaty));
}

TEST(MonitorsDocument, StructuralFindings) {
    json::Json j = example();
    j["monitors"][1]["id"] = "conformance";                  // TWN001 duplicate id
    j["monitors"][2]["severity"] = "fatal";                  // TWN003 severity
    j["monitors"][0]["events"] = json::Json::array({"start"});  // TWN010 events are PT labels a!
    j["monitors"][3]["check"] = "teleport";                  // TWN020 unknown check
    j["monitors"][4]["property"] = "x";                      // TWN004 unknown parameter for the kind
    j["alerts"][0]["monitor"] = "nope";                      // TWN030 unknown monitor
    j["requirements"][0]["monitors"] = json::Json::array({"nope"});  // TWN040
    j["monitors"].push_back({{"id", "broken"}, {"kind", "property"}, {"name", "Broken"}, {"severity", "info"},
                             {"property", "A[] (NORMAL"}});   // TWN011 property does not parse
    j["monitors"].push_back({{"id", "who"}, {"kind", "teleporter"}, {"name", "?"}, {"severity", "info"}});  // TWN002
    Result<MonitorsDocument> d = monitors_from_json(j);
    ASSERT_TRUE(d) << d.error().to_string();
    const std::vector<std::string> c = codes(validate_document(d.value()));
    for (const char* expected : {"TWN001", "TWN002", "TWN003", "TWN004", "TWN010", "TWN011", "TWN020", "TWN030", "TWN040"}) {
        EXPECT_NE(std::find(c.begin(), c.end(), expected), c.end()) << expected;
    }
}

}  // namespace
}  // namespace twin::monitoring
