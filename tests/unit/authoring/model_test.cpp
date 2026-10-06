/**
 * @file model_test.cpp
 * @brief Canonical timed-automaton model (twin-ta/1): JSON codec, strictness, hashing, layout.
 */
#include <gtest/gtest.h>

#include <string>

#include "authoring_fixtures.hpp"
#include "twin/authoring/layout.hpp"
#include "twin/authoring/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::authoring {
namespace {

TEST(ModelCodec, RoundTripKeepsEveryField) {
    const Model m = test::pump_like_model();
    const json::Json j = to_json(m);
    Result<Model> back = model_from_json(j);
    ASSERT_TRUE(back) << back.error().to_string();
    EXPECT_EQ(back.value(), m);
}

TEST(ModelCodec, JsonShapeFollowsTheFormat) {
    const json::Json j = to_json(test::pump_like_model());
    EXPECT_EQ(j.at("format"), "twin-ta/1");
    EXPECT_EQ(j.at("name"), "PumpLike");
    // tau edge: sync is null; a send edge has channel + direction
    EXPECT_TRUE(j.at("edges").at(2).at("sync").is_null());
    EXPECT_EQ(j.at("edges").at(0).at("sync").at("direction"), "!");
    // bounds are integers or constant names
    EXPECT_EQ(j.at("locations").at(1).at("invariant").at(0).at("bound"), 600);
    EXPECT_EQ(j.at("edges").at(1).at("guard").at(0).at("bound"), "COOL_MIN");
    // diagonal invariant carries "minus"
    EXPECT_EQ(j.at("locations").at(2).at("invariant").at(0).at("minus"), "u");
}

TEST(ModelCodec, RejectsWrongFormatTag) {
    json::Json j = to_json(test::pump_like_model());
    j["format"] = "twin-ta/2";
    EXPECT_FALSE(model_from_json(j));
}

TEST(ModelCodec, RejectsUnknownKeys) {
    json::Json j = to_json(test::pump_like_model());
    j["locations"][0]["urgent"] = true;
    Result<Model> r = model_from_json(j);
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().to_string().find("urgent"), std::string::npos);
}

TEST(ModelCodec, RejectsBadSyncDirection) {
    json::Json j = to_json(test::pump_like_model());
    j["edges"][0]["sync"]["direction"] = "x";
    EXPECT_FALSE(model_from_json(j));
}

TEST(ModelCodec, RejectsNonIntegerConstant) {
    json::Json j = to_json(test::pump_like_model());
    j["constants"][0]["value"] = 2.5;
    EXPECT_FALSE(model_from_json(j));
}

TEST(ModelCodec, RejectsUnknownComparison) {
    json::Json j = to_json(test::pump_like_model());
    j["locations"][1]["invariant"][0]["op"] = "!=";
    EXPECT_FALSE(model_from_json(j));
}

TEST(ModelCodec, ContentHashIsIndependentOfKeyOrderButNotOfNotes) {
    const Model m = test::pump_like_model();
    // Re-parse from a non-canonical text (keys in another order) -> same hash.
    const std::string shuffled = R"({"name":"X","format":"twin-ta/1","note":"","clocks":[],"constants":[],)"
                                 R"("channels":[],"edges":[],"locations":[{"note":"","name":"A","initial":true,"invariant":[]}]})";
    Result<json::Json> j = json::parse(shuffled);
    ASSERT_TRUE(j);
    Result<Model> a = model_from_json(j.value());
    ASSERT_TRUE(a) << a.error().to_string();
    Model b;
    b.name = "X";
    b.locations.push_back(LocationDecl{"A", true, {}, ""});
    EXPECT_EQ(content_sha256(a.value()), content_sha256(b));
    Model c = m;
    c.locations[0].note = "changed";
    EXPECT_NE(content_sha256(c), content_sha256(m));
}

TEST(LayoutCodec, RoundTrip) {
    Layout l;
    l.locations["A"] = LocationLayout{Point{10, -20}, Point{12, -40}};
    l.locations["B"] = LocationLayout{Point{300, 0}, std::nullopt};
    l.edges["e1"] = EdgeLayout{{Point{100, 50}, Point{150, 60}}, Point{120, 40}};
    const json::Json j = to_json(l);
    EXPECT_EQ(j.at("format"), "twin-ta-layout/1");
    Result<Layout> back = layout_from_json(j);
    ASSERT_TRUE(back) << back.error().to_string();
    EXPECT_EQ(back.value(), l);
}

TEST(LayoutCodec, RejectsNonIntegerCoordinates) {
    json::Json j = {{"format", "twin-ta-layout/1"},
                    {"locations", {{"A", {{"x", 1.5}, {"y", 0}}}}},
                    {"edges", json::Json::object()}};
    EXPECT_FALSE(layout_from_json(j));
}

}  // namespace
}  // namespace twin::authoring
