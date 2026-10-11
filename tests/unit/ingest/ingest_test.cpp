/**
 * @file ingest_test.cpp
 * @brief Plant connectivity (twin::ingest): MQTT codec, topic matching, mapping, logical clock,
 *        the service against a fake runtime, and both MQTT directions over real sockets.
 */
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "twin/ingest/clock.hpp"
#include "twin/ingest/config.hpp"
#include "twin/ingest/mapping.hpp"
#include "twin/ingest/mqtt.hpp"
#include "twin/ingest/mqtt_endpoint.hpp"
#include "twin/ingest/mqtt_subscriber.hpp"
#include "twin/ingest/net.hpp"
#include "twin/ingest/service.hpp"

namespace twin::ingest {
namespace {

using json::Json;
namespace m = mqtt;

// ------------------------------------------------------------------------------ helpers

m::Packet one_packet(const std::string& bytes) {
    m::FrameReader r;
    r.feed(bytes);
    auto p = r.next();
    EXPECT_TRUE(p && p.value()) << (p ? "incomplete" : p.error().to_string());
    return *p.value();
}

/// Reads packets from a socket (test side of a connection).
class Peer {
public:
    explicit Peer(net::Socket s) : socket_(std::move(s)) {}
    void send(const std::string& bytes) { ASSERT_TRUE(net::write_all(socket_.fd(), bytes)); }
    std::optional<m::Packet> read(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        const auto until = std::chrono::steady_clock::now() + timeout;
        std::array<char, 4096> buf{};
        for (;;) {
            auto p = reader_.next();
            if (!p) return std::nullopt;
            if (p.value()) return *p.value();
            if (std::chrono::steady_clock::now() > until) return std::nullopt;
            auto r = net::read_for(socket_.fd(), buf.data(), buf.size(), std::chrono::milliseconds(100));
            if (!r || r.value().kind == net::ReadResult::Kind::Closed) return std::nullopt;
            if (r.value().kind == net::ReadResult::Kind::Data) reader_.feed(std::string_view(buf.data(), r.value().bytes));
        }
    }

private:
    net::Socket socket_;
    m::FrameReader reader_;
};

/// Collects deliveries from another thread.
struct Inbox {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<Delivery> items;
    void add(const Delivery& d) {
        std::lock_guard l(mu);
        items.push_back(d);
        cv.notify_all();
    }
    bool wait_for(std::size_t n) {
        std::unique_lock l(mu);
        return cv.wait_for(l, std::chrono::seconds(5), [&] { return items.size() >= n; });
    }
};

/// A runtime that records what it receives; events are accepted unless listed in `reject`,
/// a delay past `deadline` is refused.
class FakeRuntime final : public RuntimeApi {
public:
    Result<RuntimeState> state() override {
        ++state_reads;
        if (!up) return make_error(ErrorCode::Unavailable, "down");
        return RuntimeState{now, deadline, false};
    }
    Result<Json> pt_event(const std::string& label, Ticks at, const Json&) override {
        if (!up) return make_error(ErrorCode::Unavailable, "down");
        events.emplace_back(label, at);
        if (reject.count(label) != 0) {
            return Json{{"accepted", false}, {"label", "dt_" + label}, {"error", {{"code", "transition_not_enabled"}, {"message", "not enabled"}}}};
        }
        now = at;
        return Json{{"accepted", true}, {"label", "dt_" + label}, {"from", "A"}, {"to", "B"}, {"ledger_seq", events.size()}};
    }
    Status telemetry(Ticks at, const Json& fields) override {
        if (!up) return make_error(ErrorCode::Unavailable, "down");
        samples.emplace_back(at, fields);
        return ok_status();
    }
    Result<Json> advance(Ticks to) override {
        advances.push_back(to);
        if (deadline && to > *deadline) {
            return Json{{"accepted", false}, {"error", {{"code", "invariant_violation"}, {"message", "letting this much time elapse violates the invariant"}}}};
        }
        now = to;
        return Json{{"accepted", true}};
    }

    bool up{true};
    Ticks now{0};
    std::optional<Ticks> deadline;
    std::set<std::string> reject;
    int state_reads{0};
    std::vector<std::pair<std::string, Ticks>> events;
    std::vector<std::pair<Ticks, Json>> samples;
    std::vector<Ticks> advances;
};

Mapping pump_mapping() {
    auto mapping = mapping_from_json(Json::parse(R"json({
      "telemetry": [
        {"id": "bearing_temp", "key": "TT101_degC", "type": "real", "select": {"field": "TT101_degC"}},
        {"id": "shaft_speed", "key": "shaft_speed", "type": "integer", "select": {"path": "$.drive.rpm"}},
        {"id": "motor_running", "key": "motor_running", "type": "boolean", "select": {"field": "running"}},
        {"id": "vibration_rms", "key": "vibration_rms", "type": "real", "select": {"topic": "plant/+/VT101"}}
      ],
      "events": [
        {"id": "start_command", "pt": "start_cmd!", "select": {"field": "event"}, "value": "START"},
        {"id": "halted", "pt": "halted!", "select": {}},
        {"id": "bearing_alarm", "pt": "alarm_raise!", "select": {"field": "alarm"}},
        {"id": "trip", "pt": "emergency_trip!", "select": {"topic": "plant/pump/trip"}}
      ],
      "eventField": "event",
      "timestamp": {"field": "ts", "unit": "ms"}
    })json"));
    EXPECT_TRUE(mapping) << mapping.error().to_string();
    return mapping.value();
}

// ------------------------------------------------------------------------------ codec

TEST(Mqtt, PacketsRoundTripThroughTheFrameReader) {
    m::Connect c;
    c.client_id = "plc-4";
    c.username = "pump";
    c.password = "secret";
    c.keep_alive = 15;
    auto parsed = m::parse_connect(one_packet(m::connect(c)));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().level, 4);
    EXPECT_EQ(parsed.value().client_id, "plc-4");
    EXPECT_EQ(parsed.value().username, std::optional<std::string>("pump"));
    EXPECT_EQ(parsed.value().password, std::optional<std::string>("secret"));
    EXPECT_EQ(parsed.value().keep_alive, 15);

    m::Publish p{"plant/line4/pump", std::string(300, 'x'), 1, false, false, 42};  // two-byte remaining length
    auto pp = m::parse_publish(one_packet(m::publish(p)));
    ASSERT_TRUE(pp);
    EXPECT_EQ(pp.value().topic, p.topic);
    EXPECT_EQ(pp.value().payload, p.payload);
    EXPECT_EQ(pp.value().qos, 1);
    EXPECT_EQ(pp.value().packet_id, 42);

    m::Subscribe s{9, {{"a/#", 1}, {"b/+/c", 0}}};
    auto ps = m::parse_subscribe(one_packet(m::subscribe(s)));
    ASSERT_TRUE(ps);
    ASSERT_EQ(ps.value().topics.size(), 2U);
    EXPECT_EQ(ps.value().topics[1].filter, "b/+/c");
    auto sa = m::parse_suback(one_packet(m::suback(m::Suback{9, {1, 0x80}})));
    ASSERT_TRUE(sa);
    EXPECT_EQ(sa.value().codes, (std::vector<std::uint8_t>{1, 0x80}));
    auto rc = m::parse_connack(one_packet(m::connack(false, m::ConnectReturn::BadCredentials)));
    ASSERT_TRUE(rc);
    EXPECT_EQ(rc.value(), m::ConnectReturn::BadCredentials);
    auto id = m::parse_ack(one_packet(m::ack(m::Type::Pubrel, 77)));
    ASSERT_TRUE(id);
    EXPECT_EQ(id.value(), 77);
}

TEST(Mqtt, FrameReaderWaitsForWholePacketsAndRejectsMalformedOnes) {
    const std::string bytes = m::publish(m::Publish{"t", "hello", 0, false, false, 0}) + m::pingreq();
    m::FrameReader r;
    r.feed(bytes.substr(0, 3));
    auto first = r.next();
    ASSERT_TRUE(first);
    EXPECT_FALSE(first.value().has_value());
    r.feed(bytes.substr(3));
    auto a = r.next();
    auto b = r.next();
    ASSERT_TRUE(a && a.value() && b && b.value());
    EXPECT_EQ(a.value()->type, m::Type::Publish);
    EXPECT_EQ(b.value()->type, m::Type::Pingreq);

    m::FrameReader flags;
    flags.feed(std::string("\x82\x05\x00\x01\x00\x01x", 7));  // SUBSCRIBE must carry flags 0b0010, not 0
    flags.feed(std::string("\x80\x01\x00", 3));
    EXPECT_TRUE(flags.next().ok());  // 0x82 is the correct SUBSCRIBE header
    EXPECT_FALSE(flags.next().ok());  // 0x80: reserved flags

    m::FrameReader huge(16);
    huge.feed(m::publish(m::Publish{"t", std::string(64, 'x'), 0, false, false, 0}));
    EXPECT_FALSE(huge.next().ok());

    m::FrameReader long_length;
    long_length.feed(std::string("\x30\xFF\xFF\xFF\xFF\x01", 6));
    EXPECT_FALSE(long_length.next().ok());

    EXPECT_FALSE(m::parse_publish(one_packet(m::encode(m::Type::Publish, 0x06, std::string("\x00\x01t", 3)))).ok());  // QoS 3
}

TEST(Mqtt, TopicFiltersFollowTheStandard) {
    EXPECT_TRUE(m::topic_matches("sport/tennis/#", "sport/tennis"));
    EXPECT_TRUE(m::topic_matches("sport/tennis/#", "sport/tennis/player1/ranking"));
    EXPECT_TRUE(m::topic_matches("sport/+/player1", "sport/tennis/player1"));
    EXPECT_TRUE(m::topic_matches("+/+", "/finance"));
    EXPECT_TRUE(m::topic_matches("#", "anything/at/all"));
    EXPECT_FALSE(m::topic_matches("sport/+", "sport/tennis/player1"));
    EXPECT_FALSE(m::topic_matches("sport/tennis", "sport/tennis/x"));
    EXPECT_FALSE(m::topic_matches("#", "$SYS/broker"));
    EXPECT_TRUE(m::valid_topic_filter("a/+/b/#"));
    EXPECT_FALSE(m::valid_topic_filter("a/b#"));
    EXPECT_FALSE(m::valid_topic_filter("a+/b"));
    EXPECT_FALSE(m::valid_topic_name("a/+/b"));
}

// ------------------------------------------------------------------------------ mapping

TEST(Mapping, PlantFieldsPathsAndTopicsBecomeTypedTelemetry) {
    const Mapping mp = pump_mapping();
    const Translation t = translate(mp, Json::parse(R"({"TT101_degC": "71.5", "drive": {"rpm": 2950}, "running": 1, "ts": 1700000000123})"), "");
    EXPECT_EQ(t.telemetry, Json::parse(R"({"TT101_degC": 71.5, "shaft_speed": 2950, "motor_running": true})"));
    ASSERT_TRUE(t.source_seconds);
    EXPECT_DOUBLE_EQ(*t.source_seconds, 1700000000.123);
    EXPECT_TRUE(t.problems.empty());

    const Translation per_tag = translate(mp, decode_payload("4.2"), "plant/pump/VT101");
    EXPECT_EQ(per_tag.telemetry, Json::parse(R"({"vibration_rms": 4.2})"));
    EXPECT_TRUE(translate(mp, decode_payload("4.2"), "plant/other/XT1").telemetry.empty());

    const Translation native = translate(mp, Json::parse(R"({"bearing_temp": 70, "vibration_rms": 3})"), "");
    EXPECT_EQ(native.telemetry, Json::parse(R"({"TT101_degC": 70, "vibration_rms": 3})")) << "the twin's own ids always work";

    const Translation wrong = translate(mp, Json::parse(R"({"TT101_degC": "hot", "drive": {"rpm": 2950.5}})"), "");
    EXPECT_TRUE(wrong.telemetry.empty());
    EXPECT_EQ(wrong.problems.size(), 2U) << "never a silent coercion";
}

TEST(Mapping, EventsComeFromCodesIdsLabelsFlagsAndTopics) {
    const Mapping mp = pump_mapping();
    EXPECT_EQ(translate(mp, Json::parse(R"({"event": "START"})"), "").events, std::vector<std::string>{"start_cmd!"});
    EXPECT_EQ(translate(mp, Json::parse(R"({"event": ["halted", "start_cmd!"]})"), "").events,
              (std::vector<std::string>{"halted!", "start_cmd!"}));
    EXPECT_EQ(translate(mp, Json::parse(R"({"alarm": true})"), "").events, std::vector<std::string>{"alarm_raise!"});
    EXPECT_TRUE(translate(mp, Json::parse(R"({"alarm": false})"), "").events.empty());
    EXPECT_EQ(translate(mp, decode_payload("1"), "plant/pump/trip").events, std::vector<std::string>{"emergency_trip!"});
    const Translation unknown = translate(mp, Json::parse(R"({"event": "SELF_DESTRUCT"})"), "");
    EXPECT_TRUE(unknown.events.empty());
    EXPECT_EQ(unknown.unmapped, std::vector<std::string>{"SELF_DESTRUCT"});
}

TEST(Mapping, TimestampsInMillisecondsSecondsAndIso8601) {
    EXPECT_DOUBLE_EQ(*timestamp_seconds(Json(1500), "ms"), 1.5);
    EXPECT_DOUBLE_EQ(*timestamp_seconds(Json("12.25"), "s"), 12.25);
    EXPECT_DOUBLE_EQ(*timestamp_seconds(Json("1970-01-02T00:00:01.5Z"), "iso8601"), 86401.5);
    EXPECT_DOUBLE_EQ(*timestamp_seconds(Json("1970-01-01T01:00:00+01:00"), "iso8601"), 0.0);
    EXPECT_FALSE(timestamp_seconds(Json("yesterday"), "iso8601"));
    EXPECT_FALSE(timestamp_seconds(Json(true), "ms"));
}

// ------------------------------------------------------------------------------ clock

TEST(LogicalClock, PlantTimestampsKeepTheirSpacingAndNeverGoBack) {
    LogicalClock c(1000);
    c.reset(5000, 100.0);
    EXPECT_EQ(c.from_source(1000.0, 102.0).ticks, 7000) << "anchored at the twin's time reached at arrival";
    EXPECT_EQ(c.from_source(1002.5, 102.1).ticks, 9500);
    const LogicalClock::Stamp late = c.from_source(1001.0, 102.2);
    EXPECT_TRUE(late.late);
    EXPECT_EQ(late.ticks, 9500);
    const LogicalClock::Stamp jump = c.from_source(500.0, 103.0);  // the plant clock went back 8 minutes
    EXPECT_TRUE(jump.reanchored);
    EXPECT_GE(jump.ticks, 9500);
}

TEST(LogicalClock, ArrivalTimeAndHeartbeatsShareOneMonotonicLine) {
    LogicalClock c(1000);
    c.reset(0, 10.0);
    EXPECT_EQ(c.from_arrival(12.25).ticks, 2250);
    EXPECT_EQ(c.due(13.25), 3250);
    c.raise_floor(3000);
    EXPECT_EQ(c.due(13.25), 3250) << "a heartbeat does not shift the extrapolation";
    EXPECT_EQ(c.from_arrival(12.5).ticks, 3000) << "never before the floor";
    EXPECT_EQ(ticks_per_second(1000, "min").value(), 1000.0 / 60.0);
    EXPECT_FALSE(ticks_per_second(1000, "fortnight"));
}

// ------------------------------------------------------------------------------ service

TEST(IngestService, ForwardsTelemetryAndEventsAndReportsVerdicts) {
    FakeRuntime rt;
    rt.now = 1000;
    rt.reject = {"halted!"};
    double wall = 50.0;
    auto s = IngestService::create(pump_mapping(), ServiceOptions{}, rt, [&] { return wall; });
    ASSERT_TRUE(s) << s.error().to_string();
    wall = 52.0;
    const Json r = s.value()->handle(Json::parse(R"({"event": "START", "TT101_degC": 70.1, "ts": 10000})"), Origin{"rest", "10.0.0.7", ""});
    EXPECT_EQ(r["at"]["ticks"], 3000);
    EXPECT_EQ(r["telemetry"], 1);
    ASSERT_EQ(rt.samples.size(), 1U);
    EXPECT_EQ(rt.samples[0].second, Json::parse(R"({"TT101_degC": 70.1})"));
    ASSERT_EQ(rt.events.size(), 1U);
    EXPECT_EQ(rt.events[0], (std::pair<std::string, Ticks>{"start_cmd!", 3000}));
    EXPECT_EQ(r["events"][0]["accepted"], true);
    EXPECT_EQ(r["events"][0]["label"], "dt_start_cmd!");

    const Json rej = s.value()->handle(Json::parse(R"({"event": "halted", "ts": 12500})"), Origin{"mqtt", "plc", "t"});
    EXPECT_EQ(rej["at"]["ticks"], 5500);
    EXPECT_EQ(rej["events"][0]["accepted"], false);
    const Json st = s.value()->status();
    EXPECT_EQ(st["counters"]["eventsAccepted"], 1);
    EXPECT_EQ(st["counters"]["eventsRejected"], 1);
    EXPECT_EQ(st["lastEvent"]["pt"], "halted!");

    rt.up = false;
    const Json down = s.value()->handle(Json::parse(R"({"event": "START"})"), Origin{"rest", "x", ""});
    EXPECT_TRUE(down.contains("error")) << down.dump();
}

TEST(IngestService, EventsAreNeverStampedBeforeTheTwinsTime) {
    FakeRuntime rt;
    double wall = 0.0;
    auto s = IngestService::create(pump_mapping(), ServiceOptions{}, rt, [&] { return wall; });
    ASSERT_TRUE(s);
    rt.now = 60000;  // another writer (an operator) moved the twin to t = 60
    wall = 1.0;
    const Json r = s.value()->handle(Json::parse(R"({"event": "START"})"), Origin{"rest", "x", ""});
    EXPECT_EQ(r["at"]["ticks"], 60000);
}

TEST(IngestService, AMissedDeadlineIsReportedOnceThroughTheKernel) {
    FakeRuntime rt;
    rt.deadline = 10000;  // the twin's current location allows at most 10 s
    double wall = 0.0;
    ServiceOptions opt;
    opt.grace_seconds = 0.5;
    auto s = IngestService::create(pump_mapping(), opt, rt, [&] { return wall; });
    ASSERT_TRUE(s);
    wall = 10.4;
    s.value()->tick();
    EXPECT_TRUE(rt.advances.empty()) << "still within the grace period";
    wall = 10.6;
    s.value()->tick();
    ASSERT_EQ(rt.advances.size(), 1U);
    EXPECT_GT(rt.advances[0], 10000);
    wall = 20.0;
    s.value()->tick();
    EXPECT_EQ(rt.advances.size(), 1U) << "no repeated violations";
    EXPECT_FALSE(s.value()->status()["missedDeadline"].is_null());
}

TEST(IngestService, OptionalClockSyncAdvancesWhenThePlantIsQuiet) {
    FakeRuntime rt;
    double wall = 0.0;
    ServiceOptions opt;
    opt.sync_seconds = 2.0;
    auto s = IngestService::create(pump_mapping(), opt, rt, [&] { return wall; });
    ASSERT_TRUE(s);
    wall = 1.0;
    s.value()->tick();
    EXPECT_TRUE(rt.advances.empty());
    wall = 3.0;
    s.value()->tick();
    ASSERT_EQ(rt.advances.size(), 1U);
    EXPECT_EQ(rt.advances[0], 2500);  // 3 s minus the 0.5 s grace
}

// ------------------------------------------------------------------------------ config

TEST(IngestConfig, ChecksInputsPortsAndToken) {
    Json doc = Json::parse(R"json({"format": "twin-ingest/1", "instance": "p", "token": "0123456789abcdef0123",
        "rest": {"enabled": true, "port": 19000}, "mqtt": {"enabled": true, "port": 19001},
        "mapping": {"telemetry": [], "events": []}})json");
    ASSERT_TRUE(config_from_json(doc)) << config_from_json(doc).error().to_string();
    Json same = doc;
    same["mqtt"]["port"] = 19000;
    EXPECT_FALSE(config_from_json(same));
    Json none = doc;
    none["rest"]["enabled"] = false;
    none["mqtt"]["enabled"] = false;
    EXPECT_FALSE(config_from_json(none));
    Json weak = doc;
    weak["token"] = "short";
    EXPECT_FALSE(config_from_json(weak));
    Json broker = doc;
    broker["broker"] = Json::parse(R"({"enabled": true, "host": "gw", "port": 1883, "topics": ["a/b#"]})");
    EXPECT_FALSE(config_from_json(broker)) << "invalid topic filter";
}

// ------------------------------------------------------------------------------ sockets

TEST(MqttEndpoint, AuthenticatesAcknowledgesAndDeliversEveryQos) {
    Inbox inbox;
    auto ep = MqttEndpoint::start(EndpointOptions{"127.0.0.1", 0}, [](const m::Connect& c) {
        return c.password == std::optional<std::string>("token-0123456789") ? m::ConnectReturn::Accepted : m::ConnectReturn::BadCredentials;
    }, [&](const Delivery& d) { inbox.add(d); });
    ASSERT_TRUE(ep) << ep.error().to_string();
    const int port = ep.value()->port();

    {
        auto s = net::connect_tcp("127.0.0.1", port, std::chrono::milliseconds(2000));
        ASSERT_TRUE(s);
        Peer bad(std::move(s).value());
        bad.send(m::connect(m::Connect{4, true, 30, "intruder", std::string("u"), std::string("wrong"), false}));
        auto ack = bad.read();
        ASSERT_TRUE(ack);
        EXPECT_EQ(m::parse_connack(*ack).value(), m::ConnectReturn::BadCredentials);
    }
    auto s = net::connect_tcp("127.0.0.1", port, std::chrono::milliseconds(2000));
    ASSERT_TRUE(s);
    Peer plc(std::move(s).value());
    plc.send(m::connect(m::Connect{4, true, 30, "plc-4", std::string("pump"), std::string("token-0123456789"), false}));
    auto connack = plc.read();
    ASSERT_TRUE(connack);
    EXPECT_EQ(m::parse_connack(*connack).value(), m::ConnectReturn::Accepted);
    plc.send(m::publish(m::Publish{"twins/p/data", "{\"a\":0}", 0, false, false, 0}));
    plc.send(m::publish(m::Publish{"twins/p/data", "{\"a\":1}", 1, false, false, 5}));
    auto puback = plc.read();
    ASSERT_TRUE(puback);
    EXPECT_EQ(puback->type, m::Type::Puback);
    plc.send(m::publish(m::Publish{"twins/p/data", "{\"a\":2}", 2, false, false, 6}));
    plc.send(m::publish(m::Publish{"twins/p/data", "{\"a\":2}", 2, false, true, 6}));  // resent before PUBREL: delivered once
    EXPECT_EQ(plc.read()->type, m::Type::Pubrec);
    EXPECT_EQ(plc.read()->type, m::Type::Pubrec);
    plc.send(m::ack(m::Type::Pubrel, 6));
    EXPECT_EQ(plc.read()->type, m::Type::Pubcomp);
    plc.send(m::subscribe(m::Subscribe{7, {{"x/#", 0}}}));
    auto suback = plc.read();
    ASSERT_TRUE(suback);
    EXPECT_EQ(m::parse_suback(*suback).value().codes, std::vector<std::uint8_t>{0x80}) << "receive-only endpoint";
    plc.send(m::pingreq());
    EXPECT_EQ(plc.read()->type, m::Type::Pingresp);
    ASSERT_TRUE(inbox.wait_for(3));
    EXPECT_EQ(inbox.items.size(), 3U);
    EXPECT_EQ(inbox.items[2].payload, "{\"a\":2}");
    EXPECT_EQ(inbox.items[0].client_id, "plc-4");
    EXPECT_EQ(ep.value()->status()["clients"].size(), 1U);
    EXPECT_EQ(ep.value()->status()["refused"], 1);
    ep.value()->stop();
}

TEST(MqttSubscriber, SubscribesToTheBrokerAndAcknowledgesDeliveries) {
    auto listener = net::listen_tcp("127.0.0.1", 0);
    ASSERT_TRUE(listener);
    const int port = net::local_port(listener.value().fd());
    Inbox inbox;
    SubscriberOptions opt;
    opt.host = "127.0.0.1";
    opt.port = port;
    opt.client_id = "twin-p";
    opt.username = "twin";
    opt.password = "pw";
    opt.topics = {"plant/line4/#"};
    MqttSubscriber sub(opt, [&](const Delivery& d) { inbox.add(d); });
    sub.start();
    // A one-connection fake broker.
    auto accepted = net::accept_for(listener.value().fd(), std::chrono::milliseconds(5000));
    ASSERT_TRUE(accepted && accepted.value().valid());
    Peer broker(std::move(accepted).value());
    auto hello = broker.read();
    ASSERT_TRUE(hello);
    auto c = m::parse_connect(*hello);
    ASSERT_TRUE(c);
    EXPECT_EQ(c.value().client_id, "twin-p");
    EXPECT_EQ(c.value().password, std::optional<std::string>("pw"));
    broker.send(m::connack(false, m::ConnectReturn::Accepted));
    auto subscribe = broker.read();
    ASSERT_TRUE(subscribe);
    auto sp = m::parse_subscribe(*subscribe);
    ASSERT_TRUE(sp);
    EXPECT_EQ(sp.value().topics[0].filter, "plant/line4/#");
    broker.send(m::suback(m::Suback{sp.value().packet_id, {1}}));
    broker.send(m::publish(m::Publish{"plant/line4/pump/TT101", "71.5", 1, false, false, 3}));
    auto puback = broker.read();
    ASSERT_TRUE(puback);
    EXPECT_EQ(puback->type, m::Type::Puback);
    ASSERT_TRUE(inbox.wait_for(1));
    EXPECT_EQ(inbox.items[0].topic, "plant/line4/pump/TT101");
    EXPECT_EQ(sub.status()["state"], "connected");
    sub.stop();
    EXPECT_EQ(sub.status()["state"], "stopped");
}

}  // namespace
}  // namespace twin::ingest
