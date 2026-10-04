/**
 * @file app_log.cpp
 * @brief Structured application log (see app_log.hpp).
 */
#include "twin/platform/app_log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <fstream>

namespace twin::platform {

namespace {

constexpr std::array<std::string_view, 9> kSecretMarkers = {
    "password", "secret", "token", "credential", "authorization", "api_key", "apikey", "private_key", "cookie"};

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool secret_key(std::string_view key) {
    const std::string k = lower(key);
    return std::any_of(kSecretMarkers.begin(), kSecretMarkers.end(),
                       [&](std::string_view m) { return k.find(m) != std::string::npos; });
}

int rank(LogLevel l) { return static_cast<int>(l); }

Result<LogLevel> level_from(std::string_view s) {
    if (s == "debug") return LogLevel::Debug;
    if (s == "info") return LogLevel::Info;
    if (s == "warn") return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    return make_error(ErrorCode::ParseError, "unknown log level");
}

std::optional<std::string> opt(const json::Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

}  // namespace

std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "info";
}

AppLog::AppLog(std::filesystem::path file, const Clock& clock) : file_(std::move(file)), clock_(clock) {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
}

json::Json AppLog::redact(const json::Json& fields) {
    if (fields.is_object()) {
        json::Json out = json::Json::object();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            out[it.key()] = secret_key(it.key()) ? json::Json("[redacted]") : redact(it.value());
        }
        return out;
    }
    if (fields.is_array()) {
        json::Json out = json::Json::array();
        for (const auto& v : fields) out.push_back(redact(v));
        return out;
    }
    return fields;
}

void AppLog::write(LogLevel level, std::string_view component, std::string_view message, json::Json fields,
                   std::optional<std::string> correlation_id, std::optional<std::string> execution_id,
                   std::optional<std::string> asset_id) {
    json::Json line = {{"ts", iso8601_utc(clock_.now_ms())},
                       {"level", std::string(to_string(level))},
                       {"component", std::string(component)},
                       {"message", std::string(message)},
                       {"fields", redact(fields.is_null() ? json::Json::object() : fields)}};
    if (correlation_id) line["correlationId"] = *correlation_id;
    if (execution_id) line["executionId"] = *execution_id;
    if (asset_id) line["assetId"] = *asset_id;
    const std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(file_, std::ios::app);
    if (out) out << line.dump(-1, ' ', false, json::Json::error_handler_t::replace) << '\n';
}

Result<std::vector<LogEntry>> AppLog::read(const LogFilter& f) const {
    std::deque<std::string> lines;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream in(file_);
        if (!in) return std::vector<LogEntry>{};
        std::string line;
        // Keep a bounded tail: the viewer is for recent diagnostics.
        constexpr std::size_t kMaxScan = 50000;
        while (std::getline(in, line)) {
            lines.push_back(std::move(line));
            if (lines.size() > kMaxScan) lines.pop_front();
        }
    }
    std::vector<LogEntry> out;
    for (auto it = lines.rbegin(); it != lines.rend() && out.size() < f.limit; ++it) {
        auto j = json::parse(*it);
        if (!j || !j.value().is_object()) continue;  // tolerate a torn last line
        const json::Json& v = j.value();
        LogEntry e;
        e.ts = opt(v, "ts").value_or("");
        auto level = level_from(opt(v, "level").value_or(""));
        if (!level) continue;
        e.level = level.value();
        e.component = opt(v, "component").value_or("");
        e.message = opt(v, "message").value_or("");
        e.correlation_id = opt(v, "correlationId");
        e.execution_id = opt(v, "executionId");
        e.asset_id = opt(v, "assetId");
        if (auto fit = v.find("fields"); fit != v.end()) e.fields = *fit;

        if (f.min_level && rank(e.level) < rank(*f.min_level)) continue;
        if (f.component && e.component.rfind(*f.component, 0) != 0) continue;
        if (f.correlation_id && e.correlation_id != f.correlation_id) continue;
        if (f.execution_id && e.execution_id != f.execution_id) continue;
        if (f.asset_id && e.asset_id != f.asset_id) continue;
        if (f.text && lower(e.message).find(lower(*f.text)) == std::string::npos) continue;
        if (f.since && e.ts < *f.since) continue;
        if (f.until && e.ts > *f.until) continue;
        out.push_back(std::move(e));
    }
    return out;
}

json::Json to_json(const LogEntry& e) {
    json::Json j = {{"ts", e.ts},
                    {"level", std::string(to_string(e.level))},
                    {"component", e.component},
                    {"message", e.message},
                    {"fields", e.fields}};
    j["correlationId"] = e.correlation_id ? json::Json(*e.correlation_id) : json::Json(nullptr);
    j["executionId"] = e.execution_id ? json::Json(*e.execution_id) : json::Json(nullptr);
    j["assetId"] = e.asset_id ? json::Json(*e.asset_id) : json::Json(nullptr);
    return j;
}

}  // namespace twin::platform
