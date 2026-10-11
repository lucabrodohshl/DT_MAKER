/**
 * @file config.cpp
 * @brief twin-ingest configuration documents (see config.hpp).
 */
#include "twin/ingest/config.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace twin::ingest {

using json::Json;

namespace {

Result<int> port_of(const Json& j, const char* what) {
    const Json p = j.value("port", Json());
    if (!p.is_number_integer() || p.get<std::int64_t>() < 1 || p.get<std::int64_t>() > 65535) {
        return make_error(ErrorCode::InvalidArgument, std::string(what) + ": the port must be a number from 1 to 65535");
    }
    return static_cast<int>(p.get<std::int64_t>());
}

std::optional<std::string> optional_text(const Json& j, const char* key) {
    if (!j.contains(key) || !j.at(key).is_string() || j.at(key).get<std::string>().empty()) return std::nullopt;
    return j.at(key).get<std::string>();
}

}  // namespace

Result<IngestConfig> config_from_json(const Json& doc) {
    if (!doc.is_object() || doc.value("format", std::string()) != kConfigFormat) {
        return make_error(ErrorCode::InvalidArgument, std::string("not a ") + kConfigFormat + " configuration");
    }
    IngestConfig c;
    c.instance = doc.value("instance", std::string());
    c.token = doc.value("token", std::string());
    if (c.token.size() < 16) return make_error(ErrorCode::InvalidArgument, "the access token must have at least 16 characters");
    const Json time = doc.value("time", Json::object());
    c.service.ticks_per_unit = time.value("ticksPerUnit", std::int64_t{1000});
    c.service.time_unit = time.value("unit", std::string("s"));
    const Json clock = doc.value("clock", Json::object());
    c.service.sync_seconds = clock.value("syncSeconds", 0.0);
    c.service.grace_seconds = clock.value("graceSeconds", 0.5);
    if (c.service.sync_seconds < 0 || c.service.grace_seconds < 0) {
        return make_error(ErrorCode::InvalidArgument, "clock.syncSeconds and clock.graceSeconds must not be negative");
    }
    auto mapping = mapping_from_json(doc.value("mapping", Json::object()));
    if (!mapping) return std::move(mapping).error();
    c.mapping = std::move(mapping).value();

    const Json rest = doc.value("rest", Json::object());
    c.rest.enabled = rest.value("enabled", false);
    if (c.rest.enabled) {
        c.rest.host = rest.value("host", std::string("0.0.0.0"));
        auto p = port_of(rest, "REST endpoint");
        if (!p) return std::move(p).error();
        c.rest.port = p.value();
    }
    const Json mqtt_doc = doc.value("mqtt", Json::object());
    c.mqtt_enabled = mqtt_doc.value("enabled", false);
    if (c.mqtt_enabled) {
        c.mqtt.host = mqtt_doc.value("host", std::string("0.0.0.0"));
        auto p = port_of(mqtt_doc, "MQTT endpoint");
        if (!p) return std::move(p).error();
        c.mqtt.port = p.value();
    }
    const Json broker = doc.value("broker", Json::object());
    c.broker_enabled = broker.value("enabled", false);
    if (c.broker_enabled) {
        c.broker.host = broker.value("host", std::string());
        if (c.broker.host.empty()) return make_error(ErrorCode::InvalidArgument, "broker: the host is required");
        auto p = port_of(broker, "broker");
        if (!p) return std::move(p).error();
        c.broker.port = p.value();
        for (const Json& t : broker.value("topics", Json::array())) {
            if (!t.is_string() || !mqtt::valid_topic_filter(t.get<std::string>())) {
                return make_error(ErrorCode::InvalidArgument, "broker: invalid topic filter " + t.dump());
            }
            c.broker.topics.push_back(t.get<std::string>());
        }
        if (c.broker.topics.empty()) return make_error(ErrorCode::InvalidArgument, "broker: give at least one topic to subscribe to");
        const std::int64_t qos = broker.value("qos", std::int64_t{1});
        if (qos < 0 || qos > 2) return make_error(ErrorCode::InvalidArgument, "broker: QoS is 0, 1 or 2");
        c.broker.qos = static_cast<std::uint8_t>(qos);
        c.broker.client_id = broker.value("clientId", "twin-" + c.instance);
        c.broker.username = optional_text(broker, "username");
        c.broker.password = optional_text(broker, "password");
        const std::int64_t keep = broker.value("keepAliveSeconds", std::int64_t{30});
        c.broker.keep_alive = static_cast<std::uint16_t>(std::clamp<std::int64_t>(keep, 0, 65535));
    }
    if (!c.rest.enabled && !c.mqtt_enabled && !c.broker_enabled) {
        return make_error(ErrorCode::InvalidArgument, "no input is enabled (REST endpoint, MQTT endpoint or broker subscription)");
    }
    if (c.rest.enabled && c.mqtt_enabled && c.rest.port == c.mqtt.port) {
        return make_error(ErrorCode::InvalidArgument, "the REST and MQTT endpoints need different ports");
    }
    return c;
}

Result<IngestConfig> load_config(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return make_error(ErrorCode::IoError, "cannot read " + file.string());
    std::ostringstream text;
    text << in.rdbuf();
    auto doc = json::parse(text.str());
    if (!doc) return std::move(doc).error();
    return config_from_json(doc.value());
}

}  // namespace twin::ingest
