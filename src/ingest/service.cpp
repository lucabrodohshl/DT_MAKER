/**
 * @file service.cpp
 * @brief The plant connector's core (see service.hpp).
 */
#include "twin/ingest/service.hpp"

#include <chrono>
#include <cmath>

#include "twin/core/wall_clock.hpp"

namespace twin::ingest {

using json::Json;

namespace {

constexpr std::size_t kRecent = 30;          // entries kept for the connection page
constexpr double kStateRefreshSeconds = 5.0; // the twin's state is re-read at least this often

Json error_json(const Error& e) { return Json{{"code", std::string(to_string(e.code))}, {"message", e.message}}; }

}  // namespace

Result<std::unique_ptr<IngestService>> IngestService::create(Mapping mapping, ServiceOptions options, RuntimeApi& runtime, WallClock wall) {
    const auto tps = ticks_per_second(options.ticks_per_unit, options.time_unit);
    if (!tps) {
        return make_error(ErrorCode::InvalidArgument, "unknown time base")
            .with("ticksPerUnit", std::to_string(options.ticks_per_unit))
            .with("unit", options.time_unit);
    }
    if (!wall) {
        wall = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    }
    std::unique_ptr<IngestService> s(  // NOLINT(cppcoreguidelines-owning-memory): private constructor
        new IngestService(std::move(mapping), std::move(options), runtime, std::move(wall), *tps));
    {
        std::lock_guard l(s->mu_);
        (void)s->refresh_runtime(s->wall_());  // the runtime may still be starting: tick() retries
    }
    return s;
}

IngestService::IngestService(Mapping mapping, ServiceOptions options, RuntimeApi& runtime, WallClock wall, double tps)
    : mapping_(std::move(mapping)), options_(std::move(options)), runtime_(runtime), wall_(std::move(wall)), clock_(tps),
      started_at_(now_utc_millis()) {}

Status IngestService::refresh_runtime(double wall) {
    auto st = runtime_.state();
    if (!st) {
        if (runtime_error_ != st.error().message) note("error", "twin runtime: " + st.error().message);
        runtime_error_ = st.error().message;
        return st.error();
    }
    runtime_error_.clear();
    runtime_state_ = st.value();
    state_read_at_ = wall;
    state_stale_ = false;
    if (!connected_) {
        clock_.reset(st.value().ticks, wall);
        connected_ = true;
        note("info", "connected to the twin runtime at t = " + time_text(st.value().ticks));
    } else {
        clock_.observe(st.value().ticks, wall);  // another writer may have moved the twin on
    }
    return ok_status();
}

void IngestService::note(const std::string& kind, const std::string& text) {
    recent_.push_front(Json{{"at", now_utc_millis()}, {"kind", kind}, {"text", text}});
    while (recent_.size() > kRecent) recent_.pop_back();
    if (options_.log) options_.log(kind + ": " + text);
}

std::string IngestService::time_text(Ticks t) const {
    const std::int64_t tpu = options_.ticks_per_unit;
    const bool negative = t < 0;
    const Ticks a = negative ? -t : t;
    std::string out = (negative ? "-" : "") + std::to_string(a / tpu);
    Ticks frac = a % tpu;
    if (frac == 0) return out;
    int digits = 0;
    for (std::int64_t p = 1; p < tpu; p *= 10) ++digits;
    std::int64_t pow10 = 1;
    for (int i = 0; i < digits; ++i) pow10 *= 10;
    if (pow10 != tpu) {  // not a decimal resolution: approximate for display only
        std::string d = std::to_string(static_cast<double>(frac) / static_cast<double>(tpu)).substr(1);
        while (!d.empty() && d.back() == '0') d.pop_back();
        return out + d;
    }
    std::string f = std::to_string(frac);
    f.insert(0, static_cast<std::size_t>(digits) - f.size(), '0');
    while (!f.empty() && f.back() == '0') f.pop_back();
    return out + "." + f;
}

Json IngestService::time_json(Ticks t) const { return Json{{"ticks", t}, {"text", time_text(t)}}; }

Json IngestService::handle(const Json& message, const Origin& origin) {
    std::lock_guard l(mu_);
    const double now = wall_();
    if (!connected_) {
        if (auto st = refresh_runtime(now); !st) {
            ++counters_.failed;
            return Json{{"error", {{"code", "runtime_unavailable"}, {"message", "the twin runtime is not reachable: " + st.error().message}}}};
        }
    }
    const Translation tr = translate(mapping_, message, origin.topic);
    ++counters_.messages;
    last_message_ = Json{{"at", now_utc_millis()}, {"transport", origin.transport}, {"client", origin.client}};
    if (!origin.topic.empty()) last_message_["topic"] = origin.topic;
    for (const std::string& code : tr.unmapped) {
        ++counters_.unmapped;
        note("unmapped", "event code '" + code + "' matches no event of the twin; not sent");
    }
    for (const std::string& p : tr.problems) {
        ++counters_.problems;
        note("problem", p);
    }
    // Events are stamped against the twin's exact current time (never before it: a time
    // regression would be recorded as a rejection).
    if (!tr.events.empty()) {
        if (auto st = refresh_runtime(now); !st) {
            ++counters_.failed;
            return Json{{"error", {{"code", "runtime_unavailable"}, {"message", "the twin runtime is not reachable: " + st.error().message}}}};
        }
    }
    const LogicalClock::Stamp stamp = tr.source_seconds ? clock_.from_source(*tr.source_seconds, now) : clock_.from_arrival(now);
    if (stamp.reanchored) note("info", "the plant clock jumped back; its timestamps are placed from t = " + time_text(stamp.ticks) + " on");
    if (stamp.late) {
        ++counters_.late;
        if (stamp.late_seconds >= 1.0) {
            note("late", "a message " + std::to_string(std::lround(stamp.late_seconds)) + " s older than the previous one was placed at t = " +
                             time_text(stamp.ticks) + " (logical time never goes back)");
        }
    }
    Json out{{"at", time_json(stamp.ticks)},
             {"telemetry", 0},
             {"events", Json::array()},
             {"unmapped", tr.unmapped},
             {"problems", tr.problems},
             {"late", stamp.late}};
    last_input_wall_ = now;
    if (!tr.telemetry.empty()) {
        if (auto r = runtime_.telemetry(stamp.ticks, tr.telemetry); !r) {
            ++counters_.failed;
            runtime_error_ = r.error().message;
            note("error", "telemetry not delivered: " + r.error().message);
            out["error"] = Json{{"code", "runtime_unavailable"}, {"message", r.error().message}};
            return out;
        }
        counters_.telemetry += tr.telemetry.size();
        out["telemetry"] = tr.telemetry.size();
    }
    for (const std::string& label : tr.events) {
        ++counters_.events;
        Json detail{{"transport", origin.transport}, {"client", origin.client}};
        if (!origin.topic.empty()) detail["topic"] = origin.topic;
        Json e{{"pt", label}};
        auto v = runtime_.pt_event(label, stamp.ticks, detail);
        if (!v) {
            ++counters_.failed;
            e["accepted"] = false;
            e["error"] = error_json(v.error());
            note("error", "event '" + label + "' was not submitted: " + v.error().message);
        } else {
            const Json& view = v.value();
            const bool accepted = view.value("accepted", false);
            e["label"] = view.value("label", std::string());
            e["accepted"] = accepted;
            for (const char* k : {"from", "to", "error", "ledger_seq"}) {
                if (view.contains(k)) e[k] = view.at(k);
            }
            if (accepted) {
                ++counters_.accepted;
                state_stale_ = true;          // the location (and its deadline) may have changed
                missed_deadline_.reset();     // the twin moved on: deadlines are watched again
                if (runtime_state_) runtime_state_->ticks = stamp.ticks;
            } else {
                ++counters_.rejected;
                note("rejected", "event '" + label + "' at t = " + time_text(stamp.ticks) + " was rejected by the twin: " +
                                     view.value("error", Json::object()).value("message", std::string("not allowed in this state")));
            }
        }
        last_event_ = e;
        last_event_["at"] = time_json(stamp.ticks);
        last_event_["transport"] = origin.transport;
        out["events"].push_back(std::move(e));
    }
    return out;
}

void IngestService::tick() {
    std::lock_guard l(mu_);
    const double now = wall_();
    if (!connected_ || state_stale_ || now - state_read_at_ >= kStateRefreshSeconds) {
        if (!refresh_runtime(now)) return;
    }
    if (missed_deadline_ || !runtime_state_ || runtime_state_->failed) return;
    const Ticks grace = static_cast<Ticks>(std::llround(options_.grace_seconds * clock_.rate()));
    const Ticks target = clock_.due(now) - grace;
    if (target <= clock_.last() || target <= runtime_state_->ticks) return;
    const std::optional<Ticks> deadline = runtime_state_->deadline;
    const bool past_deadline = deadline && target > *deadline;
    const bool sync = options_.sync_seconds > 0 && now - std::max(last_input_wall_, last_advance_wall_) >= options_.sync_seconds;
    if (!past_deadline && !sync) return;
    auto v = runtime_.advance(target);
    ++counters_.advances;
    if (!v) {
        runtime_error_ = v.error().message;
        note("error", "could not advance the twin's time: " + v.error().message);
        return;
    }
    last_advance_wall_ = now;
    if (v.value().value("accepted", false)) {
        clock_.raise_floor(target);
        runtime_state_->ticks = target;
        state_stale_ = true;
        return;
    }
    const Json err = v.value().value("error", Json::object());
    if (err.value("code", std::string()) == "time_regression") {  // the twin moved on: resynchronise
        state_stale_ = true;
        return;
    }
    missed_deadline_ = Json{{"deadline", deadline ? time_json(*deadline) : Json(nullptr)},
                            {"at", time_json(target)},
                            {"detectedAt", now_utc_millis()},
                            {"message", err.value("message", std::string())},
                            {"ledgerSeq", v.value().value("ledger_seq", Json(nullptr))}};
    note("deadline", "missed deadline: the plant did not act by t = " + (deadline ? time_text(*deadline) : std::string("?")) +
                         "; the twin recorded the violation at t = " + time_text(target));
}

Json IngestService::status() const {
    std::lock_guard l(mu_);
    Json recent = Json::array();
    for (const Json& r : recent_) recent.push_back(r);
    const std::string mode = mapping_.timestamp.field.empty()
                                 ? std::string("arrival time")
                                 : "plant timestamps (" + mapping_.timestamp.field + ", " + mapping_.timestamp.unit + ")";
    return Json{{"startedAt", started_at_},
                {"runtime",
                 {{"connected", connected_ && runtime_error_.empty()},
                  {"time", runtime_state_ ? time_json(runtime_state_->ticks) : Json(nullptr)},
                  {"deadline", runtime_state_ && runtime_state_->deadline ? time_json(*runtime_state_->deadline) : Json(nullptr)},
                  {"lastError", runtime_error_.empty() ? Json(nullptr) : Json(runtime_error_)}}},
                {"clock",
                 {{"last", time_json(clock_.last())},
                  {"timestamps", mode},
                  {"syncSeconds", options_.sync_seconds},
                  {"graceSeconds", options_.grace_seconds}}},
                {"counters",
                 {{"messages", counters_.messages},
                  {"telemetrySamples", counters_.telemetry},
                  {"events", counters_.events},
                  {"eventsAccepted", counters_.accepted},
                  {"eventsRejected", counters_.rejected},
                  {"notDelivered", counters_.failed},
                  {"unmappedCodes", counters_.unmapped},
                  {"problems", counters_.problems},
                  {"late", counters_.late},
                  {"timeAdvances", counters_.advances}}},
                {"lastMessage", last_message_.is_null() ? Json(nullptr) : last_message_},
                {"lastEvent", last_event_.is_null() ? Json(nullptr) : last_event_},
                {"missedDeadline", missed_deadline_ ? *missed_deadline_ : Json(nullptr)},
                {"recent", recent}};
}

}  // namespace twin::ingest
