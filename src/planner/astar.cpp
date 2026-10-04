/**
 * @file astar.cpp
 * @brief Weighted A* on the occupancy grid with clearance-aware costs and
 *        line-of-sight waypoint simplification.
 */
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <queue>
#include <vector>

#include "twin/planner/planner.hpp"

namespace twin::planner {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kSqrt2 = 1.4142135623730951;

/// Multi-source BFS distance (metres, 8-connected chamfer) to the nearest blocking cell.
std::vector<double> clearance_field(const geo::OccupancyGrid& map) {
    const int w = map.width();
    const int h = map.height();
    std::vector<double> dist(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), kInf);
    using Node = std::pair<double, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> pq;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const geo::Occupancy o = map.at(geo::Cell{x, y});
            if (geo::is_blocking(o) && o != geo::Occupancy::Hazard) {
                const auto i = static_cast<std::size_t>(y * w + x);
                dist[i] = 0.0;
                pq.emplace(0.0, y * w + x);
            }
        }
    }
    const double cs = map.cell_size();
    while (!pq.empty()) {
        const auto [d, i] = pq.top();
        pq.pop();
        if (d > dist[static_cast<std::size_t>(i)]) continue;
        const int x = i % w;
        const int y = i / w;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const double nd = d + ((dx != 0 && dy != 0) ? kSqrt2 : 1.0) * cs;
                const auto ni = static_cast<std::size_t>(ny * w + nx);
                if (nd < dist[ni]) {
                    dist[ni] = nd;
                    pq.emplace(nd, ny * w + nx);
                }
            }
        }
    }
    return dist;
}

/// Distance (metres) to the nearest hazard cell, BFS limited to the margin.
std::vector<double> hazard_field(const geo::OccupancyGrid& map, double margin_m) {
    const int w = map.width();
    const int h = map.height();
    std::vector<double> dist(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), kInf);
    std::deque<int> q;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (map.at(geo::Cell{x, y}) == geo::Occupancy::Hazard) {
                dist[static_cast<std::size_t>(y * w + x)] = 0.0;
                q.push_back(y * w + x);
            }
        }
    }
    const double cs = map.cell_size();
    while (!q.empty()) {
        const int i = q.front();
        q.pop_front();
        const double d = dist[static_cast<std::size_t>(i)];
        if (d >= margin_m) continue;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = i % w + dx;
                const int ny = i / w + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const auto ni = static_cast<std::size_t>(ny * w + nx);
                if (dist[ni] > d + cs) {
                    dist[ni] = d + cs;
                    q.push_back(ny * w + nx);
                }
            }
        }
    }
    return dist;
}

/// Per-metre cost multiplier of flying through a cell.
class CostField {
public:
    CostField(const geo::OccupancyGrid& map, const CostWeights& w)
        : map_(map), w_(w), clearance_(clearance_field(map)), hazard_(hazard_field(map, w.hazard_margin_m)) {}

    [[nodiscard]] double multiplier(geo::Cell c) const {
        const auto i = static_cast<std::size_t>(c.y * map_.width() + c.x);
        double m = 1.0;
        if (w_.clearance_m > 0.0) {
            m += w_.proximity * std::max(0.0, 1.0 - clearance_[i] / w_.clearance_m);
        }
        if (map_.at(c) == geo::Occupancy::Unknown) m += w_.unknown;
        if (hazard_[i] <= w_.hazard_margin_m) m += w_.hazard;
        return m;
    }

private:
    const geo::OccupancyGrid& map_;
    CostWeights w_;
    std::vector<double> clearance_;
    std::vector<double> hazard_;
};

/// Segment check: every cell crossed must be passable (no corner cutting).
bool segment_clear(const geo::OccupancyGrid& map, geo::Cell a, geo::Cell b, bool allow_unknown) {
    const std::vector<geo::Cell> cells = geo::line_cells(a, b);
    for (std::size_t k = 0; k < cells.size(); ++k) {
        if (!passable(map.at(cells[k]), allow_unknown)) return false;
        if (k > 0 && cells[k].x != cells[k - 1].x && cells[k].y != cells[k - 1].y) {
            if (!passable(map.at(geo::Cell{cells[k].x, cells[k - 1].y}), allow_unknown) ||
                !passable(map.at(geo::Cell{cells[k - 1].x, cells[k].y}), allow_unknown)) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

double EnergyModel::flight_energy_wh(double length_m) const noexcept {
    const double power = hover_power_w + drag_coeff * cruise_speed_mps * cruise_speed_mps;
    return power * (length_m / cruise_speed_mps) / 3600.0;
}

bool passable(geo::Occupancy o, bool allow_unknown) noexcept {
    return geo::is_navigable(o) || (allow_unknown && o == geo::Occupancy::Unknown);
}

Plan AStarPlanner::plan(const PlanningProblem& p) {
    Plan out;
    if (p.map == nullptr) {
        out.failure = "no map";
        return out;
    }
    const geo::OccupancyGrid& map = *p.map;
    if (!map.contains(p.start) || !map.contains(p.goal)) {
        out.failure = "start or goal outside the map";
        return out;
    }
    if (!passable(map.at(p.goal), p.allow_unknown)) {
        out.failure = "the goal cell is not passable (" + std::string(geo::to_string(map.at(p.goal))) + ")";
        return out;
    }
    const int w = map.width();
    const int h = map.height();
    const double cs = map.cell_size();
    const CostField field(map, p.weights);
    auto index = [w](geo::Cell c) { return static_cast<std::size_t>(c.y * w + c.x); };
    auto heuristic = [&](geo::Cell c) {
        const double dx = std::abs(c.x - p.goal.x);
        const double dy = std::abs(c.y - p.goal.y);
        return cs * (std::max(dx, dy) + (kSqrt2 - 1.0) * std::min(dx, dy));
    };
    std::vector<double> g(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), kInf);
    std::vector<int> parent(g.size(), -1);
    using Node = std::pair<double, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> open;
    g[index(p.start)] = 0.0;
    open.emplace(heuristic(p.start), static_cast<int>(index(p.start)));
    while (!open.empty()) {
        const auto [f, i] = open.top();
        open.pop();
        const geo::Cell c{i % w, i / w};
        const double gc = g[static_cast<std::size_t>(i)];
        if (f > gc + heuristic(c) + 1e-9) continue;  // stale entry
        ++out.expanded;
        if (c == p.goal) break;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const geo::Cell n{c.x + dx, c.y + dy};
                if (!map.contains(n) || !passable(map.at(n), p.allow_unknown)) continue;
                const bool diagonal = dx != 0 && dy != 0;
                if (diagonal && (!passable(map.at(geo::Cell{c.x + dx, c.y}), p.allow_unknown) ||
                                 !passable(map.at(geo::Cell{c.x, c.y + dy}), p.allow_unknown))) {
                    continue;  // no corner cutting
                }
                const double step = (diagonal ? kSqrt2 : 1.0) * cs;
                const double cost = step * 0.5 * (field.multiplier(c) + field.multiplier(n));
                const double ng = gc + cost;
                if (ng < g[index(n)]) {
                    g[index(n)] = ng;
                    parent[index(n)] = i;
                    open.emplace(ng + heuristic(n), static_cast<int>(index(n)));
                }
            }
        }
    }
    if (g[index(p.goal)] == kInf) {
        out.failure = "no admissible path in the known map";
        return out;
    }
    for (int i = static_cast<int>(index(p.goal)); i != -1; i = parent[static_cast<std::size_t>(i)]) {
        out.cells.push_back(geo::Cell{i % w, i / w});
    }
    std::reverse(out.cells.begin(), out.cells.end());
    out.cost = g[index(p.goal)];
    out.found = true;

    // Line-of-sight simplification (string pulling over the cell path).
    std::size_t anchor = 0;
    while (anchor + 1 < out.cells.size()) {
        std::size_t next = anchor + 1;
        for (std::size_t k = out.cells.size() - 1; k > anchor + 1; --k) {
            if (segment_clear(map, out.cells[anchor], out.cells[k], p.allow_unknown)) {
                next = k;
                break;
            }
        }
        out.waypoints.push_back(map.center(out.cells[next]));
        anchor = next;
    }
    geo::Point prev = map.center(p.start);
    for (const geo::Point& wp : out.waypoints) {
        out.length_m += geo::distance(prev, wp);
        prev = wp;
    }
    for (std::size_t k = 1; k < out.cells.size(); ++k) {
        if (map.at(out.cells[k]) == geo::Occupancy::Unknown) {
            out.unknown_m += (out.cells[k].x != out.cells[k - 1].x && out.cells[k].y != out.cells[k - 1].y ? kSqrt2 : 1.0) * cs;
        }
    }
    out.energy_wh = energy_.flight_energy_wh(out.length_m);
    return out;
}

double route_cost(const geo::OccupancyGrid& map, geo::Point from, const std::vector<geo::Point>& waypoints,
                  const CostWeights& weights) {
    const CostField field(map, weights);
    double total = 0.0;
    geo::Point prev = from;
    for (const geo::Point& wp : waypoints) {
        const std::vector<geo::Cell> cells = geo::line_cells(map.cell_of(prev), map.cell_of(wp));
        const double seg = geo::distance(prev, wp);
        const double per_cell = cells.size() > 1 ? seg / static_cast<double>(cells.size() - 1) : 0.0;
        for (std::size_t k = 0; k < cells.size(); ++k) {
            if (!passable(map.at(cells[k]), true)) return kInf;
            if (k > 0) total += per_cell * field.multiplier(cells[k]);
        }
        prev = wp;
    }
    return total;
}

}  // namespace twin::planner
