/**
 * @file wall_clock.hpp
 * @brief Informational wall-clock timestamps (ISO 8601, UTC).
 * @ingroup core
 *
 * Wall-clock time is NEVER semantic in this system: the kernel uses logical
 * time only (see logical_time.hpp and docs/logical-time-model.md). These
 * helpers stamp provenance records (manifests, ledger metadata) and are not
 * used by twin::kernel (checked by tests/architecture).
 */
#pragma once

#include <string>

namespace twin {

/**
 * @brief Current UTC time as "YYYY-MM-DDTHH:MM:SSZ".
 *
 * If the environment variable SOURCE_DATE_EPOCH is set (reproducible builds,
 * https://reproducible-builds.org/specs/source-date-epoch/), that time is used.
 */
[[nodiscard]] std::string build_timestamp_utc();

/// @brief Current UTC time with milliseconds, "YYYY-MM-DDTHH:MM:SS.mmmZ".
[[nodiscard]] std::string now_utc_millis();

}  // namespace twin
