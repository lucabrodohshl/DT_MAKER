/**
 * @file session.hpp
 * @brief TwinSession: the single mutation path of the Digital Twin's semantic state.
 * @ingroup runtime
 *
 * @defgroup runtime Production runtime (K_prod)
 * @brief The production shell around the semantic kernel.
 *
 * TwinSession owns the authoritative semantic state of a running twin (a
 * kernel::StateSet; a singleton for event-deterministic models) and the
 * execution ledger. It enforces the three architectural properties the
 * correctness proof relies on (proof/sections/07-production-bisimulation.tex):
 *
 *  - **Semantic isolation**: the state is private; every other component
 *    (adapters, API, planner, mission controller, UI) only receives copies.
 *  - **Single semantic authority**: the state changes only to values computed
 *    by the kernel's transition functions (kernel::observe / kernel::advance_to).
 *  - **Write-ahead, fail-stop**: a successor is computed, then recorded in the
 *    ledger, and only then committed. If the record cannot be made durable the
 *    session enters the failed state and refuses all further inputs: the twin
 *    may stop, but it never runs ahead of its ledger.
 *
 * Every input that reaches submit() is recorded — accepted steps as "step" or
 * "delay" records, refused inputs as "reject" records (state unchanged).
 *
 * Thread-safe: all members serialise on an internal mutex; listeners are
 * invoked after commit, outside the lock.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/kernel/explore.hpp"
#include "twin/kernel/model.hpp"
#include "twin/kernel/state_set.hpp"
#include "twin/ledger/record.hpp"
#include "twin/ledger/writer.hpp"
#include "twin/package/package.hpp"

namespace twin::runtime {

/// @brief Session start-up options.
struct SessionOptions {
    std::filesystem::path ledger_path;  ///< New ledger file (must not exist).
    std::string session_id;             ///< Session id (generated if empty).
    bool deterministic{false};          ///< No wall-clock metadata in the ledger.
    bool fsync{true};                   ///< fsync every ledger record.
    /// Fault injection (tests only): the ledger fails after this many records.
    std::optional<std::uint64_t> ledger_fault_after_records;
};

/// @brief Outcome of submit(): accepted or semantically rejected (both are recorded).
struct SubmitResult {
    bool accepted{false};                               ///< The kernel admitted the input.
    std::uint64_t ledger_seq{0};                        ///< Sequence number of the record.
    std::string ledger_hash;                            ///< Chain hash of the record.
    std::optional<kernel::ObservationOutcome> outcome;  ///< For accepted label/transition inputs.
    std::optional<Error> rejection;                     ///< For rejected inputs.
};

/// @brief An outgoing transition of a possible configuration, for APIs.
struct EnabledInfo {
    std::uint32_t member{0};              ///< Index of the configuration in the state set.
    ir::TransitionIndex transition{0};    ///< Transition.
    kernel::DelayWindow window;           ///< Admissible delays from the committed time.
    bool enabled_now{false};              ///< Admissible without delay.
};

/**
 * @brief Monitoring summary, aggregated by the session from the kernel's verdicts.
 *
 * Inputs from the mission controller or an operator are the twin's own
 * *decisions*; every other input (PT adapter, external API) is an
 * *observation* of the physical side. The twin is **conformant** while every
 * observation has been explained by the verified model and no deadline alarm
 * has been raised. Nothing here is computed outside the kernel: the counters
 * only count the kernel's accept/reject verdicts and the recorded alarms.
 */
struct MonitoringSummary {
    std::uint64_t observations{0};           ///< Observations submitted.
    std::uint64_t observations_rejected{0};  ///< ... that the model could not explain.
    std::uint64_t decisions{0};              ///< Decisions submitted.
    std::uint64_t decisions_rejected{0};     ///< ... refused by the kernel.
    std::uint64_t alarms{0};                 ///< Monitoring alarms recorded.
    std::optional<std::uint64_t> first_violation_seq;  ///< Ledger seq of the first violation.
    std::string first_violation;             ///< Its description.
    /// @brief True while no observation was rejected and no alarm was raised.
    [[nodiscard]] bool conformant() const noexcept { return observations_rejected == 0 && alarms == 0; }
};

/// @brief The last committed discrete transition (for observers).
struct LastTransition {
    std::uint64_t seq{0};       ///< Ledger record.
    std::string transition;     ///< Transition id.
    std::string label;          ///< Action label.
    std::string source;         ///< Source location.
    std::string target;         ///< Target location.
    std::string input_source;   ///< Who submitted the input.
    Ticks at{0};                ///< Logical time of the step.
};

/// @brief True for inputs that are the twin's own decisions (not observations of the PT).
[[nodiscard]] bool is_decision_source(std::string_view source) noexcept;

/// @brief A consistent copy of the session state for queries.
struct Snapshot {
    kernel::StateSet state;                       ///< Committed semantic state.
    std::vector<ir::PropositionIndex> certain;    ///< Propositions holding in all members.
    std::vector<EnabledInfo> enabled;             ///< Outgoing transitions with windows.
    std::optional<Ticks> deadline;                ///< Latest admissible logical time (invariant).
    std::uint64_t ledger_records{0};              ///< Records written so far.
    std::string ledger_head;                      ///< Hash of the last record.
    bool failed{false};                           ///< Fail-stop state.
    bool closed{false};                           ///< Session ended.
    MonitoringSummary monitoring;                 ///< Conformance counters.
    std::optional<LastTransition> last_transition;  ///< Most recent discrete step.
};

/// @brief Notification after every committed record.
struct SessionEvent {
    std::string kind;     ///< Record kind ("step", "reject", ...).
    json::Json record;    ///< The full ledger line {"body", "hash"}.
};

/// @brief The authoritative execution of one verified package (see file documentation).
class TwinSession {
public:
    /// @brief Start a session: initial state from the kernel, genesis record in a new ledger.
    [[nodiscard]] static Result<std::unique_ptr<TwinSession>> start(package::LoadedPackage package,
                                                                    const SessionOptions& options);

    ~TwinSession() = default;
    TwinSession(const TwinSession&) = delete;
    TwinSession& operator=(const TwinSession&) = delete;
    TwinSession(TwinSession&&) = delete;
    TwinSession& operator=(TwinSession&&) = delete;

    /**
     * @brief Submit a semantic input (observation, internal transition or time advance).
     * @return SubmitResult (accepted or rejected — both recorded), or
     *         ErrorCode::Unavailable if the session is failed/closed or the ledger
     *         write failed (fail-stop).
     */
    [[nodiscard]] Result<SubmitResult> submit(const ledger::Input& input);

    /// @brief Record a monitoring alarm (no state change).
    [[nodiscard]] Result<SubmitResult> raise_alarm(std::string_view alarm, std::string_view detail);

    /**
     * @brief Record non-semantic context (map knowledge, a planning episode, a command).
     *
     * Context is chained into the ledger in order with the semantic records so
     * that audit and replay can show what the twin knew and did; the semantic
     * state is unchanged. Like every record, a failed write makes the session
     * fail-stop. @p data must be canonical-safe (no floating-point numbers).
     */
    [[nodiscard]] Result<SubmitResult> record_context(std::string_view topic, Ticks at, const json::Json& data);

    /// @brief End the session with an "end" record; further inputs are refused.
    [[nodiscard]] Result<SubmitResult> close(std::string_view reason);

    /// @brief Consistent copy of the state with derived kernel queries.
    [[nodiscard]] Snapshot snapshot() const;

    /**
     * @brief The state projected to logical time @p at (pure query on a copy).
     * @return the delayed state, or the kernel's refusal (e.g. a missed deadline).
     */
    [[nodiscard]] Result<kernel::StateSet> projected(Ticks at) const;

    /// @brief Bounded exploration from every possible configuration (on copies).
    [[nodiscard]] std::vector<kernel::ExplorationResult> predict(const kernel::ExplorationLimits& limits) const;

    /// @brief Simulate a candidate schedule from the committed state (on a copy).
    [[nodiscard]] Result<std::vector<kernel::ObservationOutcome>> simulate(
        std::span<const kernel::ScheduledObservation> schedule) const;

    /// @brief Register a listener called after each committed record.
    void subscribe(std::function<void(const SessionEvent&)> listener);

    /// @brief The executed model.
    [[nodiscard]] const kernel::Model& model() const noexcept { return *model_; }
    /// @brief The verified package.
    [[nodiscard]] const package::LoadedPackage& package() const noexcept { return package_; }
    /// @brief Session id.
    [[nodiscard]] const std::string& session_id() const noexcept { return session_id_; }
    /// @brief Ledger path.
    [[nodiscard]] const std::filesystem::path& ledger_path() const noexcept { return ledger_->path(); }
    /// @brief Number of records written so far (cheap; for anchoring telemetry to the ledger).
    [[nodiscard]] std::uint64_t record_count() const;

private:
    TwinSession(package::LoadedPackage package, std::shared_ptr<const kernel::Model> model,
                kernel::StateSet initial, std::unique_ptr<ledger::LedgerWriter> ledger, std::string session_id)
        : package_(std::move(package)),
          model_(std::move(model)),
          state_(std::move(initial)),
          ledger_(std::move(ledger)),
          session_id_(std::move(session_id)) {}

    Result<ledger::Receipt> record(std::string_view kind, json::Json fields);
    void notify(std::string_view kind, const ledger::Receipt& receipt);

    package::LoadedPackage package_;
    std::shared_ptr<const kernel::Model> model_;
    kernel::StateSet state_;  ///< The authoritative semantic state. Assigned only from kernel results.
    std::unique_ptr<ledger::LedgerWriter> ledger_;
    std::string session_id_;
    bool failed_{false};
    bool closed_{false};
    MonitoringSummary monitoring_;
    std::optional<LastTransition> last_transition_;
    mutable std::mutex mutex_;
    std::mutex listener_mutex_;
    std::vector<std::function<void(const SessionEvent&)>> listeners_;
};

}  // namespace twin::runtime
