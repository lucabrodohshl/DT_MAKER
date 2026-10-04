/**
 * @file cosim_driver.cpp
 * @brief Deterministic co-simulation tick and wall-clock pacing.
 */
#include "twin/runtime/cosim_driver.hpp"

#include <algorithm>
#include <chrono>

#include "twin/core/wall_clock.hpp"
#include "twin/runtime/views.hpp"

namespace twin::runtime {
namespace {

PtAdapter must_adapter(const package::LoadedPackage& p) {
    Result<PtAdapter> a = PtAdapter::from_evidence(p.alignment_evidence);
    if (!a) {
        throw std::runtime_error("cannot build the PT adapter: " + a.error().to_string());
    }
    return std::move(a).value();
}

}  // namespace

const char* to_string(SimStatus status) noexcept {
    switch (status) {
        case SimStatus::Paused: return "paused";
        case SimStatus::Running: return "running";
        case SimStatus::Finished: return "finished";
        case SimStatus::Failed: return "failed";
    }
    return "paused";
}

CoSimDriver::CoSimDriver(package::LoadedPackage package, std::unique_ptr<WorldPort> world, EventHub& hub,
                         DriverConfig config)
    : package_(std::move(package)),
      world_(std::move(world)),
      hub_(hub),
      config_(std::move(config)),
      adapter_(must_adapter(package_)),
      speed_(config_.speed) {}

CoSimDriver::~CoSimDriver() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

Status CoSimDriver::reset() {
    std::lock_guard lock(mutex_);
    status_ = SimStatus::Paused;
    if (Status s = world_->reset(); !s) return s;
    Result<json::Json> known = world_->known_map();
    if (!known) return std::move(known).error();
    if (Status s = world_model_.reset(known.value()); !s) return s;
    Result<json::Json> mission_json = world_->mission();
    if (!mission_json) return std::move(mission_json).error();
    Result<Mission> mission = mission_from_json(mission_json.value());
    if (!mission) return std::move(mission).error();

    ++session_counter_;
    SessionOptions options;
    options.deterministic = config_.deterministic;
    const std::string stamp = config_.deterministic ? std::string("deterministic") : now_utc_millis();
    std::string name = "session-" + std::to_string(session_counter_) + "-" + stamp + ".ledger.jsonl";
    for (char& c : name) {
        if (c == ':') c = '-';
    }
    options.ledger_path = config_.ledger_dir / name;
    if (config_.deterministic) {
        std::error_code ec;
        std::filesystem::remove(options.ledger_path, ec);
    }
    Result<std::unique_ptr<TwinSession>> session = TwinSession::start(package_, options);
    if (!session) return std::move(session).error();
    session_ = std::shared_ptr<TwinSession>(std::move(session).value());
    session_->subscribe([this](const SessionEvent& e) {
        const json::Json& body = e.record.at("body");
        json::Json summary{{"seq", body.value("seq", 0)},
                           {"kind", e.kind},
                           {"time_after", body.value("time_after", Ticks{0})},
                           {"prev_hash", body.value("prev_hash", std::string())},
                           {"hash", e.record.value("hash", std::string())}};
        if (body.contains("outcome") && !body.at("outcome").at("branches").empty()) {
            const json::Json& b = body.at("outcome").at("branches").at(0);
            summary["transition"] = b.value("transition", std::string());
            summary["label"] = b.value("label", std::string());
            summary["from"] = b.value("source", std::string());
            summary["to"] = b.value("target", std::string());
        }
        if (body.contains("input")) summary["input"] = body.at("input").value("name", std::string());
        if (body.contains("error")) summary["error"] = body.at("error").value("code", std::string());
        if (body.contains("alarm")) summary["alarm"] = body.at("alarm");
        hub_.publish("ledger", summary);
    });
    controller_ = std::make_unique<MissionController>(*session_, planner_, config_.controller);
    controller_->reset(std::move(mission).value(), &world_model_);
    if (config_.autostart) controller_->request_start();
    telemetry_ = json::Json::object();
    now_ = 0;
    ticks_ = 0;
    alarmed_deadline_.reset();
    last_error_.clear();
    hub_.publish("sim", json::Json{{"status", "reset"}, {"session", session_->session_id()},
                                    {"ledger", session_->ledger_path().string()}});
    hub_.publish("map_reset", json::Json{{"map", geo::to_json(world_model_.map())}, {"seq", world_model_.last_seq()}});
    publish_state(*session_);
    return ok_status();
}

void CoSimDriver::publish_state(const TwinSession& session) {
    hub_.publish("state", state_view(session, session.snapshot()));
}

Status CoSimDriver::tick() {
    std::lock_guard lock(mutex_);
    return tick_locked();
}

Status CoSimDriver::tick_locked() {
    if (!session_ || !controller_) return make_error(ErrorCode::StateError, "no session; reset first");
    if (status_ == SimStatus::Finished || status_ == SimStatus::Failed) return ok_status();
    send_commands();                                     // 1
    Result<json::Json> stepped = step_physical_twin();   // 2
    if (!stepped) return fail(stepped.error());
    if (Status s = observe_pt_events(stepped.value()); !s) return fail(s.error());  // 3
    apply_map_updates();                                 // 4
    controller_->decide(now_);                           // 5
    for (json::Json& e : controller_->take_events()) {
        const std::string type = e.value("type", std::string("event"));
        hub_.publish(type, std::move(e));
    }
    check_deadline();                                    // 6
    if (ticks_ % 2 == 0 || controller_->finished()) publish_state(*session_);
    if (controller_->finished()) {
        status_ = SimStatus::Finished;
        (void)session_->close("mission finished");
        hub_.publish("sim", json::Json{{"status", "finished"}, {"at", now_}});
        publish_state(*session_);
    }
    return ok_status();
}

Status CoSimDriver::fail(const Error& error) {
    status_ = SimStatus::Failed;
    last_error_ = error.to_string();
    hub_.publish("sim", json::Json{{"status", "failed"}, {"error", last_error_}});
    return error;
}

void CoSimDriver::send_commands() {
    for (const json::Json& c : controller_->take_commands()) {
        Status s = world_->command(c);
        hub_.publish("command", json::Json{{"command", c}, {"accepted", s.ok()},
                                           {"error", s.ok() ? std::string() : s.error().message}});
    }
}

Result<json::Json> CoSimDriver::step_physical_twin() {
    Result<json::Json> stepped = world_->step(config_.step);
    if (!stepped) return stepped;
    ++ticks_;
    telemetry_ = stepped.value().at("telemetry");
    now_ = telemetry_.value("at", now_);
    controller_->on_telemetry(telemetry_);
    hub_.publish("telemetry", telemetry_);
    for (const json::Json& line : stepped.value().value("facility_log", json::Json::array())) {
        hub_.publish("facility", json::Json{{"at", now_}, {"text", line}});
    }
    return stepped;
}

Status CoSimDriver::observe_pt_events(const json::Json& stepped) {
    for (const json::Json& e : stepped.value("events", json::Json::array())) {
        const Translation t = adapter_.translate(e);
        hub_.publish("pt_event", json::Json{{"at", e.value("at", now_)},
                                            {"pt_label", t.pt_label},
                                            {"dt_label", t.input ? t.input->name : std::string()},
                                            {"note", t.note},
                                            {"detail", e.value("detail", json::Json::object())}});
        if (!t.input) continue;
        Result<SubmitResult> r = session_->submit(*t.input);  // the kernel decides; the ledger records
        if (!r) return r.error();
        hub_.publish("observation", submission_view(*session_, *t.input, r.value()));
        if (r.value().accepted) controller_->on_accepted(t.input->name, t.input->at);
    }
    return ok_status();
}

void CoSimDriver::apply_map_updates() {
    Result<json::Json> updates = world_->updates_since(world_model_.last_seq());
    if (!updates) return;
    for (const json::Json& uj : updates.value().value("updates", json::Json::array())) {
        Result<geo::MapUpdate> u = geo::map_update_from_json(uj);
        if (!u) continue;
        const std::vector<geo::CellChange> changed = world_model_.apply(u.value());
        if (changed.empty()) continue;
        json::Json payload = geo::to_json(u.value());
        payload["unknown_cells"] = static_cast<std::int64_t>(world_model_.unknown_cells());
        hub_.publish("map", payload);
        controller_->on_map_changed(changed);
    }
}

void CoSimDriver::check_deadline() {
    Result<kernel::StateSet> projected = session_->projected(now_);
    if (projected) return;
    const Snapshot snap = session_->snapshot();
    if (snap.deadline && alarmed_deadline_ != snap.deadline && !snap.closed) {
        alarmed_deadline_ = snap.deadline;
        (void)session_->raise_alarm("deadline_missed", projected.error().to_string());
        hub_.publish("alarm", json::Json{{"at", now_}, {"text", "deadline missed: " + projected.error().message}});
    }
}

void CoSimDriver::launch() {
    thread_ = std::thread([this] { loop(); });
}

void CoSimDriver::loop() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (!stop_) {
        bool running = false;
        {
            std::lock_guard lock(mutex_);
            running = status_ == SimStatus::Running;
            if (running) (void)tick_locked();
        }
        const double logical_s = static_cast<double>(config_.step) / 1000.0;
        const double wall_s = running ? logical_s / std::max(0.05, speed_.load()) : 0.05;
        next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(wall_s));
        if (next < clock::now()) next = clock::now();
        std::this_thread::sleep_until(next);
    }
}

void CoSimDriver::play() {
    std::lock_guard lock(mutex_);
    if (status_ == SimStatus::Paused) status_ = SimStatus::Running;
    hub_.publish("sim", json::Json{{"status", to_string(status_)}});
}

void CoSimDriver::pause() {
    std::lock_guard lock(mutex_);
    if (status_ == SimStatus::Running) status_ = SimStatus::Paused;
    hub_.publish("sim", json::Json{{"status", to_string(status_)}});
}

void CoSimDriver::set_speed(double logical_per_wall) {
    speed_ = std::clamp(logical_per_wall, 0.1, 50.0);
    hub_.publish("sim", json::Json{{"speed_permille", static_cast<std::int64_t>(speed_.load() * 1000)}});
}

std::shared_ptr<TwinSession> CoSimDriver::session() const {
    std::lock_guard lock(mutex_);
    return session_;
}

json::Json CoSimDriver::status() const {
    std::lock_guard lock(mutex_);
    return json::Json{{"status", to_string(status_)},
                      {"speed_permille", static_cast<std::int64_t>(speed_.load() * 1000)},
                      {"now", time_view(now_, package_.model.time)},
                      {"ticks", ticks_},
                      {"session", session_ ? session_->session_id() : std::string()},
                      {"ledger", session_ ? session_->ledger_path().string() : std::string()},
                      {"error", last_error_}};
}

json::Json CoSimDriver::known_world() const {
    std::lock_guard lock(mutex_);
    return json::Json{{"map", geo::to_json(world_model_.map())},
                      {"seq", world_model_.last_seq()},
                      {"unknown_cells", static_cast<std::int64_t>(world_model_.unknown_cells())}};
}

json::Json CoSimDriver::mission() const {
    std::lock_guard lock(mutex_);
    return controller_ ? controller_->status() : json::Json::object();
}

json::Json CoSimDriver::plans() const {
    std::lock_guard lock(mutex_);
    return controller_ ? controller_->plans() : json::Json::object();
}

json::Json CoSimDriver::telemetry() const {
    std::lock_guard lock(mutex_);
    return telemetry_;
}

}  // namespace twin::runtime
