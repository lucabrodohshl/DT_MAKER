/**
 * @file supervisor.hpp
 * @brief Deployment supervisor: starts, watches and stops the processes of deployed twin instances.
 * @ingroup studio
 *
 * Deploying an instance is real: the supervisor launches, on this machine,
 *  - `twin-world` with the simulator scenario generated from the Blueprint's world (mobile-robot
 *    simulation), when the instance is simulated that way;
 *  - `twin-runtime` on the instance's Verified Twin Package (co-simulation mode against that
 *    world, or monitor mode);
 *  - `twin-pt-feed` with the Blueprint's event script, when the instance's data source is the
 *    event-script simulator;
 * on free local ports, waits until each answers its health check, registers the URLs on the
 * twin record and starts the telemetry bridge, so the instance appears in Operate at once.
 *
 * A runtime that exits unexpectedly is reported as `failed` and is never restarted
 * automatically: a fail-stop (e.g. the ledger could not be written) is an integrity event an
 * operator must see. Processes run in their own process group and are stopped with SIGTERM
 * (SIGKILL after a grace period) when the instance is stopped or Studio exits.
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::studio {

class Services;

/// @brief What to launch for one instance.
struct LaunchPlan {
    std::string instance_id;                       ///< Twin (instance) id.
    std::string mode{"monitor"};                   ///< "monitor" or "cosimulation".
    std::filesystem::path package_dir;             ///< Verified Twin Package directory.
    std::optional<std::filesystem::path> world_scenario;  ///< twin-world scenario (co-simulation).
    std::optional<std::filesystem::path> feed;     ///< twin-pt-feed script (monitor mode, event-script simulator).
    double speed{1.0};                             ///< Co-simulation / feed speed (logical s per wall s).
    bool paused{true};                             ///< Co-simulation starts paused (operator starts the mission).
    std::filesystem::path work_dir;                ///< Ledgers and logs of the instance.
    std::vector<std::filesystem::path> package_store;  ///< Directories of further packages (replay).
    std::string kind{"instance"};                  ///< "instance" (a twin) or "preview" (Studio sandbox).
    bool bridge{true};                             ///< Bridge runtime telemetry into the twin's channels.
};

/// @brief See file documentation.
class Supervisor {
public:
    /// @brief Supervisor for @p services, launching binaries from @p bin_dir on ports [@p port_begin, @p port_end].
    Supervisor(Services& services, std::filesystem::path bin_dir, int port_begin, int port_end);
    ~Supervisor();
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;
    Supervisor(Supervisor&&) = delete;
    Supervisor& operator=(Supervisor&&) = delete;

    /// @brief Whether the runtime binaries were found.
    [[nodiscard]] bool available() const;
    /// @brief The directory binaries are launched from.
    [[nodiscard]] const std::filesystem::path& bin_dir() const noexcept { return bin_dir_; }

    /**
     * @brief Start (or restart) an instance: stops its previous processes, launches the plan,
     * waits for health, registers runtime/world URLs on the twin record and starts the bridge.
     * @return {state, runtimeUrl, worldUrl?, processes[]}; Unavailable if a process does not become healthy.
     */
    [[nodiscard]] Result<json::Json> start(const LaunchPlan& plan);
    /// @brief Stop an instance's processes (idempotent).
    void stop(const std::string& instance_id, const std::string& reason);
    /// @brief Stop everything (Studio shutdown).
    void stop_all();
    /// @brief Process state of one instance ({state: "not_started"} if never started here).
    [[nodiscard]] json::Json status(const std::string& instance_id) const;
    /// @brief Process state of every supervised instance.
    [[nodiscard]] json::Json status_all() const;

    /// @brief Implementation state.
    struct Instance;

private:
    void watch();
    [[nodiscard]] int free_port();

    Services& services_;
    std::filesystem::path bin_dir_;
    int port_begin_;
    int port_end_;
    mutable std::mutex mu_;
    std::map<std::string, std::shared_ptr<Instance>> instances_;
    std::atomic<bool> stopping_{false};
    std::thread watcher_;
};

/// @brief Directory of the running executable (for locating sibling binaries).
[[nodiscard]] std::filesystem::path executable_dir();

}  // namespace twin::studio
