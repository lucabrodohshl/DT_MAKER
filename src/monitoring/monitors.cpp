/**
 * @file monitors.cpp
 * @brief twin-monitors/1 codec and structural validation.
 */
#include "twin/monitoring/monitors.hpp"

#include <map>
#include <set>

#include "twin/monitoring/property.hpp"

namespace twin::monitoring {
namespace {

Error invalid(const std::string& path, std::string message) {
    return make_error(ErrorCode::ValidationError, std::move(message)).with("path", path);
}

/// Configuration values are strings, integers, booleans or arrays of those (canonical-safe).
bool canonical_value(const json::Json& v) {
    if (v.is_string() || v.is_boolean() || v.is_number_integer() || v.is_number_unsigned()) return true;
    if (v.is_array()) {
        for (const json::Json& e : v) {
            if (!canonical_value(e)) return false;
        }
        return true;
    }
    return false;
}

Result<std::string> text(const json::Json& o, const char* key, const std::string& path, bool required) {
    if (!o.contains(key)) {
        if (required) return invalid(path + "." + key, std::string("missing '") + key + "'");
        return std::string();
    }
    if (!o.at(key).is_string()) return invalid(path + "." + key, std::string("'") + key + "' must be a string");
    return o.at(key).get<std::string>();
}

Result<std::vector<std::string>> strings(const json::Json& o, const char* key, const std::string& path) {
    std::vector<std::string> out;
    if (!o.contains(key)) return out;
    if (!o.at(key).is_array()) return invalid(path + "." + key, std::string("'") + key + "' must be an array of strings");
    for (const json::Json& e : o.at(key)) {
        if (!e.is_string()) return invalid(path + "." + key, std::string("'") + key + "' must be an array of strings");
        out.push_back(e.get<std::string>());
    }
    return out;
}

const std::set<std::string>& severities() {
    static const std::set<std::string> s = {"info", "warning", "critical"};
    return s;
}

const std::map<std::string, std::set<std::string>>& allowed_parameters() {
    static const std::map<std::string, std::set<std::string>> a = {
        {"conformance", {"events", "unmatchedEvents"}},
        {"property", {"property"}},
        {"data_quality", {"field", "check", "maxAgeSeconds", "min", "max", "maxSilenceSeconds", "source"}}};
    return a;
}

class Validator {
public:
    explicit Validator(const MonitorsDocument& d) : d_(d) {}

    std::vector<Finding> run() {
        std::set<std::string> ids;
        for (std::size_t i = 0; i < d_.monitors.size(); ++i) {
            const MonitorSpec& m = d_.monitors[i];
            const std::string p = "monitors[" + std::to_string(i) + "]";
            if (!ids.insert(m.id).second) error("TWN001", p + ".id", "monitor id '" + m.id + "' is used twice");
            if (!severities().contains(m.severity)) {
                error("TWN003", p + ".severity", "severity must be info, warning or critical");
            }
            const auto kind = allowed_parameters().find(m.kind);
            if (kind == allowed_parameters().end()) {
                error("TWN002", p + ".kind", "unknown monitor kind '" + m.kind + "' (conformance, property, data_quality)");
                continue;
            }
            for (const auto& [key, unused] : m.config.items()) {
                if (!kind->second.contains(key)) {
                    error("TWN004", p + "." + key, "'" + key + "' is not a parameter of " + m.kind + " monitors");
                }
            }
            if (m.kind == "conformance") conformance(m, p);
            if (m.kind == "property") property(m, p);
            if (m.kind == "data_quality") data_quality(m, p);
        }
        alerts(ids);
        requirements(ids);
        return std::move(out_);
    }

private:
    void error(std::string code, std::string path, std::string message) {
        out_.push_back(Finding{"error", std::move(code), std::move(message), std::move(path)});
    }

    void conformance(const MonitorSpec& m, const std::string& p) {
        if (m.config.contains("events")) {
            const json::Json& e = m.config.at("events");
            const bool all = e.is_string() && e.get<std::string>() == "all";
            bool labels = e.is_array();
            if (labels) {
                for (const json::Json& l : e) {
                    labels = labels && l.is_string() && l.get<std::string>().size() > 1 && l.get<std::string>().back() == '!';
                }
            }
            if (!all && !labels) error("TWN010", p + ".events", "events must be \"all\" or a list of PT labels such as \"start!\"");
        }
        if (m.config.contains("unmatchedEvents")) {
            const json::Json& u = m.config.at("unmatchedEvents");
            if (!u.is_string() || (u.get<std::string>() != "reject" && u.get<std::string>() != "record")) {
                error("TWN010", p + ".unmatchedEvents", "unmatchedEvents must be \"reject\" or \"record\"");
            }
        }
    }

    void property(const MonitorSpec& m, const std::string& p) {
        if (!m.config.contains("property") || !m.config.at("property").is_string()) {
            error("TWN011", p + ".property", "a property monitor needs 'property'");
            return;
        }
        Result<Property> parsed = parse_property(m.config.at("property").get<std::string>());
        if (!parsed) {
            error("TWN011", p + ".property",
                  "the property does not parse: " + parsed.error().message + " (column " +
                      std::string(parsed.error().context_value("column")) + ")");
        }
    }

    static bool number_like(const json::Json& v) {
        if (v.is_number_integer() || v.is_number_unsigned()) return true;
        if (!v.is_string()) return false;
        try {
            std::size_t used = 0;
            (void)std::stod(v.get<std::string>(), &used);
            return used == v.get<std::string>().size();
        } catch (const std::exception&) {
            return false;
        }
    }

    void data_quality(const MonitorSpec& m, const std::string& p) {
        static const std::set<std::string> checks = {"stale", "missing", "invalid_type", "out_of_range",
                                                     "clock_regression", "duplicate", "disconnected"};
        const json::Json& c = m.config;
        if (!c.contains("field") || !c.at("field").is_string() || c.at("field").get<std::string>().empty()) {
            error("TWN020", p + ".field", "a data-quality monitor needs the telemetry 'field' it watches");
        }
        const std::string check = c.contains("check") && c.at("check").is_string() ? c.at("check").get<std::string>() : "";
        if (!checks.contains(check)) {
            error("TWN020", p + ".check",
                  "check must be one of stale, missing, invalid_type, out_of_range, clock_regression, duplicate, "
                  "disconnected");
            return;
        }
        const auto positive = [&](const char* key, bool required) {
            if (!c.contains(key)) {
                if (required) error("TWN020", p + "." + key, std::string("'") + key + "' (seconds) is required");
                return;
            }
            if (!(c.at(key).is_number_integer() || c.at(key).is_number_unsigned()) || c.at(key).get<std::int64_t>() <= 0) {
                error("TWN020", p + "." + key, std::string("'") + key + "' must be a positive integer");
            }
        };
        if (check == "stale") positive("maxAgeSeconds", true);
        if (check == "disconnected") positive("maxSilenceSeconds", false);
        if (check == "out_of_range") {
            if (!c.contains("min") && !c.contains("max")) error("TWN020", p, "out_of_range needs 'min' and/or 'max'");
            for (const char* key : {"min", "max"}) {
                if (c.contains(key) && !number_like(c.at(key))) {
                    error("TWN020", p + "." + key, std::string("'") + key + "' must be an integer or a decimal string");
                }
            }
        }
    }

    void alerts(const std::set<std::string>& monitors) {
        static const std::set<std::string> on = {"violated", "inconclusive", "unknown", "finding", "satisfied"};
        std::set<std::string> ids;
        for (std::size_t i = 0; i < d_.alerts.size(); ++i) {
            const AlertPolicy& a = d_.alerts[i];
            const std::string p = "alerts[" + std::to_string(i) + "]";
            if (!ids.insert(a.id).second) error("TWN001", p + ".id", "alert id '" + a.id + "' is used twice");
            if (!monitors.contains(a.monitor)) error("TWN030", p + ".monitor", "unknown monitor '" + a.monitor + "'");
            if (!on.contains(a.on)) error("TWN030", p + ".on", "on must be violated, inconclusive, unknown, finding or satisfied");
            if (!severities().contains(a.severity)) error("TWN030", p + ".severity", "severity must be info, warning or critical");
        }
    }

    void requirements(const std::set<std::string>& monitors) {
        static const std::set<std::string> categories = {"safety", "mission", "performance", "timing", "operational"};
        std::set<std::string> ids;
        for (std::size_t i = 0; i < d_.requirements.size(); ++i) {
            const Requirement& r = d_.requirements[i];
            const std::string p = "requirements[" + std::to_string(i) + "]";
            if (!ids.insert(r.id).second) error("TWN001", p + ".id", "requirement id '" + r.id + "' is used twice");
            if (!categories.contains(r.category)) error("TWN040", p + ".category", "category must be safety, mission, performance, timing or operational");
            if (!severities().contains(r.severity)) error("TWN040", p + ".severity", "severity must be info, warning or critical");
            for (const std::string& m : r.monitors) {
                if (!monitors.contains(m)) error("TWN040", p + ".monitors", "unknown monitor '" + m + "'");
            }
            if (!r.formal.empty() && !parse_property(r.formal)) {
                error("TWN040", p + ".formal", "the formal expression does not parse");
            }
        }
    }

    const MonitorsDocument& d_;
    std::vector<Finding> out_;
};

Result<MonitorSpec> decode_monitor(const json::Json& j, const std::string& p) {
    if (!j.is_object()) return invalid(p, "a monitor is an object");
    MonitorSpec m;
    for (auto [key, field, required] : {std::tuple{"id", &m.id, true}, std::tuple{"kind", &m.kind, true},
                                        std::tuple{"name", &m.name, true}, std::tuple{"severity", &m.severity, true},
                                        std::tuple{"description", &m.description, false},
                                        std::tuple{"requirement", &m.requirement, false}}) {
        Result<std::string> v = text(j, key, p, required);
        if (!v) return std::move(v).error();
        *field = std::move(v).value();
    }
    static const std::set<std::string> fixed = {"id", "kind", "name", "severity", "description", "requirement"};
    for (const auto& [key, value] : j.items()) {
        if (fixed.contains(key)) continue;
        if (!canonical_value(value)) {
            return invalid(p + "." + key, "parameter values are strings, integers, booleans or lists of them "
                                          "(write decimals as strings, e.g. \"7.1\")");
        }
        m.config[key] = value;
    }
    return m;
}

}  // namespace

Result<MonitorsDocument> monitors_from_json(const json::Json& d) {
    if (Status s = json::expect_keys(d, {"format"}, {"requirements", "monitors", "alerts"}); !s) return s.error();
    if (!d.at("format").is_string() || d.at("format").get<std::string>() != kMonitorsFormat) {
        return invalid("format", "unsupported monitor document format (expected twin-monitors/1)");
    }
    MonitorsDocument out;
    for (const char* key : {"requirements", "monitors", "alerts"}) {
        if (d.contains(key) && !d.at(key).is_array()) return invalid(key, std::string("'") + key + "' must be an array");
    }
    if (d.contains("monitors")) {
        for (std::size_t i = 0; i < d.at("monitors").size(); ++i) {
            Result<MonitorSpec> m = decode_monitor(d.at("monitors")[i], "monitors[" + std::to_string(i) + "]");
            if (!m) return std::move(m).error();
            out.monitors.push_back(std::move(m).value());
        }
    }
    if (d.contains("requirements")) {
        for (std::size_t i = 0; i < d.at("requirements").size(); ++i) {
            const json::Json& j = d.at("requirements")[i];
            const std::string p = "requirements[" + std::to_string(i) + "]";
            Requirement r;
            for (auto [key, field, required] :
                 {std::tuple{"id", &r.id, true}, std::tuple{"title", &r.title, true}, std::tuple{"category", &r.category, true},
                  std::tuple{"severity", &r.severity, true}, std::tuple{"description", &r.description, false},
                  std::tuple{"formal", &r.formal, false}}) {
                Result<std::string> v = text(j, key, p, required);
                if (!v) return std::move(v).error();
                *field = std::move(v).value();
            }
            Result<std::vector<std::string>> ms = strings(j, "monitors", p);
            if (!ms) return std::move(ms).error();
            r.monitors = std::move(ms).value();
            out.requirements.push_back(std::move(r));
        }
    }
    if (d.contains("alerts")) {
        for (std::size_t i = 0; i < d.at("alerts").size(); ++i) {
            const json::Json& j = d.at("alerts")[i];
            const std::string p = "alerts[" + std::to_string(i) + "]";
            AlertPolicy a;
            for (auto [key, field] : {std::pair{"id", &a.id}, std::pair{"monitor", &a.monitor}, std::pair{"on", &a.on},
                                      std::pair{"severity", &a.severity}, std::pair{"message", &a.message}}) {
                Result<std::string> v = text(j, key, p, true);
                if (!v) return std::move(v).error();
                *field = std::move(v).value();
            }
            out.alerts.push_back(std::move(a));
        }
    }
    return out;
}

json::Json to_json(const MonitorsDocument& d) {
    json::Json requirements = json::Json::array();
    for (const Requirement& r : d.requirements) {
        json::Json j{{"id", r.id}, {"title", r.title}, {"category", r.category}, {"severity", r.severity},
                     {"description", r.description}, {"monitors", r.monitors}};
        if (!r.formal.empty()) j["formal"] = r.formal;
        requirements.push_back(std::move(j));
    }
    json::Json monitors = json::Json::array();
    for (const MonitorSpec& m : d.monitors) {
        json::Json j = m.config;
        j["id"] = m.id;
        j["kind"] = m.kind;
        j["name"] = m.name;
        j["severity"] = m.severity;
        if (!m.description.empty()) j["description"] = m.description;
        if (!m.requirement.empty()) j["requirement"] = m.requirement;
        monitors.push_back(std::move(j));
    }
    json::Json alerts = json::Json::array();
    for (const AlertPolicy& a : d.alerts) {
        alerts.push_back({{"id", a.id}, {"monitor", a.monitor}, {"on", a.on}, {"severity", a.severity}, {"message", a.message}});
    }
    return json::Json{{"format", std::string(kMonitorsFormat)},
                      {"requirements", requirements},
                      {"monitors", monitors},
                      {"alerts", alerts}};
}

std::vector<Finding> validate_document(const MonitorsDocument& document) { return Validator(document).run(); }

json::Json to_json(const std::vector<Finding>& findings) {
    json::Json out = json::Json::array();
    for (const Finding& f : findings) {
        out.push_back({{"severity", f.severity}, {"code", f.code}, {"message", f.message}, {"path", f.path}});
    }
    return out;
}

const MonitorSpec* find_monitor(const MonitorsDocument& document, std::string_view id) {
    for (const MonitorSpec& m : document.monitors) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

}  // namespace twin::monitoring
