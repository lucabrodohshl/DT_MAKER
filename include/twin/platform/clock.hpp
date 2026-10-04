/**
 * @file clock.hpp
 * @brief Wall-clock time for engineering records (injectable for deterministic tests).
 * @ingroup platform
 *
 * This is **wall-clock** time (creation, publication, deployment and
 * observation timestamps). It is unrelated to the kernel's logical model time,
 * which Studio never computes. All timestamps are UTC, rendered as ISO 8601
 * with millisecond precision ("2026-10-04T09:15:02.250Z").
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "twin/core/result.hpp"

namespace twin::platform {

/// @brief Source of wall-clock time in milliseconds since the Unix epoch (UTC).
class Clock {
public:
    virtual ~Clock() = default;
    /// @brief Current time in ms since 1970-01-01T00:00:00Z.
    [[nodiscard]] virtual std::int64_t now_ms() const = 0;
};

/// @brief The system clock.
class SystemClock final : public Clock {
public:
    [[nodiscard]] std::int64_t now_ms() const override;
};

/// @brief A manually driven clock for tests and deterministic demo seeding.
class ManualClock final : public Clock {
public:
    explicit ManualClock(std::int64_t start_ms) : now_(start_ms) {}
    [[nodiscard]] std::int64_t now_ms() const override { return now_; }
    /// @brief Move time forward by @p ms.
    void advance(std::int64_t ms) noexcept { now_ += ms; }
    /// @brief Set the time.
    void set(std::int64_t ms) noexcept { now_ = ms; }

private:
    std::int64_t now_;
};

/// @brief Render ms-since-epoch as "YYYY-MM-DDThh:mm:ss.mmmZ".
[[nodiscard]] std::string iso8601_utc(std::int64_t ms);

/// @brief Parse "YYYY-MM-DDThh:mm:ss[.fff]Z" (UTC only) into ms since epoch.
[[nodiscard]] Result<std::int64_t> parse_iso8601_utc(std::string_view text);

}  // namespace twin::platform
