/**
 * @file record.hpp
 * @brief Ledger record vocabulary: inputs, state encoding, hash chaining.
 * @ingroup ledger
 *
 * @defgroup ledger Tamper-evident execution ledger
 * @brief Append-only, hash-chained record of every semantic decision.
 *
 * The ledger is a JSON Lines file. Each line is the canonical JSON object
 * `{"body": <record>, "hash": <hex>}` where
 *
 *     hash_n = SHA-256( digest(hash_{n-1}) || canonical(body_n) ),
 *     hash_{-1} = 32 zero bytes.
 *
 * Every body carries: schema, session id, sequence number, kind, the previous
 * hash, the package identity (package hash, IR hash, model id/version), the
 * kernel version, and an optional informational wall-clock time. Kind-specific
 * members record the input (in full, plus its digest), the logical times, the
 * semantic state before and after, the transition(s) taken with their guard
 * evaluations and resets, and the propositions after the step.
 *
 * "context" records carry no semantic decision: they chain the information the
 * runtime acted on (map knowledge, planning episodes with their candidates and
 * verdicts, commands sent to the Physical Twin) into the same evidence, in
 * order with the semantic steps, so that an auditor can see *why* a decision
 * event was proposed and a replay can reconstruct what the twin knew.
 *
 * This makes the file *cryptographically tamper-evident*: modification,
 * reordering, insertion and deletion of records break the chain or the
 * sequence numbers; truncation of the tail is detectable against an anchor
 * (an externally kept {seq, hash} pair) or by the absence of the "end" record.
 * The file is NOT immutable — immutability would require WORM storage; the
 * ledger only makes changes detectable.
 *
 * The ledger never influences semantics: it records what the kernel decided.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/kernel/model.hpp"
#include "twin/kernel/state_set.hpp"

namespace twin::ledger {

/// @brief Record kinds.
namespace kind {
inline constexpr std::string_view kGenesis = "genesis";  ///< Session start: identity + initial state.
inline constexpr std::string_view kStep = "step";        ///< Accepted observation (delay + discrete step).
inline constexpr std::string_view kDelay = "delay";      ///< Accepted pure passage of time.
inline constexpr std::string_view kReject = "reject";    ///< Rejected input (state unchanged).
inline constexpr std::string_view kAlarm = "alarm";      ///< Monitoring alarm (e.g. missed deadline).
inline constexpr std::string_view kEnd = "end";          ///< Orderly end of the session.
/// Non-semantic context for audit and replay (what the twin knew, planned and commanded:
/// map knowledge, planning episodes, commands). Chained like every record; state unchanged.
inline constexpr std::string_view kContext = "context";
}  // namespace kind

/// @brief What an input asks the kernel to do.
enum class InputKind {
    Label,       ///< Observe action label `name` at `at`.
    Transition,  ///< Observe/fire transition id `name` at `at`.
    Advance      ///< Let logical time pass until `at`.
};

/**
 * @brief A semantic input, exactly as submitted to the session.
 *
 * Inputs are what replay re-executes; `payload` keeps the adapter's original
 * message (e.g. the telemetry sample that triggered the observation).
 */
struct Input {
    std::string source;   ///< Producer: "pt-adapter", "env-adapter", "mission", "api", ...
    InputKind kind{InputKind::Label};  ///< Kind of request.
    std::string name;     ///< Label ("waypoint_reached!") or transition id; empty for Advance.
    Ticks at{0};          ///< Logical timestamp (ticks).
    json::Json payload = json::Json::object();  ///< Original message (informational, hashed).
};

/// @brief The semantic effect of an input, as computed by the kernel.
struct InputEffect {
    kernel::StateSet after;                             ///< State after the input.
    std::optional<kernel::ObservationOutcome> outcome;  ///< Set for label/transition inputs.
};

/**
 * @brief The meaning of an input: the single definition shared by the runtime
 * session and replay (pure; @p state is not modified).
 *
 * - Label: kernel::observe with the action label (unknown labels are an
 *   IncompatibleObservation: the model cannot explain them);
 * - Transition: kernel::observe with the transition id;
 * - Advance: kernel::advance_to.
 */
[[nodiscard]] Result<InputEffect> evaluate_input(const kernel::Model& model, const kernel::StateSet& state,
                                                 const Input& input);

/// @brief Encode an input (canonical-safe JSON).
[[nodiscard]] json::Json encode_input(const Input& input);
/// @brief Decode an input.
[[nodiscard]] Result<Input> decode_input(const json::Json& j);
/// @brief SHA-256 of the canonical encoding of an input.
[[nodiscard]] Result<std::string> input_digest(const Input& input);

/// @brief Encode one configuration with names: {"location", "clocks": {name: ticks}, "time"}.
[[nodiscard]] json::Json encode_configuration(const kernel::Model& model, const kernel::Configuration& c);
/// @brief Encode a state set as an array of configurations (canonical order).
[[nodiscard]] json::Json encode_state(const kernel::Model& model, const kernel::StateSet& states);
/// @brief Encode a monitored step: delay, branches (with guard evaluations and resets), state after.
[[nodiscard]] json::Json encode_outcome(const kernel::Model& model, const kernel::ObservationOutcome& outcome);
/// @brief Encode the certain propositions of a state set (ids).
[[nodiscard]] json::Json encode_propositions(const kernel::Model& model, const kernel::StateSet& states);

/// @brief The all-zero predecessor hash of the first record.
[[nodiscard]] const std::string& genesis_prev_hash();

/**
 * @brief Chain hash: SHA-256(digest(prev_hex) || canonical_body).
 * @return ParseError if @p prev_hex is not a hex digest.
 */
[[nodiscard]] Result<std::string> chain_hash(std::string_view prev_hex, std::string_view canonical_body);

/**
 * @name Record builders (kind-specific fields)
 * The runtime session writes records with these functions and replay recomputes
 * them with the same functions, so "replay reproduces the execution" is checked
 * as plain equality of recorded and recomputed fields.
 * @{
 */
/// @brief Genesis: time base, initial state and its propositions.
[[nodiscard]] json::Json genesis_fields(const kernel::Model& model, const kernel::StateSet& initial);
/// @brief Accepted observation (delay + discrete step).
[[nodiscard]] json::Json step_fields(const kernel::Model& model, const Input& input,
                                     const kernel::ObservationOutcome& outcome);
/// @brief Accepted pure passage of time.
[[nodiscard]] json::Json delay_fields(const kernel::Model& model, const Input& input,
                                      const kernel::StateSet& before, const kernel::StateSet& after);
/// @brief Rejected input; the state is unchanged.
[[nodiscard]] json::Json reject_fields(const kernel::Model& model, const Input& input, const Error& error,
                                       const kernel::StateSet& state);
/// @brief Monitoring alarm (no state change).
[[nodiscard]] json::Json alarm_fields(const kernel::Model& model, std::string_view alarm,
                                      std::string_view detail, const kernel::StateSet& state);
/// @brief Orderly end of the session.
[[nodiscard]] json::Json end_fields(const kernel::Model& model, std::string_view reason,
                                    const kernel::StateSet& state);
/**
 * @brief Context record (non-semantic; the state is unchanged).
 * @param topic e.g. "knowledge", "map_update", "planning", "command".
 * @param at logical time the information refers to (may be later than the state's time).
 * @param data canonical-safe JSON (integers, strings, arrays, objects).
 */
[[nodiscard]] json::Json context_fields(std::string_view topic, Ticks at, const json::Json& data,
                                        const kernel::StateSet& state);
/// @}

/// @brief Identity fields stamped into every record.
struct Identity {
    std::string session;        ///< Session id.
    std::string package_hash;   ///< SHA-256 of the package manifest.
    std::string ir_sha256;      ///< IR hash.
    std::string model_id;       ///< Model id.
    std::string model_version;  ///< Model version.
};

/// @brief Encode the identity block.
[[nodiscard]] json::Json encode_identity(const Identity& identity);

}  // namespace twin::ledger
