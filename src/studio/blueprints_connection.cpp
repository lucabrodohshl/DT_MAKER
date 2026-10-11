/**
 * @file blueprints_connection.cpp
 * @brief BlueprintService: the plant connection of an instance — its inputs (REST endpoint, MQTT
 *        endpoint, broker subscription), ports, access token, the twin-ingest configuration
 *        written at deployment, and the connection information Studio shows (see blueprints.hpp).
 *
 * The connection is part of the instance (instance_config.connection); the broker password is
 * kept apart, in <data>/secrets/instances/<id>.json (mode 0600), so that it never appears in
 * the database or in an API response. The mapping from plant messages to the data contract is
 * derived from the instance's bindings and its Blueprint version's data contract; the twin's
 * own vocabulary (telemetry ids, event ids and PT labels) is always accepted as well.
 */
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#if !defined(__APPLE__)
#include <random>
#endif

#include "blueprints_impl.hpp"
#include "twin/core/wall_clock.hpp"
#include "twin/ingest/config.hpp"
#include "twin/ingest/mqtt.hpp"
#include "twin/studio/runtime_bridge.hpp"
#include "twin/studio/supervisor.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using json::Json;
using namespace twin::platform;

namespace {

constexpr int kPlantPortBegin = 19000;  ///< Automatic plant-facing ports (outside the supervisor's range).
constexpr int kPlantPortEnd = 19999;

std::string random_token() {
    std::array<unsigned char, 24> bytes{};
#if defined(__APPLE__)
    arc4random_buf(bytes.data(), bytes.size());
#else
    std::random_device rd;
    for (unsigned char& b : bytes) b = static_cast<unsigned char>(rd());
#endif
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

/// Whether a TCP port can be bound on all interfaces right now.
bool bindable(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    const bool ok = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

/// Addresses the plant can use to reach this machine: TWIN_STUDIO_PLANT_HOSTS (comma-separated,
/// e.g. a DNS name or the address seen through NAT) when set, else this machine's IPv4 addresses
/// (no loopback, no link-local) and host name; then 127.0.0.1 (for tests on this machine).
std::vector<std::string> host_addresses() {
    std::vector<std::string> out;
    if (const char* advertised = std::getenv("TWIN_STUDIO_PLANT_HOSTS"); advertised != nullptr && *advertised != '\0') {
        std::stringstream list(advertised);
        for (std::string h; std::getline(list, h, ',');) {
            h.erase(0, h.find_first_not_of(" \t"));
            h.erase(h.find_last_not_of(" \t") + 1);
            if (!h.empty()) out.push_back(h);
        }
        out.emplace_back("127.0.0.1");
        return out;
    }
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i != nullptr; i = i->ifa_next) {
            if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET) continue;
            if ((i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0) continue;
            std::array<char, INET_ADDRSTRLEN> text{};
            const auto* in = reinterpret_cast<const sockaddr_in*>(i->ifa_addr);
            if (::inet_ntop(AF_INET, &in->sin_addr, text.data(), text.size()) == nullptr) continue;
            const std::string a(text.data());
            if (a.rfind("169.254.", 0) == 0) continue;
            if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
        }
        ::freeifaddrs(list);
    }
    std::array<char, 256> host{};
    if (::gethostname(host.data(), host.size()) == 0 && host[0] != '\0') {
        std::string h(host.data());
        if (h.find('.') == std::string::npos) h += ".local";
        out.push_back(h);
    }
    out.emplace_back("127.0.0.1");
    return out;
}

/// Shell-quote @p s with single quotes.
std::string sq(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    return out + "'";
}

fs::path secrets_file(const Services& services, std::string_view id) {
    return services.config().data_dir / "secrets" / "instances" / (std::string(id) + ".json");
}

/// Write @p bytes to @p file readable by the owner only (created 0600, then renamed into place).
Status write_private(const fs::path& file, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return make_error(ErrorCode::IoError, "cannot write " + tmp.string());
    const char* p = bytes.data();
    std::size_t left = bytes.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n <= 0) {
            ::close(fd);
            return make_error(ErrorCode::IoError, "cannot write " + tmp.string());
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    ::close(fd);
    fs::rename(tmp, file, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot write " + file.string() + ": " + ec.message());
    return ok_status();
}

std::optional<std::string> broker_password(const Services& services, std::string_view id) {
    std::ifstream in(secrets_file(services, id));
    if (!in) return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    auto j = json::parse(text.str());
    if (!j || !j.value().contains("brokerPassword") || !j.value().at("brokerPassword").is_string()) return std::nullopt;
    return j.value().at("brokerPassword").get<std::string>();
}

Status set_broker_password(const Services& services, std::string_view id, const std::string& password) {
    const fs::path f = secrets_file(services, id);
    if (password.empty()) {
        std::error_code ec;
        fs::remove(f, ec);
        return ok_status();
    }
    return write_private(f, Json{{"brokerPassword", password}}.dump());
}

/// Ports configured for plant connections by every instance but @p except: port -> who uses it.
std::map<int, std::string> plant_ports(const std::vector<Twin>& twins, std::string_view except) {
    std::map<int, std::string> out;
    for (const Twin& t : twins) {
        if (t.id == except) continue;
        const Json c = t.instance_config.value("connection", Json::object());
        for (const char* k : {"rest", "mqtt"}) {
            const Json x = c.value(k, Json::object());
            if (x.value("enabled", false) && x.value("port", 0) > 0) {
                out[x.value("port", 0)] = "instance " + t.id + " (" + (std::string(k) == "rest" ? "REST" : "MQTT") + " endpoint)";
            }
        }
    }
    return out;
}

double number_or(const Json& j, const char* key, double fallback) {
    const Json v = j.value(key, Json());
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        char* end = nullptr;
        const std::string s = v.get<std::string>();
        const double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() && *end == '\0') return d;
    }
    return fallback;
}

/// The requested connection, checked and completed: ports allocated (or kept), token kept or issued.
Result<Json> normalize_connection(const Json& req, const std::string& id, const Json& previous, const std::map<int, std::string>& taken,
                                  int reserved_begin, int reserved_end) {
    if (!req.is_object()) return make_error(ErrorCode::InvalidArgument, "the plant connection must be a JSON object");
    Json out = Json::object();
    std::set<int> mine;
    auto port_for = [&](const char* key, const std::string& label) -> Result<int> {
        const Json in = req.value(key, Json::object());
        const Json p = in.contains("port") ? in.at("port") : Json(nullptr);
        const bool automatic = p.is_null() || (p.is_number_integer() && p.get<std::int64_t>() == 0) || (p.is_string() && p.get<std::string>() == "auto");
        if (automatic) {
            const int prev = previous.value(key, Json::object()).value("port", 0);
            if (prev > 0 && !taken.count(prev) && !mine.count(prev)) {
                mine.insert(prev);
                return prev;
            }
            for (int q = kPlantPortBegin; q <= kPlantPortEnd; ++q) {
                if (!taken.count(q) && !mine.count(q) && bindable(q)) {
                    mine.insert(q);
                    return q;
                }
            }
            return make_error(ErrorCode::Unavailable, label + ": no free port between " + std::to_string(kPlantPortBegin) + " and " +
                                                          std::to_string(kPlantPortEnd));
        }
        if (!p.is_number_integer()) return make_error(ErrorCode::InvalidArgument, label + ": the port is a number from 1 to 65535 (empty: automatic)");
        const auto v = static_cast<int>(p.get<std::int64_t>());
        if (v < 1 || v > 65535) return make_error(ErrorCode::InvalidArgument, label + ": the port is a number from 1 to 65535");
        if (v >= reserved_begin && v <= reserved_end) {
            return make_error(ErrorCode::InvalidArgument, label + ": port " + std::to_string(v) + " is in the range Studio reserves for twin runtimes (" +
                                                              std::to_string(reserved_begin) + "–" + std::to_string(reserved_end) + "); choose another");
        }
        if (auto t = taken.find(v); t != taken.end()) {
            return make_error(ErrorCode::InvalidArgument, label + ": port " + std::to_string(v) + " is already used by " + t->second);
        }
        if (mine.count(v)) return make_error(ErrorCode::InvalidArgument, "the REST and MQTT endpoints need different ports");
        mine.insert(v);
        return v;
    };
    for (const auto& [key, label] : {std::pair<const char*, std::string>{"rest", "REST endpoint"}, {"mqtt", "MQTT endpoint"}}) {
        const bool enabled = req.value(key, Json::object()).value("enabled", false);
        Json x{{"enabled", enabled}};
        if (enabled) {
            auto port = port_for(key, label);
            if (!port) return std::move(port).error();
            x["port"] = port.value();
        } else {
            x["port"] = previous.value(key, Json::object()).value("port", Json(nullptr));
        }
        out[key] = x;
    }

    const Json b = req.value("broker", Json::object());
    const Json pb = previous.value("broker", Json::object());
    Json broker{{"enabled", b.value("enabled", false)},
                {"host", b.value("host", pb.value("host", std::string()))},
                {"port", b.value("port", pb.value("port", Json(1883)))},
                {"qos", b.value("qos", pb.value("qos", Json(1)))},
                {"clientId", b.value("clientId", pb.value("clientId", "twin-" + id))},
                {"username", b.value("username", pb.value("username", std::string()))}};
    Json topics = b.contains("topics") ? b.at("topics") : (b.contains("topic") ? Json::array({b.at("topic")}) : pb.value("topics", Json::array()));
    if (topics.is_string()) topics = Json::array({topics});
    broker["topics"] = topics;
    if (broker.at("enabled").get<bool>()) {
        if (!broker.at("host").is_string() || broker.at("host").get<std::string>().empty()) {
            return make_error(ErrorCode::InvalidArgument, "broker: give the host name or address of the plant's MQTT broker");
        }
        if (!broker.at("port").is_number_integer() || broker.at("port").get<std::int64_t>() < 1 || broker.at("port").get<std::int64_t>() > 65535) {
            return make_error(ErrorCode::InvalidArgument, "broker: the port is a number from 1 to 65535");
        }
        if (!topics.is_array() || topics.empty()) return make_error(ErrorCode::InvalidArgument, "broker: give at least one topic to subscribe to, such as plant/line4/#");
        for (const Json& t : topics) {
            if (!t.is_string() || !ingest::mqtt::valid_topic_filter(t.get<std::string>())) {
                return make_error(ErrorCode::InvalidArgument, "broker: " + t.dump() + " is not a valid MQTT topic filter");
            }
        }
        if (!broker.at("qos").is_number_integer() || broker.at("qos").get<std::int64_t>() < 0 || broker.at("qos").get<std::int64_t>() > 2) {
            return make_error(ErrorCode::InvalidArgument, "broker: QoS is 0, 1 or 2");
        }
        if (!broker.at("clientId").is_string() || broker.at("clientId").get<std::string>().empty()) broker["clientId"] = "twin-" + id;
    }
    out["broker"] = broker;

    const Json ts = req.value("timestamp", previous.value("timestamp", Json::object()));
    const std::string unit = ts.value("unit", std::string("ms"));
    if (unit != "ms" && unit != "s" && unit != "iso8601") {
        return make_error(ErrorCode::InvalidArgument, "timestamp unit is ms (epoch milliseconds), s (epoch seconds) or iso8601");
    }
    out["timestamp"] = Json{{"field", ts.value("field", std::string())}, {"unit", unit}};
    std::string event_field = req.value("eventField", previous.value("eventField", std::string("event")));
    if (event_field.empty()) event_field = "event";
    out["eventField"] = event_field;
    const Json clock = req.value("clock", previous.value("clock", Json::object()));
    const double sync = number_or(clock, "syncSeconds", 0.0);
    const double grace = number_or(clock, "graceSeconds", 0.5);
    if (sync < 0 || sync > 3600) return make_error(ErrorCode::InvalidArgument, "clock: syncSeconds is between 0 (only at deadlines) and 3600");
    if (grace < 0 || grace > 60) return make_error(ErrorCode::InvalidArgument, "clock: graceSeconds is between 0 and 60");
    out["clock"] = Json{{"syncSeconds", sync}, {"graceSeconds", grace}};
    const std::string token = previous.value("token", std::string());
    out["token"] = token.empty() ? random_token() : token;
    return out;
}

/// The mapping document of twin-ingest: the data contract of @p document, bound by @p connectivity.
Json ingest_mapping(const Json& document, const Json& connectivity, const Json& connection) {
    const Json data = document.value("data", Json::object());
    const Json bindings = connectivity.value("bindings", Json::array());
    Json telemetry = Json::array();
    for (const Json& t : data.value("telemetry", Json::array())) {
        const std::string tid = t.value("id", std::string());
        Json binding = nullptr;
        for (const Json& b : bindings) {
            const Json target = b.value("target", Json::object());
            if (target.value("kind", std::string()) == "telemetry" && target.value("id", std::string()) == tid) {
                binding = b;
                break;
            }
        }
        Json select = Json::object();
        if (binding.is_object()) {
            for (const char* k : {"field", "path", "topic"}) {
                const Json s = binding.value("select", Json::object());
                if (s.contains(k) && s.at(k).is_string() && !s.at(k).get<std::string>().empty()) select[k] = s.at(k);
            }
        }
        telemetry.push_back(Json{{"id", tid}, {"key", channel_key(binding, tid)}, {"type", t.value("type", std::string("real"))}, {"select", select}});
    }
    Json events = Json::array();
    for (const Json& e : data.value("events", Json::array())) {
        const std::string pt = e.value("formal", Json::object()).value("pt", std::string());
        if (pt.empty()) continue;
        const std::string eid = e.value("id", std::string());
        events.push_back(Json{{"id", eid}, {"pt", pt}, {"select", Json::object()}});
        for (const Json& b : bindings) {
            const Json target = b.value("target", Json::object());
            if (target.value("kind", std::string()) != "event" || target.value("id", std::string()) != eid) continue;
            const Json s = b.value("select", Json::object());
            Json select = Json::object();
            for (const char* k : {"field", "path", "topic"}) {
                if (s.contains(k) && s.at(k).is_string() && !s.at(k).get<std::string>().empty()) select[k] = s.at(k);
            }
            if (select.empty()) continue;
            Json rule{{"id", eid}, {"pt", pt}, {"select", select}};
            if (s.contains("value") && !s.at("value").is_null() && !(s.at("value").is_string() && s.at("value").get<std::string>().empty())) {
                rule["value"] = s.at("value");
            }
            events.push_back(rule);
        }
    }
    return Json{{"telemetry", telemetry},
                {"events", events},
                {"eventField", connection.value("eventField", std::string("event"))},
                {"timestamp", connection.value("timestamp", Json{{"field", ""}, {"unit", "ms"}})}};
}

/// What the plant sends for each signal and event (the contract, as the connection page shows it).
Json contract_view(const Json& document, const Json& mapping) {
    const Json data = document.value("data", Json::object());
    std::map<std::string, Json> tdefs;
    for (const Json& t : data.value("telemetry", Json::array())) tdefs[t.value("id", std::string())] = t;
    std::map<std::string, Json> edefs;
    for (const Json& e : data.value("events", Json::array())) edefs[e.value("id", std::string())] = e;
    Json telemetry = Json::array();
    for (const Json& r : mapping.value("telemetry", Json::array())) {
        const Json def = tdefs[r.value("id", std::string())];
        const Json sel = r.value("select", Json::object());
        telemetry.push_back(Json{{"id", r.value("id", std::string())},
                                 {"label", def.value("label", r.value("id", std::string()))},
                                 {"type", r.value("type", std::string())},
                                 {"unit", def.value("unit", std::string())},
                                 {"field", sel.value("field", sel.value("path", std::string()))},
                                 {"topic", sel.value("topic", std::string())}});
    }
    std::map<std::string, Json> events;
    std::vector<std::string> order;
    for (const Json& r : mapping.value("events", Json::array())) {
        const std::string id = r.value("id", std::string());
        if (!events.count(id)) {
            order.push_back(id);
            const Json def = edefs[id];
            events[id] = Json{{"id", id}, {"label", def.value("label", id)}, {"pt", r.value("pt", std::string())}, {"codes", Json::array()},
                              {"signals", Json::array()}};
        }
        const Json sel = r.value("select", Json::object());
        if (r.contains("value")) events[id]["codes"].push_back(r.at("value"));
        else if (!sel.empty()) events[id]["signals"].push_back(sel);
    }
    Json ev = Json::array();
    for (const std::string& id : order) ev.push_back(events[id]);
    return Json{{"telemetry", telemetry}, {"events", ev}};
}

/// A plausible message in the plant's vocabulary, for the examples.
Json example_payload(const Json& document, const Json& mapping) {
    Json out = Json::object();
    std::map<std::string, Json> tdefs;
    for (const Json& t : document.value("data", Json::object()).value("telemetry", Json::array())) tdefs[t.value("id", std::string())] = t;
    int n = 0;
    for (const Json& r : mapping.value("telemetry", Json::array())) {
        const Json sel = r.value("select", Json::object());
        if (sel.contains("topic") || sel.contains("path")) continue;
        const std::string field = sel.value("field", r.value("id", std::string()));
        const Json def = tdefs[r.value("id", std::string())];
        const std::string type = r.value("type", std::string("real"));
        const Json range = def.value("range", Json::object());
        const double lo = number_or(range, "min", 0.0);
        const double hi = number_or(range, "max", lo + 100.0);
        if (type == "boolean") out[field] = true;
        else if (type == "integer") out[field] = static_cast<std::int64_t>((lo + hi) / 2);
        else if (type == "real") out[field] = std::round((lo + (hi - lo) * 0.42) * 10.0) / 10.0;
        else out[field] = "text";
        if (++n == 3) break;
    }
    const std::string event_field = mapping.value("eventField", std::string("event"));
    for (const Json& r : mapping.value("events", Json::array())) {
        if (r.contains("value")) {
            out[event_field] = r.at("value");
            break;
        }
    }
    if (!out.contains(event_field)) {
        for (const Json& r : mapping.value("events", Json::array())) {
            out[event_field] = r.value("id", std::string());
            break;
        }
    }
    // No timestamp: the example uses the arrival time, whatever the plant's clock has said before.
    return out;
}

}  // namespace

// ------------------------------------------------------------------------------ shared helpers

Result<Json> apply_plant_names(const Json& connectivity, const Json& names, const std::string& event_field) {
    if (!names.is_object()) return make_error(ErrorCode::InvalidArgument, "names must be {telemetry: {id: {field | topic}}, events: {id: [codes]}}");
    Json out = connectivity.is_object() ? connectivity : Json::object();
    Json bindings = out.value("bindings", Json::array());
    const Json sources = out.value("sources", Json::array());
    const std::string source = sources.empty() ? std::string("plant") : sources.front().value("id", std::string("plant"));
    if (sources.empty()) out["sources"] = Json::array({Json{{"id", "plant"}, {"kind", "rest"}, {"name", "Plant connection"}, {"config", Json::object()}}});
    for (const Json telemetry = names.value("telemetry", Json::object()); const auto& [tid, sel] : telemetry.items()) {  // the Json outlives the loop
        Json select = Json::object();
        const std::string field = sel.is_string() ? sel.get<std::string>() : sel.value("field", std::string());
        const std::string topic = sel.is_object() ? sel.value("topic", std::string()) : std::string();
        if (!topic.empty()) {
            if (!ingest::mqtt::valid_topic_filter(topic)) return make_error(ErrorCode::InvalidArgument, "telemetry '" + tid + "': invalid MQTT topic filter '" + topic + "'");
            select["topic"] = topic;
        }
        if (!field.empty()) select[field.rfind("$.", 0) == 0 ? "path" : "field"] = field;
        if (select.empty()) select["field"] = tid;  // back to the twin's own name
        bool found = false;
        for (Json& b : bindings) {
            const Json target = b.value("target", Json::object());
            if (target.value("kind", std::string()) == "telemetry" && target.value("id", std::string()) == tid) {
                b["select"] = select;
                found = true;
            }
        }
        if (!found) {
            bindings.push_back(Json{{"id", "b-" + tid}, {"target", {{"kind", "telemetry"}, {"id", tid}}}, {"source", source}, {"select", select}});
        }
    }
    for (const Json events = names.value("events", Json::object()); const auto& [eid, codes_in] : events.items()) {
        Json codes = codes_in.is_string() ? Json::array({codes_in}) : codes_in;
        if (!codes.is_array()) return make_error(ErrorCode::InvalidArgument, "event '" + eid + "': codes are a list of strings");
        // Replace the codes of this event (bindings on the event field); other signals are kept.
        Json kept = Json::array();
        for (const Json& b : bindings) {
            const Json target = b.value("target", Json::object());
            const Json sel = b.value("select", Json::object());
            const bool code_binding = target.value("kind", std::string()) == "event" && target.value("id", std::string()) == eid &&
                                      sel.value("field", std::string()) == event_field && sel.contains("value");
            if (!code_binding) kept.push_back(b);
        }
        bindings = kept;
        int n = 0;
        for (const Json& c : codes) {
            if (!c.is_string()) return make_error(ErrorCode::InvalidArgument, "event '" + eid + "': codes are strings");
            const std::string code = c.get<std::string>();
            if (code.empty()) continue;
            bindings.push_back(Json{{"id", "b-" + eid + "-" + std::to_string(++n)}, {"target", {{"kind", "event"}, {"id", eid}}}, {"source", source},
                                    {"select", {{"field", event_field}, {"value", code}}}});
        }
    }
    out["bindings"] = bindings;
    return out;
}

std::string channel_key(const Json& binding, const std::string& telemetry_id) {
    if (!binding.is_object()) return telemetry_id;
    const Json sel = binding.value("select", Json::object());
    std::string field = sel.value("field", sel.value("path", telemetry_id));
    if (field.rfind("$.", 0) == 0) field = field.substr(2);
    return field.empty() ? telemetry_id : field;
}

bool connection_active(const Json& connection) {
    if (!connection.is_object()) return false;
    for (const char* k : {"rest", "mqtt", "broker"}) {
        if (connection.value(k, Json::object()).value("enabled", false)) return true;
    }
    return false;
}

Json without_simulator_sources(const Json& connectivity) {
    Json out = connectivity.is_object() ? connectivity : Json::object();
    Json sources = Json::array();
    for (const Json& s : out.value("sources", Json::array())) {
        if (s.value("kind", std::string()) != "simulator") sources.push_back(s);
    }
    out["sources"] = sources;
    if (!out.contains("bindings")) out["bindings"] = Json::array();
    return out;
}

Result<Json> BlueprintService::Impl::prepare_connection(const Json& requested, const std::string& id, const Json& previous) {
    std::vector<Twin> twins;
    {
        auto l = core.lock();
        auto t = core.twins->twins();
        if (!t) return std::move(t).error();
        twins = t.value();
    }
    const StudioConfig& cfg = services.config();
    auto n = normalize_connection(requested, id, previous, plant_ports(twins, id), cfg.port_range_begin, cfg.port_range_end);
    if (!n) return n;
    const Json b = requested.value("broker", Json::object());
    if (b.contains("password") && b.at("password").is_string()) {
        if (auto st = set_broker_password(services, id, b.at("password").get<std::string>()); !st) return st.error();
    }
    n.value()["broker"]["passwordSet"] = broker_password(services, id).has_value();
    return n;
}

Result<std::optional<fs::path>> BlueprintService::Impl::write_ingest_config(const Twin& t, const Json& document) {
    const Json c = t.instance_config.value("connection", Json::object());
    if (!connection_active(c)) return std::optional<fs::path>{};
    const Json identity = document.value("identity", Json::object());
    const Json rest = c.value("rest", Json::object());
    const Json mqtt = c.value("mqtt", Json::object());
    Json broker = c.value("broker", Json::object());
    broker.erase("passwordSet");
    if (auto pw = broker_password(services, t.id)) broker["password"] = *pw;
    if (broker.value("username", std::string()).empty()) broker.erase("username");
    const Json cfg{{"format", ingest::kConfigFormat},
                   {"instance", t.id},
                   {"token", c.value("token", std::string())},
                   {"time", {{"ticksPerUnit", identity.value("ticksPerUnit", t.ticks_per_unit)}, {"unit", identity.value("timeUnit", std::string("s"))}}},
                   {"rest", {{"enabled", rest.value("enabled", false)}, {"host", "0.0.0.0"}, {"port", rest.value("port", Json(0))}}},
                   {"mqtt", {{"enabled", mqtt.value("enabled", false)}, {"host", "0.0.0.0"}, {"port", mqtt.value("port", Json(0))}}},
                   {"broker", broker},
                   {"mapping", ingest_mapping(document, t.instance_config.value("connectivity", Json::object()), c)},
                   {"clock", c.value("clock", Json::object())}};
    if (auto checked = ingest::config_from_json(cfg); !checked) {
        return Error(checked.error()).with("instance", t.id).with("hint", "fix the instance's plant connection");
    }
    const fs::path file = services.config().data_dir / "instances" / t.id / "ingest" / "config.json";
    if (auto st = write_private(file, cfg.dump(1)); !st) return st.error();
    return std::optional<fs::path>{file};
}

// ------------------------------------------------------------------------------ API

Result<Json> BlueprintService::connection(std::string_view id) {
    auto tw = services_.twin_record(id);
    if (!tw) return std::move(tw).error();
    const Twin t = tw.value();
    if (!t.blueprint_id || !t.blueprint_version) return make_error(ErrorCode::StateError, "this twin is not a Blueprint instance");
    auto ver = impl_->load(*t.blueprint_id, *t.blueprint_version);
    if (!ver) return std::move(ver).error();
    const Json& doc = ver.value().document;
    const Json c = t.instance_config.value("connection", Json::object());
    const Json mapping = ingest_mapping(doc, t.instance_config.value("connectivity", Json::object()), c);
    const std::vector<std::string> hosts = host_addresses();
    const std::string host = hosts.front();
    const std::string token = c.value("token", std::string());
    Json out{{"instance", t.id},
             {"configured", connection_active(c)},
             {"token", token.empty() ? Json(nullptr) : Json(token)},
             {"hosts", hosts},
             {"timestamp", c.value("timestamp", Json{{"field", ""}, {"unit", "ms"}})},
             {"eventField", c.value("eventField", std::string("event"))},
             {"clock", c.value("clock", Json{{"syncSeconds", 0}, {"graceSeconds", 0.5}})},
             {"contract", contract_view(doc, mapping)}};
    const Json rest = c.value("rest", Json::object());
    Json r{{"enabled", rest.value("enabled", false)}, {"port", rest.value("port", Json(nullptr))}};
    if (r.at("port").is_number_integer()) {
        const std::string port = std::to_string(r.at("port").get<int>());
        Json urls = Json::array();
        for (const std::string& h : hosts) urls.push_back("http://" + h + ":" + port + "/v1/messages");
        r["url"] = urls[0];
        r["urls"] = urls;
        r["header"] = "Authorization: Bearer " + token;
    }
    out["rest"] = r;
    const Json mq = c.value("mqtt", Json::object());
    Json m{{"enabled", mq.value("enabled", false)}, {"port", mq.value("port", Json(nullptr))}};
    if (m.at("port").is_number_integer()) {
        m["host"] = host;
        m["url"] = "mqtt://" + host + ":" + std::to_string(m.at("port").get<int>());
        m["username"] = t.id;
        m["topic"] = "twins/" + t.id + "/data";
    }
    out["mqtt"] = m;
    Json broker = c.value("broker", Json{{"enabled", false}});
    broker["passwordSet"] = broker_password(services_, t.id).has_value();
    out["broker"] = broker;

    const Json payload = example_payload(doc, mapping);
    const std::string body = payload.dump();
    Json ex{{"payload", payload}};
    if (r.contains("url")) {
        ex["curl"] = "curl -X POST " + r.at("url").get<std::string>() + " \\\n  -H " + sq("Authorization: Bearer " + token) +
                     " \\\n  -H 'Content-Type: application/json' \\\n  -d " + sq(body);
        ex["curlBatch"] = "curl -X POST " + r.at("url").get<std::string>() + " \\\n  -H " + sq("Authorization: Bearer " + token) +
                          " \\\n  --data-binary @recording.jsonl";
    }
    if (m.contains("host")) {
        ex["mosquittoPub"] = "mosquitto_pub -h " + host + " -p " + std::to_string(m.at("port").get<int>()) + " -u " + t.id + " -P " + token +
                             " \\\n  -t " + m.at("topic").get<std::string>() + " -m " + sq(body);
    }
    out["examples"] = ex;
    out["runtime"] = supervisor_ != nullptr ? supervisor_->status(t.id).value("state", std::string("not_started")) : std::string("not_started");
    out["status"] = supervisor_ != nullptr ? supervisor_->ingest_status(t.id) : Json(nullptr);
    return out;
}

Result<Json> BlueprintService::update_connection(std::string_view id, const Json& body, const Actor& actor) {
    auto tw = services_.twin_record(id);
    if (!tw) return std::move(tw).error();
    Twin t = tw.value();
    if (!t.blueprint_id || !t.blueprint_version) return make_error(ErrorCode::StateError, "this twin is not a Blueprint instance");
    auto ver = impl_->load(*t.blueprint_id, *t.blueprint_version);
    if (!ver) return std::move(ver).error();
    if (ver.value().document.value("identity", Json::object()).value("runtimeMode", std::string("monitor")) == "cosimulation" &&
        connection_active(Json{{"rest", body.value("rest", Json::object())}, {"mqtt", body.value("mqtt", Json::object())},
                               {"broker", body.value("broker", Json::object())}})) {
        return make_error(ErrorCode::InvalidArgument, "a co-simulation twin is driven by its simulator; plant connections are for monitoring twins");
    }
    const Json previous = t.instance_config.value("connection", Json::object());
    auto c = impl_->prepare_connection(body, t.id, previous);
    if (!c) return std::move(c).error();
    Json connectivity = t.instance_config.value("connectivity", Json::object());
    if (body.contains("bindings")) {
        if (!body.at("bindings").is_array()) return make_error(ErrorCode::InvalidArgument, "bindings must be an array");
        for (const Json& b : body.at("bindings")) {
            if (!b.is_object() || !b.value("target", Json()).is_object() || !b.value("select", Json()).is_object()) {
                return make_error(ErrorCode::InvalidArgument, "each binding needs a target {kind, id} and a select {field | path | topic}");
            }
        }
        connectivity["bindings"] = body.at("bindings");
    }
    if (connection_active(c.value())) connectivity = without_simulator_sources(connectivity);
    if (body.contains("names")) {
        auto named = apply_plant_names(connectivity, body.at("names"), c.value().value("eventField", std::string("event")));
        if (!named) return std::move(named).error();
        connectivity = named.value();
    }
    t.instance_config["connection"] = c.value();
    t.instance_config["connectivity"] = connectivity;
    if (auto st = services_.upsert_twin(t); !st) return st.error();
    if (body.contains("bindings") || body.contains("names")) {
        // The instance's channels listen to the selected plant fields: follow the new bindings.
        if (auto st = impl_->sync_channels(t, ver.value().document); !st) return st.error();
    }
    {
        auto l = impl_->core.lock();
        impl_->core.record("instance.connection", "success", t.id,
                           {{"rest", c.value()["rest"]}, {"mqtt", c.value()["mqtt"]}, {"broker", c.value()["broker"].value("enabled", false)}}, actor, "twin");
    }
    Json applied{{"restarted", false}};
    if (supervisor_ != nullptr && supervisor_->status(t.id).value("state", std::string()) == "running") {
        auto cfg = impl_->write_ingest_config(t, ver.value().document);
        if (!cfg) return std::move(cfg).error();
        auto r = supervisor_->restart_ingest(t.id, cfg.value());
        applied = r ? Json{{"restarted", cfg.value().has_value()}} : Json{{"restarted", false}, {"error", r.error().message}};
    }
    auto out = connection(t.id);
    if (out) out.value()["applied"] = applied;
    return out;
}

Result<Json> BlueprintService::rotate_connection_token(std::string_view id, const Actor& actor) {
    auto tw = services_.twin_record(id);
    if (!tw) return std::move(tw).error();
    Twin t = tw.value();
    Json c = t.instance_config.value("connection", Json::object());
    if (c.empty()) return make_error(ErrorCode::StateError, "this instance has no plant connection");
    c["token"] = random_token();
    t.instance_config["connection"] = c;
    if (auto st = services_.upsert_twin(t); !st) return st.error();
    {
        auto l = impl_->core.lock();
        impl_->core.record("instance.connection.token", "success", t.id, Json::object(), actor, "twin");
    }
    if (supervisor_ != nullptr && supervisor_->status(t.id).value("state", std::string()) == "running" && t.blueprint_id && t.blueprint_version) {
        auto ver = impl_->load(*t.blueprint_id, *t.blueprint_version);
        if (ver) {
            auto cfg = impl_->write_ingest_config(t, ver.value().document);
            if (cfg) (void)supervisor_->restart_ingest(t.id, cfg.value());
        }
    }
    return connection(t.id);
}

Status BlueprintService::Impl::sync_channels(const Twin& t, const Json& document) {
    auto all = services.telemetry_channels_all();
    if (!all) return all.error();
    const Json bindings = t.instance_config.value("connectivity", Json::object()).value("bindings", Json::array());
    const std::string root = t.asset_id.value_or(t.id);
    for (const Json& def : document.value("data", Json::object()).value("telemetry", Json::array())) {
        const std::string tid = def.value("id", std::string());
        Json binding = nullptr;
        for (const Json& b : bindings) {
            const Json target = b.value("target", Json::object());
            if (target.value("kind", std::string()) == "telemetry" && target.value("id", std::string()) == tid) {
                binding = b;
                break;
            }
        }
        for (TelemetryChannel ch : all.value()) {
            if (ch.id != root + "." + tid) continue;
            ch.source = runtime_source_prefix(t.id) + channel_key(binding, tid);
            Json transform = nullptr;
            const Json unit = binding.is_object() ? binding.value("unit", Json::object()) : Json::object();
            if (unit.contains("scale") || unit.contains("offset")) {
                transform = Json{{"scale", unit.value("scale", Json("1"))}, {"offset", unit.value("offset", Json("0"))}};
            }
            ch.presentation["transform"] = transform;
            if (auto st = services.upsert_channel(ch); !st) return st;
        }
    }
    return ok_status();
}

}  // namespace twin::studio
