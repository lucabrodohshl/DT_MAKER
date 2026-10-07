/**
 * @file feed_test.cpp
 * @brief Scripted Physical-Twin feeds: parsing, interpolation, noise, ordering.
 */
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include <algorithm>
#include <iterator>

#include "twin/ptfeed/feed.hpp"

namespace twin::ptfeed {
namespace {

using json::Json;

Feed parse(const Json& j) {
    Result<Feed> f = feed_from_json(j);
    EXPECT_TRUE(f.ok()) << (f ? "" : f.error().to_string());
    return std::move(f).value();
}

const Json kFeed = Json::parse(R"({
  "cycle_s": 10, "telemetry_period_ms": 1000, "seed": 7,
  "channels": {
    "temp":  {"keys": [[0, 20], [10, 40]], "noise": 0.5, "min": 0, "precision": 1},
    "speed": {"keys": [[0, 0], [2, 0], [4, 100], [10, 100]], "noise": 3, "min": 0, "precision": 0},
    "on":    {"type": "boolean", "keys": [[0, 0], [2, 1], [8, 0]]}
  },
  "events": [{"at": 2, "label": "start!"}, {"at": 8, "label": "stop!"}]
})");

TEST(Feed, ChannelsAreParsedFromTheDocument) {
    const Feed f = parse(kFeed);
    ASSERT_EQ(f.channels.size(), 3U);  // regression: items() of a temporary once yielded no channels
    ASSERT_EQ(f.events.size(), 2U);
}

TEST(Feed, InterpolationAndSteps) {
    const Feed f = parse(kFeed);
    const Channel* temp = nullptr;
    const Channel* on = nullptr;
    for (const Channel& c : f.channels) {
        if (c.name == "temp") temp = &c;
        if (c.name == "on") on = &c;
    }
    ASSERT_NE(temp, nullptr);
    ASSERT_NE(on, nullptr);
    EXPECT_DOUBLE_EQ(channel_value(*temp, 5.0), 30.0);
    EXPECT_DOUBLE_EQ(channel_value(*on, 1.9), 0.0);
    EXPECT_DOUBLE_EQ(channel_value(*on, 2.0), 1.0);
    EXPECT_DOUBLE_EQ(channel_value(*on, 9.0), 0.0);
}

TEST(Feed, ItemsAreOrderedEventsFirstAndPhysicallyPlausible) {
    const Feed f = parse(kFeed);
    const std::vector<Item> items = cycle_items(f, 1, 5000);
    ASSERT_EQ(items.size(), 12U);  // 10 samples + 2 events
    for (std::size_t i = 1; i < items.size(); ++i) EXPECT_LE(items[i - 1].at, items[i].at);
    // Cycle 1 starts at base + 10 s; the event at 2 s precedes the sample at the same time.
    const auto ev = std::find_if(items.begin(), items.end(), [](const Item& i) { return i.is_event; });
    ASSERT_NE(ev, items.end());
    EXPECT_EQ(ev->at, 5000 + 10000 + 2000);
    EXPECT_FALSE(std::next(ev)->is_event);
    EXPECT_EQ(std::next(ev)->at, ev->at);
    for (const Item& i : items) {
        if (i.is_event) continue;
        EXPECT_GE(i.body.at("speed").get<double>(), 0.0);
        if (i.at - 15000 < 2000) EXPECT_EQ(i.body.at("speed").get<double>(), 0.0) << "no noise on an exact zero";
        EXPECT_TRUE(i.body.at("on").is_boolean());
    }
}

TEST(Feed, NoiseIsDeterministic) {
    const Feed f = parse(kFeed);
    EXPECT_EQ(cycle_items(f, 3, 0).back().body.dump(), cycle_items(f, 3, 0).back().body.dump());
}

TEST(Feed, RejectsMalformedFeeds) {
    EXPECT_FALSE(feed_from_json(Json::parse(R"({"cycle_s": 0})")).ok());
    EXPECT_FALSE(feed_from_json(Json::parse(R"({"cycle_s": 5, "events": [{"at": 7, "label": "x!"}]})")).ok());
    EXPECT_FALSE(feed_from_json(Json::parse(R"({"cycle_s": 5, "channels": {"t": {"keys": [[2, 1], [1, 0]]}}})")).ok());
}

/// The pump Blueprint's event script (its simulator configuration, authored in Studio) is a feed;
/// decimals are canonical-safe strings there and read exactly as numbers.
TEST(Feed, ThePumpBlueprintScriptLoads) {
    std::ifstream in(std::filesystem::path(TWIN_SOURCE_DIR) / "examples" / "industrial-pump" / "blueprint.json");
    std::ostringstream text;
    text << in.rdbuf();
    const Json blueprint = Json::parse(text.str());
    Result<Feed> f = feed_from_json(blueprint.at("simulation").at("script"));
    ASSERT_TRUE(f.ok()) << f.error().to_string();
    const auto bearing = std::find_if(f.value().channels.begin(), f.value().channels.end(), [](const Channel& c) { return c.name == "bearing_temp"; });
    ASSERT_NE(bearing, f.value().channels.end());
    EXPECT_DOUBLE_EQ(channel_value(*bearing, 171.0), 90.6);  // the decimal string "90.6", exactly
    EXPECT_EQ(f.value().channels.size(), 7U);
    EXPECT_EQ(f.value().events.size(), 7U);
}

}  // namespace
}  // namespace twin::ptfeed
