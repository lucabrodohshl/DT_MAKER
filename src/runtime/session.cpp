/**
 * @file session.cpp
 * @brief Write-ahead, fail-stop commit protocol around the pure kernel functions.
 */
#include "twin/runtime/session.hpp"

#include <algorithm>
#include <iterator>
#include <random>

#include "twin/kernel/semantics.hpp"

namespace twin::runtime {
namespace {

std::string random_session_id() {
    std::random_device rd;
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string out;
    for (int word = 0; word < 4; ++word) {
        const std::uint32_t w = rd();
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHex[(w >> static_cast<unsigned>(shift)) & 0xFU]);
        }
    }
    return out;
}

}  // namespace

Result<std::unique_ptr<TwinSession>> TwinSession::start(package::LoadedPackage package,
                                                       const SessionOptions& options) {
    Result<std::shared_ptr<const kernel::Model>> model = kernel::Model::create(package.model);
    if (!model) {
        return std::move(model).error();
    }
    Result<kernel::Configuration> init = kernel::initial_configuration(*model.value());
    if (!init) {
        return std::move(init).error();
    }
    std::string session_id = options.session_id;
    if (session_id.empty()) {
        session_id = options.deterministic ? "session-" + package.package_hash.substr(0, 16) : random_session_id();
    }
    ledger::Identity identity{session_id, package.package_hash, package.ir_sha256, package.manifest.model_id,
                              package.manifest.model_version};
    Result<std::unique_ptr<ledger::LedgerWriter>> writer = ledger::LedgerWriter::create(
        options.ledger_path, identity,
        ledger::WriterOptions{.fsync_each_record = options.fsync,
                              .deterministic = options.deterministic,
                              .fail_after_records = options.ledger_fault_after_records});
    if (!writer) {
        return std::move(writer).error();
    }
    kernel::StateSet initial = kernel::StateSet::of(std::move(init).value());
    std::unique_ptr<TwinSession> session(new TwinSession(std::move(package), model.value(), initial,  // NOLINT
                                                         std::move(writer).value(), std::move(session_id)));
    std::lock_guard lock(session->mutex_);
    Result<ledger::Receipt> genesis =
        session->record(ledger::kind::kGenesis, ledger::genesis_fields(*session->model_, initial));
    if (!genesis) {
        return std::move(genesis).error();
    }
    return session;
}

Result<ledger::Receipt> TwinSession::record(std::string_view kind, json::Json fields) {
    Result<ledger::Receipt> receipt = ledger_->append(kind, std::move(fields));
    if (!receipt) {
        failed_ = true;  // fail-stop: never continue without a durable record
    }
    return receipt;
}

Result<SubmitResult> TwinSession::submit(const ledger::Input& input) {
    std::unique_lock lock(mutex_);
    if (failed_ || closed_) {
        return make_error(ErrorCode::Unavailable,
                          failed_ ? "session failed (ledger unavailable); no further inputs are accepted"
                                  : "session is closed");
    }
    SubmitResult result;
    std::optional<ledger::Receipt> receipt;
    std::string kind;
    Result<ledger::InputEffect> effect = ledger::evaluate_input(*model_, state_, input);  // pure
    if (effect) {
        const bool is_step = effect.value().outcome.has_value();
        kind = is_step ? ledger::kind::kStep : ledger::kind::kDelay;
        json::Json fields = is_step ? ledger::step_fields(*model_, input, *effect.value().outcome)
                                    : ledger::delay_fields(*model_, input, state_, effect.value().after);
        Result<ledger::Receipt> r = record(kind, std::move(fields));  // write-ahead
        if (!r) return std::move(r).error();                           // fail-stop: nothing committed
        state_ = effect.value().after;                                 // commit (kernel-computed value)
        result.accepted = true;
        result.outcome = std::move(effect).value().outcome;
        receipt = std::move(r).value();
    } else {
        result.rejection = effect.error();
    }
    if (!result.accepted) {
        Result<ledger::Receipt> r =
            record(ledger::kind::kReject, ledger::reject_fields(*model_, input, *result.rejection, state_));
        if (!r) return std::move(r).error();
        receipt = std::move(r).value();
        kind = ledger::kind::kReject;
    }
    result.ledger_seq = receipt->seq;
    result.ledger_hash = receipt->hash;
    lock.unlock();
    notify(kind, *receipt);
    return result;
}

Result<SubmitResult> TwinSession::raise_alarm(std::string_view alarm, std::string_view detail) {
    std::unique_lock lock(mutex_);
    if (failed_ || closed_) {
        return make_error(ErrorCode::Unavailable, "session is not running");
    }
    Result<ledger::Receipt> r = record(ledger::kind::kAlarm, ledger::alarm_fields(*model_, alarm, detail, state_));
    if (!r) return std::move(r).error();
    SubmitResult result{true, r.value().seq, r.value().hash, std::nullopt, std::nullopt};
    lock.unlock();
    notify(ledger::kind::kAlarm, r.value());
    return result;
}

Result<SubmitResult> TwinSession::close(std::string_view reason) {
    std::unique_lock lock(mutex_);
    if (failed_ || closed_) {
        return make_error(ErrorCode::Unavailable, "session is not running");
    }
    Result<ledger::Receipt> r = record(ledger::kind::kEnd, ledger::end_fields(*model_, reason, state_));
    if (!r) return std::move(r).error();
    closed_ = true;
    SubmitResult result{true, r.value().seq, r.value().hash, std::nullopt, std::nullopt};
    lock.unlock();
    notify(ledger::kind::kEnd, r.value());
    return result;
}

Snapshot TwinSession::snapshot() const {
    std::lock_guard lock(mutex_);
    Snapshot s{state_, {}, {}, std::nullopt, ledger_->next_seq(), ledger_->head_hash(), failed_, closed_};
    bool first = true;
    const auto& members = state_.members();
    for (std::size_t i = 0; i < members.size(); ++i) {
        std::vector<ir::PropositionIndex> here = kernel::propositions(*model_, members[i]);
        if (first) {
            s.certain = here;
            first = false;
        } else {
            std::vector<ir::PropositionIndex> both;
            std::set_intersection(s.certain.begin(), s.certain.end(), here.begin(), here.end(),
                                  std::back_inserter(both));
            s.certain = std::move(both);
        }
        for (ir::TransitionIndex e : model_->outgoing(members[i].location)) {
            if (std::optional<kernel::DelayWindow> w = kernel::enabling_window(*model_, members[i], e)) {
                s.enabled.push_back(EnabledInfo{static_cast<std::uint32_t>(i), e, *w, w->earliest == 0});
            }
        }
        if (std::optional<Ticks> d = kernel::max_delay(*model_, members[i])) {
            const Ticks deadline = members[i].time + *d;
            s.deadline = s.deadline ? std::max(*s.deadline, deadline) : deadline;
        }
    }
    return s;
}

Result<kernel::StateSet> TwinSession::projected(Ticks at) const {
    std::lock_guard lock(mutex_);
    return kernel::advance_to(*model_, state_, at);
}

std::vector<kernel::ExplorationResult> TwinSession::predict(const kernel::ExplorationLimits& limits) const {
    kernel::StateSet copy = [this] {
        std::lock_guard lock(mutex_);
        return state_;
    }();
    std::vector<kernel::ExplorationResult> out;
    for (const kernel::Configuration& c : copy.members()) {
        out.push_back(kernel::explore(*model_, c, limits));
    }
    return out;
}

Result<std::vector<kernel::ObservationOutcome>> TwinSession::simulate(
    std::span<const kernel::ScheduledObservation> schedule) const {
    kernel::StateSet copy = [this] {
        std::lock_guard lock(mutex_);
        return state_;
    }();
    return kernel::simulate(*model_, copy, schedule);
}

void TwinSession::subscribe(std::function<void(const SessionEvent&)> listener) {
    std::lock_guard lock(listener_mutex_);
    listeners_.push_back(std::move(listener));
}

void TwinSession::notify(std::string_view kind, const ledger::Receipt& receipt) {
    std::vector<std::function<void(const SessionEvent&)>> listeners;
    {
        std::lock_guard lock(listener_mutex_);
        listeners = listeners_;
    }
    const SessionEvent event{std::string(kind), receipt.line};
    for (const auto& l : listeners) {
        l(event);
    }
}

}  // namespace twin::runtime
