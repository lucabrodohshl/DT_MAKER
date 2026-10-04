/**
 * @file views.cpp
 * @brief JSON renderings of kernel snapshots, packages and submissions.
 */
#include "twin/runtime/views.hpp"

#include "twin/kernel/explore.hpp"

namespace twin::runtime {
namespace {

using json::Json;

Json configuration_view(const kernel::Model& m, const kernel::Configuration& c) {
    Json clocks = Json::object();
    for (std::size_t i = 0; i < c.clocks.size(); ++i) {
        clocks[m.ir().clocks[i]] = time_view(c.clocks[i], m.time_base());
    }
    return Json{{"location", m.ir().locations.at(c.location).id},
                {"clocks", clocks},
                {"time", time_view(c.time, m.time_base())}};
}

Json window_view(const kernel::DelayWindow& w, TimeBase base) {
    return Json{{"earliest", time_view(w.earliest, base)},
                {"latest", w.latest ? time_view(*w.latest, base) : Json()}};
}

}  // namespace

Json time_view(Ticks t, TimeBase base) { return Json{{"ticks", t}, {"text", format_time(t, base)}}; }

Json enabled_view(const TwinSession& session, const Snapshot& snapshot) {
    const kernel::Model& m = session.model();
    Json arr = Json::array();
    for (const EnabledInfo& e : snapshot.enabled) {
        const ir::Transition& t = m.transition(e.transition);
        arr.push_back(Json{{"member", e.member},
                           {"transition", t.id},
                           {"label", t.action.label()},
                           {"source", m.ir().locations.at(t.source).id},
                           {"target", m.ir().locations.at(t.target).id},
                           {"guard", ir::to_string(m.ir(), t.guard)},
                           {"window", window_view(e.window, m.time_base())},
                           {"enabled_now", e.enabled_now}});
    }
    return arr;
}

Json propositions_view(const TwinSession& session, const Snapshot& snapshot) {
    const kernel::Model& m = session.model();
    Json arr = Json::array();
    for (ir::PropositionIndex p : snapshot.certain) {
        const ir::Proposition& prop = m.ir().propositions.at(p);
        arr.push_back(Json{{"id", prop.id}, {"interpretation", prop.interpretation}});
    }
    return arr;
}

Json state_view(const TwinSession& session, const Snapshot& snapshot) {
    const kernel::Model& m = session.model();
    Json configs = Json::array();
    for (const kernel::Configuration& c : snapshot.state.members()) {
        configs.push_back(configuration_view(m, c));
    }
    return Json{{"time", time_view(snapshot.state.time(), m.time_base())},
                {"configurations", configs},
                {"deterministic", snapshot.state.is_singleton()},
                {"propositions", propositions_view(session, snapshot)},
                {"enabled", enabled_view(session, snapshot)},
                {"deadline", snapshot.deadline ? time_view(*snapshot.deadline, m.time_base()) : Json()},
                {"ledger", {{"records", snapshot.ledger_records}, {"head", snapshot.ledger_head}}},
                {"failed", snapshot.failed},
                {"closed", snapshot.closed},
                {"session", session.session_id()}};
}

Json package_view(const package::LoadedPackage& p) {
    Json checks = Json::array();
    for (const package::Check& c : p.checks) {
        checks.push_back(Json{{"name", c.name}, {"passed", c.passed}});
    }
    const Json& ev = p.alignment_evidence;
    return Json{{"package_hash", p.package_hash},
                {"directory", p.directory.string()},
                {"manifest", package::to_json(p.manifest)},
                {"ir_sha256", p.ir_sha256},
                {"source_sha256", p.source_sha256},
                {"checks", checks},
                {"alignment",
                 {{"aligned", ev.value("verdict", Json::object()).value("aligned", false)},
                  {"lint_clean", ev.value("lint", Json::object()).value("clean", false)},
                  {"verdict", ev.value("verdict", Json::object())},
                  {"label_equivalence", ev.value("label_equivalence", Json::array())},
                  {"location_consistency", ev.value("location_consistency", Json::array())},
                  {"aligner", ev.value("aligner", Json::object())},
                  {"syntactic_baseline", ev.value("syntactic_baseline", Json::object())}}}};
}

Json submission_view(const TwinSession& session, const ledger::Input& input, const SubmitResult& r) {
    const kernel::Model& m = session.model();
    Json v{{"source", input.source},
           {"label", input.name},
           {"kind", input.kind == ledger::InputKind::Advance ? "advance" : "event"},
           {"at", time_view(input.at, m.time_base())},
           {"accepted", r.accepted},
           {"ledger_seq", r.ledger_seq},
           {"ledger_hash", r.ledger_hash}};
    if (r.outcome && !r.outcome->branches.empty()) {
        const ir::Transition& t = m.transition(r.outcome->branches.front().transition);
        v["transition"] = t.id;
        v["from"] = m.ir().locations.at(t.source).id;
        v["to"] = m.ir().locations.at(t.target).id;
    }
    if (r.rejection) {
        Json context = Json::object();
        for (const auto& [k, val] : r.rejection->context) context[k] = val;
        v["error"] = Json{{"code", std::string(to_string(r.rejection->code))},
                          {"message", r.rejection->message},
                          {"context", context}};
    }
    return v;
}

Json prediction_view(const TwinSession& session, const std::vector<kernel::ExplorationResult>& results) {
    const kernel::Model& m = session.model();
    Json out = Json::array();
    for (const kernel::ExplorationResult& r : results) {
        Json trajectories = Json::array();
        for (std::uint32_t n = 1; n < r.nodes.size(); ++n) {
            Json steps = Json::array();
            for (const kernel::TrajectoryStep& s : kernel::trajectory_to(r, n)) {
                const ir::Transition& t = m.transition(s.transition);
                steps.push_back(Json{{"transition", t.id},
                                     {"label", t.action.label()},
                                     {"window", window_view(s.window, m.time_base())},
                                     {"state", configuration_view(m, s.after)}});
            }
            trajectories.push_back(steps);
        }
        out.push_back(Json{{"root", configuration_view(m, r.nodes.front().config)},
                           {"nodes", r.nodes.size()},
                           {"truncated", r.truncated},
                           {"trajectories", trajectories}});
    }
    return out;
}

Result<ledger::Input> input_from_request(const Json& req, TimeBase base, bool advance) {
    if (!req.is_object()) return make_error(ErrorCode::InvalidArgument, "request body must be a JSON object");
    ledger::Input in;
    in.source = req.value("source", std::string("api"));
    in.payload = Json{{"via", "runtime API"}};
    if (req.contains("ticks")) {
        if (!req.at("ticks").is_number_integer()) {
            return make_error(ErrorCode::InvalidArgument, "'ticks' must be an integer");
        }
        in.at = req.at("ticks").get<Ticks>();
    } else if (req.contains("time")) {
        if (!req.at("time").is_string()) {
            return make_error(ErrorCode::InvalidArgument,
                              "'time' must be a decimal string such as \"31.5\" (floating-point JSON numbers "
                              "are not exact and are rejected)");
        }
        Result<Ticks> t = parse_time(req.at("time").get<std::string>(), base);
        if (!t) return std::move(t).error();
        in.at = t.value();
    } else {
        return make_error(ErrorCode::InvalidArgument, "missing 'time' (decimal string) or 'ticks' (integer)");
    }
    if (advance) {
        in.kind = ledger::InputKind::Advance;
        return in;
    }
    if (req.contains("label") && req.at("label").is_string()) {
        in.kind = ledger::InputKind::Label;
        in.name = req.at("label").get<std::string>();
    } else if (req.contains("transition") && req.at("transition").is_string()) {
        in.kind = ledger::InputKind::Transition;
        in.name = req.at("transition").get<std::string>();
    } else {
        return make_error(ErrorCode::InvalidArgument, "missing 'label' or 'transition'");
    }
    return in;
}

}  // namespace twin::runtime
