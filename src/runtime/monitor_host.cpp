/**
 * @file monitor_host.cpp
 * @brief Monitor-mode host: E-translated PT events and logged telemetry.
 */
#include "twin/runtime/monitor_host.hpp"

#include "twin/core/wall_clock.hpp"
#include "twin/runtime/executions.hpp"
#include "twin/runtime/views.hpp"

namespace twin::runtime {

Result<std::unique_ptr<MonitorHost>> MonitorHost::create(package::LoadedPackage package, EventHub& hub,
                                                         MonitorConfig config) {
    Result<PtAdapter> adapter = PtAdapter::from_evidence(package.alignment_evidence);
    if (!adapter) return std::move(adapter).error();
    std::unique_ptr<MonitorHost> host(  // NOLINT(cppcoreguidelines-owning-memory): private constructor
        new MonitorHost(std::move(package), hub, std::move(config), std::move(adapter).value()));
    if (Status s = host->reset(); !s) return s.error();
    return host;
}

MonitorHost::MonitorHost(package::LoadedPackage package, EventHub& hub, MonitorConfig config, PtAdapter adapter)
    : package_(std::move(package)), hub_(hub), config_(std::move(config)), adapter_(std::move(adapter)) {}

std::shared_ptr<TwinSession> MonitorHost::session() const {
    std::lock_guard lock(mutex_);
    return session_;
}

Status MonitorHost::reset() {
    std::lock_guard lock(mutex_);
    ++session_counter_;
    SessionOptions options;
    options.deterministic = config_.deterministic;
    const std::string stamp = config_.deterministic ? std::string("deterministic") : now_utc_millis();
    std::string name = package_.manifest.model_id + "-" + std::to_string(session_counter_) + "-" + stamp + ".ledger.jsonl";
    for (char& c : name) {
        if (c == ':') c = '-';
    }
    std::error_code ec;
    std::filesystem::create_directories(config_.ledger_dir, ec);
    options.ledger_path = config_.ledger_dir / name;
    if (config_.deterministic) std::filesystem::remove(options.ledger_path, ec);
    Result<std::unique_ptr<TwinSession>> session = TwinSession::start(package_, options);
    if (!session) return std::move(session).error();
    session_ = std::shared_ptr<TwinSession>(std::move(session).value());
    stream_ledger(*session_, hub_);
    telemetry_log_ = std::ofstream(telemetry_path_for(options.ledger_path), std::ios::trunc);
    pt_events_ = 0;
    telemetry_samples_ = 0;
    last_telemetry_ = 0;
    hub_.publish("sim", json::Json{{"status", "reset"}, {"mode", "monitor"}, {"session", session_->session_id()},
                                    {"ledger", session_->ledger_path().string()}});
    hub_.publish("state", state_view(*session_, session_->snapshot()));
    return ok_status();
}

json::Json MonitorHost::status() const {
    std::lock_guard lock(mutex_);
    const Snapshot snap = session_->snapshot();
    return json::Json{{"status", snap.closed ? "finished" : (snap.failed ? "failed" : "monitoring")},
                      {"mode", "monitor"},
                      {"now", time_view(snap.state.time(), package_.model.time)},
                      {"session", session_->session_id()},
                      {"ledger", session_->ledger_path().string()},
                      {"pt_events", pt_events_},
                      {"telemetry_samples", telemetry_samples_},
                      {"last_telemetry", time_view(last_telemetry_, package_.model.time)}};
}

Result<json::Json> MonitorHost::pt_event(const json::Json& request) {
    if (!request.is_object() || !request.contains("label") || !request.at("label").is_string()) {
        return make_error(ErrorCode::InvalidArgument, "expected {\"label\": \"<PT label>\", \"ticks\"|\"time\": ...}");
    }
    // Reuse the API's exact time parsing (integer ticks or decimal strings; never floats).
    json::Json timed = request;
    timed["label"] = "unused";
    Result<ledger::Input> parsed = input_from_request(timed, package_.model.time, false);
    if (!parsed) return std::move(parsed).error();
    const json::Json pt{{"at", parsed.value().at},
                        {"label", request.at("label")},
                        {"detail", request.value("detail", json::Json::object())}};
    const Translation t = adapter_.translate(pt);
    hub_.publish("pt_event", json::Json{{"at", parsed.value().at},
                                        {"pt_label", t.pt_label},
                                        {"dt_label", t.input ? t.input->name : std::string()},
                                        {"note", t.note},
                                        {"detail", pt.at("detail")}});
    if (!t.input) return make_error(ErrorCode::InvalidArgument, t.note).with("pt_label", t.pt_label);
    std::shared_ptr<TwinSession> s = session();
    Result<SubmitResult> r = s->submit(*t.input);  // the kernel decides; the ledger records
    if (!r) return std::move(r).error();
    {
        std::lock_guard lock(mutex_);
        ++pt_events_;
    }
    json::Json view = submission_view(*s, *t.input, r.value());
    view["pt_label"] = t.pt_label;
    hub_.publish("observation", view);
    hub_.publish("state", state_view(*s, s->snapshot()));
    return view;
}

Status MonitorHost::telemetry(const json::Json& sample) {
    if (!sample.is_object() || !sample.contains("at") || !sample.at("at").is_number_integer()) {
        return make_error(ErrorCode::InvalidArgument, "telemetry needs an integer logical timestamp 'at' (ticks)");
    }
    std::shared_ptr<TwinSession> s = session();
    json::Json line = sample;
    line["ledger_seq"] = s->record_count();
    {
        std::lock_guard lock(mutex_);
        if (telemetry_log_) {
            telemetry_log_ << line.dump() << '\n';
            telemetry_log_.flush();
        }
        ++telemetry_samples_;
        last_telemetry_ = sample.at("at").get<Ticks>();
    }
    hub_.publish("telemetry", sample);
    return ok_status();
}

}  // namespace twin::runtime
