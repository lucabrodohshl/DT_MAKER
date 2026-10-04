/**
 * @file grid.cpp
 * @brief Occupancy grid and map-update encodings.
 */
#include "twin/geo/grid.hpp"

#include <cmath>
#include <cstdlib>
#include <optional>

namespace twin::geo {

char to_char(Occupancy o) noexcept {
    switch (o) {
        case Occupancy::Unknown: return '?';
        case Occupancy::Free: return '.';
        case Occupancy::Wall: return '#';
        case Occupancy::Obstacle: return 'o';
        case Occupancy::DoorOpen: return 'D';
        case Occupancy::DoorClosed: return 'd';
        case Occupancy::Hazard: return '!';
    }
    return '?';
}

std::optional<Occupancy> occupancy_from_char(char c) noexcept {
    switch (c) {
        case '?': return Occupancy::Unknown;
        case '.': return Occupancy::Free;
        case '#': return Occupancy::Wall;
        case 'o': return Occupancy::Obstacle;
        case 'D': return Occupancy::DoorOpen;
        case 'd': return Occupancy::DoorClosed;
        case '!': return Occupancy::Hazard;
        default: return std::nullopt;
    }
}

std::string_view to_string(Occupancy o) noexcept {
    switch (o) {
        case Occupancy::Unknown: return "unknown";
        case Occupancy::Free: return "free";
        case Occupancy::Wall: return "wall";
        case Occupancy::Obstacle: return "obstacle";
        case Occupancy::DoorOpen: return "door_open";
        case Occupancy::DoorClosed: return "door_closed";
        case Occupancy::Hazard: return "hazard";
    }
    return "unknown";
}

bool is_navigable(Occupancy o) noexcept { return o == Occupancy::Free || o == Occupancy::DoorOpen; }

bool is_blocking(Occupancy o) noexcept {
    return o == Occupancy::Wall || o == Occupancy::Obstacle || o == Occupancy::DoorClosed || o == Occupancy::Hazard;
}

bool blocks_sight(Occupancy o) noexcept {
    return o == Occupancy::Wall || o == Occupancy::Obstacle || o == Occupancy::DoorClosed;
}

OccupancyGrid::OccupancyGrid(int width, int height, double cell_size_m, Occupancy fill)
    : width_(width),
      height_(height),
      cell_size_(cell_size_m),
      cells_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill) {}

Result<OccupancyGrid> OccupancyGrid::from_rows(const std::vector<std::string>& rows, double cell_size_m) {
    if (rows.empty() || rows.front().empty()) {
        return make_error(ErrorCode::ValidationError, "a grid needs at least one non-empty row");
    }
    const int w = static_cast<int>(rows.front().size());
    const int h = static_cast<int>(rows.size());
    OccupancyGrid g(w, h, cell_size_m, Occupancy::Unknown);
    for (int y = 0; y < h; ++y) {
        const std::string& row = rows[static_cast<std::size_t>(y)];
        if (static_cast<int>(row.size()) != w) {
            return make_error(ErrorCode::ValidationError, "grid rows differ in length")
                .with("row", std::to_string(y));
        }
        for (int x = 0; x < w; ++x) {
            const std::optional<Occupancy> o = occupancy_from_char(row[static_cast<std::size_t>(x)]);
            if (!o) {
                return make_error(ErrorCode::ValidationError, "unknown map character")
                    .with("char", std::string(1, row[static_cast<std::size_t>(x)]))
                    .with("cell", std::to_string(x) + "," + std::to_string(y));
            }
            g.set(Cell{x, y}, *o);
        }
    }
    return g;
}

std::vector<std::string> OccupancyGrid::to_rows() const {
    std::vector<std::string> rows;
    rows.reserve(static_cast<std::size_t>(height_));
    for (int y = 0; y < height_; ++y) {
        std::string row;
        row.reserve(static_cast<std::size_t>(width_));
        for (int x = 0; x < width_; ++x) {
            row.push_back(to_char(at(Cell{x, y})));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

Occupancy OccupancyGrid::at(Cell c) const noexcept {
    if (!contains(c)) return Occupancy::Wall;
    return cells_[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(c.x)];
}

void OccupancyGrid::set(Cell c, Occupancy o) noexcept {
    if (!contains(c)) return;
    cells_[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(c.x)] = o;
}

Point OccupancyGrid::center(Cell c) const noexcept {
    return Point{(c.x + 0.5) * cell_size_, (c.y + 0.5) * cell_size_};
}

Cell OccupancyGrid::cell_of(Point p) const noexcept {
    return Cell{static_cast<int>(std::floor(p.x / cell_size_)), static_cast<int>(std::floor(p.y / cell_size_))};
}

json::Json to_json(Cell c) { return json::Json::array({c.x, c.y}); }

Result<Cell> cell_from_json(const json::Json& j) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer()) {
        return make_error(ErrorCode::ValidationError, "a cell is [x, y] with integers");
    }
    return Cell{j[0].get<int>(), j[1].get<int>()};
}

json::Json to_json(const MapUpdate& u) {
    json::Json cells = json::Json::array();
    for (const CellChange& c : u.cells) {
        cells.push_back(json::Json{{"cell", to_json(c.cell)}, {"occupancy", std::string(to_string(c.occupancy))}});
    }
    return json::Json{{"seq", u.seq}, {"at", u.at}, {"kind", u.kind}, {"description", u.description},
                      {"cells", cells}};
}

Result<MapUpdate> map_update_from_json(const json::Json& j) {
    if (Status s = json::expect_keys(j, {"seq", "at", "kind", "description", "cells"}); !s) return s.error();
    MapUpdate u;
    Result<std::int64_t> seq = json::get_int(j, "seq");
    Result<std::int64_t> at = json::get_int(j, "at");
    Result<std::string> kind = json::get_string(j, "kind");
    Result<std::string> description = json::get_string(j, "description");
    if (!seq || !at || !kind || !description) return make_error(ErrorCode::ValidationError, "malformed map update");
    u.seq = static_cast<std::uint64_t>(seq.value());
    u.at = at.value();
    u.kind = kind.value();
    u.description = description.value();
    for (const json::Json& c : j.at("cells")) {
        Result<Cell> cell = cell_from_json(c.value("cell", json::Json()));
        const std::string occ = c.value("occupancy", std::string());
        std::optional<Occupancy> o;
        for (Occupancy cand : {Occupancy::Unknown, Occupancy::Free, Occupancy::Wall, Occupancy::Obstacle,
                               Occupancy::DoorOpen, Occupancy::DoorClosed, Occupancy::Hazard}) {
            if (to_string(cand) == occ) o = cand;
        }
        if (!cell || !o) return make_error(ErrorCode::ValidationError, "malformed cell change");
        u.cells.push_back(CellChange{cell.value(), *o});
    }
    return u;
}

json::Json to_json(const OccupancyGrid& g) {
    return json::Json{{"width", g.width()},
                      {"height", g.height()},
                      {"cell_size_mm", static_cast<std::int64_t>(std::llround(g.cell_size() * 1000.0))},
                      {"rows", g.to_rows()}};
}

Result<OccupancyGrid> grid_from_json(const json::Json& j) {
    if (Status s = json::expect_keys(j, {"width", "height", "cell_size_mm", "rows"}); !s) return s.error();
    Result<std::int64_t> mm = json::get_int(j, "cell_size_mm");
    if (!mm || mm.value() <= 0) return make_error(ErrorCode::ValidationError, "bad cell size");
    std::vector<std::string> rows;
    for (const json::Json& r : j.at("rows")) {
        if (!r.is_string()) return make_error(ErrorCode::ValidationError, "rows must be strings");
        rows.push_back(r.get<std::string>());
    }
    return OccupancyGrid::from_rows(rows, static_cast<double>(mm.value()) / 1000.0);
}

double distance(Point a, Point b) noexcept { return std::hypot(a.x - b.x, a.y - b.y); }

std::vector<Cell> line_cells(Cell from, Cell to) {
    std::vector<Cell> out;
    int x0 = from.x;
    int y0 = from.y;
    const int dx = std::abs(to.x - x0);
    const int dy = -std::abs(to.y - y0);
    const int sx = x0 < to.x ? 1 : -1;
    const int sy = y0 < to.y ? 1 : -1;
    int err = dx + dy;
    while (true) {
        out.push_back(Cell{x0, y0});
        if (x0 == to.x && y0 == to.y) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    return out;
}

}  // namespace twin::geo
