/**
 * @file executions.cpp
 * @brief Execution listing, ledger/telemetry access and the package registry.
 */
#include "twin/runtime/executions.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace twin::runtime {
namespace {

using json::Json;

constexpr std::string_view kLedgerSuffix = ".ledger.jsonl";
constexpr std::string_view kTelemetrySuffix = ".telemetry.jsonl";

bool is_ledger_file(const std::filesystem::path& p) {
    const std::string name = p.filename().string();
    return name.size() > kLedgerSuffix.size() && name.ends_with(kLedgerSuffix);
}

Result<std::string> read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return make_error(ErrorCode::IoError, "cannot read file").with("file", p.string());
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

/// Summary from the first and last ledger lines (no verification: that is /verify's job).
std::optional<ExecutionInfo> summarize(const std::filesystem::path& ledger) {
    Result<std::string> text = read_file(ledger);
    if (!text) return std::nullopt;
    std::istringstream in(text.value());
    std::string first;
    std::string last;
    std::uint64_t lines = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        if (lines == 0) first = line;
        last = line;
        ++lines;
    }
    if (lines == 0) return std::nullopt;
    Result<Json> head = json::parse(first);
    Result<Json> tail = json::parse(last);
    if (!head || !tail || !head.value().contains("body") || !tail.value().contains("body")) return std::nullopt;
    const Json& h = head.value().at("body");
    const Json& t = tail.value().at("body");
    ExecutionInfo info;
    info.session = h.value("session", std::string());
    info.ledger = ledger;
    info.telemetry = telemetry_path_for(ledger);
    const Json pkg = h.value("package", Json::object());
    info.package_hash = pkg.value("hash", std::string());
    info.model_id = pkg.value("model_id", std::string());
    info.model_version = pkg.value("model_version", std::string());
    info.records = lines;
    info.ended = t.value("kind", std::string()) == "end";
    info.started_wall = h.value("wall_time", std::string());
    info.ended_wall = t.value("wall_time", std::string());
    info.last_time = t.value("time_after", std::int64_t{0});
    const Json state = t.value("state_after", Json::array());
    if (state.is_array() && !state.empty()) info.last_location = state.at(0).value("location", std::string());
    return info;
}

}  // namespace

std::filesystem::path telemetry_path_for(const std::filesystem::path& ledger) {
    std::string name = ledger.filename().string();
    if (name.ends_with(kLedgerSuffix)) name.resize(name.size() - kLedgerSuffix.size());
    return ledger.parent_path() / (name + std::string(kTelemetrySuffix));
}

Json to_json(const ExecutionInfo& i) {
    return Json{{"session", i.session},
                {"ledger", i.ledger.string()},
                {"telemetry", std::filesystem::exists(i.telemetry) ? Json(i.telemetry.string()) : Json()},
                {"package_hash", i.package_hash},
                {"model_id", i.model_id},
                {"model_version", i.model_version},
                {"records", i.records},
                {"ended", i.ended},
                {"started", i.started_wall},
                {"ended_at", i.ended_wall},
                {"last_time_ticks", i.last_time},
                {"last_location", i.last_location}};
}

std::vector<ExecutionInfo> ExecutionStore::list() const {
    std::vector<std::pair<std::filesystem::file_time_type, ExecutionInfo>> found;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir_, ec)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(dir_, ec)) {
        if (!entry.is_regular_file() || !is_ledger_file(entry.path())) continue;
        if (std::optional<ExecutionInfo> info = summarize(entry.path())) {
            found.emplace_back(entry.last_write_time(ec), std::move(*info));
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second.ledger.filename() > b.second.ledger.filename();
    });
    std::vector<ExecutionInfo> out;
    out.reserve(found.size());
    for (auto& [_, info] : found) out.push_back(std::move(info));
    return out;
}

Result<ExecutionInfo> ExecutionStore::find(std::string_view session) const {
    for (ExecutionInfo& info : list()) {
        if (info.session == session || info.ledger.filename().string() == session) return std::move(info);
    }
    return make_error(ErrorCode::NotFound, "no recorded execution with this session id")
        .with("session", std::string(session));
}

Result<std::string> ExecutionStore::ledger_text(const ExecutionInfo& info) const { return read_file(info.ledger); }

Result<Json> ExecutionStore::telemetry(const ExecutionInfo& info, std::size_t max_samples) const {
    Json samples = Json::array();
    if (!std::filesystem::exists(info.telemetry)) {
        return Json{{"session", info.session}, {"samples", samples}, {"total", 0}};
    }
    Result<std::string> text = read_file(info.telemetry);
    if (!text) return std::move(text).error();
    std::vector<Json> all;
    std::istringstream in(text.value());
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        if (Result<Json> j = json::parse(line)) all.push_back(std::move(j).value());
    }
    const std::size_t n = all.size();
    const std::size_t keep = std::max<std::size_t>(2, max_samples);
    if (n <= keep) {
        for (Json& j : all) samples.push_back(std::move(j));
    } else {
        // Even thinning by index; deterministic, keeps first and last.
        for (std::size_t k = 0; k < keep; ++k) {
            samples.push_back(all[k * (n - 1) / (keep - 1)]);
        }
    }
    return Json{{"session", info.session}, {"samples", samples}, {"total", n}};
}

PackageRegistry::PackageRegistry(const package::LoadedPackage& running, std::vector<std::filesystem::path> store_dirs)
    : store_dirs_(std::move(store_dirs)) {
    loaded_.emplace(running.package_hash, running);
}

Result<package::LoadedPackage> PackageRegistry::get(const std::string& package_hash) {
    std::lock_guard lock(mutex_);
    if (auto it = loaded_.find(package_hash); it != loaded_.end()) return it->second;
    std::error_code ec;
    for (const std::filesystem::path& store : store_dirs_) {
        if (!std::filesystem::is_directory(store, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(store, ec)) {
            if (!entry.is_directory() || !std::filesystem::exists(entry.path() / "manifest.json")) continue;
            Result<package::LoadedPackage> p = package::load_and_verify(entry.path(), package::VerifyOptions{});
            if (!p) continue;  // an unverifiable package is never used
            const std::string hash = p.value().package_hash;
            loaded_.emplace(hash, p.value());
            if (hash == package_hash) return std::move(p).value();
        }
    }
    return make_error(ErrorCode::NotFound,
                      "the package recorded in this execution is not available to this runtime; the execution is "
                      "never reinterpreted with a different package")
        .with("package_hash", package_hash);
}

}  // namespace twin::runtime
