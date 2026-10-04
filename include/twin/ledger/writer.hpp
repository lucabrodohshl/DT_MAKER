/**
 * @file writer.hpp
 * @brief Append-only, hash-chained ledger writer.
 * @ingroup ledger
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/ledger/record.hpp"

namespace twin::ledger {

/// @brief Writer options.
struct WriterOptions {
    bool fsync_each_record{true};  ///< Durability: fsync after every record.
    bool deterministic{false};     ///< Omit wall-clock times (byte-reproducible ledgers for tests).
    /// Fault injection for tests of the fail-stop behaviour: every append after
    /// this many records fails as if the storage were unavailable.
    std::optional<std::uint64_t> fail_after_records;
};

/// @brief Confirmation of an appended record.
struct Receipt {
    std::uint64_t seq{0};   ///< Sequence number of the record.
    std::string hash;       ///< Its chain hash.
    json::Json line;        ///< The full line object {"body", "hash"}.
};

/**
 * @brief Writes a new ledger file, one canonical JSON line per record.
 *
 * The file is created exclusively (an existing ledger is never overwritten),
 * opened in append mode, and each record is written with a single write call
 * followed (by default) by fsync. If any write fails the writer enters a
 * failed state and refuses further appends — the runtime then fails stop
 * (it never continues executing without recording).
 *
 * Not thread-safe; the session serialises all appends.
 */
class LedgerWriter {
public:
    /// @brief Create a new ledger at @p path (fails if the file exists).
    [[nodiscard]] static Result<std::unique_ptr<LedgerWriter>> create(const std::filesystem::path& path,
                                                                      Identity identity,
                                                                      WriterOptions options = {});
    ~LedgerWriter();
    LedgerWriter(const LedgerWriter&) = delete;
    LedgerWriter& operator=(const LedgerWriter&) = delete;
    LedgerWriter(LedgerWriter&&) = delete;
    LedgerWriter& operator=(LedgerWriter&&) = delete;

    /**
     * @brief Append a record of kind @p kind with kind-specific @p fields.
     *
     * The writer adds the chain fields (schema, session, seq, kind, prev_hash,
     * package, kernel_version, wall_time); @p fields must not contain them.
     * @return the receipt, or ErrorCode::Unavailable if the record could not be
     *         made durable (the writer is then permanently failed).
     */
    [[nodiscard]] Result<Receipt> append(std::string_view kind, json::Json fields);

    /// @brief Hash of the last record (or the all-zero hash if empty).
    [[nodiscard]] const std::string& head_hash() const noexcept { return head_hash_; }
    /// @brief Sequence number the next record will get.
    [[nodiscard]] std::uint64_t next_seq() const noexcept { return next_seq_; }
    /// @brief Identity stamped into every record.
    [[nodiscard]] const Identity& identity() const noexcept { return identity_; }
    /// @brief Ledger file path.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    /// @brief True after a failed write (no further appends possible).
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    LedgerWriter(int fd, std::filesystem::path path, Identity identity, WriterOptions options)
        : fd_(fd), path_(std::move(path)), identity_(std::move(identity)), options_(options) {}

    int fd_{-1};
    std::filesystem::path path_;
    Identity identity_;
    WriterOptions options_;
    std::string head_hash_{genesis_prev_hash()};
    std::uint64_t next_seq_{0};
    bool failed_{false};
};

}  // namespace twin::ledger
