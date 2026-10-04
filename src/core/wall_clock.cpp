/**
 * @file wall_clock.cpp
 * @brief ISO 8601 UTC timestamps for provenance records.
 */
#include "twin/core/wall_clock.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>

namespace twin {
namespace {

std::string format_utc(std::time_t t, const char* pattern) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::array<char, 40> buf{};
    const std::size_t n = std::strftime(buf.data(), buf.size(), pattern, &tm);
    return std::string(buf.data(), n);
}

}  // namespace

std::string build_timestamp_utc() {
    std::time_t t = std::time(nullptr);
    if (const char* epoch = std::getenv("SOURCE_DATE_EPOCH")) {  // NOLINT(concurrency-mt-unsafe)
        t = static_cast<std::time_t>(std::strtoll(epoch, nullptr, 10));
    }
    return format_utc(t, "%Y-%m-%dT%H:%M:%SZ");
}

std::string now_utc_millis() {
    const auto now = std::chrono::system_clock::now();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::string out = format_utc(std::chrono::system_clock::to_time_t(now), "%Y-%m-%dT%H:%M:%S");
    std::string ms = std::to_string(millis);
    ms.insert(0, 3 - ms.size(), '0');
    return out + "." + ms + "Z";
}

}  // namespace twin
