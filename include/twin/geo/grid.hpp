/**
 * @file grid.hpp
 * @brief Occupancy grids, cells and map-update messages shared by the PT simulator,
 *        the environment service, the planner and the runtime's world model.
 * @ingroup geo
 *
 * @defgroup geo Geometry and map vocabulary
 * @brief Geometric world representation — deliberately OUTSIDE the semantic kernel.
 *
 * The behavioural Digital Twin (the verified timed automaton) governs mission
 * modes; the geometric world (walls, doors, hazards, unknown space) is
 * represented here, outside the trusted core. Geometry uses floating-point
 * metres: it is never part of semantic state.
 */
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::geo {

/// @brief Integer grid cell (column x, row y; y grows downwards).
struct Cell {
    int x{0};  ///< Column.
    int y{0};  ///< Row.
    /// @brief Total order (row-major) for use in ordered containers.
    friend auto operator<=>(const Cell&, const Cell&) = default;
};

/// @brief A point in metres in the building frame.
struct Point {
    double x{0.0};  ///< Metres east.
    double y{0.0};  ///< Metres south.
};

/// @brief Occupancy of a cell, as known by an observer (or as it truly is).
enum class Occupancy : std::uint8_t {
    Unknown,     ///< Not (yet) known — only in beliefs, never in ground truth.
    Free,        ///< Navigable.
    Wall,        ///< Permanent structure.
    Obstacle,    ///< Temporary object (pallets, shelving, a parked vehicle).
    DoorOpen,    ///< Navigable door.
    DoorClosed,  ///< Closed door (blocks).
    Hazard       ///< No-fly cell (declared hazard area).
};

/// @brief Single-character map encoding: ? . # o D d !
[[nodiscard]] char to_char(Occupancy o) noexcept;
/// @brief Inverse of to_char; std::nullopt for unknown characters.
[[nodiscard]] std::optional<Occupancy> occupancy_from_char(char c) noexcept;
/// @brief Lowercase name ("free", "door_closed", ...).
[[nodiscard]] std::string_view to_string(Occupancy o) noexcept;
/// @brief True iff a drone may fly through the cell (Free, DoorOpen).
[[nodiscard]] bool is_navigable(Occupancy o) noexcept;
/// @brief True iff the cell blocks flight (Wall, Obstacle, DoorClosed, Hazard).
[[nodiscard]] bool is_blocking(Occupancy o) noexcept;
/// @brief True iff the cell blocks line of sight (Wall, Obstacle, DoorClosed).
[[nodiscard]] bool blocks_sight(Occupancy o) noexcept;

/**
 * @brief A rectangular occupancy grid with a metric scale.
 */
class OccupancyGrid {
public:
    OccupancyGrid() = default;
    /// @brief Grid of @p width x @p height cells of @p cell_size_m metres, filled with @p fill.
    OccupancyGrid(int width, int height, double cell_size_m, Occupancy fill);

    /// @brief Parse rows of characters (all rows of equal length).
    [[nodiscard]] static Result<OccupancyGrid> from_rows(const std::vector<std::string>& rows, double cell_size_m);
    /// @brief Rows of characters (inverse of from_rows).
    [[nodiscard]] std::vector<std::string> to_rows() const;

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] double cell_size() const noexcept { return cell_size_; }
    [[nodiscard]] bool contains(Cell c) const noexcept {
        return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_;
    }
    /// @brief Occupancy of @p c (Wall outside the grid).
    [[nodiscard]] Occupancy at(Cell c) const noexcept;
    /// @brief Set the occupancy of @p c (ignored outside the grid).
    void set(Cell c, Occupancy o) noexcept;

    /// @brief Centre of a cell in metres.
    [[nodiscard]] Point center(Cell c) const noexcept;
    /// @brief Cell containing a point.
    [[nodiscard]] Cell cell_of(Point p) const noexcept;

    /// @brief Member-wise equality.
    friend bool operator==(const OccupancyGrid&, const OccupancyGrid&) = default;

private:
    int width_{0};
    int height_{0};
    double cell_size_{0.5};
    std::vector<Occupancy> cells_;
};

/// @brief A changed cell in a map update.
struct CellChange {
    Cell cell;                               ///< Cell.
    Occupancy occupancy{Occupancy::Unknown};  ///< New occupancy.
};

/**
 * @brief An update published by the environment / building-information service.
 *
 * Kinds: "sensor_observation" (the drone's onboard sensing revealed cells),
 * "facility_notice" (the facility declared a hazard, closed a door, ...).
 */
struct MapUpdate {
    std::uint64_t seq{0};           ///< Monotone sequence number of the feed.
    Ticks at{0};                    ///< Logical time of publication.
    std::string kind;               ///< "sensor_observation" | "facility_notice".
    std::string description;        ///< Human-readable summary ("Closed fire door in corridor C").
    std::vector<CellChange> cells;  ///< Changed cells.
};

/// @brief JSON encodings (integers and strings only; positions as cells).
[[nodiscard]] json::Json to_json(const MapUpdate& update);
[[nodiscard]] Result<MapUpdate> map_update_from_json(const json::Json& j);
[[nodiscard]] json::Json to_json(const OccupancyGrid& grid);
[[nodiscard]] Result<OccupancyGrid> grid_from_json(const json::Json& j);
[[nodiscard]] json::Json to_json(Cell c);
[[nodiscard]] Result<Cell> cell_from_json(const json::Json& j);

/// @brief Euclidean distance in metres.
[[nodiscard]] double distance(Point a, Point b) noexcept;

/**
 * @brief Cells on the segment between two cells (Bresenham), both endpoints included.
 */
[[nodiscard]] std::vector<Cell> line_cells(Cell from, Cell to);

}  // namespace twin::geo
