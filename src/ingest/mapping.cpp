/**
 * @file mapping.cpp
 * @brief Translation of plant messages into the twin's data contract (see mapping.hpp).
 */
#include "twin/ingest/mapping.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "twin/ingest/mqtt.hpp"

namespace twin::ingest {

using json::Json;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// A whole-string decimal number ("71.5", "-3", "1e3"); none for anything else.
std::optional<double> number_text(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

/// Text of a scalar for comparisons with an event value ("START", "3", "true").
std::string text_of(const Json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<std::int64_t>());
    return v.dump();
}

bool truthy(const Json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0.0;
    if (v.is_string()) {
        const std::string s = lower(v.get<std::string>());
        return !s.empty() && s != "false" && s != "0" && s != "off";
    }
    return false;
}

Selector selector_from_json(const Json& j) {
    Selector s;
    if (!j.is_object()) return s;
    s.field = j.value("field", std::string());
    s.path = j.value("path", std::string());
    s.topic = j.value("topic", std::string());
    return s;
}

Json selector_to_json(const Selector& s) {
    Json j = Json::object();
    if (!s.field.empty()) j["field"] = s.field;
    if (!s.path.empty()) j["path"] = s.path;
    if (!s.topic.empty()) j["topic"] = s.topic;
    return j;
}

bool selects_something(const Selector& s) { return !s.field.empty() || !s.path.empty() || !s.topic.empty(); }

/// The value a selector designates in @p message received on @p topic, or nullptr.
const Json* pick(const Selector& s, const Json& message, std::string_view topic) {
    if (!s.topic.empty()) {
        if (topic.empty() || !mqtt::topic_matches(s.topic, topic)) return nullptr;
        if (!s.path.empty()) return message.is_object() || message.is_array() ? select_path(message, s.path) : nullptr;
        if (!s.field.empty()) {
            if (!message.is_object()) return nullptr;
            auto it = message.find(s.field);
            return it == message.end() ? nullptr : &*it;
        }
        if (message.is_object()) {  // {"value": 71.5, ...} on a per-tag topic
            auto it = message.find("value");
            return it == message.end() ? nullptr : &*it;
        }
        return &message;  // the payload itself ("71.5")
    }
    if (!message.is_object()) return nullptr;
    if (!s.field.empty()) {
        auto it = message.find(s.field);
        return it == message.end() ? nullptr : &*it;
    }
    if (!s.path.empty()) return select_path(message, s.path);
    return nullptr;
}

/// Convert @p v to the telemetry @p type; an error text when it does not convert.
Result<Json> typed(const Json& v, const std::string& type) {
    auto bad = [&](const std::string& expected) {
        return make_error(ErrorCode::InvalidArgument, v.dump() + " is not " + expected);
    };
    if (type == "real" || type == "integer") {
        std::optional<double> x;
        if (v.is_number()) x = v.get<double>();
        else if (v.is_string()) x = number_text(v.get<std::string>());
        if (!x) return bad("a number");
        if (type == "integer") {
            if (std::floor(*x) != *x) return bad("an integer");
            return Json(static_cast<std::int64_t>(*x));
        }
        if (v.is_number_integer()) return v;
        return Json(*x);
    }
    if (type == "boolean") {
        if (v.is_boolean()) return v;
        if (v.is_number()) return Json(v.get<double>() != 0.0);
        if (v.is_string()) {
            const std::string s = lower(v.get<std::string>());
            if (s == "true" || s == "1") return Json(true);
            if (s == "false" || s == "0") return Json(false);
        }
        return bad("a boolean");
    }
    if (v.is_string()) return v;  // string, enum
    if (v.is_number() || v.is_boolean()) return Json(text_of(v));
    return bad("a text value");
}

/// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm; no time zone).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

/// "YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]" (a space may replace T; no offset means UTC).
std::optional<double> parse_iso8601(const std::string& s) {
    auto num = [&](std::size_t at, std::size_t len) -> std::optional<int> {
        if (at + len > s.size()) return std::nullopt;
        int v = 0;
        const auto [p, ec] = std::from_chars(s.data() + at, s.data() + at + len, v);
        if (ec != std::errc() || p != s.data() + at + len) return std::nullopt;
        return v;
    };
    if (s.size() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't' && s[10] != ' ') || s[13] != ':' || s[16] != ':') {
        return std::nullopt;
    }
    const auto y = num(0, 4);
    const auto mo = num(5, 2);
    const auto d = num(8, 2);
    const auto h = num(11, 2);
    const auto mi = num(14, 2);
    const auto se = num(17, 2);
    if (!y || !mo || !d || !h || !mi || !se || *mo < 1 || *mo > 12 || *d < 1 || *d > 31 || *h > 23 || *mi > 59 || *se > 60) {
        return std::nullopt;
    }
    std::size_t i = 19;
    double fraction = 0;
    if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
        double scale = 0.1;
        ++i;
        const std::size_t first = i;
        for (; i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0; ++i, scale /= 10) fraction += (s[i] - '0') * scale;
        if (i == first) return std::nullopt;
    }
    int offset = 0;  // seconds east of UTC
    if (i < s.size()) {
        if ((s[i] == 'Z' || s[i] == 'z') && i + 1 == s.size()) {
            ++i;
        } else if ((s[i] == '+' || s[i] == '-') && (s.size() == i + 6 || s.size() == i + 5)) {
            const bool colon = s.size() == i + 6;
            const auto oh = num(i + 1, 2);
            const auto om = num(i + (colon ? 4 : 3), 2);
            if (!oh || !om || (colon && s[i + 3] != ':')) return std::nullopt;
            offset = (*oh * 3600 + *om * 60) * (s[i] == '-' ? -1 : 1);
            i = s.size();
        } else {
            return std::nullopt;
        }
    }
    const std::int64_t days = days_from_civil(*y, static_cast<unsigned>(*mo), static_cast<unsigned>(*d));
    const std::int64_t secs = days * 86400 + *h * 3600 + *mi * 60 + *se - offset;
    return static_cast<double>(secs) + fraction;
}

}  // namespace

// ------------------------------------------------------------------------------ documents

Result<Mapping> mapping_from_json(const Json& doc) {
    if (!doc.is_object()) return make_error(ErrorCode::InvalidArgument, "the mapping must be a JSON object");
    Mapping m;
    for (const Json& t : doc.value("telemetry", Json::array())) {
        TelemetryRule r;
        r.id = t.value("id", std::string());
        r.key = t.value("key", r.id);
        r.type = t.value("type", std::string("real"));
        r.select = selector_from_json(t.value("select", Json::object()));
        if (r.id.empty() || r.key.empty()) return make_error(ErrorCode::InvalidArgument, "a telemetry rule needs an id and a key");
        if (!r.select.topic.empty() && !mqtt::valid_topic_filter(r.select.topic)) {
            return make_error(ErrorCode::InvalidArgument, "invalid MQTT topic filter '" + r.select.topic + "'").with("telemetry", r.id);
        }
        m.telemetry.push_back(std::move(r));
    }
    for (const Json& e : doc.value("events", Json::array())) {
        EventRule r;
        r.id = e.value("id", std::string());
        r.pt = e.value("pt", std::string());
        r.select = selector_from_json(e.value("select", Json::object()));
        if (e.contains("value") && !e.at("value").is_null()) r.value = text_of(e.at("value"));
        if (r.id.empty() || r.pt.empty()) return make_error(ErrorCode::InvalidArgument, "an event rule needs an id and a PT label");
        if (!r.select.topic.empty() && !mqtt::valid_topic_filter(r.select.topic)) {
            return make_error(ErrorCode::InvalidArgument, "invalid MQTT topic filter '" + r.select.topic + "'").with("event", r.id);
        }
        m.events.push_back(std::move(r));
    }
    m.event_field = doc.value("eventField", std::string("event"));
    const Json ts = doc.value("timestamp", Json::object());
    m.timestamp.field = ts.value("field", std::string());
    m.timestamp.unit = ts.value("unit", std::string("ms"));
    if (m.timestamp.unit != "ms" && m.timestamp.unit != "s" && m.timestamp.unit != "iso8601") {
        return make_error(ErrorCode::InvalidArgument, "timestamp unit is ms, s or iso8601").with("unit", m.timestamp.unit);
    }
    return m;
}

Json to_json(const Mapping& m) {
    Json telemetry = Json::array();
    for (const TelemetryRule& r : m.telemetry) {
        telemetry.push_back(Json{{"id", r.id}, {"key", r.key}, {"type", r.type}, {"select", selector_to_json(r.select)}});
    }
    Json events = Json::array();
    for (const EventRule& r : m.events) {
        Json e{{"id", r.id}, {"pt", r.pt}, {"select", selector_to_json(r.select)}};
        if (r.value) e["value"] = *r.value;
        events.push_back(e);
    }
    return Json{{"telemetry", telemetry},
                {"events", events},
                {"eventField", m.event_field},
                {"timestamp", {{"field", m.timestamp.field}, {"unit", m.timestamp.unit}}}};
}

// ------------------------------------------------------------------------------ values

Json decode_payload(std::string_view payload) {
    std::string text(payload);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.pop_back();
    auto j = json::parse(text);
    if (j) return std::move(j).value();
    return Json(text);
}

const Json* select_path(const Json& doc, std::string_view path) {
    if (path == "$") return &doc;
    if (path.rfind("$.", 0) == 0) path.remove_prefix(2);
    const Json* cur = &doc;
    std::size_t i = 0;
    while (i < path.size()) {
        std::size_t j = i;
        while (j < path.size() && path[j] != '.' && path[j] != '[') ++j;
        const std::string key(path.substr(i, j - i));
        if (!key.empty()) {
            if (!cur->is_object()) return nullptr;
            auto it = cur->find(key);
            if (it == cur->end()) return nullptr;
            cur = &*it;
        }
        while (j < path.size() && path[j] == '[') {
            const std::size_t close = path.find(']', j);
            if (close == std::string_view::npos || !cur->is_array()) return nullptr;
            std::size_t idx = 0;
            const auto [p, ec] = std::from_chars(path.data() + j + 1, path.data() + close, idx);
            if (ec != std::errc() || p != path.data() + close || idx >= cur->size()) return nullptr;
            cur = &(*cur)[idx];
            j = close + 1;
        }
        i = j < path.size() && path[j] == '.' ? j + 1 : j;
    }
    return cur;
}

std::optional<double> timestamp_seconds(const Json& value, std::string_view unit) {
    if (unit == "iso8601") return value.is_string() ? parse_iso8601(value.get<std::string>()) : std::nullopt;
    std::optional<double> x;
    if (value.is_number()) x = value.get<double>();
    else if (value.is_string()) x = number_text(value.get<std::string>());
    if (!x) return std::nullopt;
    return unit == "ms" ? *x / 1000.0 : *x;
}

// ------------------------------------------------------------------------------ translation

Translation translate(const Mapping& m, const Json& message, std::string_view topic) {
    Translation out;
    const bool object = message.is_object();

    for (const TelemetryRule& r : m.telemetry) {
        const Json* v = selects_something(r.select) ? pick(r.select, message, topic) : nullptr;
        if (v == nullptr && object) {  // the twin's own vocabulary: the telemetry id as a field
            auto it = message.find(r.id);
            if (it != message.end()) v = &*it;
        }
        if (v == nullptr || v->is_null()) continue;
        auto t = typed(*v, r.type);
        if (!t) {
            out.problems.push_back("telemetry '" + r.id + "': " + t.error().message);
            continue;
        }
        out.telemetry[r.key] = std::move(t).value();
    }

    // Events signalled through the event field, in the order the message lists them.
    auto on_event_field = [&](const EventRule& r) {
        const bool here = r.select.topic.empty() || (!topic.empty() && mqtt::topic_matches(r.select.topic, topic));
        const std::string& f = r.select.field.empty() ? r.select.path : r.select.field;
        return here && r.value && (f == m.event_field || f == "$." + m.event_field);
    };
    if (object && !m.event_field.empty()) {
        if (auto it = message.find(m.event_field); it != message.end() && !it->is_null()) {
            std::vector<Json> codes = it->is_array() ? it->get<std::vector<Json>>() : std::vector<Json>{*it};
            for (const Json& c : codes) {
                if (!c.is_string() && !c.is_number()) {
                    out.problems.push_back("event field '" + m.event_field + "': " + c.dump() + " is not an event code");
                    continue;
                }
                const std::string code = text_of(c);
                const EventRule* hit = nullptr;
                for (const EventRule& r : m.events) {
                    if (on_event_field(r) && *r.value == code) hit = hit == nullptr ? &r : hit;
                }
                for (const EventRule& r : m.events) {  // the twin's own vocabulary: event id or PT label
                    if (hit == nullptr && (r.id == code || r.pt == code)) hit = &r;
                }
                if (hit != nullptr) out.events.push_back(hit->pt);
                else out.unmapped.push_back(code);
            }
        }
    }
    // Events signalled by other fields (flags) or by topics. A rule on the event field itself only
    // ever matches a code (above).
    for (const EventRule& r : m.events) {
        const std::string& f = r.select.field.empty() ? r.select.path : r.select.field;
        if (!selects_something(r.select) || (r.select.topic.empty() && (f == m.event_field || f == "$." + m.event_field)) ||
            on_event_field(r)) {
            continue;
        }
        const Json* v = pick(r.select, message, topic);
        if (v == nullptr) continue;
        const bool raised = r.value ? (v->is_array() ? std::any_of(v->begin(), v->end(), [&](const Json& x) { return text_of(x) == *r.value; })
                                                     : text_of(*v) == *r.value)
                                    : truthy(*v);
        if (raised) out.events.push_back(r.pt);
    }

    if (!m.timestamp.field.empty() && object) {
        const Json* t = select_path(message, m.timestamp.field);
        if (t != nullptr) {
            out.source_seconds = timestamp_seconds(*t, m.timestamp.unit);
            if (!out.source_seconds) {
                out.problems.push_back("timestamp '" + m.timestamp.field + "': " + t->dump() + " is not a time in " + m.timestamp.unit +
                                       "; the arrival time is used");
            }
        }
    }
    return out;
}

}  // namespace twin::ingest
