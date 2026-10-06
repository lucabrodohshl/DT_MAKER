/**
 * @file executions.hpp
 * @brief Recorded executions (ledgers + telemetry logs) and the packages that can replay them.
 * @ingroup runtime
 *
 * Every twin session leaves two files in the ledger directory:
 *  - `<name>.ledger.jsonl`    — the tamper-evident execution ledger (authoritative);
 *  - `<name>.telemetry.jsonl` — the telemetry received during the session, one
 *    sample per line, each anchored to the ledger position at which it arrived
 *    (`ledger_seq` = records written so far). Telemetry is observation data,
 *    not evidence: it is not hash-chained, and nothing semantic is derived
 *    from it.
 *
 * ExecutionStore lists and reads these files. PackageRegistry maps package
 * hashes to verified package directories, so that a historical execution is
 * always verified and replayed against the EXACT package recorded in its
 * ledger — never reinterpreted with whatever package happens to be running.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/package/package.hpp"

namespace twin::runtime {

/// @brief Summary of one recorded execution.
struct ExecutionInfo {
    std::string session;              ///< Session id (from the genesis record).
    std::filesystem::path ledger;     ///< Ledger file.
    std::filesystem::path telemetry;  ///< Telemetry log (may not exist).
    std::string package_hash;         ///< Package the session executed.
    std::string model_id;             ///< Model id.
    std::string model_version;        ///< Model version.
    std::uint64_t records{0};         ///< Lines in the ledger.
    bool ended{false};                ///< The last record is an "end" record.
    std::string started_wall;         ///< Wall-clock time of the genesis record ("" if deterministic).
    std::string ended_wall;           ///< Wall-clock time of the last record.
    std::int64_t last_time{0};        ///< Logical time after the last record (ticks).
    std::string last_location;        ///< Location after the last record (first member).
};

/// @brief Encode an execution summary.
[[nodiscard]] json::Json to_json(const ExecutionInfo& info);

/// @brief Telemetry log path belonging to a ledger path.
[[nodiscard]] std::filesystem::path telemetry_path_for(const std::filesystem::path& ledger);

/// @brief Read access to the recorded executions in a ledger directory.
class ExecutionStore {
public:
    /// @brief Store over the ledgers in @p ledger_dir.
    explicit ExecutionStore(std::filesystem::path ledger_dir) : dir_(std::move(ledger_dir)) {}

    /// @brief All executions, newest first (by file modification time, then name).
    [[nodiscard]] std::vector<ExecutionInfo> list() const;
    /// @brief One execution by session id (or by ledger file name).
    [[nodiscard]] Result<ExecutionInfo> find(std::string_view session) const;
    /// @brief The ledger text of an execution.
    [[nodiscard]] Result<std::string> ledger_text(const ExecutionInfo& info) const;
    /**
     * @brief Recorded telemetry, evenly thinned to at most @p max_samples
     * (the first and last samples are always kept).
     */
    [[nodiscard]] Result<json::Json> telemetry(const ExecutionInfo& info, std::size_t max_samples) const;

    /// @brief The ledger directory.
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return dir_; }

private:
    std::filesystem::path dir_;
};

/**
 * @brief Verified packages by hash (the running package plus a package store).
 *
 * Packages found in the store directory are verified on first use with the
 * same load_and_verify() the runtime uses at start-up; a package that does not
 * verify is never used.
 */
class PackageRegistry {
public:
    /// @brief Registry seeded with the running package and an optional store of *.twinpkg dirs.
    PackageRegistry(const package::LoadedPackage& running, std::vector<std::filesystem::path> store_dirs);

    /// @brief The verified package with this manifest hash, or NotFound/IntegrityError.
    [[nodiscard]] Result<package::LoadedPackage> get(const std::string& package_hash);

private:
    std::mutex mutex_;
    std::map<std::string, package::LoadedPackage> loaded_;
    std::vector<std::filesystem::path> store_dirs_;
};

}  // namespace twin::runtime
