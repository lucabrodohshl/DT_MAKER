/**
 * @file monitor_host.hpp
 * @brief Monitoring runtime host: an external Physical Twin pushes events and telemetry.
 * @ingroup runtime
 *
 * In monitor mode the runtime is not a co-simulation master. The Physical
 * Twin (a PLC gateway, a data pipeline, or the twin-pt-feed tool) pushes:
 *  - PT events in the PT vocabulary of V_P — translated through the verified
 *    label equivalence E of the package's alignment evidence (PtAdapter) and
 *    submitted to the session: the kernel decides, the ledger records;
 *  - telemetry samples — observation data: written to the session's telemetry
 *    log and streamed to observers; never interpreted semantically here.
 * Logical time is carried by the inputs (the PT's timestamps); the host never
 * reads a wall clock for semantics.
 */
#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include "twin/runtime/host.hpp"
#include "twin/runtime/pt_adapter.hpp"

namespace twin::runtime {

/// @brief Monitor-mode configuration.
struct MonitorConfig {
    std::filesystem::path ledger_dir{"var/ledgers"};  ///< Where session ledgers are written.
    bool deterministic{false};                        ///< Deterministic ledger (no wall-clock metadata).
};

/// @brief See file documentation.
class MonitorHost final : public RuntimeHost {
public:
    /// @brief Host for @p package; fails if its alignment evidence yields no label translation.
    [[nodiscard]] static Result<std::unique_ptr<MonitorHost>> create(package::LoadedPackage package, EventHub& hub,
                                                                     MonitorConfig config);

    [[nodiscard]] std::shared_ptr<TwinSession> session() const override;
    [[nodiscard]] const package::LoadedPackage& package() const noexcept override { return package_; }
    [[nodiscard]] json::Json status() const override;
    [[nodiscard]] Status reset() override;
    [[nodiscard]] std::string mode() const override { return "monitor"; }

    /**
     * @brief A PT event {"label": "<PT label>", "ticks": n | "time": "12.5", "detail": {...}}.
     * @return the kernel's verdict view (accepted or rejected — both recorded), or an
     *         error for malformed requests and labels that E does not translate.
     */
    [[nodiscard]] Result<json::Json> pt_event(const json::Json& request);

    /// @brief A telemetry sample {"at": ticks, ...fields}: logged and streamed (no semantics).
    [[nodiscard]] Status telemetry(const json::Json& sample);

private:
    MonitorHost(package::LoadedPackage package, EventHub& hub, MonitorConfig config, PtAdapter adapter);

    package::LoadedPackage package_;
    EventHub& hub_;
    MonitorConfig config_;
    PtAdapter adapter_;
    mutable std::mutex mutex_;
    std::shared_ptr<TwinSession> session_;
    std::ofstream telemetry_log_;
    std::uint32_t session_counter_{0};
    std::uint64_t pt_events_{0};
    std::uint64_t telemetry_samples_{0};
    Ticks last_telemetry_{0};
};

}  // namespace twin::runtime
