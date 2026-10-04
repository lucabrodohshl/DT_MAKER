/**
 * @file clock.cpp
 * @brief Wall-clock helpers (see clock.hpp).
 */
#include "twin/platform/clock.hpp"

#include <array>
#include <chrono>
#include <cstdio>

namespace twin::platform {

std::int64_t SystemClock::now_ms() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

namespace {

// Howard Hinnant's civil-from-days / days-from-civil (proleptic Gregorian, UTC).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civil_from_days(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2 ? 1 : 0;
}

bool digits(std::string_view s, std::size_t pos, std::size_t n, unsigned& out) noexcept {
    if (pos + n > s.size()) return false;
    out = 0;
    for (std::size_t i = pos; i < pos + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + static_cast<unsigned>(s[i] - '0');
    }
    return true;
}

}  // namespace

std::string iso8601_utc(std::int64_t ms) {
    std::int64_t days = ms / 86400000;
    std::int64_t rem = ms % 86400000;
    if (rem < 0) {
        rem += 86400000;
        --days;
    }
    std::int64_t y = 0;
    unsigned m = 0;
    unsigned d = 0;
    civil_from_days(days, y, m, d);
    const auto h = static_cast<int>(rem / 3600000);
    const auto mi = static_cast<int>((rem / 60000) % 60);
    const auto s = static_cast<int>((rem / 1000) % 60);
    const auto f = static_cast<int>(rem % 1000);
    std::array<char, 40> buf{};
    std::snprintf(buf.data(), buf.size(), "%04lld-%02u-%02uT%02d:%02d:%02d.%03dZ", static_cast<long long>(y), m, d, h,
                  mi, s, f);
    return buf.data();
}

Result<std::int64_t> parse_iso8601_utc(std::string_view t) {
    unsigned y = 0;
    unsigned mo = 0;
    unsigned d = 0;
    unsigned h = 0;
    unsigned mi = 0;
    unsigned s = 0;
    unsigned f = 0;
    const bool ok = t.size() >= 20 && digits(t, 0, 4, y) && t[4] == '-' && digits(t, 5, 2, mo) && t[7] == '-' &&
                    digits(t, 8, 2, d) && t[10] == 'T' && digits(t, 11, 2, h) && t[13] == ':' &&
                    digits(t, 14, 2, mi) && t[16] == ':' && digits(t, 17, 2, s);
    if (!ok || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) {
        return make_error(ErrorCode::InvalidArgument, "expected an ISO 8601 UTC timestamp like 2026-10-04T09:15:02Z")
            .with("value", std::string(t));
    }
    std::size_t pos = 19;
    if (pos < t.size() && t[pos] == '.') {
        ++pos;
        unsigned scale = 100;
        while (pos < t.size() && t[pos] >= '0' && t[pos] <= '9') {
            f += static_cast<unsigned>(t[pos] - '0') * scale;
            scale /= 10;
            ++pos;
        }
    }
    if (pos + 1 != t.size() || t[pos] != 'Z') {
        return make_error(ErrorCode::InvalidArgument, "timestamps must be UTC and end in 'Z'")
            .with("value", std::string(t));
    }
    const std::int64_t days = days_from_civil(y, mo, d);
    return days * 86400000 + static_cast<std::int64_t>(h) * 3600000 + static_cast<std::int64_t>(mi) * 60000 +
           static_cast<std::int64_t>(s) * 1000 + static_cast<std::int64_t>(f);
}

}  // namespace twin::platform
