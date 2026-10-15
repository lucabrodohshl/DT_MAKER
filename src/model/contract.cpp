// Derivation of the existing Blueprint sections (structure, data contract, presentation,
// monitors) from the model, so the release gate, instances, deployment and Operate keep
// working unchanged (04-compilation.md, section 4.6).
#include "contract.hpp"

#include <algorithm>
#include <map>

#include "theory.hpp"
#include "twin/model/expression.hpp"

namespace twin::model::detail {

using json::Json;

std::string asset_id(const Resolved& model, const Instance& instance) {
    return instance.path.empty() ? model.doc().name : instance.path;
}

namespace {

std::string label_of(const std::string& label, const std::string& name) { return label.empty() ? name : label; }

std::string type_name(DataType t) { return std::string(to_string(t)); }

/// ceil(3 × period) seconds, at least 1.
std::int64_t stale_seconds(const std::string& period) {
    // period is an exact positive decimal: integer part and fraction.
    const std::size_t dot = period.find('.');
    const std::int64_t whole = std::stoll(dot == std::string::npos ? period : period.substr(0, dot));
    const bool fraction = dot != std::string::npos && period.find_first_not_of('0', dot + 1) != std::string::npos;
    std::int64_t s = 3 * whole;
    if (fraction) {
        // 3 × (whole + f) with 0 < f < 1 adds between 0 and 3 seconds; round the product up exactly.
        const std::string frac = period.substr(dot + 1);
        std::int64_t num = std::stoll(frac);
        std::int64_t den = 1;
        for (std::size_t i = 0; i < frac.size(); ++i) den *= 10;
        num *= 3;
        s += num / den + (num % den != 0 ? 1 : 0);
    }
    return std::max<std::int64_t>(s, 1);
}

std::int64_t period_ms(const std::string& period) {
    const std::size_t dot = period.find('.');
    std::int64_t ms = std::stoll(dot == std::string::npos ? period : period.substr(0, dot)) * 1000;
    if (dot != std::string::npos) {
        std::string frac = period.substr(dot + 1, 3);
        while (frac.size() < 3) frac += '0';
        ms += std::stoll(frac);
    }
    return ms;
}

const char* tone_word(Tone t) {
    switch (t) {
        case Tone::Normal: return "neutral";
        case Tone::Warning: return "warning";
        case Tone::Critical: return "critical";
    }
    return "neutral";
}

}  // namespace

Json derive_sections(const Resolved& r, const BehaviorOutput& b) {
    const Document& d = r.doc();
    // ------------------------------------------------------------------ structure
    Json types = Json::array();
    for (const AssetType& t : d.types) {
        Json props = Json::array();
        const EffectiveType* e = r.type(t.name);
        if (e != nullptr) {
            for (const Property* p : e->properties) {
                if (p->kind != PropertyKind::Fixed) continue;
                Json x = {{"key", p->name}, {"label", label_of(p->label, p->name)}, {"type", type_name(p->type)}};
                if (const auto it = e->values.find(p->name); it != e->values.end()) x["default"] = it->second;
                props.push_back(std::move(x));
            }
        }
        types.push_back({{"id", t.name}, {"name", label_of(t.label, t.name)}, {"category", ""}, {"description", t.description}, {"properties", props}});
    }
    Json assets = Json::array();
    for (const auto& inst : r.instances()) {
        Json values = Json::object();
        for (const auto& [k, v] : inst->values) values[k] = v;
        Json a = {{"id", asset_id(r, *inst)},
                  {"name", inst->part != nullptr ? label_of(inst->part->label, inst->part->name) : label_of(d.label, d.name)},
                  {"type", inst->type->type->name},
                  {"scope", inst->external ? "context" : "instance"},
                  {"description", inst->part != nullptr ? inst->part->description : d.description},
                  {"properties", values},
                  {"tags", Json::array()}};
        if (inst->parent != nullptr) a["parent"] = asset_id(r, *inst->parent);
        assets.push_back(std::move(a));
    }
    Json relationships = Json::array();
    for (const auto& inst : r.instances()) {
        if (inst->external) continue;
        for (const Relationship* rel : inst->type->relationships) {
            const Instance* from = r.part(*inst, rel->from);
            const Instance* to = r.part(*inst, rel->to);
            if (from == nullptr || to == nullptr) continue;
            relationships.push_back({{"id", inst->path.empty() ? rel->id : inst->path + "__" + rel->id},
                                     {"source", asset_id(r, *from)},
                                     {"type", rel->type},
                                     {"target", asset_id(r, *to)}});
        }
    }
    Json structure = {{"root", r.root() != nullptr ? asset_id(r, *r.root()) : std::string{}},
                      {"assetTypes", types}, {"assets", assets}, {"relationships", relationships}};

    // ------------------------------------------------------------------ data contract
    std::map<std::string, std::string> pt_label;  // DT channel -> PT label
    if (d.reference && r.root() != nullptr) {
        for (const auto& c : d.reference->automaton.channels) {
            const auto it = d.reference->labels.find(c.name);
            if (it == d.reference->labels.end() || it->second.internal) continue;
            const auto m = r.member(*r.root(), it->second.trigger);
            if (!m || (m->event == nullptr && m->action == nullptr)) continue;
            const std::string ch = Resolved::qualify(*m->owner, m->event != nullptr ? m->event->name : m->action->name);
            pt_label.try_emplace(ch, c.name + "!");
        }
    }
    const auto in_automaton = [&](const std::string& channel) {
        return std::any_of(b.channels.begin(), b.channels.end(), [&](const ChannelInfo& c) { return c.name == channel; });
    };
    Json telemetry = Json::array();
    Json events = Json::array();
    Json commands = Json::array();
    Json statics = Json::array();
    Json monitors = Json::array();
    Json alerts = Json::array();
    monitors.push_back({{"id", "conformance"}, {"kind", "conformance"}, {"name", "Behavioural conformance"}, {"severity", "critical"},
                        {"events", "all"}, {"unmatchedEvents", "record"}});
    alerts.push_back({{"id", "alert-conformance"}, {"monitor", "conformance"}, {"on", "violated"}, {"severity", "critical"},
                      {"message", "Observed events deviate from the verified model"}});
    for (const auto& inst : r.instances()) {
        const std::string owner = asset_id(r, *inst);
        const std::string where = inst->dotted.empty() ? d.name : inst->dotted;
        for (const Property* p : inst->type->properties) {
            const std::string sym = symbol_of(*inst, *p);
            if (p->kind == PropertyKind::PerTwin) {
                statics.push_back({{"id", sym}, {"label", label_of(p->label, p->name)}, {"type", type_name(p->type)}, {"asset", owner}, {"perInstance", true}, {"value", ""}});
                continue;
            }
            if (p->kind != PropertyKind::Measured) continue;
            Json t = {{"id", sym}, {"label", label_of(p->label, p->name)}, {"type", type_name(p->type)}, {"unit", p->unit}, {"asset", owner},
                      {"description", p->description}, {"range", Json::object()}};
            if (p->type != DataType::String) t["ontologySymbol"] = sym;
            if (p->type == DataType::Enum) t["values"] = p->values;
            if (p->min) t["range"]["min"] = *p->min;
            if (p->max) t["range"]["max"] = *p->max;
            if (p->period) {
                t["expectedPeriodMs"] = period_ms(*p->period);
                t["quality"] = {{"maxAgeMs", stale_seconds(*p->period) * 1000}};
                monitors.push_back({{"id", sym + "-fresh"}, {"kind", "data_quality"}, {"name", label_of(p->label, p->name) + " of " + where + " is fresh"},
                                    {"severity", "warning"}, {"field", sym}, {"check", "stale"}, {"maxAgeSeconds", stale_seconds(*p->period)}});
                alerts.push_back({{"id", "alert-" + sym + "-fresh"}, {"monitor", sym + "-fresh"}, {"on", "finding"}, {"severity", "warning"},
                                  {"message", label_of(p->label, p->name) + " of " + where + " is stale"}});
            }
            if (p->min || p->max) {
                Json m = {{"id", sym + "-range"}, {"kind", "data_quality"}, {"name", label_of(p->label, p->name) + " of " + where + " is plausible"},
                          {"severity", "warning"}, {"field", sym}, {"check", "out_of_range"}};
                if (p->min) m["min"] = *p->min;
                if (p->max) m["max"] = *p->max;
                monitors.push_back(std::move(m));
            }
            telemetry.push_back(std::move(t));
        }
        for (const Event* e : inst->type->events) {
            const std::string ch = Resolved::qualify(*inst, e->name);
            Json x = {{"id", ch}, {"label", label_of(e->label, e->name)}, {"asset", owner}, {"description", e->description}, {"payload", Json::array()},
                      {"origin", e->origin == EventOrigin::Condition ? "condition" : "reported"}};
            Json formal = Json::object();
            if (in_automaton(ch)) formal["dt"] = ch + std::string(1, e->direction);
            if (const auto it = pt_label.find(ch); it != pt_label.end()) formal["pt"] = it->second;
            x["formal"] = formal;
            if (e->origin == EventOrigin::Condition) {
                const auto when = expr::condition(e->when, r.resolver(*inst));
                const auto rearm = expr::condition(e->rearm.empty() ? "not (" + e->when + ")" : e->rearm, r.resolver(*inst));
                if (!when.error && !rearm.error) x["condition"] = {{"when", when.smt}, {"rearm", rearm.smt}, {"symbols", when.symbols}};
            }
            events.push_back(std::move(x));
        }
        for (const Action* a : inst->type->actions) {
            const std::string ch = Resolved::qualify(*inst, a->name);
            Json x = {{"id", ch}, {"label", label_of(a->label, a->name)}, {"asset", owner}, {"parameters", Json::array()}, {"description", a->description}};
            if (in_automaton(ch)) x["formal"] = {{"dt", ch + std::string(1, a->direction)}};
            commands.push_back(std::move(x));
        }
    }
    Json data = {{"properties", statics}, {"telemetry", telemetry}, {"events", events}, {"commands", commands}};

    // ------------------------------------------------------------------ presentation
    Json states = Json::object();
    for (const LocationInfo& loc : b.locations) {
        std::string label;
        std::string summary;
        Tone tone = Tone::Normal;
        for (const auto& [inst, st] : loc.components) {
            const std::string l = label_of(st->label, st->name);
            label += (label.empty() ? "" : " · ") + (b.locations.front().components.size() > 1 && !inst->dotted.empty() ? inst->dotted + " " + l : l);
            if (!st->description.empty()) summary += (summary.empty() ? "" : " ") + st->description;
            tone = std::max(tone, st->tone);
        }
        states[loc.name] = {{"label", label}, {"tone", tone_word(tone)}, {"summary", summary}};
    }
    Json event_labels = Json::object();
    for (const ChannelInfo& c : b.channels) {
        std::string l;
        if (c.event != nullptr) l = label_of(c.event->label, c.event->name);
        else if (c.action != nullptr) l = label_of(c.action->label, c.action->name);
        else if (c.state != nullptr) l = "Time limit of " + label_of(c.state->label, c.state->name);
        if (c.owner != nullptr && !c.owner->dotted.empty()) l = c.owner->dotted + ": " + l;
        event_labels[c.name + std::string(1, c.direction)] = {{"label", l}};
    }
    Json key = Json::array();
    for (const Json& t : telemetry) {
        if (key.size() < 6) key.push_back(t["id"]);
    }
    Json presentation = {{"displayName", label_of(d.label, d.name)},
                         {"icon", nullptr},
                         {"primaryView", "status"},
                         {"plugin", nullptr},
                         {"keyTelemetry", key},
                         {"importantAssets", Json::array()},
                         {"importantPropositions", Json::array()},
                         {"importantMonitors", Json::array({"conformance"})},
                         {"importantPredictions", Json::array()},
                         {"states", states},
                         {"events", event_labels},
                         {"charts", Json::array()},
                         {"units", Json::object()},
                         {"precision", Json::object()}};

    // ------------------------------------------------------------------ requirements -> monitors
    Json requirements = Json::array();
    for (const Requirement& q : d.requirements) {
        std::string property = q.property;
        if (q.kind == "never") {
            // Every location of V_D that contains the state.
            std::vector<std::string> locs;
            for (const LocationInfo& loc : b.locations) {
                for (const auto& [inst, st] : loc.components) {
                    const std::string path = inst->dotted.empty() ? st->name : inst->dotted + "." + st->name;
                    if (path == q.state) locs.push_back(loc.name);
                }
            }
            if (locs.empty()) continue;
            std::string any;
            for (const std::string& l : locs) any += (any.empty() ? "" : " || ") + l;
            property = locs.size() == 1 ? "A[] !" + any : "A[] !(" + any + ")";
        }
        const std::string mid = "req-" + q.name;
        monitors.push_back({{"id", mid}, {"kind", "property"}, {"name", label_of(q.title, q.name)}, {"severity", q.severity}, {"property", property}, {"requirement", q.name}});
        alerts.push_back({{"id", "alert-" + q.name}, {"monitor", mid}, {"on", "violated"}, {"severity", q.severity}, {"message", label_of(q.title, q.name) + " is violated"}});
        requirements.push_back({{"id", q.name},
                                {"title", label_of(q.title, q.name)},
                                {"category", q.kind == "never" ? "safety" : "operational"},
                                {"severity", q.severity},
                                {"description", q.description},
                                {"monitors", Json::array({mid})},
                                {"formal", property}});
    }
    Json assurance = {{"format", "twin-monitors/1"}, {"requirements", requirements}, {"monitors", monitors}, {"alerts", alerts}};

    return {{"structure", structure}, {"data", data}, {"presentation", presentation}, {"assurance", assurance}};
}

}  // namespace twin::model::detail
