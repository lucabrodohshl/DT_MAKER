/**
 * @file record.cpp
 * @brief Ledger record encoding and the chain hash.
 */
#include "twin/ledger/record.hpp"

#include <algorithm>
#include <iterator>

#include "twin/core/sha256.hpp"
#include "twin/kernel/semantics.hpp"

namespace twin::ledger {
namespace {

using json::Json;

const char* kind_name(InputKind k) {
    switch (k) {
        case InputKind::Label: return "label";
        case InputKind::Transition: return "transition";
        case InputKind::Advance: return "advance";
    }
    return "label";
}

}  // namespace

Result<InputEffect> evaluate_input(const kernel::Model& model, const kernel::StateSet& state, const Input& in) {
    if (in.kind == InputKind::Advance) {
        Result<kernel::StateSet> after = kernel::advance_to(model, state, in.at);
        if (!after) return std::move(after).error();
        return InputEffect{std::move(after).value(), std::nullopt};
    }
    kernel::Selector selector;
    if (in.kind == InputKind::Label) {
        std::optional<kernel::LabelId> label = model.label_id(in.name);
        if (!label) {
            return make_error(ErrorCode::IncompatibleObservation, "the model has no transition with this label")
                .with("label", in.name);
        }
        selector = kernel::Selector::label(*label);
    } else {
        std::optional<ir::TransitionIndex> t = ir::find_transition(model.ir(), in.name);
        if (!t) {
            return make_error(ErrorCode::IncompatibleObservation, "the model has no transition with this id")
                .with("transition", in.name);
        }
        selector = kernel::Selector::transition(*t);
    }
    Result<kernel::ObservationOutcome> outcome = kernel::observe(model, state, in.at, selector);
    if (!outcome) return std::move(outcome).error();
    kernel::StateSet after = outcome.value().after;
    return InputEffect{std::move(after), std::move(outcome).value()};
}

Json encode_input(const Input& in) {
    return Json{{"source", in.source},
                {"kind", kind_name(in.kind)},
                {"name", in.name},
                {"at", in.at},
                {"payload", in.payload}};
}

Result<Input> decode_input(const Json& j) {
    if (Status s = json::expect_keys(j, {"source", "kind", "name", "at", "payload"}); !s) {
        return s.error();
    }
    Result<std::string> source = json::get_string(j, "source");
    Result<std::string> kind = json::get_string(j, "kind");
    Result<std::string> name = json::get_string(j, "name");
    Result<std::int64_t> at = json::get_int(j, "at");
    if (!source) return std::move(source).error();
    if (!kind) return std::move(kind).error();
    if (!name) return std::move(name).error();
    if (!at) return std::move(at).error();
    Input in;
    in.source = source.value();
    in.name = name.value();
    in.at = at.value();
    in.payload = j.at("payload");
    if (kind.value() == "label") {
        in.kind = InputKind::Label;
    } else if (kind.value() == "transition") {
        in.kind = InputKind::Transition;
    } else if (kind.value() == "advance") {
        in.kind = InputKind::Advance;
    } else {
        return make_error(ErrorCode::ValidationError, "unknown input kind").with("kind", kind.value());
    }
    return in;
}

Result<std::string> input_digest(const Input& input) { return json::canonical_sha256(encode_input(input)); }

Json encode_configuration(const kernel::Model& model, const kernel::Configuration& c) {
    Json clocks = Json::object();
    for (std::size_t i = 0; i < c.clocks.size() && i < model.clock_count(); ++i) {
        clocks[model.ir().clocks[i]] = c.clocks[i];
    }
    return Json{{"location", model.ir().locations.at(c.location).id}, {"clocks", clocks}, {"time", c.time}};
}

Json encode_state(const kernel::Model& model, const kernel::StateSet& states) {
    Json arr = Json::array();
    for (const kernel::Configuration& c : states.members()) {
        arr.push_back(encode_configuration(model, c));
    }
    return arr;
}

Json encode_outcome(const kernel::Model& model, const kernel::ObservationOutcome& o) {
    Json branches = Json::array();
    for (const kernel::Branch& b : o.branches) {
        const ir::Transition& t = model.transition(b.transition);
        Json guard = Json::array();
        for (const kernel::AtomEvaluation& a : kernel::explain_guard(model, o.delayed.at(b.from), b.transition)) {
            guard.push_back(Json{{"atom", ir::to_string(model.ir(), a.atom)},
                                 {"value", a.lhs_minus_rhs},
                                 {"bound", a.bound},
                                 {"holds", a.holds}});
        }
        Json resets = Json::array();
        for (ir::ClockIndex r : t.resets) {
            resets.push_back(ir::clock_name(model.ir(), r));
        }
        branches.push_back(Json{{"from_member", b.from},
                                {"transition", t.id},
                                {"label", t.action.label()},
                                {"source", model.ir().locations.at(t.source).id},
                                {"target", model.ir().locations.at(t.target).id},
                                {"guard", guard},
                                {"resets", resets},
                                {"to_member", b.to}});
    }
    return Json{{"delay", o.delay}, {"branches", branches}};
}

Json encode_propositions(const kernel::Model& model, const kernel::StateSet& states) {
    // Certain propositions: those holding in every possible configuration.
    std::vector<ir::PropositionIndex> certain;
    bool first = true;
    for (const kernel::Configuration& c : states.members()) {
        std::vector<ir::PropositionIndex> here = kernel::propositions(model, c);
        if (first) {
            certain = here;
            first = false;
        } else {
            std::vector<ir::PropositionIndex> both;
            std::set_intersection(certain.begin(), certain.end(), here.begin(), here.end(),
                                  std::back_inserter(both));
            certain = std::move(both);
        }
    }
    Json arr = Json::array();
    for (ir::PropositionIndex p : certain) {
        arr.push_back(model.ir().propositions.at(p).id);
    }
    return arr;
}

const std::string& genesis_prev_hash() {
    static const std::string zeros(64, '0');
    return zeros;
}

Result<std::string> chain_hash(std::string_view prev_hex, std::string_view canonical_body) {
    Result<Digest> prev = digest_from_hex(prev_hex);
    if (!prev) {
        return std::move(prev).error();
    }
    Sha256 h;
    h.update(std::span<const std::uint8_t>(prev.value().data(), prev.value().size()));
    h.update(canonical_body);
    return to_hex(h.finish());
}

namespace {

Json input_block(const Input& input) {
    Result<std::string> digest = input_digest(input);
    return Json{{"input", encode_input(input)}, {"input_digest", digest ? digest.value() : std::string()}};
}

}  // namespace

Json genesis_fields(const kernel::Model& model, const kernel::StateSet& initial) {
    return Json{{"time_base", model.time_base().ticks_per_unit},
                {"time_after", initial.time()},
                {"state_after", encode_state(model, initial)},
                {"propositions", encode_propositions(model, initial)}};
}

Json step_fields(const kernel::Model& model, const Input& input, const kernel::ObservationOutcome& o) {
    Json f = input_block(input);
    f["time_before"] = o.before.time();
    f["time_after"] = o.after.time();
    f["state_before"] = encode_state(model, o.before);
    f["outcome"] = encode_outcome(model, o);
    f["state_after"] = encode_state(model, o.after);
    f["propositions"] = encode_propositions(model, o.after);
    return f;
}

Json delay_fields(const kernel::Model& model, const Input& input, const kernel::StateSet& before,
                  const kernel::StateSet& after) {
    Json f = input_block(input);
    f["time_before"] = before.time();
    f["time_after"] = after.time();
    f["state_before"] = encode_state(model, before);
    f["state_after"] = encode_state(model, after);
    f["propositions"] = encode_propositions(model, after);
    return f;
}

Json reject_fields(const kernel::Model& model, const Input& input, const Error& error,
                   const kernel::StateSet& state) {
    Json context = Json::object();
    for (const auto& [k, v] : error.context) {
        context[k] = v;  // last value wins for repeated keys (informational)
    }
    Json f = input_block(input);
    f["error"] = Json{{"code", std::string(to_string(error.code))}, {"message", error.message}, {"context", context}};
    f["time_after"] = state.time();
    f["state_after"] = encode_state(model, state);
    return f;
}

Json alarm_fields(const kernel::Model& model, std::string_view alarm, std::string_view detail,
                  const kernel::StateSet& state) {
    return Json{{"alarm", std::string(alarm)},
                {"detail", std::string(detail)},
                {"time_after", state.time()},
                {"state_after", encode_state(model, state)}};
}

Json end_fields(const kernel::Model& model, std::string_view reason, const kernel::StateSet& state) {
    return Json{{"reason", std::string(reason)},
                {"time_after", state.time()},
                {"state_after", encode_state(model, state)},
                {"propositions", encode_propositions(model, state)}};
}

Json context_fields(std::string_view topic, Ticks at, const Json& data, const kernel::StateSet& state) {
    return Json{{"topic", std::string(topic)}, {"at", at}, {"data", data}, {"time_after", state.time()}};
}

Json encode_identity(const Identity& id) {
    return Json{{"hash", id.package_hash},
                {"ir_sha256", id.ir_sha256},
                {"model_id", id.model_id},
                {"model_version", id.model_version}};
}

}  // namespace twin::ledger
