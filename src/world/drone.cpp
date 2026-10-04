/**
 * @file drone.cpp
 * @brief Path-following kinematics, battery, inspection, sensing and braking.
 *
 * Motion model: the drone moves exactly along the commanded polyline (no
 * lateral drift), with a scalar speed bounded by max speed, max acceleration,
 * and a corner speed limit that decreases with the turn angle. This keeps the
 * planned clearance and gives credible mission timing without aerodynamics.
 */
#include "twin/world/drone.hpp"

#include <algorithm>
#include <cmath>

namespace twin::world {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::int64_t mm(double metres) { return static_cast<std::int64_t>(std::llround(metres * 1000.0)); }

double turn_angle_deg(geo::Point a, geo::Point b, geo::Point c) {
    const double ux = b.x - a.x;
    const double uy = b.y - a.y;
    const double vx = c.x - b.x;
    const double vy = c.y - b.y;
    const double nu = std::hypot(ux, uy);
    const double nv = std::hypot(vx, vy);
    if (nu < 1e-9 || nv < 1e-9) return 0.0;
    const double cosang = std::clamp((ux * vx + uy * vy) / (nu * nv), -1.0, 1.0);
    return std::acos(cosang) * 180.0 / kPi;
}

}  // namespace

const char* to_string(FcMode mode) noexcept {
    switch (mode) {
        case FcMode::Boot: return "FC_BOOT";
        case FcMode::Idle: return "FC_IDLE";
        case FcMode::Arming: return "FC_ARMING";
        case FcMode::Climb: return "FC_CLIMB";
        case FcMode::Auto: return "FC_AUTO_WP";
        case FcMode::Hold: return "FC_HOLD";
        case FcMode::Brake: return "FC_BRAKE_HOLD";
        case FcMode::Survey: return "FC_SURVEY";
        case FcMode::Descent: return "FC_DESCENT";
        case FcMode::Disarmed: return "FC_DISARMED";
    }
    return "FC_BOOT";
}

json::Json to_json(const Command& c) {
    static constexpr const char* kKinds[] = {"upload_mission", "arm_takeoff", "follow_route", "hold", "inspect", "land"};
    json::Json wps = json::Json::array();
    for (const geo::Point& p : c.waypoints) wps.push_back(json::Json::array({mm(p.x), mm(p.y)}));
    return json::Json{{"kind", kKinds[static_cast<int>(c.kind)]},
                      {"waypoints_mm", wps},
                      {"goal", c.goal},
                      {"route_id", c.route_id},
                      {"target_id", c.target_id}};
}

Result<Command> command_from_json(const json::Json& j) {
    Command c;
    const std::string kind = j.value("kind", std::string());
    if (kind == "upload_mission") c.kind = Command::Kind::UploadMission;
    else if (kind == "arm_takeoff") c.kind = Command::Kind::ArmTakeoff;
    else if (kind == "follow_route") c.kind = Command::Kind::FollowRoute;
    else if (kind == "hold") c.kind = Command::Kind::Hold;
    else if (kind == "inspect") c.kind = Command::Kind::Inspect;
    else if (kind == "land") c.kind = Command::Kind::Land;
    else return make_error(ErrorCode::InvalidArgument, "unknown command kind").with("kind", kind);
    for (const json::Json& w : j.value("waypoints_mm", json::Json::array())) {
        if (!w.is_array() || w.size() != 2) return make_error(ErrorCode::InvalidArgument, "waypoint is [x_mm, y_mm]");
        c.waypoints.push_back(geo::Point{w[0].get<double>() / 1000.0, w[1].get<double>() / 1000.0});
    }
    c.goal = j.value("goal", std::string());
    c.route_id = j.value("route_id", std::int64_t{0});
    c.target_id = j.value("target_id", std::string());
    return c;
}

json::Json to_json(const Telemetry& t) {
    return json::Json{{"at", t.at},
                      {"x_mm", mm(t.position.x)},
                      {"y_mm", mm(t.position.y)},
                      {"vx_mm_s", mm(t.velocity.x)},
                      {"vy_mm_s", mm(t.velocity.y)},
                      {"alt_mm", mm(t.altitude_m)},
                      {"battery_permille", static_cast<std::int64_t>(std::llround(t.battery_pct * 10.0))},
                      {"energy_mwh", static_cast<std::int64_t>(std::llround(t.energy_wh * 1000.0))},
                      {"heading_cdeg", static_cast<std::int64_t>(std::llround(t.heading_deg * 100.0))},
                      {"gimbal_cdeg", static_cast<std::int64_t>(std::llround(t.gimbal_deg * 100.0))},
                      {"mode", to_string(t.mode)},
                      {"route_id", t.route_id},
                      {"waypoint_index", t.waypoint_index},
                      {"goal", t.goal}};
}

DroneSimulator::DroneSimulator(const Scenario& scenario, const geo::OccupancyGrid* ground_truth)
    : scenario_(scenario), truth_(ground_truth), spec_(scenario.drone) {
    pos_ = truth_->center(scenario.home);
    energy_wh_ = spec_.battery_capacity_wh * spec_.battery_start_pct / 100.0;
}

Status DroneSimulator::command(const Command& c) {
    auto refuse = [&](const char* why) {
        return make_error(ErrorCode::StateError, why).with("mode", to_string(mode_));
    };
    switch (c.kind) {
        case Command::Kind::UploadMission:
            if (mode_ != FcMode::Boot && mode_ != FcMode::Idle) return refuse("mission upload only on the ground");
            mode_ = FcMode::Idle;
            return ok_status();
        case Command::Kind::ArmTakeoff:
            if (mode_ != FcMode::Idle) return refuse("arm/take-off requires an uploaded mission");
            mode_ = FcMode::Arming;
            mode_timer_s_ = 0.0;
            return ok_status();
        case Command::Kind::FollowRoute:
            if (mode_ != FcMode::Hold && mode_ != FcMode::Auto && mode_ != FcMode::Brake) {
                return refuse("routes are accepted only while airborne and not inspecting");
            }
            if (c.waypoints.empty()) return refuse("empty route");
            route_ = c.waypoints;
            next_wp_ = 0;
            goal_ = c.goal;
            route_id_ = c.route_id;
            braked_for_obstacle_ = false;
            mode_ = FcMode::Auto;
            return ok_status();
        case Command::Kind::Hold:
            if (mode_ == FcMode::Auto || mode_ == FcMode::Brake) mode_ = FcMode::Hold;
            vel_ = geo::Point{};
            return ok_status();
        case Command::Kind::Inspect:
            if (mode_ != FcMode::Hold) return refuse("inspection requires hovering at the target");
            inspecting_ = c.target_id;
            gimbal_deg_ = 0.0;
            mode_ = FcMode::Survey;
            return ok_status();
        case Command::Kind::Land:
            if (mode_ != FcMode::Hold) return refuse("landing requires hovering");
            mode_ = FcMode::Descent;
            return ok_status();
    }
    return refuse("unknown command");
}

void DroneSimulator::drain(double dt_s, double speed) {
    const bool airborne = mode_ != FcMode::Boot && mode_ != FcMode::Idle && mode_ != FcMode::Disarmed;
    if (!airborne) return;
    const double power = spec_.hover_power_w + spec_.drag_coeff * speed * speed;
    energy_wh_ = std::max(0.0, energy_wh_ - power * dt_s / 3600.0);
}

bool DroneSimulator::blocked_ahead(geo::Point dir) const {
    for (double s = 0.1; s <= spec_.proximity_range_m + 1e-9; s += 0.1) {
        const geo::Point p{pos_.x + dir.x * s, pos_.y + dir.y * s};
        const geo::Occupancy o = truth_->at(truth_->cell_of(p));
        if (geo::blocks_sight(o)) return true;  // walls, obstacles, closed doors (hazards are invisible)
    }
    return false;
}

void DroneSimulator::fly(Ticks at, double dt_s, std::vector<PtEvent>& events) {
    if (next_wp_ >= route_.size()) {
        mode_ = FcMode::Hold;
        vel_ = geo::Point{};
        return;
    }
    geo::Point target = route_[next_wp_];
    double dx = target.x - pos_.x;
    double dy = target.y - pos_.y;
    double dist = std::hypot(dx, dy);
    const geo::Point dir = dist > 1e-9 ? geo::Point{dx / dist, dy / dist} : geo::Point{};
    if (blocked_ahead(dir)) {
        vel_ = geo::Point{};
        mode_ = FcMode::Brake;
        braked_for_obstacle_ = true;
        const geo::Cell ahead = truth_->cell_of(geo::Point{pos_.x + dir.x * spec_.proximity_range_m,
                                                           pos_.y + dir.y * spec_.proximity_range_m});
        events.push_back(PtEvent{at, "proximity_brake!", json::Json{{"cell", geo::to_json(ahead)}}});
        return;
    }
    // Corner speed limit at the next waypoint.
    double corner_limit = 0.0;  // stop at the final waypoint
    if (next_wp_ + 1 < route_.size()) {
        const double turn = turn_angle_deg(pos_, target, route_[next_wp_ + 1]);
        corner_limit = spec_.max_speed_mps * std::clamp((180.0 - turn) / 180.0, 0.25, 1.0);
    }
    const double speed = std::hypot(vel_.x, vel_.y);
    const double brake_limit = std::sqrt(corner_limit * corner_limit + 2.0 * spec_.max_accel_mps2 * dist);
    const double new_speed =
        std::max(0.05, std::min({spec_.max_speed_mps, brake_limit, speed + spec_.max_accel_mps2 * dt_s}));
    double travel = new_speed * dt_s;
    heading_deg_ = std::atan2(dir.y, dir.x) * 180.0 / kPi;
    // Advance along the polyline, possibly passing several waypoints in one step.
    while (travel > 0.0 && next_wp_ < route_.size()) {
        target = route_[next_wp_];
        dx = target.x - pos_.x;
        dy = target.y - pos_.y;
        dist = std::hypot(dx, dy);
        if (dist <= travel || dist <= spec_.waypoint_tolerance_m) {
            pos_ = target;
            travel -= dist;
            const bool last = next_wp_ + 1 == route_.size();
            json::Json detail{{"route_id", route_id_}, {"index", static_cast<std::int64_t>(next_wp_)}};
            if (last && goal_.rfind("target:", 0) == 0) {
                detail["target"] = goal_.substr(7);
                events.push_back(PtEvent{at, "poi_arrived!", detail});
            } else if (last && goal_ == "home") {
                events.push_back(PtEvent{at, "home_arrived!", detail});
            } else {
                events.push_back(PtEvent{at, "wp_arrived!", detail});
            }
            ++next_wp_;
            if (last) {
                mode_ = FcMode::Hold;
                vel_ = geo::Point{};
                return;
            }
        } else {
            pos_.x += dx / dist * travel;
            pos_.y += dy / dist * travel;
            travel = 0.0;
        }
    }
    vel_ = geo::Point{dir.x * new_speed, dir.y * new_speed};
}

std::vector<PtEvent> DroneSimulator::step(Ticks now_after, Ticks dt) {
    std::vector<PtEvent> events;
    const double dt_s = static_cast<double>(dt) / static_cast<double>(kWorldTime.ticks_per_unit);
    mode_timer_s_ += dt_s;
    switch (mode_) {
        case FcMode::Boot:
        case FcMode::Idle:
        case FcMode::Disarmed:
            break;
        case FcMode::Arming:
            if (mode_timer_s_ >= spec_.spin_up_s) {
                mode_ = FcMode::Climb;  // internal step (tau in V_P)
                mode_timer_s_ = 0.0;
            }
            break;
        case FcMode::Climb:
            altitude_ = std::min(spec_.cruise_height_m, altitude_ + spec_.climb_rate_mps * dt_s);
            if (altitude_ >= spec_.cruise_height_m) {
                mode_ = FcMode::Hold;
                events.push_back(PtEvent{now_after, "altitude_reached!", json::Json{{"altitude_mm", mm(altitude_)}}});
            }
            break;
        case FcMode::Auto:
            fly(now_after, dt_s, events);
            break;
        case FcMode::Hold:
            vel_ = geo::Point{};
            break;
        case FcMode::Brake:
            if (braked_for_obstacle_ && next_wp_ < route_.size()) {
                const geo::Point t = route_[next_wp_];
                const double d = std::hypot(t.x - pos_.x, t.y - pos_.y);
                const geo::Point dir = d > 1e-9 ? geo::Point{(t.x - pos_.x) / d, (t.y - pos_.y) / d} : geo::Point{};
                if (!blocked_ahead(dir)) {
                    braked_for_obstacle_ = false;
                    mode_ = FcMode::Auto;
                    events.push_back(PtEvent{now_after, "path_clear!", json::Json::object()});
                }
            }
            break;
        case FcMode::Survey:
            gimbal_deg_ = std::min(360.0, gimbal_deg_ + spec_.scan_rate_deg_s * dt_s);
            if (gimbal_deg_ >= 360.0) {
                mode_ = FcMode::Hold;
                events.push_back(PtEvent{now_after, "survey_done!", json::Json{{"target", inspecting_}}});
                inspecting_.clear();
            }
            break;
        case FcMode::Descent:
            altitude_ = std::max(0.0, altitude_ - spec_.climb_rate_mps * dt_s);
            if (altitude_ <= 0.05) {
                altitude_ = 0.0;
                mode_ = FcMode::Disarmed;
                events.push_back(PtEvent{now_after, "touchdown_disarm!", json::Json::object()});
            }
            break;
    }
    drain(dt_s, std::hypot(vel_.x, vel_.y));
    // Firmware battery reserve: remaining <= energy to fly home (+30 % detour) + reserve.
    const bool airborne = mode_ != FcMode::Boot && mode_ != FcMode::Idle && mode_ != FcMode::Disarmed;
    if (airborne && !reserve_reported_) {
        const double home_m = 1.3 * geo::distance(pos_, truth_->center(scenario_.home));
        const double power = spec_.hover_power_w + spec_.drag_coeff * spec_.max_speed_mps * spec_.max_speed_mps;
        const double rtl_wh = power * (home_m / spec_.max_speed_mps) / 3600.0;
        const double rtl_pct = 100.0 * rtl_wh / spec_.battery_capacity_wh;
        const double pct = 100.0 * energy_wh_ / spec_.battery_capacity_wh;
        if (pct <= spec_.reserve_pct + rtl_pct) {
            reserve_reported_ = true;
            events.push_back(PtEvent{now_after, "battery_reserve_reached!",
                                     json::Json{{"battery_permille", static_cast<std::int64_t>(std::llround(pct * 10))},
                                                {"rtl_permille", static_cast<std::int64_t>(std::llround(rtl_pct * 10))}}});
        }
    }
    return events;
}

Telemetry DroneSimulator::telemetry(Ticks at) const {
    Telemetry t;
    t.at = at;
    t.position = pos_;
    t.velocity = vel_;
    t.altitude_m = altitude_;
    t.energy_wh = energy_wh_;
    t.battery_pct = 100.0 * energy_wh_ / spec_.battery_capacity_wh;
    t.heading_deg = heading_deg_;
    t.gimbal_deg = gimbal_deg_;
    t.mode = mode_;
    t.route_id = route_id_;
    t.waypoint_index = static_cast<int>(next_wp_);
    t.goal = goal_;
    return t;
}

std::vector<geo::CellChange> DroneSimulator::sense() const {
    std::vector<geo::CellChange> seen;
    const geo::Cell me = truth_->cell_of(pos_);
    const int r = static_cast<int>(std::ceil(spec_.sensor_range_m / truth_->cell_size()));
    for (int y = me.y - r; y <= me.y + r; ++y) {
        for (int x = me.x - r; x <= me.x + r; ++x) {
            const geo::Cell c{x, y};
            if (!truth_->contains(c)) continue;
            if (geo::distance(truth_->center(c), pos_) > spec_.sensor_range_m) continue;
            bool visible = true;
            for (const geo::Cell& on_ray : geo::line_cells(me, c)) {
                if (on_ray == c) break;
                if (on_ray != me && geo::blocks_sight(truth_->at(on_ray))) {
                    visible = false;
                    break;
                }
            }
            if (!visible) continue;
            const geo::Occupancy o = truth_->at(c);
            if (o == geo::Occupancy::Hazard) continue;  // not observable by lidar
            seen.push_back(geo::CellChange{c, o});
        }
    }
    return seen;
}

}  // namespace twin::world
