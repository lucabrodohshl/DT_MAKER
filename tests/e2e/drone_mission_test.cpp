/**
 * @file drone_mission_test.cpp
 * @brief End-to-end validation of the indoor-drone demonstration mission.
 *
 * The whole production path runs: the verified package is built from the
 * models (compiler + aligner), the runtime's co-simulation driver steps the
 * Physical Twin through the world API, PT events are translated through the
 * verified label equivalence, the kernel decides every step inside the
 * session, the ledger records it, and the mission controller plans over the
 * twin-KNOWN map. The deterministic scenario (scenarios/inspection_default.json)
 * must produce the demonstration story: a closed fire door the facility plan
 * shows open is discovered, the route is invalidated, the twin enters
 * REPLANNING, a new route is selected and flown; a facility no-fly notice
 * forces a second replan; both targets are inspected; the drone returns and
 * lands; the ledger verifies and replays identically.
 *
 * These are validation tests of one scenario, not a proof (see proof/).
 * Set TWIN_E2E_VERBOSE=1 to print the semantic timeline.
 */
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "support/drone_package.hpp"
#include "support/in_process_world_port.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/ledger/verifier.hpp"
#include "twin/runtime/cosim_driver.hpp"
#include "twin/world/service.hpp"

namespace twin {
namespace {

using json::Json;

/// Everything observable about one run of the demonstration mission.
struct MissionRun {
    std::filesystem::path ledger;
    std::vector<Json> bodies;          ///< Ledger record bodies.
    std::vector<std::string> hashes;   ///< Ledger record hashes.
    std::vector<runtime::HubEvent> events;  ///< Everything published to observers.
    Json status;
    Json mission;
    Json plans;
    Json episodes;
    Json world_updates;                ///< The environment service's update feed (PT side).
    Json truth_state;                  ///< Ground-truth drone state (observer API).
    std::uint64_t ticks{0};
};

std::filesystem::path scenario_path() {
    return std::filesystem::path(TWIN_SOURCE_DIR) / "scenarios" / "inspection_default.json";
}

std::vector<std::string> read_lines(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::string location_after(const Json& body) {
    const Json& state = body.at("state_after");
    return state.empty() ? std::string() : state.at(0).value("location", std::string());
}

std::string step_label(const Json& body) {
    return body.contains("input") ? body.at("input").value("name", std::string()) : std::string();
}

const Json* first_branch(const Json& body) {
    if (!body.contains("outcome")) return nullptr;
    const Json& branches = body.at("outcome").at("branches");
    return branches.empty() ? nullptr : &branches.at(0);
}

void print_timeline(const MissionRun& run) {
    for (const Json& b : run.bodies) {
        const std::string kind = b.value("kind", std::string());
        const double t = static_cast<double>(b.value("time_after", Ticks{0})) / 1000.0;
        std::cout << "#" << b.value("seq", 0) << " t=" << t << " " << kind;
        if (kind == "step") {
            const Json* br = first_branch(b);
            std::cout << " " << step_label(b) << (br ? " " + br->value("source", std::string()) + " -> " +
                                                           br->value("target", std::string())
                                                     : std::string());
            const Json& payload = b.at("input").at("payload");
            if (payload.contains("reason")) std::cout << "  [" << payload.value("reason", std::string()) << "]";
        } else if (kind == "context") {
            const std::string topic = b.value("topic", std::string());
            std::cout << " " << topic;
            const Json& d = b.at("data");
            if (topic == "map_update") std::cout << "  " << d.value("description", std::string());
            if (topic == "planning") {
                std::cout << "  goal=" << d.value("goal", std::string()) << " selected=" << d.value("selected", std::string());
                for (const Json& c : d.at("candidates")) {
                    std::cout << "\n      " << c.value("label", std::string()) << " " << c.value("profile", std::string())
                              << " found=" << c.value("found", false) << " dup=" << c.value("duplicate_of", std::string())
                              << " geo=" << c.at("geometric").value("ok", false)
                              << " beh=" << c.at("behavioural").value("ok", false)
                              << " obj=" << (c.value("found", false) ? c.at("plan").value("objective_mm", 0) : 0)
                              << " len=" << (c.value("found", false) ? c.at("plan").value("length_mm", 0) : 0) << "  "
                              << c.at("behavioural").value("detail", std::string()) << " "
                              << (c.at("geometric").at("issues").empty() ? std::string()
                                                                         : c.at("geometric").at("issues").dump());
                }
            }
            if (topic == "command") {
                std::cout << "  " << d.at("command").value("kind", std::string())
                          << " route=" << d.at("command").value("route_id", 0) << " accepted=" << d.value("accepted", false);
            }
        } else if (kind == "reject") {
            std::cout << " " << step_label(b) << "  " << b.at("error").value("message", std::string());
        } else if (kind == "alarm") {
            std::cout << " " << b.value("alarm", std::string()) << " " << b.value("detail", std::string());
        }
        std::cout << "\n";
    }
}

MissionRun run_mission(const package::LoadedPackage& pkg, const std::filesystem::path& ledger_dir) {
    MissionRun run;
    Result<std::unique_ptr<world::WorldService>> opened = world::WorldService::open(scenario_path());
    if (!opened) {
        ADD_FAILURE() << opened.error().to_string();
        return run;
    }
    const std::shared_ptr<world::WorldService> service(std::move(opened).value());
    runtime::EventHub hub(1000000);  // keep every event for inspection
    runtime::DriverConfig config;
    config.deterministic = true;
    config.ledger_dir = ledger_dir;
    config.autostart = true;
    runtime::CoSimDriver driver(pkg, std::make_unique<test::InProcessWorldPort>(service), hub, config);
    if (Status s = driver.reset(); !s) {
        ADD_FAILURE() << s.error().to_string();
        return run;
    }
    for (int i = 0; i < 6000; ++i) {
        const std::string st = driver.status().value("status", std::string());
        if (st == "finished" || st == "failed") break;
        if (Status s = driver.tick(); !s) {
            ADD_FAILURE() << "tick failed: " << s.error().to_string();
            break;
        }
    }
    run.status = driver.status();
    run.mission = driver.mission();
    run.plans = driver.plans();
    run.episodes = driver.episodes();
    run.ticks = run.status.value("ticks", std::uint64_t{0});
    run.ledger = driver.session()->ledger_path();
    run.events = hub.wait_after(0, 0);
    run.world_updates = service->get("/env/map/updates").body;
    run.truth_state = service->get("/observer/state").body;
    for (const std::string& line : read_lines(run.ledger)) {
        Result<Json> j = json::parse(line);
        if (!j) {
            ADD_FAILURE() << "unparsable ledger line";
            continue;
        }
        run.bodies.push_back(j.value().at("body"));
        run.hashes.push_back(j.value().value("hash", std::string()));
    }
    return run;
}

class DroneMission : public test::DronePackageSuite {
public:
    static void SetUpTestSuite() {
        DronePackageSuite::SetUpTestSuite();
        ledger_dir_ = new test::TempDir();  // NOLINT(cppcoreguidelines-owning-memory)
        run_ = new MissionRun(run_mission(pkg(), ledger_dir_->path()));  // NOLINT
        if (const char* v = std::getenv("TWIN_E2E_VERBOSE"); v != nullptr && std::string(v) == "1") {
            print_timeline(*run_);
        }
    }
    static void TearDownTestSuite() {
        delete run_;         // NOLINT
        delete ledger_dir_;  // NOLINT
        run_ = nullptr;
        ledger_dir_ = nullptr;
        DronePackageSuite::TearDownTestSuite();
    }

protected:
    static const MissionRun& run() { return *run_; }

    /// Indices of step records labelled @p label.
    static std::vector<std::size_t> steps(const std::string& label) {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < run().bodies.size(); ++i) {
            const Json& b = run().bodies[i];
            if (b.value("kind", std::string()) == "step" && step_label(b) == label) out.push_back(i);
        }
        return out;
    }

    /// Indices of context records with @p topic.
    static std::vector<std::size_t> contexts(const std::string& topic) {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < run().bodies.size(); ++i) {
            const Json& b = run().bodies[i];
            if (b.value("kind", std::string()) == "context" && b.value("topic", std::string()) == topic) out.push_back(i);
        }
        return out;
    }

private:
    static inline test::TempDir* ledger_dir_ = nullptr;
    static inline MissionRun* run_ = nullptr;
};

TEST_F(DroneMission, CompletesBothInspectionsAndLands) {
    ASSERT_FALSE(run().bodies.empty());
    EXPECT_EQ(run().status.value("status", std::string()), "finished") << run().status.dump();
    EXPECT_EQ(location_after(run().bodies.back()), "LANDED");
    EXPECT_EQ(run().bodies.back().value("kind", std::string()), "end");
    for (const Json& t : run().mission.at("targets")) {
        EXPECT_EQ(t.value("status", std::string()), "inspected") << t.dump();
    }
    EXPECT_EQ(steps("target_reached!").size(), 2U);
    EXPECT_EQ(steps("inspection_complete!").size(), 2U);
    EXPECT_EQ(steps("home_reached!").size(), 1U);
    EXPECT_EQ(steps("landing_complete!").size(), 1U);
    // Physical outcome (ground truth, observer API): the drone is disarmed on the home pad.
    const Json& truth = run().truth_state.at("telemetry");
    EXPECT_EQ(truth.value("mode", std::string()), "FC_DISARMED");
    EXPECT_NEAR(static_cast<double>(truth.value("x_mm", 0)), 3250.0, 300.0);
    EXPECT_NEAR(static_cast<double>(truth.value("y_mm", 0)), 11750.0, 300.0);
}

TEST_F(DroneMission, InitialRouteIsPlannedOnTwinKnowledgeNotGroundTruth) {
    const std::vector<std::size_t> planning = contexts("planning");
    ASSERT_FALSE(planning.empty());
    const Json& first = run().bodies[planning.front()].at("data");
    EXPECT_EQ(first.value("decision", std::string()), "start_mission!");
    // The facility plan shows fire door FD-2 (column 31) open, so the selected initial route
    // crosses it — although the ground truth has it closed. The planner never sees the truth.
    bool crosses_fd2 = false;
    for (const Json& c : first.at("candidates")) {
        if (!c.value("selected", false)) continue;
        Json pts = c.at("plan").at("start_mm");
        std::vector<std::pair<std::int64_t, std::int64_t>> poly{{pts[0].get<std::int64_t>(), pts[1].get<std::int64_t>()}};
        for (const Json& w : c.at("plan").at("waypoints_mm")) poly.emplace_back(w[0].get<std::int64_t>(), w[1].get<std::int64_t>());
        for (std::size_t i = 1; i < poly.size(); ++i) {
            const auto [x0, y0] = poly[i - 1];
            const auto [x1, y1] = poly[i];
            if ((x0 < 15500 && x1 > 16000) || (x1 < 15500 && x0 > 16000)) {
                if (y0 > 6500 && y0 < 8000 && y1 > 6500 && y1 < 8000) crosses_fd2 = true;
            }
        }
    }
    EXPECT_TRUE(crosses_fd2) << "the initial route should use the corridor through FD-2";
}

TEST_F(DroneMission, DiscoveredClosedDoorInvalidatesRouteAndForcesReplanning) {
    // 1. The onboard sensing reveals closed-door cells in column 31 (FD-2).
    std::optional<std::size_t> discovery;
    for (std::size_t i : contexts("map_update")) {
        for (const Json& cell : run().bodies[i].at("data").at("cells")) {
            if (cell.at("cell")[0].get<int>() == 31 && cell.value("occupancy", std::string()) == "door_closed") {
                discovery = i;
                break;
            }
        }
        if (discovery) break;
    }
    ASSERT_TRUE(discovery.has_value()) << "FD-2 was never observed closed";
    EXPECT_EQ(run().bodies[*discovery].at("data").value("kind", std::string()), "sensor_observation");
    // 2. The next semantic decision is path_invalidated!: NAVIGATING -> REPLANNING.
    std::optional<std::size_t> invalidated;
    for (std::size_t i = *discovery + 1; i < run().bodies.size(); ++i) {
        if (run().bodies[i].value("kind", std::string()) == "step") {
            invalidated = i;
            break;
        }
    }
    ASSERT_TRUE(invalidated.has_value());
    const Json& inv = run().bodies[*invalidated];
    EXPECT_EQ(step_label(inv), "path_invalidated!");
    ASSERT_NE(first_branch(inv), nullptr);
    EXPECT_EQ(first_branch(inv)->value("source", std::string()), "NAVIGATING");
    EXPECT_EQ(first_branch(inv)->value("target", std::string()), "REPLANNING");
    EXPECT_NE(inv.at("input").at("payload").value("reason", std::string()).find("door_closed"), std::string::npos);
    // 3. A planning episode selects a replacement; plan_accepted!: REPLANNING -> NAVIGATING.
    std::optional<std::size_t> episode;
    std::optional<std::size_t> accepted;
    for (std::size_t i = *invalidated + 1; i < run().bodies.size(); ++i) {
        const Json& b = run().bodies[i];
        if (!episode && b.value("kind", std::string()) == "context" && b.value("topic", std::string()) == "planning") {
            episode = i;
        }
        if (b.value("kind", std::string()) == "step") {
            accepted = i;
            break;
        }
    }
    ASSERT_TRUE(episode.has_value());
    ASSERT_TRUE(accepted.has_value());
    EXPECT_LT(*episode, *accepted) << "the planning evidence precedes the decision it supports";
    const Json& acc = run().bodies[*accepted];
    EXPECT_EQ(step_label(acc), "plan_accepted!");
    EXPECT_EQ(first_branch(acc)->value("source", std::string()), "REPLANNING");
    EXPECT_EQ(first_branch(acc)->value("target", std::string()), "NAVIGATING");
    const Json& ep = run().bodies[*episode].at("data");
    EXPECT_GE(ep.at("candidates").size(), 2U);
    EXPECT_FALSE(ep.value("selected", std::string()).empty());
    const std::int64_t new_route = acc.at("input").at("payload").value("plan_id", std::int64_t{-1});
    // 4. The drone actually flies the new route (telemetry carries its id in autopilot mode).
    bool flown = false;
    for (const runtime::HubEvent& e : run().events) {
        if (e.type == "telemetry" && e.data.value("route_id", std::int64_t{0}) == new_route &&
            e.data.value("mode", std::string()) == "FC_AUTO_WP") {
            flown = true;
            break;
        }
    }
    EXPECT_TRUE(flown) << "route " << new_route << " was never flown";
}

TEST_F(DroneMission, FacilityNoFlyNoticeCausesSecondReplan) {
    const std::vector<std::size_t> invalidations = steps("path_invalidated!");
    ASSERT_GE(invalidations.size(), 2U);
    const Json& second = run().bodies[invalidations[1]];
    EXPECT_NE(second.at("input").at("payload").value("reason", std::string()).find("hazard"), std::string::npos)
        << second.dump();
    EXPECT_GE(steps("plan_accepted!").size(), 2U);
}

TEST_F(DroneMission, EverySemanticTransitionIsInTheLedgerAndItVerifies) {
    const ledger::VerificationReport report = ledger::verify_file(run().ledger, ledger::VerifyOptions{});
    EXPECT_TRUE(report.valid) << ledger::to_json(report).dump();
    EXPECT_TRUE(report.has_end_record);
    // Every record after the genesis record (written before observers can subscribe) is
    // published on the event stream exactly once, in order, with the ledger's hash.
    std::uint64_t expected_seq = 1;
    for (const runtime::HubEvent& e : run().events) {
        if (e.type != "ledger") continue;
        const auto seq = e.data.value("seq", std::uint64_t{0});
        EXPECT_EQ(seq, expected_seq++);
        ASSERT_LT(seq, run().hashes.size());
        EXPECT_EQ(e.data.value("hash", std::string()), run().hashes[seq]);
    }
    EXPECT_EQ(expected_seq, run().bodies.size());
    EXPECT_EQ(run().bodies.front().value("kind", std::string()), "genesis");
    // Every transition of the run is a step record (count by kind).
    std::size_t semantic_steps = 0;
    for (const Json& b : run().bodies) semantic_steps += b.value("kind", std::string()) == "step" ? 1 : 0;
    EXPECT_GE(semantic_steps, 20U);
}

TEST_F(DroneMission, ReplayReproducesTheExecutionExactly) {
    Result<ledger::ReplayReport> replay = ledger::replay_file(pkg(), run().ledger, ledger::ReplayOptions{true});
    ASSERT_TRUE(replay.ok()) << replay.error().to_string();
    EXPECT_TRUE(replay.value().identical) << ledger::to_json(replay.value()).dump().substr(0, 2000);
    EXPECT_EQ(replay.value().frames.size(), run().bodies.size());
    EXPECT_GT(replay.value().contexts, 0U);
    EXPECT_EQ(replay.value().final_state.at(0).value("location", std::string()), "LANDED");
}

/**
 * Cross-component traceability for one PT observation, end to end:
 * simulator event (poi_arrived!) -> input (E-translated label) -> semantic
 * interpretation (label equivalence in the alignment evidence) -> kernel
 * transition -> resulting state -> ledger record -> API/stream event.
 */
TEST_F(DroneMission, TraceabilityFromSimulatorEventToLedgerAndStream) {
    // Semantic interpretation: the verified alignment evidence maps the PT label to the DT label.
    bool in_e = false;
    for (const Json& row : pkg().alignment_evidence.at("label_equivalence")) {
        if (row.value("pt", std::string()) == "poi_arrived!" && row.at("dt").size() == 1 &&
            row.at("dt")[0] == "target_reached!") {
            in_e = true;
        }
    }
    ASSERT_TRUE(in_e);
    // Simulator observation, as received by the runtime.
    const runtime::HubEvent* pt = nullptr;
    for (const runtime::HubEvent& e : run().events) {
        if (e.type == "pt_event" && e.data.value("pt_label", std::string()) == "poi_arrived!") {
            pt = &e;
            break;
        }
    }
    ASSERT_NE(pt, nullptr);
    EXPECT_EQ(pt->data.value("dt_label", std::string()), "target_reached!");
    const Ticks at = pt->data.value("at", Ticks{0});
    // Kernel transition + state + ledger record.
    const std::vector<std::size_t> reached = steps("target_reached!");
    ASSERT_FALSE(reached.empty());
    const Json& rec = run().bodies[reached.front()];
    EXPECT_EQ(rec.at("input").value("source", std::string()), "pt-adapter");
    EXPECT_EQ(rec.at("input").value("at", Ticks{-1}), at);
    EXPECT_EQ(rec.at("input").at("payload").value("pt_label", std::string()), "poi_arrived!");
    EXPECT_EQ(first_branch(rec)->value("source", std::string()), "NAVIGATING");
    EXPECT_EQ(first_branch(rec)->value("target", std::string()), "INSPECTING");
    EXPECT_EQ(location_after(rec), "INSPECTING");
    bool prop = false;
    for (const Json& p : rec.at("propositions")) prop = prop || p == "at(INSPECTING)";
    EXPECT_TRUE(prop);
    // Stream: the observation verdict and the ledger summary carry the same record.
    const auto seq = rec.value("seq", std::uint64_t{0});
    bool streamed = false;
    for (const runtime::HubEvent& e : run().events) {
        if (e.type == "ledger" && e.data.value("seq", std::uint64_t{0}) == seq) {
            streamed = e.data.value("hash", std::string()) == run().hashes[seq] &&
                       e.data.value("to", std::string()) == "INSPECTING" &&
                       e.data.value("label", std::string()) == "target_reached!";
        }
    }
    EXPECT_TRUE(streamed);
}

}  // namespace
}  // namespace twin
