/**
 * @file codec.cpp
 * @brief JSON <-> IR model conversion (format "twin-ir/1").
 */
#include "twin/ir/codec.hpp"

#include <utility>

#include "twin/core/sha256.hpp"
#include "twin/core/version.hpp"
#include "twin/ir/validate.hpp"

namespace twin::ir {
namespace {

using json::Json;

const char* action_kind_name(ActionKind kind) noexcept {
    switch (kind) {
        case ActionKind::Internal: return "internal";
        case ActionKind::Send: return "send";
        case ActionKind::Receive: return "receive";
    }
    return "internal";
}

Json constraint_to_json(const Model& m, const ClockConstraint& c) {
    Json j = Json::object();
    j["clock"] = clock_name(m, c.lhs);
    if (c.rhs != kReferenceClock) {
        j["minus"] = clock_name(m, c.rhs);
    }
    j["op"] = std::string(to_string(c.op));
    j["bound"] = c.bound;
    return j;
}

Json conjunction_to_json(const Model& m, const Conjunction& g) {
    Json arr = Json::array();
    for (const ClockConstraint& c : g) {
        arr.push_back(constraint_to_json(m, c));
    }
    return arr;
}

/// Decoding context: name tables built from the document before resolving references.
class Decoder {
public:
    Result<Model> decode(const Json& doc) {
        if (Status s = json::expect_keys(doc,
                                         {"format", "model", "time", "clocks", "channels",
                                          "locations", "initial", "transitions", "propositions",
                                          "event_interpretations"});
            !s) {
            return s.error();
        }
        Result<std::string> format = json::get_string(doc, "format");
        if (!format) return std::move(format).error();
        if (format.value() != version::kIrFormat) {
            return make_error(ErrorCode::ValidationError, "unsupported IR format")
                .with("format", format.value())
                .with("supported", std::string(version::kIrFormat));
        }
        if (Status s = decode_info(doc); !s) return s.error();
        if (Status s = decode_names(doc); !s) return s.error();
        if (Status s = decode_locations(doc); !s) return s.error();
        if (Status s = decode_transitions(doc); !s) return s.error();
        if (Status s = decode_propositions(doc); !s) return s.error();
        if (Status s = decode_event_interpretations(doc); !s) return s.error();
        if (Status s = validate(model_); !s) return s.error();
        return std::move(model_);
    }

private:
    Status decode_info(const Json& doc) {
        const Json& info = doc.at("model");
        if (Status s = json::expect_keys(info, {"id", "version", "source_template", "source_sha256"});
            !s)
            return s;
        Result<std::string> id = json::get_string(info, "id");
        Result<std::string> ver = json::get_string(info, "version");
        Result<std::string> tpl = json::get_string(info, "source_template");
        Result<std::string> sha = json::get_string(info, "source_sha256");
        if (!id) return id.error();
        if (!ver) return ver.error();
        if (!tpl) return tpl.error();
        if (!sha) return sha.error();
        model_.info = ModelInfo{id.value(), ver.value(), tpl.value(), sha.value()};

        const Json& time = doc.at("time");
        if (Status s = json::expect_keys(time, {"ticks_per_unit"}); !s) return s;
        Result<std::int64_t> r = json::get_int(time, "ticks_per_unit");
        if (!r) return r.error();
        model_.time = TimeBase{r.value()};
        return ok_status();
    }

    static Result<std::vector<std::string>> string_array(const Json& doc, std::string_view key) {
        Result<const Json*> arr = json::get_array(doc, key);
        if (!arr) return std::move(arr).error();
        std::vector<std::string> out;
        for (const Json& v : *arr.value()) {
            if (!v.is_string()) {
                return make_error(ErrorCode::ValidationError, "array elements must be strings")
                    .with("member", std::string(key));
            }
            out.push_back(v.get<std::string>());
        }
        return out;
    }

    Status decode_names(const Json& doc) {
        Result<std::vector<std::string>> clocks = string_array(doc, "clocks");
        if (!clocks) return clocks.error();
        model_.clocks = std::move(clocks).value();
        Result<std::vector<std::string>> channels = string_array(doc, "channels");
        if (!channels) return channels.error();
        model_.channels = std::move(channels).value();
        return ok_status();
    }

    Result<ClockIndex> clock_ref(const std::string& name) const {
        if (std::optional<ClockIndex> c = find_clock(model_, name)) return *c;
        return make_error(ErrorCode::ValidationError, "reference to an undeclared clock")
            .with("clock", name);
    }

    Result<Conjunction> decode_conjunction(const Json& arr) const {
        if (!arr.is_array()) {
            return make_error(ErrorCode::ValidationError, "a conjunction must be an array");
        }
        Conjunction g;
        for (const Json& c : arr) {
            if (Status s = json::expect_keys(c, {"clock", "op", "bound"}, {"minus"}); !s)
                return s.error();
            Result<std::string> clock = json::get_string(c, "clock");
            Result<std::string> op = json::get_string(c, "op");
            Result<std::int64_t> bound = json::get_int(c, "bound");
            if (!clock) return std::move(clock).error();
            if (!op) return std::move(op).error();
            if (!bound) return std::move(bound).error();
            Result<ClockIndex> lhs = clock_ref(clock.value());
            if (!lhs) return std::move(lhs).error();
            ClockIndex rhs = kReferenceClock;
            if (c.contains("minus")) {
                Result<std::string> minus = json::get_string(c, "minus");
                if (!minus) return std::move(minus).error();
                Result<ClockIndex> r = clock_ref(minus.value());
                if (!r) return std::move(r).error();
                rhs = r.value();
            }
            std::optional<Comparison> cmp = parse_comparison(op.value());
            if (!cmp) {
                return make_error(ErrorCode::ValidationError, "unknown comparison operator")
                    .with("op", op.value());
            }
            g.push_back(ClockConstraint{lhs.value(), rhs, *cmp, bound.value()});
        }
        return g;
    }

    Result<LocationIndex> location_ref(const Json& v) const {
        if (!v.is_string()) {
            return make_error(ErrorCode::ValidationError, "location reference must be a string");
        }
        if (std::optional<LocationIndex> l = find_location(model_, v.get<std::string>())) return *l;
        return make_error(ErrorCode::ValidationError, "reference to an unknown location")
            .with("location", v.get<std::string>());
    }

    Status decode_locations(const Json& doc) {
        Result<const Json*> arr = json::get_array(doc, "locations");
        if (!arr) return arr.error();
        for (const Json& l : *arr.value()) {
            if (Status s = json::expect_keys(l, {"id", "invariant"}, {"layout"}); !s) return s;
            Result<std::string> id = json::get_string(l, "id");
            if (!id) return id.error();
            Result<Conjunction> inv = decode_conjunction(l.at("invariant"));
            if (!inv) return inv.error();
            Location loc{id.value(), std::move(inv).value(), std::nullopt};
            if (l.contains("layout")) {
                const Json& lay = l.at("layout");
                if (Status s = json::expect_keys(lay, {"x", "y"}); !s) return s;
                Result<std::int64_t> x = json::get_int(lay, "x");
                Result<std::int64_t> y = json::get_int(lay, "y");
                if (!x) return x.error();
                if (!y) return y.error();
                loc.layout = LayoutHint{x.value(), y.value()};
            }
            model_.locations.push_back(std::move(loc));
        }
        Result<LocationIndex> init = location_ref(doc.at("initial"));
        if (!init) return init.error();
        model_.initial = init.value();
        return ok_status();
    }

    Status decode_transitions(const Json& doc) {
        Result<const Json*> arr = json::get_array(doc, "transitions");
        if (!arr) return arr.error();
        for (const Json& t : *arr.value()) {
            if (Status s = json::expect_keys(t, {"id", "source", "target", "action", "guard", "resets"});
                !s)
                return s;
            Transition tr;
            Result<std::string> id = json::get_string(t, "id");
            if (!id) return id.error();
            tr.id = id.value();
            Result<LocationIndex> src = location_ref(t.at("source"));
            Result<LocationIndex> dst = location_ref(t.at("target"));
            if (!src) return src.error();
            if (!dst) return dst.error();
            tr.source = src.value();
            tr.target = dst.value();

            const Json& a = t.at("action");
            Result<std::string> kind = json::get_string(a, "kind");
            if (!kind) return kind.error();
            if (kind.value() == "internal") {
                if (Status s = json::expect_keys(a, {"kind"}); !s) return s;
                tr.action = Action{ActionKind::Internal, ""};
            } else if (kind.value() == "send" || kind.value() == "receive") {
                if (Status s = json::expect_keys(a, {"kind", "channel"}); !s) return s;
                Result<std::string> ch = json::get_string(a, "channel");
                if (!ch) return ch.error();
                tr.action = Action{kind.value() == "send" ? ActionKind::Send : ActionKind::Receive,
                                   ch.value()};
            } else {
                return make_error(ErrorCode::ValidationError, "unknown action kind")
                    .with("kind", kind.value());
            }
            Result<Conjunction> guard = decode_conjunction(t.at("guard"));
            if (!guard) return guard.error();
            tr.guard = std::move(guard).value();
            Result<std::vector<std::string>> resets = string_array(t, "resets");
            if (!resets) return resets.error();
            for (const std::string& r : resets.value()) {
                Result<ClockIndex> c = clock_ref(r);
                if (!c) return c.error();
                tr.resets.push_back(c.value());
            }
            model_.transitions.push_back(std::move(tr));
        }
        return ok_status();
    }

    Status decode_propositions(const Json& doc) {
        Result<const Json*> arr = json::get_array(doc, "propositions");
        if (!arr) return arr.error();
        for (const Json& p : *arr.value()) {
            if (Status s = json::expect_keys(p, {"id", "location", "interpretation"}); !s) return s;
            Result<std::string> id = json::get_string(p, "id");
            Result<std::string> interp = json::get_string(p, "interpretation");
            Result<LocationIndex> loc = location_ref(p.at("location"));
            if (!id) return id.error();
            if (!interp) return interp.error();
            if (!loc) return loc.error();
            model_.propositions.push_back(Proposition{id.value(), loc.value(), interp.value()});
        }
        return ok_status();
    }

    Status decode_event_interpretations(const Json& doc) {
        Result<const Json*> arr = json::get_array(doc, "event_interpretations");
        if (!arr) return arr.error();
        for (const Json& e : *arr.value()) {
            if (Status s = json::expect_keys(e, {"label", "formula"}); !s) return s;
            Result<std::string> label = json::get_string(e, "label");
            Result<std::string> formula = json::get_string(e, "formula");
            if (!label) return label.error();
            if (!formula) return formula.error();
            model_.event_interpretations.push_back(EventInterpretation{label.value(), formula.value()});
        }
        return ok_status();
    }

    Model model_;
};

}  // namespace

json::Json to_json(const Model& m) {
    Json doc = Json::object();
    doc["format"] = std::string(version::kIrFormat);
    doc["model"] = Json{{"id", m.info.id},
                        {"version", m.info.version},
                        {"source_template", m.info.source_template},
                        {"source_sha256", m.info.source_sha256}};
    doc["time"] = Json{{"ticks_per_unit", m.time.ticks_per_unit}};
    doc["clocks"] = m.clocks;
    doc["channels"] = m.channels;

    Json locations = Json::array();
    for (const Location& l : m.locations) {
        Json jl = Json::object();
        jl["id"] = l.id;
        jl["invariant"] = conjunction_to_json(m, l.invariant);
        if (l.layout) {
            jl["layout"] = Json{{"x", l.layout->x}, {"y", l.layout->y}};
        }
        locations.push_back(std::move(jl));
    }
    doc["locations"] = std::move(locations);
    doc["initial"] = m.initial < m.locations.size() ? m.locations[m.initial].id : std::string();

    Json transitions = Json::array();
    for (const Transition& t : m.transitions) {
        Json jt = Json::object();
        jt["id"] = t.id;
        jt["source"] = t.source < m.locations.size() ? m.locations[t.source].id : std::string();
        jt["target"] = t.target < m.locations.size() ? m.locations[t.target].id : std::string();
        Json action = Json::object();
        action["kind"] = action_kind_name(t.action.kind);
        if (t.action.kind != ActionKind::Internal) {
            action["channel"] = t.action.channel;
        }
        jt["action"] = std::move(action);
        jt["guard"] = conjunction_to_json(m, t.guard);
        Json resets = Json::array();
        for (ClockIndex r : t.resets) {
            resets.push_back(clock_name(m, r));
        }
        jt["resets"] = std::move(resets);
        transitions.push_back(std::move(jt));
    }
    doc["transitions"] = std::move(transitions);

    Json props = Json::array();
    for (const Proposition& p : m.propositions) {
        props.push_back(Json{{"id", p.id},
                             {"location", p.location < m.locations.size()
                                              ? m.locations[p.location].id
                                              : std::string()},
                             {"interpretation", p.interpretation}});
    }
    doc["propositions"] = std::move(props);

    Json events = Json::array();
    for (const EventInterpretation& e : m.event_interpretations) {
        events.push_back(Json{{"label", e.label}, {"formula", e.formula}});
    }
    doc["event_interpretations"] = std::move(events);
    return doc;
}

Result<Model> from_json(const json::Json& document) { return Decoder().decode(document); }

Result<std::string> to_canonical_text(const Model& model) {
    if (Status s = validate(model); !s) {
        return s.error();
    }
    return json::canonical_dump(to_json(model));
}

Result<Model> from_canonical_text(std::string_view text) {
    Result<json::Json> doc = json::parse_canonical(text);
    if (!doc) {
        return std::move(doc).error();
    }
    Result<Model> model = from_json(doc.value());
    if (!model) {
        return model;
    }
    // Round trip: the decoded model must re-encode to exactly the input bytes.
    Result<std::string> again = to_canonical_text(model.value());
    if (!again) {
        return std::move(again).error();
    }
    if (again.value() != text) {
        return make_error(ErrorCode::IntegrityError,
                          "IR document does not round-trip through the decoder");
    }
    return model;
}

Result<std::string> ir_sha256(const Model& model) {
    Result<std::string> text = to_canonical_text(model);
    if (!text) {
        return text;
    }
    return sha256_hex(text.value());
}

}  // namespace twin::ir
