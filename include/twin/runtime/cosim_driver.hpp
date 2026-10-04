/**
 * @file cosim_driver.hpp
 * @brief Co-simulation master: drives the PT simulator and feeds the Digital Twin.
 * @ingroup runtime
 *
 * One logical tick (default 100 ms of model time) performs, in this fixed order:
 *  1. send the commands the mission controller queued in the previous tick;
 *  2. step the Physical Twin (POST /pt/step) — telemetry and PT events;
 *  3. translate PT events with the E-driven PtAdapter and submit them to the
 *     session (kernel decides, ledger records);
 *  4. poll the building-information service for map updates and apply them to
 *     the twin's world model;
 *  5. let the mission controller decide (it may submit decision events);
 *  6. check the current location's deadline (monitoring alarm if the passage
 *     of logical time alone is no longer admissible).
 * The order makes runs deterministic given the scenario; wall-clock time only
 * paces ticks (speed control) and never enters semantic decisions.
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "twin/planner/planner.hpp"
#include "twin/runtime/event_hub.hpp"
#include "twin/runtime/mission_controller.hpp"
#include "twin/runtime/pt_adapter.hpp"
#include "twin/runtime/session.hpp"
#include "twin/runtime/world_model.hpp"
#include "twin/runtime/world_port.hpp"

namespace twin::runtime {

/// @brief Driver configuration.
struct DriverConfig {
    Ticks step{100};                     ///< Logical step per tick (ticks).
    double speed{2.0};                   ///< Logical seconds per wall-clock second.
    bool autostart{true};                ///< Start the mission as soon as possible.
    bool deterministic{false};           ///< Deterministic ledger (no wall-clock metadata).
    std::filesystem::path ledger_dir{"var/ledgers"};  ///< Where session ledgers are written.
    ControllerConfig controller{};       ///< Mission controller tuning.
};

/// @brief Simulation lifecycle.
enum class SimStatus { Paused, Running, Finished, Failed };

/// @brief The co-simulation driver (see file documentation).
class CoSimDriver {
public:
    CoSimDriver(package::LoadedPackage package, std::unique_ptr<WorldPort> world, EventHub& hub,
                DriverConfig config);
    ~CoSimDriver();
    CoSimDriver(const CoSimDriver&) = delete;
    CoSimDriver& operator=(const CoSimDriver&) = delete;
    CoSimDriver(CoSimDriver&&) = delete;
    CoSimDriver& operator=(CoSimDriver&&) = delete;

    /// @brief Reset the physical scenario and start a fresh twin session (new ledger).
    [[nodiscard]] Status reset();
    /// @brief Start the background pacing thread.
    void launch();
    /// @brief Run one tick synchronously (used by the API "step" control and by tests).
    [[nodiscard]] Status tick();

    /// @name Controls
    /// @{
    void play();
    void pause();
    void set_speed(double logical_per_wall);
    /// @}

    /// @brief Current session (shared: stays valid for callers across resets).
    [[nodiscard]] std::shared_ptr<TwinSession> session() const;
    /// @brief Status for the API: lifecycle, speed, logical time, session, ledger.
    [[nodiscard]] json::Json status() const;
    /// @brief The twin's world model (belief) for the visualisation.
    [[nodiscard]] json::Json known_world() const;
    /// @brief Mission progress and plans.
    [[nodiscard]] json::Json mission() const;
    [[nodiscard]] json::Json plans() const;
    /// @brief Latest telemetry as received by the twin.
    [[nodiscard]] json::Json telemetry() const;
    /// @brief The verified package.
    [[nodiscard]] const package::LoadedPackage& package() const noexcept { return package_; }

private:
    Status tick_locked();
    Status fail(const Error& error);
    void send_commands();
    Result<json::Json> step_physical_twin();
    Status observe_pt_events(const json::Json& stepped);
    void apply_map_updates();
    void check_deadline();
    void publish_state(const TwinSession& session);
    void loop();

    package::LoadedPackage package_;
    std::unique_ptr<WorldPort> world_;
    EventHub& hub_;
    DriverConfig config_;
    PtAdapter adapter_;
    planner::AStarPlanner planner_;

    mutable std::mutex mutex_;
    std::shared_ptr<TwinSession> session_;
    std::unique_ptr<MissionController> controller_;
    TwinWorldModel world_model_;
    json::Json telemetry_ = json::Json::object();
    SimStatus status_{SimStatus::Paused};
    Ticks now_{0};
    std::uint64_t ticks_{0};
    std::uint32_t session_counter_{0};
    std::optional<Ticks> alarmed_deadline_;
    std::string last_error_;

    std::atomic<double> speed_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

/// @brief Lowercase status name.
[[nodiscard]] const char* to_string(SimStatus status) noexcept;

}  // namespace twin::runtime
