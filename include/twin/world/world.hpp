/**
 * @file world.hpp
 * @brief The Physical Twin world: ground truth + drone + building-information service.
 * @ingroup world
 *
 * World combines
 *  - the GROUND TRUTH building (mutated only by the scenario timeline and by
 *    explicit operator injections);
 *  - the drone simulator (reads the ground truth for physics and sensing);
 *  - the EnvironmentService: the external building-information system the
 *    Digital Twin queries. It publishes the prior floor plan, and afterwards
 *    only what was *observed* by the drone's sensors or *announced* by the
 *    facility — never the ground truth itself.
 *
 * The Digital Twin talks to this world only through the environment API
 * (/env/...) and the flight-controller API (/pt/...), both defined by
 * WorldService (service.hpp). The ground truth is exposed exclusively on
 * /observer/... for the visualisation; the runtime has no client for it
 * (checked by tests/architecture).
 */
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "twin/geo/grid.hpp"
#include "twin/world/drone.hpp"
#include "twin/world/scenario.hpp"

namespace twin::world {

/**
 * @brief The building-information service: published knowledge and update feed.
 */
class EnvironmentService {
public:
    /// @brief Service publishing the scenario's prior knowledge.
    explicit EnvironmentService(const Scenario& scenario);

    /// @brief Current published knowledge (prior plan + all updates).
    [[nodiscard]] const geo::OccupancyGrid& published() const noexcept { return published_; }
    /// @brief Integrate sensed cells; differences to the published map become an update.
    void observe(Ticks at, const std::vector<geo::CellChange>& sensed);
    /// @brief Publish a facility notice (e.g. a declared hazard).
    void notice(Ticks at, const std::string& description, const std::vector<geo::CellChange>& cells);
    /// @brief Updates with seq > @p since.
    [[nodiscard]] std::vector<geo::MapUpdate> updates_since(std::uint64_t since) const;
    /// @brief Sequence number of the newest update (0 if none).
    [[nodiscard]] std::uint64_t head() const noexcept { return updates_.empty() ? 0 : updates_.back().seq; }

private:
    geo::OccupancyGrid published_;
    std::vector<geo::MapUpdate> updates_;
};

/// @brief Result of advancing the world by one co-simulation step.
struct StepResult {
    Telemetry telemetry;          ///< Telemetry after the step.
    std::vector<PtEvent> events;  ///< Flight-controller events during the step.
    std::vector<std::string> facility_log;  ///< Scenario events that fired (for observers).
};

/**
 * @brief Thread-safe facade of the simulated physical world.
 */
class World {
public:
    /// @brief World at logical time 0 of @p scenario.
    explicit World(Scenario scenario);

    /// @brief Advance logical time by @p dt ticks.
    [[nodiscard]] StepResult step(Ticks dt);
    /// @brief Send a command to the drone.
    [[nodiscard]] Status command(const Command& c);
    /// @brief Current logical time of the world.
    [[nodiscard]] Ticks now() const;
    /// @brief Current telemetry.
    [[nodiscard]] Telemetry telemetry() const;

    /// @name Environment API (what the Digital Twin may know)
    /// @{
    /// @brief Published knowledge (prior plan plus all updates).
    [[nodiscard]] json::Json env_known_map() const;
    /// @brief Updates with sequence number > @p since.
    [[nodiscard]] json::Json env_updates_since(std::uint64_t since) const;
    /// @brief Mission definition (home, targets, drone parameters).
    [[nodiscard]] json::Json env_mission() const;
    /// @brief Declared no-fly cells.
    [[nodiscard]] json::Json env_hazards() const;
    /// @}

    /// @name Observer API (ground truth — visualisation only, never the DT)
    /// @{
    [[nodiscard]] json::Json observer_ground_truth() const;
    [[nodiscard]] json::Json observer_state() const;
    /// @brief Operator "act of god": change the ground truth (e.g. drop an obstacle).
    [[nodiscard]] Status observer_inject(const std::vector<geo::Cell>& cells, geo::Occupancy occupancy,
                                         const std::string& description, bool notify);
    /// @}

    /// @brief Scenario metadata (name, description, targets).
    [[nodiscard]] json::Json scenario_info() const;

private:
    void apply_due_events(std::vector<std::string>& log);

    mutable std::mutex mutex_;
    Scenario scenario_;
    geo::OccupancyGrid truth_;
    DroneSimulator drone_;
    EnvironmentService env_;
    Ticks now_{0};
    Ticks last_sense_{-1};
    std::size_t next_event_{0};
    std::uint64_t truth_version_{0};
};

}  // namespace twin::world
