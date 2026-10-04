/**
 * @file world_model.hpp
 * @brief The twin's geometric BELIEF about the building, and the mission it was given.
 * @ingroup runtime
 *
 * TwinWorldModel is built exclusively from the building-information service:
 * the published floor plan plus the update feed. It is the twin's knowledge,
 * which may be incomplete or wrong — the visualisation contrasts it with the
 * ground truth. It is not semantic state: the behavioural state of the twin
 * lives only in the kernel (TwinSession).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/geo/grid.hpp"

namespace twin::runtime {

/// @brief An inspection target of the mission.
struct MissionTarget {
    std::string id;    ///< "T1"
    std::string name;  ///< Human-readable.
    geo::Cell cell;    ///< Location.
};

/// @brief The mission as published by the environment service.
struct Mission {
    std::string name;                    ///< Mission name.
    geo::Cell home;                      ///< Take-off and landing pad.
    std::vector<MissionTarget> targets;  ///< Ordered inspection targets.
};

/// @brief Parse GET /env/mission.
[[nodiscard]] Result<Mission> mission_from_json(const json::Json& j);

/**
 * @brief Occupancy belief + applied update history.
 */
class TwinWorldModel {
public:
    /// @brief Initialise from GET /env/map/known.
    [[nodiscard]] Status reset(const json::Json& known_map);
    /// @brief Apply an update; returns the cells whose belief changed.
    std::vector<geo::CellChange> apply(const geo::MapUpdate& update);

    [[nodiscard]] const geo::OccupancyGrid& map() const noexcept { return map_; }
    [[nodiscard]] std::uint64_t last_seq() const noexcept { return last_seq_; }
    /// @brief Number of cells still unknown.
    [[nodiscard]] std::size_t unknown_cells() const;
    /// @brief Recent updates (newest last, bounded).
    [[nodiscard]] const std::vector<geo::MapUpdate>& history() const noexcept { return history_; }

private:
    geo::OccupancyGrid map_;
    std::uint64_t last_seq_{0};
    std::vector<geo::MapUpdate> history_;
};

}  // namespace twin::runtime
