/**
 * @file services_impl.hpp
 * @brief (private) Shared state and helpers of twin::studio::Services, split across services_*.cpp.
 * @ingroup studio
 */
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "twin/platform/applicability.hpp"
#include "twin/platform/artifacts.hpp"
#include "twin/platform/assets.hpp"
#include "twin/platform/audit.hpp"
#include "twin/platform/database.hpp"
#include "twin/platform/evidence.hpp"
#include "twin/platform/object_store.hpp"
#include "twin/platform/telemetry.hpp"
#include "twin/platform/twins.hpp"
#include "twin/studio/services.hpp"

namespace twin::studio {

/// @brief Convert a json value into a Result-compatible error when it is not an object.
[[nodiscard]] inline json::Json ref_json(const platform::ArtifactRef& r) { return r.str(); }

/// @brief Shared state (see services.hpp for the threading model).
struct Services::Impl {
    Services& self;
    std::unique_ptr<platform::Database> db;
    std::unique_ptr<platform::ObjectStore> store;
    std::unique_ptr<platform::ArtifactRepository> artifacts;
    std::unique_ptr<platform::EvidenceRepository> evidence;
    std::unique_ptr<platform::AuditLog> audit;
    std::unique_ptr<platform::AssetRepository> assets;
    std::unique_ptr<platform::TelemetryRepository> telemetry;
    std::unique_ptr<platform::TwinRepository> twins;

    /// Serialises every database access (recursive: services call each other).
    std::recursive_mutex mu;
    /// Checks currently executing ("refinement:CHG-0002"); exposed as CHECK_RUNNING.
    std::mutex running_mu;
    std::set<std::string> running;

    explicit Impl(Services& s) : self(s) {}

    [[nodiscard]] std::unique_lock<std::recursive_mutex> lock() { return std::unique_lock<std::recursive_mutex>(mu); }

    /// @brief Append an audit record and publish a live event; audit failures are logged, never thrown away silently.
    void record(std::string_view operation, std::string_view outcome, std::string_view subject, json::Json details,
                const Actor& actor, std::string_view topic);

    /// @brief Write a version's content to `<data>/work/<sha>.<ext>` (content-addressed; reused).
    [[nodiscard]] Result<std::filesystem::path> materialize(const platform::Binding& binding);

    /// @brief Binding for a role from a version (hash looked up).
    [[nodiscard]] Result<platform::Binding> bind(std::string role, const platform::ArtifactRef& ref);

    /// @brief JSON summary of a version (id, ref, state, hash, ...) without content.
    [[nodiscard]] Result<json::Json> version_summary(const platform::ArtifactRef& ref);

    /// @brief Whether a check key is running.
    [[nodiscard]] bool is_running(const std::string& key);

    /// @brief Evidence list as JSON.
    [[nodiscard]] json::Json evidence_json(const std::vector<platform::EvidenceRecord>& records);
};

/// @brief RAII marker for a running check (CHECK_RUNNING in the UI); refuses concurrent duplicates.
class RunningCheck {
public:
    RunningCheck(Services::Impl& impl, std::string key);
    ~RunningCheck();
    RunningCheck(const RunningCheck&) = delete;
    RunningCheck& operator=(const RunningCheck&) = delete;
    RunningCheck(RunningCheck&&) = delete;
    RunningCheck& operator=(RunningCheck&&) = delete;
    /// @brief False if the same check was already running.
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
    Services::Impl& impl_;
    std::string key_;
    bool acquired_{false};
};

/// @brief Find a binding by role.
[[nodiscard]] const platform::Binding* find_binding(const std::vector<platform::Binding>& bindings,
                                                    std::string_view role);

/// @brief Normalised trust state names shared with the UI.
namespace trust {
inline constexpr std::string_view kPass = "pass";
inline constexpr std::string_view kFail = "fail";
inline constexpr std::string_view kUnknown = "unknown";
inline constexpr std::string_view kNotChecked = "not_checked";
inline constexpr std::string_view kStale = "stale";
inline constexpr std::string_view kInvalidated = "invalidated";
inline constexpr std::string_view kRunning = "check_running";
inline constexpr std::string_view kUnavailable = "unavailable";
inline constexpr std::string_view kError = "error";
inline constexpr std::string_view kBlocked = "blocked";
inline constexpr std::string_view kNotApplicable = "not_applicable";
}  // namespace trust

}  // namespace twin::studio
