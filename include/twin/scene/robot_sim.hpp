/**
 * @file robot_sim.hpp
 * @brief Mobile-robot simulation adapter: a Blueprint world -> the drone simulator's scenario.
 * @ingroup scene
 *
 * The adapter gives meaning to a small, documented vocabulary of semantic types and turns a
 * twin-world/1 document plus the Blueprint's `simulation` section into the scenario file
 * that `twin-world` (the Physical Twin simulator and building-information service) loads.
 * It is the only place where world geometry becomes simulator input, so the world an
 * engineer edits in Studio is exactly the world the simulator runs.
 *
 * Vocabulary (semanticType -> occupancy), applied in layer order and, within a layer, in
 * object order (later objects override earlier ones):
 *
 * | semanticType | occupancy | geometry |
 * |---|---|---|
 * | floor, free  | free       | rect, polygon, region, zone |
 * | wall         | wall       | rect, polygon, region, line/polyline (stroke width) |
 * | obstacle     | obstacle   | rect, polygon, region, line/polyline |
 * | door         | door_open or door_closed (`properties.state` "open"/"closed") | as above |
 * | hazard       | hazard (declared no-fly) | as above |
 * | unknown      | unknown (knowledge layers only) | as above |
 * | start        | the robot's home cell | point |
 * | target       | an inspection target (`properties.targetId`, name) | point |
 *
 * Other semantic types (zone labels, rooms, corridors, waypoints) are presentation and do
 * not change occupancy. Cells outside every floor are walls. A cell belongs to an area
 * when its centre lies inside it (edges inclusive); a stroked line covers the cells whose
 * centre lies within half the stroke width of the line (at least half a cell).
 *
 * The **ground truth** is composed from `shared` and `ground-truth` layers, the twin's
 * **initial knowledge** from `shared` and `knowledge` layers. The ground truth may not
 * contain unknown cells. The observation model (sensor range, proximity range, update
 * interval, observed semantic types) is passed to the simulator; the twin learns the world
 * only through what the simulator's sensor model observes.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/geo/grid.hpp"
#include "twin/json/canonical.hpp"
#include "twin/scene/world.hpp"

namespace twin::scene {

/// @brief Result of rasterising a world.
struct Raster {
    std::int64_t cell_mm{500};        ///< Cell edge length (world units, mm).
    geo::OccupancyGrid ground_truth;  ///< Physical ground truth (simulator only).
    geo::OccupancyGrid knowledge;     ///< The twin's initial knowledge.
    std::vector<Finding> findings;    ///< Problems found while rasterising (TWS0xx).
};

/**
 * @brief Rasterise @p world into ground truth and initial knowledge.
 *
 * The grid covers the world bounds; @p cell_mm must divide the bounds' width and height.
 * Findings: TWS001 cell size does not divide the bounds, TWS002 unknown cells in the
 * ground truth, TWS003 an occupancy-relevant object has unsuitable geometry, TWS004 bad
 * door state.
 * @return InvalidArgument for a non-spatial world or a non-positive cell size.
 */
[[nodiscard]] Result<Raster> rasterize(const World& world, std::int64_t cell_mm);

/// @brief The cells an object's footprint covers on a grid of @p cell_mm cells over @p bounds.
[[nodiscard]] std::vector<geo::Cell> footprint(const Object& object, const Rect& bounds, std::int64_t cell_mm);

/// @brief The cell containing a point.
[[nodiscard]] geo::Cell cell_of(const Vec& point, const Rect& bounds, std::int64_t cell_mm);

/**
 * @brief The `twin-world` scenario (the scenario file format of twin-world) for a world and a
 * `simulation` section of kind "mobile-robot".
 *
 * @p simulation members: `cellSize` (mm), `seed`, `robot` (decimal strings: maxSpeed,
 * maxAccel, batteryCapacityWh, batteryStartPct, reservePct), `observation` (sensorRange,
 * proximityRange, updateIntervalMs, observes[], noise "none", unobservableLayers[]),
 * `mission` ({start: point object id, targets: [point object ids]}) and `timeline`
 * ([{id, at, kind "hazard"|"set", object, occupancy?, description, notify}]).
 * @return ValidationError listing every problem (with findings in the context) if the
 *         world cannot be simulated.
 */
[[nodiscard]] Result<json::Json> simulator_scenario(const World& world, const json::Json& simulation,
                                                    std::string_view name, std::string_view description);

/// @brief Raster as API JSON: {cellMm, width, height, groundTruth: rows, knowledge: rows, findings}.
[[nodiscard]] json::Json to_json(const Raster& raster);

}  // namespace twin::scene
