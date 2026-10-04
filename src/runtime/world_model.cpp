/**
 * @file world_model.cpp
 * @brief The twin's belief map, maintained from the environment service only.
 */
#include "twin/runtime/world_model.hpp"

namespace twin::runtime {

Result<Mission> mission_from_json(const json::Json& j) {
    Mission m;
    m.name = j.value("name", std::string("mission"));
    Result<geo::Cell> home = geo::cell_from_json(j.value("home", json::Json()));
    if (!home) return std::move(home).error().with("member", "home");
    m.home = home.value();
    for (const json::Json& t : j.value("targets", json::Json::array())) {
        Result<geo::Cell> cell = geo::cell_from_json(t.value("cell", json::Json()));
        if (!cell) return std::move(cell).error().with("member", "targets");
        m.targets.push_back(MissionTarget{t.value("id", std::string()), t.value("name", std::string()), cell.value()});
    }
    if (m.targets.empty()) return make_error(ErrorCode::ValidationError, "the mission has no targets");
    return m;
}

Status TwinWorldModel::reset(const json::Json& known_map) {
    Result<geo::OccupancyGrid> g = geo::grid_from_json(known_map.value("map", json::Json()));
    if (!g) return std::move(g).error();
    map_ = std::move(g).value();
    last_seq_ = known_map.value("seq", std::uint64_t{0});
    history_.clear();
    return ok_status();
}

std::vector<geo::CellChange> TwinWorldModel::apply(const geo::MapUpdate& update) {
    std::vector<geo::CellChange> changed;
    if (update.seq <= last_seq_) return changed;  // already applied (feed is idempotent)
    for (const geo::CellChange& c : update.cells) {
        if (map_.at(c.cell) != c.occupancy) {
            map_.set(c.cell, c.occupancy);
            changed.push_back(c);
        }
    }
    last_seq_ = update.seq;
    history_.push_back(update);
    if (history_.size() > 200) history_.erase(history_.begin());
    return changed;
}

std::size_t TwinWorldModel::unknown_cells() const {
    std::size_t n = 0;
    for (int y = 0; y < map_.height(); ++y) {
        for (int x = 0; x < map_.width(); ++x) {
            if (map_.at(geo::Cell{x, y}) == geo::Occupancy::Unknown) ++n;
        }
    }
    return n;
}

}  // namespace twin::runtime
