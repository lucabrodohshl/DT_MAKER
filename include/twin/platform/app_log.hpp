/**
 * @file app_log.hpp
 * @brief Structured application log (JSON lines) with filtering for the log viewer.
 * @ingroup platform
 *
 * Application logs are **diagnostics**. They are not formal evidence, not the
 * execution ledger and not the engineering audit; they may be rotated or
 * deleted without affecting any assurance claim.
 *
 * Each line: `{"ts","level","component","message","correlationId","executionId","assetId","fields"}`.
 * Field values whose key looks secret (password, secret, token, credential,
 * authorization, api_key, private_key) are replaced with "[redacted]" before
 * they are written, so secrets cannot reach the log viewer.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/clock.hpp"

namespace twin::platform {

/// @brief Log severity.
enum class LogLevel { Debug, Info, Warn, Error };
/// @brief "debug" / "info" / "warn" / "error".
[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

/// @brief One log entry.
struct LogEntry {
    std::string ts;                          ///< ISO 8601 UTC.
    LogLevel level{LogLevel::Info};          ///< Severity.
    std::string component;                   ///< e.g. "studio.http", "ontology.refinement".
    std::string message;                     ///< Human-readable message.
    std::optional<std::string> correlation_id;  ///< Request id.
    std::optional<std::string> execution_id; ///< Runtime execution, if any.
    std::optional<std::string> asset_id;     ///< Asset, if any.
    json::Json fields = json::Json::object();  ///< Structured details (redacted).
};

/// @brief Filter for reading.
struct LogFilter {
    std::optional<LogLevel> min_level;       ///< At least this severity.
    std::optional<std::string> component;    ///< Component prefix.
    std::optional<std::string> correlation_id;  ///< Exact.
    std::optional<std::string> execution_id; ///< Exact.
    std::optional<std::string> asset_id;     ///< Exact.
    std::optional<std::string> text;         ///< Substring of message.
    std::optional<std::string> since;        ///< ISO 8601 lower bound (inclusive).
    std::optional<std::string> until;        ///< ISO 8601 upper bound (inclusive).
    std::size_t limit{200};                  ///< Max entries (newest first).
};

/// @brief Append-only JSONL logger (thread-safe) and reader.
class AppLog {
public:
    AppLog(std::filesystem::path file, const Clock& clock);

    /// @brief Write an entry (ts is filled in; fields are redacted). Never fails the caller.
    void write(LogLevel level, std::string_view component, std::string_view message, json::Json fields = {},
               std::optional<std::string> correlation_id = std::nullopt,
               std::optional<std::string> execution_id = std::nullopt,
               std::optional<std::string> asset_id = std::nullopt);

    /// @brief Read entries matching @p filter, newest first (scans the tail of the file).
    [[nodiscard]] Result<std::vector<LogEntry>> read(const LogFilter& filter) const;

    /// @brief Redact secret-looking keys recursively (exposed for tests).
    [[nodiscard]] static json::Json redact(const json::Json& fields);

private:
    std::filesystem::path file_;
    const Clock& clock_;
    mutable std::mutex mutex_;
};

/// @brief API representation.
[[nodiscard]] json::Json to_json(const LogEntry& entry);

}  // namespace twin::platform
