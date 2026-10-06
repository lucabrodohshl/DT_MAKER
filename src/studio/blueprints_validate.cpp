/**
 * @file blueprints_validate.cpp
 * @brief BlueprintService: section validation, overview and release gate, impact, world raster.
 *
 * Section findings are structural (VALID / not valid). Formal states (VERIFIED) come only from
 * evidence records produced by the formal tools; the gate reads them for exactly the pinned
 * artefacts, so stale evidence can never look current.
 */
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "blueprints_impl.hpp"
#include "twin/authoring/model.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/monitoring/monitors.hpp"
#include "twin/ptfeed/feed.hpp"
#include "twin/scene/robot_sim.hpp"
#include "twin/scene/world.hpp"

namespace twin::studio {

using json::Json;
using namespace twin::platform;

namespace {

bool identifier(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    if (std::isalpha(static_cast<unsigned char>(s.front())) == 0 && s.front() != '_') return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.'; });
}

bool decimal_text(const Json& j) {
    if (j.is_number_integer()) return true;
    if (!j.is_string()) return false;
    const std::string s = j.get<std::string>();
    if (s.empty()) return false;
    char* end = nullptr;
    (void)std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

double decimal_value(const Json& j) {
    if (j.is_number_integer()) return static_cast<double>(j.get<std::int64_t>());
    return std::strtod(j.get<std::string>().c_str(), nullptr);
}

struct Collector {
    std::vector<SectionFinding>& out;
    std::string section;
    void error(const std::string& code, const std::string& message, const std::string& target = {}, const std::string& path = {}) {
        out.push_back(SectionFinding{section, "error", code, message, target, path});
    }
    void warning(const std::string& code, const std::string& message, const std::string& target = {}, const std::string& path = {}) {
        out.push_back(SectionFinding{section, "warning", code, message, target, path});
    }
};

/// Labels and locations of a pinned model (empty when absent or unreadable).
struct ModelFacts {
    bool present{false};
    std::set<std::string> locations;
    std::set<std::string> labels;  // "a!", "a?"
    std::size_t internal_edges{0};
};

ModelFacts facts_of(Services::Impl& core, const BlueprintVersion& v, const std::string& role) {
    ModelFacts f;
    auto p = v.pins.find(role);
    if (p == v.pins.end()) return f;
    auto ref = parse_ref(p->second);
    if (!ref) return f;
    std::string content;
    {
        auto l = core.lock();
        auto c = core.artifacts->content(ref.value());
        if (!c) return f;
        content = c.value();
    }
    if (authoring::content_format(content) != "twin-ta/1") {
        // Legacy UPPAAL content: render through the toolchain reader is costly; report as present only.
        f.present = true;
        return f;
    }
    auto j = json::parse(content);
    if (!j) return f;
    auto m = authoring::model_from_json(j.value());
    if (!m) return f;
    f.present = true;
    for (const auto& l : m.value().locations) f.locations.insert(l.name);
    for (const auto& e : m.value().edges) {
        if (e.sync) f.labels.insert(authoring::edge_label(e));
        else ++f.internal_edges;
    }
    return f;
}

void validate_identity(const Json& s, const Json& doc, Collector c) {
    if (s.value("name", std::string()).empty()) c.error("TWB001", "Give the Blueprint a name.", "", "name");
    if (!identifier(s.value("modelId", std::string()))) {
        c.error("TWB002", "The model id must be an identifier (letters, digits, '_', '-'); it is stamped into the IR and packages.", "", "modelId");
    }
    const Json t = s.value("ticksPerUnit", Json());
    if (!t.is_number_integer() || t.get<std::int64_t>() <= 0) {
        c.error("TWB003", "The logical-time resolution (ticks per model time unit) must be a positive integer.", "", "ticksPerUnit");
    }
    const std::string mode = s.value("runtimeMode", std::string("monitor"));
    if (mode != "monitor" && mode != "cosimulation") {
        c.error("TWB004", "The runtime mode is 'monitor' (an external or simulated source pushes events) or 'cosimulation'.", "", "runtimeMode");
    }
    if (mode == "cosimulation" && doc.value("simulation", Json::object()).value("kind", std::string("none")) != "mobile-robot") {
        c.error("TWB005", "Co-simulation needs a mobile-robot simulation (Test → Preview); use monitor mode otherwise.", "", "runtimeMode");
    }
}

void validate_structure(const Json& s, Collector c, std::set<std::string>& asset_ids) {
    std::set<std::string> types;
    for (const Json& t : s.value("assetTypes", Json::array())) {
        const std::string id = t.value("id", std::string());
        if (id.empty() || !types.insert(id).second) c.error("TWB016", "Asset type ids must be unique and non-empty.", id, "assetTypes");
        std::set<std::string> keys;
        for (const Json& p : t.value("properties", Json::array())) {
            if (!keys.insert(p.value("key", std::string())).second) {
                c.warning("TWB016", "Asset type '" + id + "' defines property '" + p.value("key", std::string()) + "' twice.", id, "assetTypes");
            }
        }
    }
    std::map<std::string, std::string> parent;
    const Json assets = s.value("assets", Json::array());
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const Json& a = assets[i];
        const std::string id = a.value("id", std::string());
        const std::string path = "assets[" + std::to_string(i) + "]";
        if (!identifier(id)) c.error("TWB010", "Asset ids are identifiers (letters, digits, '-', '_').", id, path + ".id");
        if (!asset_ids.insert(id).second) c.error("TWB010", "Duplicate asset id '" + id + "'.", id, path + ".id");
        if (a.value("name", std::string()).empty()) c.warning("TWB018", "Asset '" + id + "' has no display name.", id, path + ".name");
        const std::string type = a.value("type", std::string());
        if (type.empty()) c.error("TWB013", "Asset '" + id + "' needs a type.", id, path + ".type");
        else if (!types.empty() && !types.count(type)) {
            c.warning("TWB013", "Asset '" + id + "' uses type '" + type + "', which is not defined under Asset types.", id, path + ".type");
        }
        const std::string scope = a.value("scope", std::string("instance"));
        if (scope != "instance" && scope != "context") {
            c.error("TWB019", "Asset scope is 'instance' (created per twin instance) or 'context' (shared, e.g. a building).", id, path + ".scope");
        }
        if (a.contains("parent") && a.at("parent").is_string() && !a.at("parent").get<std::string>().empty()) {
            parent[id] = a.at("parent").get<std::string>();
        }
    }
    for (const auto& [child, p] : parent) {
        if (!asset_ids.count(p)) c.error("TWB011", "Asset '" + child + "' has unknown parent '" + p + "'.", child, "parent");
    }
    for (const auto& [start, unused] : parent) {
        std::set<std::string> seen{start};
        std::string cur = start;
        while (parent.count(cur)) {
            cur = parent.at(cur);
            if (!seen.insert(cur).second) {
                c.error("TWB012", "The asset hierarchy has a cycle through '" + start + "'.", start, "parent");
                break;
            }
        }
    }
    for (const Json& r : s.value("relationships", Json::array())) {
        const std::string src = r.value("source", std::string());
        const std::string dst = r.value("target", std::string());
        if (!asset_ids.count(src) || !asset_ids.count(dst)) {
            c.error("TWB014", "Relationship '" + r.value("type", std::string()) + "' connects unknown assets (" + src + " → " + dst + ").",
                    r.value("id", std::string()), "relationships");
        }
        if (r.value("type", std::string()).empty()) c.error("TWB014", "A relationship needs a type (e.g. locatedIn, feeds).", r.value("id", std::string()), "relationships");
    }
    const Json root = s.value("root", Json());
    if (assets.empty()) {
        c.warning("TWB017", "No assets yet: add the asset this twin represents (Build → Structure).");
    } else if (!root.is_string() || !asset_ids.count(root.get<std::string>())) {
        c.error("TWB015", "Choose the root asset: the asset this twin is the twin of.", "", "root");
    } else {
        const auto it = std::find_if(assets.begin(), assets.end(), [&](const Json& a) { return a.value("id", std::string()) == root.get<std::string>(); });
        if (it != assets.end() && it->value("scope", std::string("instance")) != "instance") {
            c.error("TWB015", "The root asset must be instance-scoped: every twin instance has its own.", root.get<std::string>(), "root");
        }
    }
}

void validate_world(const Json& w, const Json& simulation, const std::set<std::string>& assets, Collector c) {
    auto world = scene::world_from_json(w);
    if (!world) {
        c.error("TWW000", world.error().message, "", std::string(world.error().context_value("path")));
        return;
    }
    for (const scene::Finding& f : scene::validate(world.value())) {
        c.out.push_back(SectionFinding{"world", f.severity, f.code, f.message, f.object, f.path});
    }
    for (const scene::Object& o : world.value().objects) {
        if (o.asset && !assets.count(*o.asset)) {
            c.error("TWB020", "World object '" + (o.name.empty() ? o.id : o.name) + "' is bound to unknown asset '" + *o.asset + "'.", o.id, "asset");
        }
    }
    if (simulation.value("kind", std::string("none")) == "mobile-robot") {
        auto raster = scene::rasterize(world.value(), simulation.value("cellSize", std::int64_t{500}));
        if (!raster) {
            c.error("TWS000", raster.error().message);
            return;
        }
        for (const scene::Finding& f : raster.value().findings) {
            c.out.push_back(SectionFinding{"world", f.severity, f.code, f.message, f.object, f.path});
        }
    }
}

void validate_data(const Json& d, const std::set<std::string>& assets, const ModelFacts& pt, const ModelFacts& dt, Collector c,
                   std::set<std::string>& telemetry_ids, std::set<std::string>& event_ids) {
    static const std::set<std::string> kTypes = {"real", "integer", "boolean", "string", "enum"};
    const Json tel = d.value("telemetry", Json::array());
    for (std::size_t i = 0; i < tel.size(); ++i) {
        const Json& t = tel[i];
        const std::string id = t.value("id", std::string());
        const std::string path = "telemetry[" + std::to_string(i) + "]";
        if (!identifier(id)) c.error("TWB030", "Telemetry ids are identifiers such as battery_level.", id, path + ".id");
        else if (!telemetry_ids.insert(id).second) c.error("TWB030", "Duplicate telemetry id '" + id + "'.", id, path + ".id");
        if (!kTypes.count(t.value("type", std::string()))) {
            c.error("TWB031", "Telemetry '" + id + "' needs a data type: real, integer, boolean, string or enum.", id, path + ".type");
        }
        const std::string asset = t.value("asset", std::string());
        if (asset.empty()) c.warning("TWB032", "Telemetry '" + id + "' has no owning asset.", id, path + ".asset");
        else if (!assets.count(asset)) c.error("TWB032", "Telemetry '" + id + "' is owned by unknown asset '" + asset + "'.", id, path + ".asset");
        const Json range = t.value("range", Json::object());
        if (range.contains("min") && !decimal_text(range.at("min"))) c.error("TWB033", "Telemetry '" + id + "': min must be a number such as \"0\".", id, path + ".range.min");
        if (range.contains("max") && !decimal_text(range.at("max"))) c.error("TWB033", "Telemetry '" + id + "': max must be a number such as \"100\".", id, path + ".range.max");
        if (range.contains("min") && range.contains("max") && decimal_text(range.at("min")) && decimal_text(range.at("max")) &&
            decimal_value(range.at("min")) > decimal_value(range.at("max"))) {
            c.error("TWB033", "Telemetry '" + id + "': min is above max.", id, path + ".range");
        }
        if (t.contains("expectedPeriodMs") && (!t.at("expectedPeriodMs").is_number_integer() || t.at("expectedPeriodMs").get<std::int64_t>() <= 0)) {
            c.warning("TWB034", "Telemetry '" + id + "': the expected period must be a positive number of milliseconds.", id, path + ".expectedPeriodMs");
        }
        if (t.value("type", std::string()) == "enum" && t.value("values", Json::array()).empty()) {
            c.error("TWB031", "Telemetry '" + id + "' is an enum: list its values.", id, path + ".values");
        }
    }
    const Json events = d.value("events", Json::array());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const Json& e = events[i];
        const std::string id = e.value("id", std::string());
        const std::string path = "events[" + std::to_string(i) + "]";
        if (!identifier(id)) c.error("TWB035", "Event ids are identifiers such as obstacle_detected.", id, path + ".id");
        else if (!event_ids.insert(id).second) c.error("TWB035", "Duplicate event id '" + id + "'.", id, path + ".id");
        const std::string asset = e.value("asset", std::string());
        if (!asset.empty() && !assets.count(asset)) c.error("TWB032", "Event '" + id + "' is raised by unknown asset '" + asset + "'.", id, path + ".asset");
        const Json formal = e.value("formal", Json::object());
        const std::string pl = formal.value("pt", std::string());
        const std::string dl = formal.value("dt", std::string());
        if (!pl.empty() && pt.present && !pt.labels.empty() && !pt.labels.count(pl)) {
            c.error("TWB036", "Event '" + id + "' maps to PT label '" + pl + "', which the Physical System View does not have.", id, path + ".formal.pt");
        }
        if (!dl.empty() && dt.present && !dt.labels.empty() && !dt.labels.count(dl)) {
            c.error("TWB036", "Event '" + id + "' maps to DT label '" + dl + "', which the Digital Twin View does not have.", id, path + ".formal.dt");
        }
    }
    std::set<std::string> commands;
    for (const Json& m : d.value("commands", Json::array())) {
        const std::string id = m.value("id", std::string());
        if (!identifier(id) || !commands.insert(id).second) c.error("TWB037", "Command ids must be unique identifiers.", id, "commands");
        const std::string ack = m.value("acknowledgement", Json::object()).value("event", std::string());
        if (!ack.empty() && !event_ids.count(ack)) {
            c.warning("TWB038", "Command '" + id + "' expects acknowledgement event '" + ack + "', which the data contract does not define.", id, "commands");
        }
        const std::string conseq = m.value("observedConsequence", Json::object()).value("event", std::string());
        if (!conseq.empty() && !event_ids.count(conseq)) {
            c.warning("TWB038", "Command '" + id + "' expects consequence event '" + conseq + "', which the data contract does not define.", id, "commands");
        }
    }
    std::set<std::string> props;
    for (const Json& p : d.value("properties", Json::array())) {
        const std::string id = p.value("id", std::string());
        if (!identifier(id) || !props.insert(id).second) c.error("TWB039", "Static property ids must be unique identifiers.", id, "properties");
    }
}

void validate_connectivity(const Json& s, const std::set<std::string>& telemetry, const std::set<std::string>& events, Collector c) {
    static const std::set<std::string> kKinds = {"simulator", "mqtt", "opcua", "rest", "replay", "file"};
    std::set<std::string> sources;
    for (const Json& src : s.value("sources", Json::array())) {
        const std::string id = src.value("id", std::string());
        const std::string kind = src.value("kind", std::string());
        if (!identifier(id) || !sources.insert(id).second) c.error("TWB040", "Data source ids must be unique identifiers.", id, "sources");
        if (!kKinds.count(kind)) {
            c.error("TWB040", "Data source '" + id + "' has kind '" + kind + "'; use simulator, mqtt, opcua, rest, replay or file.", id, "sources");
            continue;
        }
        const Json cfg = src.value("config", Json::object());
        if (kind == "mqtt") {
            if (cfg.value("host", std::string()).empty()) c.error("TWB041", "MQTT source '" + id + "' needs a broker host.", id, "config.host");
            if (cfg.contains("port") && !cfg.at("port").is_number_integer()) c.error("TWB041", "MQTT source '" + id + "': the port is a number.", id, "config.port");
        } else if (kind == "rest") {
            const std::string url = cfg.value("url", std::string());
            if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) c.error("TWB041", "REST source '" + id + "' needs an http(s) URL.", id, "config.url");
        } else if (kind == "opcua") {
            if (cfg.value("endpoint", std::string()).rfind("opc.tcp://", 0) != 0) c.error("TWB041", "OPC UA source '" + id + "' needs an opc.tcp:// endpoint.", id, "config.endpoint");
            c.warning("TWB045", "OPC UA source '" + id + "' can be defined and validated, but this build has no OPC UA client: connection tests and deployment report the adapter as unavailable.", id, "kind");
        } else if (kind == "replay" || kind == "file") {
            if (cfg.value("file", std::string()).empty()) c.error("TWB041", "Replay source '" + id + "' needs a trace file (CSV or JSON lines).", id, "config.file");
        }
    }
    std::set<std::string> bound;
    for (const Json& b : s.value("bindings", Json::array())) {
        const Json target = b.value("target", Json::object());
        const std::string kind = target.value("kind", std::string());
        const std::string id = target.value("id", std::string());
        if (kind == "telemetry" ? !telemetry.count(id) : kind == "event" ? !events.count(id) : true) {
            c.error("TWB042", "A binding targets unknown " + (kind.empty() ? std::string("data item") : kind) + " '" + id + "'.", b.value("id", std::string()), "bindings");
        }
        if (!sources.count(b.value("source", std::string()))) {
            c.error("TWB043", "Binding of '" + id + "' uses unknown source '" + b.value("source", std::string()) + "'.", b.value("id", std::string()), "bindings");
        }
        const Json unit = b.value("unit", Json::object());
        for (const char* k : {"scale", "offset"}) {
            if (unit.contains(k) && !decimal_text(unit.at(k))) {
                c.error("TWB046", "Binding of '" + id + "': unit " + std::string(k) + " must be a number such as \"0.1\".", b.value("id", std::string()), std::string("unit.") + k);
            }
        }
        if (kind == "telemetry") bound.insert(id);
    }
    for (const std::string& t : telemetry) {
        if (!bound.count(t)) c.warning("TWB044", "Telemetry '" + t + "' is not bound to a data source (unbound input).", t, "bindings");
    }
}

void validate_presentation(const Json& p, const std::set<std::string>& telemetry, const std::set<std::string>& assets,
                           const ModelFacts& dt, const std::set<std::string>& monitors, Collector c) {
    for (const Json& k : p.value("keyTelemetry", Json::array())) {
        if (k.is_string() && !telemetry.count(k.get<std::string>())) c.warning("TWB050", "Key telemetry '" + k.get<std::string>() + "' is not in the data contract.", k.get<std::string>(), "keyTelemetry");
    }
    for (const Json& a : p.value("importantAssets", Json::array())) {
        if (a.is_string() && !assets.count(a.get<std::string>())) c.warning("TWB051", "Important asset '" + a.get<std::string>() + "' does not exist.", a.get<std::string>(), "importantAssets");
    }
    if (dt.present && !dt.locations.empty()) {
        for (const Json items_loc_unused = p.value("states", Json::object()); const auto& [loc, unused] : items_loc_unused.items()) {
            if (!dt.locations.count(loc)) c.warning("TWB052", "Presentation names state '" + loc + "', which the Digital Twin View does not have.", loc, "states");
        }
    }
    for (const Json& m : p.value("importantMonitors", Json::array())) {
        if (m.is_string() && !monitors.count(m.get<std::string>())) c.warning("TWB053", "Important monitor '" + m.get<std::string>() + "' does not exist.", m.get<std::string>(), "importantMonitors");
    }
}

void validate_assurance(const Json& a, const std::set<std::string>& telemetry, Collector c, std::set<std::string>& monitor_ids) {
    Json doc = a;
    doc["format"] = std::string(monitoring::kMonitorsFormat);
    auto m = monitoring::monitors_from_json(doc);
    if (!m) {
        c.error("TWN000", m.error().message);
        return;
    }
    for (const monitoring::Finding& f : monitoring::validate_document(m.value())) {
        c.out.push_back(SectionFinding{"assurance", f.severity, f.code, f.message, "", f.path});
    }
    for (const auto& spec : m.value().monitors) {
        monitor_ids.insert(spec.id);
        if (spec.kind == "data_quality") {
            const std::string field = spec.config.value("field", std::string());
            if (!field.empty() && !telemetry.count(field)) {
                c.error("TWB061", "Data-quality monitor '" + spec.id + "' watches '" + field + "', which is not in the data contract.", spec.id, "monitors");
            }
        }
    }
    for (const auto& r : m.value().requirements) {
        for (const std::string& mid : r.monitors) {
            if (!monitor_ids.count(mid)) c.error("TWB060", "Requirement " + r.id + " is checked by unknown monitor '" + mid + "'.", r.id, "requirements");
        }
    }
}

void validate_simulation(const Json& s, const Json& world, const ModelFacts& pt, Collector c) {
    const std::string kind = s.value("kind", std::string("none"));
    if (kind != "none" && kind != "mobile-robot" && kind != "event-script") {
        c.error("TWB070", "Simulation kind is none, mobile-robot or event-script.", "", "kind");
        return;
    }
    if (kind == "mobile-robot") {
        auto w = scene::world_from_json(world);
        if (!w) return;
        if (w.value().mode != "spatial") {
            c.error("TWB071", "The mobile-robot simulator needs a spatial world (Build → World & Layout).", "", "kind");
            return;
        }
        auto sc = scene::simulator_scenario(w.value(), s, "check", "");
        if (!sc) {
            for (const auto& [code, message] : sc.error().context) c.error(code, message);
        }
    } else if (kind == "event-script") {
        Json feed = s.value("script", Json::object());
        auto f = ptfeed::feed_from_json(feed);
        if (!f) {
            c.error("TWB072", "Event script: " + f.error().message, "", "script");
            return;
        }
        for (const ptfeed::Event& e : f.value().events) {
            if (pt.present && !pt.labels.empty() && !pt.labels.count(e.label)) {
                c.error("TWB073", "The event script sends '" + e.label + "', which the Physical System View does not have.", e.label, "script.events");
            }
        }
    }
}

void validate_scenarios(const Json& list, const ModelFacts& pt, const ModelFacts& dt, const Json& world, Collector c) {
    std::set<std::string> ids;
    std::set<std::string> objects;
    for (const Json& o : world.value("objects", Json::array())) objects.insert(o.value("id", std::string()));
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Json& s = list[i];
        const std::string id = s.value("id", std::string());
        const std::string path = "[" + std::to_string(i) + "]";
        if (!identifier(id) || !ids.insert(id).second) c.error("TWB080", "Scenario ids must be unique identifiers.", id, path + ".id");
        const Json steps = s.value("steps", Json::array());
        // Objects of the scenario's world as it evolves: earlier steps may add or remove objects.
        std::set<std::string> present = objects;
        for (std::size_t k = 0; k < steps.size(); ++k) {
            const Json& st = steps[k];
            const std::string kind = st.value("kind", std::string());
            const std::string sp = path + ".steps[" + std::to_string(k) + "]";
            if (kind == "event") {
                const std::string label = st.value("label", std::string());
                const ModelFacts& m = st.value("level", std::string("dt")) == "pt" ? pt : dt;
                if (m.present && !m.labels.empty() && !m.labels.count(label)) {
                    c.error("TWB082", "Scenario '" + id + "' sends '" + label + "', which the " +
                                          (st.value("level", std::string("dt")) == "pt" ? "Physical System View" : "Digital Twin View") + " does not have.",
                            id, sp);
                }
            } else if (kind == "world") {
                const Json change = st.value("change", Json::object());
                const std::string action = change.value("action", std::string());
                const std::string obj = change.value("objectId", change.value("object", Json::object()).value("id", std::string()));
                if (action == "add_object") {
                    if (!obj.empty()) present.insert(obj);
                } else if (!obj.empty() && !present.count(obj)) {
                    c.error("TWB083", "Scenario '" + id + "' changes unknown world object '" + obj + "'.", id, sp);
                } else if (action == "remove_object") {
                    present.erase(obj);
                }
            } else if (kind == "expect") {
                const Json e = st.value("expect", Json::object());
                if (e.contains("location") && dt.present && !dt.locations.empty() && !dt.locations.count(e.value("location", std::string()))) {
                    c.error("TWB084", "Scenario '" + id + "' expects unknown location '" + e.value("location", std::string()) + "'.", id, sp);
                }
            } else if (kind != "delay" && kind != "observe") {
                c.error("TWB081", "Scenario '" + id + "' has a step of unknown kind '" + kind + "'.", id, sp);
            }
        }
    }
}

}  // namespace

std::vector<SectionFinding> BlueprintService::Impl::validate_sections(const BlueprintVersion& v) {
    std::vector<SectionFinding> out;
    const Json& d = v.document;
    const ModelFacts pt = facts_of(core, v, "pt_model");
    const ModelFacts dt = facts_of(core, v, "dt_model");
    std::set<std::string> assets;
    std::set<std::string> telemetry;
    std::set<std::string> events;
    std::set<std::string> monitors;
    // A section is user input: one whose members have unexpected types is reported (TWB000) instead
    // of failing the whole validation, so the other sections and the overview stay usable.
    auto guarded = [&](const char* section, auto&& validator) {
        try {
            validator();
        } catch (const std::exception& e) {
            Collector{out, section}.error("TWB000", std::string("The section has a member of an unexpected type (") + e.what() +
                                                        "); correct it in the editor or the JSON view.", "", "");
        }
    };
    guarded("identity", [&] { validate_identity(d.value("identity", Json::object()), d, Collector{out, "identity"}); });
    guarded("structure", [&] { validate_structure(d.value("structure", Json::object()), Collector{out, "structure"}, assets); });
    guarded("world", [&] {
        validate_world(d.value("world", Json::object()), d.value("simulation", Json::object()), assets, Collector{out, "world"});
    });
    guarded("data", [&] { validate_data(d.value("data", Json::object()), assets, pt, dt, Collector{out, "data"}, telemetry, events); });
    guarded("connectivity", [&] { validate_connectivity(d.value("connectivity", Json::object()), telemetry, events, Collector{out, "connectivity"}); });
    guarded("assurance", [&] { validate_assurance(d.value("assurance", Json::object()), telemetry, Collector{out, "assurance"}, monitors); });
    guarded("presentation", [&] {
        validate_presentation(d.value("presentation", Json::object()), telemetry, assets, dt, monitors, Collector{out, "presentation"});
    });
    guarded("simulation", [&] {
        validate_simulation(d.value("simulation", Json::object()), d.value("world", Json::object()), pt, Collector{out, "simulation"});
    });
    guarded("scenarios", [&] {
        validate_scenarios(d.value("scenarios", Json::array()), pt, dt, d.value("world", Json::object()), Collector{out, "scenarios"});
    });
    return out;
}

Result<Json> BlueprintService::validate(std::string_view id, std::int64_t v) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    const auto findings = impl_->validate_sections(ver.value());
    Json by_section = Json::object();
    for (const auto section : kBlueprintSections) by_section[std::string(section)] = Json{{"errors", 0}, {"warnings", 0}};
    for (const SectionFinding& f : findings) {
        Json& s = by_section[f.section];
        s[f.severity == "error" ? "errors" : "warnings"] = s.value(f.severity == "error" ? "errors" : "warnings", 0) + 1;
    }
    const auto errors = std::count_if(findings.begin(), findings.end(), [](const SectionFinding& f) { return f.severity == "error"; });
    return Json{{"valid", errors == 0}, {"errors", errors}, {"warnings", static_cast<std::int64_t>(findings.size()) - errors},
                {"sections", by_section}, {"findings", to_json(findings)}};
}

// ----------------------------------------------------------------------------- status

namespace {

Json gate_item(std::string id, std::string title, std::string state, std::string detail, bool blocking, std::string fix,
               Json evidence = nullptr) {
    return Json{{"id", std::move(id)}, {"title", std::move(title)}, {"state", std::move(state)}, {"detail", std::move(detail)},
                {"blocking", blocking}, {"fix", std::move(fix)}, {"evidenceId", std::move(evidence)}};
}

}  // namespace

Result<Json> BlueprintService::status(std::string_view id, std::int64_t v) {
    auto ver_r = impl_->load(id, v);
    if (!ver_r) return std::move(ver_r).error();
    const BlueprintVersion& ver = ver_r.value();
    const Json& d = ver.document;
    const auto findings = impl_->validate_sections(ver);
    auto count = [&](const std::string& section, const char* severity) {
        return std::count_if(findings.begin(), findings.end(), [&](const SectionFinding& f) { return f.section == section && f.severity == severity; });
    };
    auto first = [&](const std::string& section) -> std::string {
        for (const SectionFinding& f : findings) {
            if (f.section == section && f.severity == "error") return f.message;
        }
        for (const SectionFinding& f : findings) {
            if (f.section == section) return f.message;
        }
        return {};
    };
    auto section_state = [&](const std::string& section, bool empty) {
        if (count(section, "error") > 0) return std::string("errors");
        if (empty) return std::string("empty");
        if (count(section, "warning") > 0) return std::string("warnings");
        return std::string("complete");
    };

    // Formal artefacts: version, lifecycle and validation evidence for exactly the pinned bytes.
    struct Pinned {
        std::optional<ArtifactVersion> version;
        std::optional<EvidenceRecord> validation;
    };
    std::map<std::string, Pinned> pinned;
    {
        auto l = impl_->core.lock();
        for (const auto role : kBlueprintRoles) {
            auto p = ver.pins.find(std::string(role));
            if (p == ver.pins.end()) continue;
            auto ref = parse_ref(p->second);
            if (!ref) continue;
            auto av = impl_->core.artifacts->version(ref.value());
            if (!av) continue;
            Pinned x;
            x.version = av.value();
            std::vector<EvidenceInput> in{{"subject", ref.value(), av.value().content_sha256}};
            if (av.value().kind == ArtifactKind::Interpretation) {
                auto o = ver.pins.find("ontology");
                if (o != ver.pins.end()) {
                    auto oref = parse_ref(o->second);
                    auto ov = oref ? impl_->core.artifacts->version(oref.value()) : Result<ArtifactVersion>(make_error(ErrorCode::NotFound, ""));
                    if (ov) in.push_back({"ontology", oref.value(), ov.value().content_sha256});
                }
            }
            auto ev = impl_->core.evidence->latest_for(EvidenceKind::Validation, in);
            if (ev && ev.value()) x.validation = *ev.value();
            pinned[std::string(role)] = x;
        }
    }
    auto formal_state = [&](const std::string& role) -> std::pair<std::string, std::string> {
        auto it = pinned.find(role);
        if (it == pinned.end()) return {"empty", "Not defined yet"};
        const Pinned& p = it->second;
        const std::string ref = p.version->ref().str();
        if (p.validation && p.validation->outcome == Outcome::Pass) return {"complete", ref + " · validated"};
        if (p.validation) return {"errors", ref + " · validation failed: " + p.validation->summary};
        return {"warnings", ref + " · not validated yet"};
    };

    // Sections with summaries.
    const Json structure = d.value("structure", Json::object());
    const Json world = d.value("world", Json::object());
    const Json data = d.value("data", Json::object());
    const Json assurance = d.value("assurance", Json::object());
    const Json scenarios = d.value("scenarios", Json::array());
    const std::size_t assets_n = structure.value("assets", Json::array()).size();
    const std::size_t objects_n = world.value("objects", Json::array()).size();
    const std::size_t layers_n = world.value("layers", Json::array()).size();
    const std::size_t telemetry_n = data.value("telemetry", Json::array()).size();
    const std::size_t events_n = data.value("events", Json::array()).size();
    const std::size_t commands_n = data.value("commands", Json::array()).size();
    const std::size_t requirements_n = assurance.value("requirements", Json::array()).size();
    const std::size_t monitors_n = assurance.value("monitors", Json::array()).size();

    Json sections = Json::array();
    auto add_section = [&](std::string sid, std::string title, std::string state, std::string summary, std::string route,
                           std::int64_t errors, std::int64_t warnings, std::string detail) {
        sections.push_back(Json{{"id", std::move(sid)}, {"title", std::move(title)}, {"state", std::move(state)},
                                {"summary", std::move(summary)}, {"route", std::move(route)}, {"errors", errors},
                                {"warnings", warnings}, {"detail", std::move(detail)}});
    };
    add_section("structure", "Structure", section_state("structure", assets_n == 0),
                std::to_string(assets_n) + " asset" + (assets_n == 1 ? "" : "s") + ", " +
                    std::to_string(structure.value("relationships", Json::array()).size()) + " relationships",
                "build/structure", count("structure", "error"), count("structure", "warning"), first("structure"));
    add_section("world", "World & Layout", section_state("world", objects_n == 0),
                std::string(world.value("mode", std::string("spatial"))) + " · " + std::to_string(layers_n) + " layers · " + std::to_string(objects_n) + " objects",
                "build/world", count("world", "error"), count("world", "warning"), first("world"));
    add_section("data", "Data contract", section_state("data", telemetry_n + events_n == 0),
                std::to_string(telemetry_n) + " signals · " + std::to_string(events_n) + " events · " + std::to_string(commands_n) + " commands",
                "build/data", count("data", "error"), count("data", "warning"), first("data"));
    const std::size_t bindings_n = d.value("connectivity", Json::object()).value("bindings", Json::array()).size();
    add_section("connectivity", "Connectivity", section_state("connectivity", bindings_n == 0 && telemetry_n > 0),
                std::to_string(d.value("connectivity", Json::object()).value("sources", Json::array()).size()) + " sources · " +
                    std::to_string(bindings_n) + " bindings",
                "build/data?tab=bindings", count("connectivity", "error"), count("connectivity", "warning"), first("connectivity"));
    add_section("presentation", "Presentation", section_state("presentation", false),
                "Primary view: " + d.value("presentation", Json::object()).value("primaryView", std::string("status")),
                "build/presentation", count("presentation", "error"), count("presentation", "warning"), first("presentation"));
    for (const auto& [role, title, route] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"pt_model", "Physical System View", "behavior/pt"}, {"dt_model", "Digital Twin View", "behavior/dt"},
             {"ontology", "Ontology", "semantics/ontology"}}) {
        const auto [state, summary] = formal_state(role);
        add_section(role, title, state, summary, route, state == "errors" ? 1 : 0, state == "warnings" ? 1 : 0, "");
    }
    {
        const auto [ps, psum] = formal_state("pt_interpretation");
        const auto [ds, dsum] = formal_state("dt_interpretation");
        const std::string state = ps == "errors" || ds == "errors" ? "errors" : ps == "empty" || ds == "empty" ? (ps == ds ? "empty" : "warnings")
                                  : ps == "warnings" || ds == "warnings" ? "warnings" : "complete";
        add_section("interpretations", "Interpretations", state, "I_P: " + psum + " · I_D: " + dsum, "semantics/interpretations",
                    state == "errors" ? 1 : 0, state == "warnings" ? 1 : 0, "");
    }
    add_section("requirements", "Requirements", section_state("assurance", requirements_n == 0),
                std::to_string(requirements_n) + " requirements", "assurance/requirements", 0, 0, "");
    add_section("monitors", "Monitors", section_state("assurance", monitors_n == 0), std::to_string(monitors_n) + " monitors",
                "assurance/monitors", count("assurance", "error"), count("assurance", "warning"), first("assurance"));
    add_section("scenarios", "Scenarios", section_state("scenarios", scenarios.empty()),
                std::to_string(scenarios.size()) + " scenario" + (scenarios.size() == 1 ? "" : "s"), "test/scenarios",
                count("scenarios", "error"), count("scenarios", "warning"), first("scenarios"));

    // ---------------------------------------------------------------- release gate
    Json gate = Json::array();
    auto structural = [&](const std::string& gid, const std::string& title, const std::string& section, const std::string& fix, bool required_nonempty, bool empty) {
        const auto errors = count(section, "error");
        if (errors > 0) gate.push_back(gate_item(gid, title, "fail", first(section), true, fix));
        else if (required_nonempty && empty) gate.push_back(gate_item(gid, title, "fail", "Not defined yet.", true, fix));
        else gate.push_back(gate_item(gid, title, "pass", count(section, "warning") > 0 ? std::to_string(count(section, "warning")) + " warning(s)" : "Valid", true, fix));
    };
    structural("structure", "Structure", "structure", "build/structure", true, assets_n == 0);
    structural("world", "World", "world", "build/world", false, objects_n == 0);
    structural("data", "Data contract", "data", "build/data", false, false);
    structural("bindings", "Bindings", "connectivity", "build/data?tab=bindings", false, false);
    for (const auto& [role, title, fix] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"pt_model", "PT model", "behavior/pt"}, {"dt_model", "DT model", "behavior/dt"}, {"ontology", "Ontology", "semantics/ontology"},
             {"pt_interpretation", "PT interpretation", "semantics/interpretations"}, {"dt_interpretation", "DT interpretation", "semantics/interpretations"}}) {
        auto it = pinned.find(role);
        if (it == pinned.end()) {
            gate.push_back(gate_item(role, title, "fail", "Not defined yet.", true, fix));
        } else if (it->second.validation && it->second.validation->outcome == Outcome::Pass) {
            gate.push_back(gate_item(role, title, "pass", it->second.version->ref().str() + " validated", true, fix, it->second.validation->id));
        } else if (it->second.validation) {
            gate.push_back(gate_item(role, title, "fail", it->second.validation->summary, true, fix, it->second.validation->id));
        } else {
            gate.push_back(gate_item(role, title, "not_run", it->second.version->ref().str() + " has not been validated (Assurance → Verification → Validate formal artefacts).", true, fix));
        }
    }
    // Alignment, compilation, package: evidence for exactly these inputs.
    auto all_bindings = impl_->bindings(ver);
    std::optional<EvidenceRecord> align;
    std::optional<EvidenceRecord> compile;
    std::optional<EvidenceRecord> package;
    std::optional<EvidenceRecord> scenario_ev;
    std::optional<Json> align_doc;
    if (all_bindings) {
        std::vector<EvidenceInput> in;
        std::vector<EvidenceInput> dt_in;
        for (const Binding& b : all_bindings.value()) {
            in.push_back({b.role, b.ref, b.sha256});
            if (b.role == "dt_model" || b.role == "dt_interpretation") dt_in.push_back({b.role, b.ref, b.sha256});
        }
        auto l = impl_->core.lock();
        if (auto e = impl_->core.evidence->latest_for(EvidenceKind::Alignment, in); e && e.value()) {
            align = *e.value();
            if (auto doc = impl_->core.evidence->document(*align); doc) align_doc = doc.value();
        }
        if (auto e = impl_->core.evidence->latest_for(EvidenceKind::Compilation, dt_in); e && e.value()) compile = *e.value();
        if (auto e = impl_->core.evidence->latest_for(EvidenceKind::Package, in); e && e.value()) package = *e.value();
        std::vector<EvidenceInput> sin = dt_in;
        sin.push_back(Impl::document_input(ver));
        if (auto e = impl_->core.evidence->latest_for(EvidenceKind::Scenario, sin); e && e.value()) scenario_ev = *e.value();
    }
    if (!all_bindings) {
        gate.push_back(gate_item("alignment", "Semantic alignment", "blocked", all_bindings.error().message, true, "assurance/alignment"));
    } else if (!align) {
        gate.push_back(gate_item("alignment", "Semantic alignment", "not_run", "Run the aligner on the pinned PT/DT views, ontology and interpretations.", true, "assurance/alignment"));
    } else if (align->outcome == Outcome::Pass) {
        const Json modes = align_doc ? align_doc->value("modes", Json::object()) : Json::object();
        const bool strong = modes.value("strong", std::string()) == "aligned";
        gate.push_back(gate_item("alignment", "Semantic alignment", "pass",
                                 std::string("PASS – ") + (strong ? "STRONG" : "WEAK") + " (" + modes.value("strong_reason", std::string()) + ")", true,
                                 "assurance/alignment", align->id));
    } else {
        gate.push_back(gate_item("alignment", "Semantic alignment", align->outcome == Outcome::Fail ? "fail" : "error", align->summary, true, "assurance/alignment", align->id));
    }
    if (!compile) {
        gate.push_back(gate_item("compiler", "Compiler", "not_run", "Compile the Digital Twin View (translation validation).", true, "assurance/verification"));
    } else {
        gate.push_back(gate_item("compiler", "Compiler", compile->outcome == Outcome::Pass ? "pass" : "fail", compile->summary, true, "assurance/verification", compile->id));
    }
    gate.push_back(gate_item("monitors", "Monitor validation", count("assurance", "error") > 0 ? "fail" : "pass",
                             count("assurance", "error") > 0 ? first("assurance") : std::to_string(monitors_n) + " monitor(s) valid", true, "assurance/monitors"));
    if (scenarios.empty()) {
        gate.push_back(gate_item("scenarios", "Scenario regression (tests)", "not_applicable", "No scenarios defined.", false, "test/scenarios"));
    } else if (!scenario_ev) {
        gate.push_back(gate_item("scenarios", "Scenario regression (tests)", "not_run", "Run the scenario tests for this version.", true, "test/scenarios"));
    } else {
        gate.push_back(gate_item("scenarios", "Scenario regression (tests)", scenario_ev->outcome == Outcome::Pass ? "pass" : "fail",
                                 scenario_ev->summary, true, "test/scenarios", scenario_ev->id));
    }
    const bool package_current = package && ver.package_id;
    if (!package_current) {
        gate.push_back(gate_item("package", "Package integrity", "not_run", "Build the Verified Core Package and the Deployment Bundle.", true, "release/package"));
    } else {
        gate.push_back(gate_item("package", "Package integrity", package->outcome == Outcome::Pass ? "pass" : "fail", package->summary, true, "release/package", package->id));
    }
    std::vector<std::string> blockers;
    bool ready = true;
    for (const Json& g : gate) {
        if (g.value("blocking", false) && g.value("state", std::string()) != "pass") {
            ready = false;
            blockers.push_back(g.value("title", std::string()) + ": " + g.value("detail", std::string()));
        }
    }
    // Ready to package = everything except the package itself passes.
    bool ready_to_package = true;
    for (const Json& g : gate) {
        if (g.value("id", std::string()) == "package") continue;
        if (g.value("blocking", false) && g.value("state", std::string()) != "pass") ready_to_package = false;
    }

    // Completeness and next steps.
    std::int64_t complete = 0;
    Json next = Json::array();
    for (const Json& s : sections) {
        if (s.value("state", std::string()) == "complete") ++complete;
        else if (next.size() < 4) next.push_back(Json{{"section", s["id"]}, {"title", s["title"]}, {"state", s["state"]}, {"route", s["route"]},
                                                      {"detail", s.value("detail", std::string()).empty() ? s["summary"] : s["detail"]}});
    }
    Json alignment_summary = Json{{"state", align ? (align->outcome == Outcome::Pass ? "pass" : "fail") : (all_bindings ? "not_run" : "blocked")},
                                  {"evidenceId", align ? Json(align->id) : Json(nullptr)}};
    if (align) alignment_summary["at"] = align->created_at;
    return Json{{"version", version_summary(ver)},
                {"sections", sections},
                {"completeness", {{"complete", complete}, {"total", sections.size()}, {"next", next}}},
                {"readiness", {{"verdict", ready ? "ready" : "blocked"}, {"readyToPackage", ready_to_package}, {"items", gate}, {"blockers", blockers}}},
                {"alignment", alignment_summary},
                {"findings", to_json(findings)}};
}

// ----------------------------------------------------------------------------- impact

Result<Json> BlueprintService::impact(std::string_view id, std::int64_t v, std::optional<std::int64_t> against) {
    auto cur = impl_->load(id, v);
    if (!cur) return std::move(cur).error();
    std::int64_t base_v = against.value_or(cur.value().parent.value_or(0));
    if (base_v <= 0) return Json{{"against", nullptr}, {"sections", Json::array()}, {"summary", "First version: nothing to compare."}};
    auto base = impl_->load(id, base_v);
    if (!base) return std::move(base).error();
    const Json& a = base.value().document;
    const Json& b = cur.value().document;
    Json sections = Json::array();
    bool alignment_stale = false;
    bool ir_stale = false;
    bool package_stale = false;
    bool bundle_stale = false;
    auto changed = [&](const char* s) { return a.value(s, Json()) != b.value(s, Json()); };
    auto add = [&](std::string section, std::string classification, std::vector<std::string> consequences) {
        Json c = Json::array();
        for (auto& x : consequences) c.push_back(std::move(x));
        sections.push_back(Json{{"section", std::move(section)}, {"classification", std::move(classification)}, {"consequences", c}});
    };
    if (changed("identity")) {
        const Json ia = a.value("identity", Json::object());
        const Json ib = b.value("identity", Json::object());
        if (ia.value("modelId", Json()) != ib.value("modelId", Json()) || ia.value("ticksPerUnit", Json()) != ib.value("ticksPerUnit", Json())) {
            ir_stale = package_stale = true;
            add("identity", "formal", {"Model id or time resolution changed: the Twin IR and the package must be rebuilt."});
        } else {
            bundle_stale = true;
            add("identity", "presentation", {"Name or description only: no formal evidence is invalidated."});
        }
    }
    for (const char* s : {"structure", "world", "data", "connectivity", "presentation", "simulation"}) {
        if (!changed(s)) continue;
        bundle_stale = true;
        std::vector<std::string> cons = {"No formal evidence is invalidated (outside the verified core).", "The deployment bundle must be rebuilt."};
        if (std::string(s) == "world" || std::string(s) == "simulation") cons.push_back("Simulator inputs change: re-run the scenario tests and the preview.");
        if (std::string(s) == "data" || std::string(s) == "connectivity") cons.push_back("Instances' telemetry channels and bindings are re-created on upgrade.");
        add(s, "deployment", cons);
    }
    if (a.value("behavior", Json()) != b.value("behavior", Json())) {
        add("behavior", "presentation", {"Diagram layout only: the canonical models and their evidence are unchanged."});
    }
    if (changed("assurance")) {
        package_stale = true;
        add("assurance", "verified-core", {"Monitors ship in the Verified Core Package: the package must be rebuilt.", "Formal alignment and IR are unaffected."});
    }
    if (changed("scenarios")) add("scenarios", "tests", {"Scenario tests must be re-run (tests, not proofs)."});
    for (const auto role : kBlueprintRoles) {
        const std::string r(role);
        const auto pa = base.value().pins.count(r) ? base.value().pins.at(r) : std::string();
        const auto pb = cur.value().pins.count(r) ? cur.value().pins.at(r) : std::string();
        if (pa == pb) continue;
        std::vector<std::string> cons;
        alignment_stale = true;
        package_stale = true;
        if (r == "dt_model") {
            ir_stale = true;
            cons = {"Alignment evidence is stale.", "The Twin IR is stale (recompile).", "The package is stale."};
        } else if (r == "pt_model") {
            cons = {"Alignment evidence is stale.", "The package is stale (it carries V_P and the evidence)."};
        } else if (r == "ontology") {
            cons = {"Interpretations are affected (re-validate them against the new ontology).", "Alignment is potentially stale.",
                    "Run a refinement check against the previous ontology."};
        } else {
            if (r == "dt_interpretation") ir_stale = true;
            cons = {"Alignment evidence is stale.", r == "dt_interpretation" ? "IR propositions are stale (recompile)." : "The package is stale."};
        }
        Json s{{"section", r}, {"classification", "formal"}, {"from", pa}, {"to", pb}, {"consequences", Json(cons)}};
        sections.push_back(s);
    }
    // Instances that run the base version.
    Json affected = Json::array();
    {
        auto l = impl_->core.lock();
        auto twins = impl_->core.twins->twins();
        if (twins) {
            for (const Twin& t : twins.value()) {
                if (t.blueprint_id && *t.blueprint_id == id) {
                    affected.push_back(Json{{"id", t.id}, {"name", t.name}, {"version", t.blueprint_version ? Json(*t.blueprint_version) : Json(nullptr)}});
                }
            }
        }
    }
    std::string summary = sections.empty() ? "No changes." : alignment_stale || ir_stale ? "Formal evidence is invalidated: re-verify before release."
                                                                                       : "No formal evidence is invalidated.";
    return Json{{"against", base_v},
                {"version", v},
                {"sections", sections},
                {"formal", {{"alignmentStale", alignment_stale}, {"irStale", ir_stale}, {"packageStale", package_stale}, {"bundleStale", bundle_stale || package_stale}}},
                {"instances", affected},
                {"summary", summary}};
}

// ------------------------------------------------------------------------------ raster

Result<Json> BlueprintService::world_raster(std::string_view id, std::int64_t v) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    auto world = scene::world_from_json(ver.value().document.value("world", Json::object()));
    if (!world) return std::move(world).error();
    const Json sim = ver.value().document.value("simulation", Json::object());
    auto r = scene::rasterize(world.value(), sim.value("cellSize", std::int64_t{500}));
    if (!r) return std::move(r).error();
    Json out = scene::to_json(r.value());
    if (sim.value("kind", std::string("none")) == "mobile-robot") {
        auto s = scene::simulator_scenario(world.value(), sim, ver.value().document.value("identity", Json::object()).value("name", std::string()), "");
        if (s) {
            out["simulator"] = Json{{"valid", true}, {"home", s.value()["home"]}, {"targets", s.value()["targets"]},
                                    {"events", s.value()["events"].size()}, {"observation", s.value()["observation"]}, {"drone", s.value()["drone"]}};
        } else {
            Json problems = Json::array();
            for (const auto& [code, message] : s.error().context) problems.push_back(Json{{"code", code}, {"message", message}});
            out["simulator"] = Json{{"valid", false}, {"problems", problems}};
        }
    }
    return out;
}

}  // namespace twin::studio
