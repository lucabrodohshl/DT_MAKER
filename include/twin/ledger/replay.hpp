/**
 * @file replay.hpp
 * @brief Deterministic replay: verified package + ledger -> re-executed semantics.
 * @ingroup ledger
 *
 * Replay first verifies the ledger's chain and that it belongs to the package,
 * then re-executes every recorded input with a fresh kernel instance created
 * from the package's IR and recomputes each record's semantic fields with the
 * same record builders the runtime uses (record.hpp). The replay is
 * *identical* iff every recomputed record equals the recorded one: same
 * accepted steps, same transitions and guard evaluations, same rejections
 * with the same error codes, same states and propositions.
 *
 * Because the kernel functions are pure and logical time is part of the
 * input, replay is deterministic: it does not depend on wall-clock time,
 * thread scheduling or the machine.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/ledger/verifier.hpp"
#include "twin/package/package.hpp"

namespace twin::ledger {

/// @brief A record whose recomputation differs from the recorded value.
struct ReplayMismatch {
    std::uint64_t seq{0};   ///< Sequence number of the record.
    std::string kind;       ///< Record kind.
    std::string detail;     ///< What differs.
};

/// @brief Result of a replay.
struct ReplayReport {
    VerificationReport chain;              ///< Chain verification performed first.
    bool identical{false};                 ///< Every record reproduced exactly.
    std::uint64_t steps{0};                ///< Accepted observations re-executed.
    std::uint64_t delays{0};               ///< Accepted delays re-executed.
    std::uint64_t rejections{0};           ///< Rejections reproduced.
    std::uint64_t alarms{0};               ///< Alarms checked.
    std::vector<ReplayMismatch> mismatches;  ///< Differences (empty iff identical).
    json::Json final_state;                ///< State after the last record.
};

/// @brief Encode a replay report.
[[nodiscard]] json::Json to_json(const ReplayReport& report);

/// @brief Replay ledger text against a verified package.
[[nodiscard]] Result<ReplayReport> replay_text(const package::LoadedPackage& package, std::string_view text);

/// @brief Replay a ledger file against a verified package.
[[nodiscard]] Result<ReplayReport> replay_file(const package::LoadedPackage& package,
                                               const std::filesystem::path& ledger);

}  // namespace twin::ledger
