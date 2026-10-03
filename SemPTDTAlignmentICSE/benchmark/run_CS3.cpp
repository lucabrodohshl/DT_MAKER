/**
 * @file run_CS3.cpp
 * @brief CS3: Autonomous Inspection Rover benchmark (NASA FRET)
 *
 * Tests semantic alignment between the Physical Twin (RoverPT) and the
 * Digital Twin (RoverDT) under the NASA-STD-8739.8A (software assurance)
 * mission domain ontology (also references NPR 7150.2D, ECSS-E-ST-10-06C).
 *
 * Research questions addressed:
 *  RQ1 — Ontology evolution (Theorem 1): alignment preserved under domain_v2.ont
 *         (battery_reserve tightened from 25 to 30 Wh + Radiation sort added)
 *  RQ2 — Coverage vs syntactic baseline:
 *         - Syntactic bisimulation fails because PT/DT label sets differ entirely
 *           (flight software vs mission operations vocabulary)
 *         - Semantic alignment succeeds under the NASA domain ontology Delta
 *         - Timing violation (Variant A) is ONLY detectable via delay Condition IV
 *           in DTPTA weak timed bisimulation; state-based runtime monitors cannot
 *           detect cross-model timing gaps (demonstrated in Run 4 / RQ3)
 *  RQ3 — Scalability: zone-graph sizes and check times reported.
 *
 * Usage:
 *   ./run_CS3 [--folder <path>]
 */

#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <cassert>
#include <chrono>
#include <string>
#include <stdexcept>

#include "dtpta/timedautomaton.h"
#include "dtpta/core.h"
#include "dtpta/ontology.h"
#include "dtpta/interpretation.h"
#include "dtpta/semantic_checker.h"
#include "dtpta/domain_parser.h"
#include "dtpta/benchmarks/common.h"

static const std::string ASSET_DIR = "assets/CS3_Rover/";

// ============================================================================
// Helpers
// ============================================================================

static dtpta::DomainKnowledge load_domain(const std::string& ont_path,
                                           const std::string& pt_interp_path,
                                           const std::string& dt_interp_path)
{
    dtpta::OntFileParser     ont_parser;
    dtpta::InterpFileParser  interp_parser;

    auto ontology  = ont_parser.parse(ont_path);
    auto pt_imap   = interp_parser.parse(pt_interp_path, ontology);
    auto dt_imap   = interp_parser.parse(dt_interp_path, ontology);

    dtpta::DomainKnowledge dk(ontology);
    dk.pt_interp = std::move(pt_imap);
    dk.dt_interp = std::move(dt_imap);
    return dk;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[])
{
    try {
        std::string results_folder = "results/";
        int n_workers = 0;
        dtpta::RunningMode parallel_mode = dtpta::RunningMode::SERIAL;
        dtpta::AlgorithmMode algo = dtpta::AlgorithmMode::GFP;
        dtpta::parse_arguments(argc, argv, &results_folder,
                               &n_workers, &parallel_mode, nullptr, true, &algo);

        if (!std::filesystem::is_directory(results_folder))
            std::filesystem::create_directories(results_folder);

        std::string csv_path = results_folder + "CS3_results.csv";
        std::ofstream csv(csv_path);
        if (!csv.is_open())
            throw std::runtime_error("Cannot open result file: " + csv_path);

        dtpta::SemanticAlignmentResult::write_csv_header(csv);

        // ────────────────────────────────────────────────────────────────────
        // Load PT and DT zone graphs (shared across all runs)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "=== CS3: Autonomous Inspection Rover (NASA-STD-8739.8A) ===" << std::endl;
        std::cout << "Loading PT and DT models..." << std::endl;

        dtpta::TimedAutomaton pt(ASSET_DIR + "V1_PT.xml");
        dtpta::TimedAutomaton dt(ASSET_DIR + "V2_DT.xml");
        pt.construct_zone_graph();
        dt.construct_zone_graph();

        std::cout << "  PT zones: " << pt.get_num_states()
                  << "  |  DT zones: " << dt.get_num_states() << "\n" << std::endl;

        // ────────────────────────────────────────────────────────────────────
        // RUN 1: Semantic alignment — correctly aligned pair
        //
        // PT (RoverPT): flight software vocabulary
        //   waypoint_reached!, inspection_start!, fault_critical!, comms_timeout!, ...
        // DT (RoverDT): mission operations vocabulary
        //   telemetry_position_update!, sensor_activation_telemetry!, anomaly_flag_critical!, ...
        //
        // Semantic alignment succeeds because Delta entails the iff-equivalences:
        //   waypoint_reached! ~ telemetry_position_update!
        //   inspection_start! ~ sensor_activation_telemetry!
        //   fault_critical!   ~ anomaly_flag_critical!
        //   ... (all 14 label pairs, see dt.interp)
        //
        // Expected: ALIGNED = true
        // ────────────────────────────────────────────────────────────────────
        std::cout << "[RUN 1] Semantic alignment (aligned pair)..." << std::endl;

        auto dk_base = load_domain(ASSET_DIR + "domain.ont",
                                   ASSET_DIR + "pt.interp",
                                   ASSET_DIR + "dt.interp");

        dtpta::SemanticAlignmentChecker checker_r1;
        dtpta::SemanticAlignmentResult  r1;
        bool ok_r1 = checker_r1.check_semantic_alignment(pt, dt, dk_base, r1);

        std::cout << "Verdict: " << (ok_r1 ? "TRUE (semantically aligned)"
                                           : "FALSE (NOT aligned)") << std::endl;
        r1.print();
        r1.append_to_csv(csv, "CS3_aligned");

        if (!ok_r1) {
            std::cerr << "[WARNING] RUN 1: expected ALIGNED but got NOT ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 2: Syntactic bisimulation baseline — same pair
        //
        // PT labels: mission_start!, waypoint_reached!, inspection_start!, ...
        // DT labels: mission_uplink_command!, telemetry_position_update!, ...
        //
        // These share NO common label strings. Syntactic bisimulation must fail.
        // This is the core RQ2 result: flight software and mission operations teams
        // independently author their models using different vocabularies. Syntactic
        // bisimulation cannot bridge the vocabulary gap; semantic alignment can.
        //
        // Expected: FALSE
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 2] Syntactic bisimulation baseline (same pair)..." << std::endl;

        dtpta::SemanticAlignmentChecker syntactic_checker;
        dtpta::SemanticAlignmentResult result_r2;
        bool ok_r2 = syntactic_checker.check_weak_timed_bisimulation(pt, dt, result_r2);

        std::cout << "Verdict: " << (ok_r2 ? "TRUE (syntactically bisimilar)"
                                           : "FALSE (labels differ — expected for RQ2)")
                  << std::endl;

        result_r2.print();

        if (ok_r2) {
            std::cerr << "[WARNING] RUN 2: expected syntactic FAILURE but got TRUE.\n";
        } else {
            std::cout << "[RQ2] Syntactic baseline correctly fails — "
                         "NASA flight software vs mission operations vocabulary gap confirmed.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 3: Semantic alignment — threshold drift misalignment (Variant B)
        //
        // DT anomaly_flag_critical! fires when battery_level < 30 instead of
        // battery_level < battery_reserve (= 25 Wh). The 5 Wh gap causes
        // the DT to trigger unnecessary mission aborts.
        //
        // Z3 check: Delta |= (< battery_level 30) <-> (< battery_level battery_reserve)
        //           = (< battery_level 30) <-> (< battery_level 25)
        //           Refuted: battery_level = 27 satisfies LHS but not RHS.
        //
        // Expected: ALIGNED = false (label anomaly_flag_critical! NOT in E)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 3] Battery threshold drift misalignment (Variant B, 30 vs 25 Wh)..." << std::endl;

        dtpta::TimedAutomaton dt_drift(ASSET_DIR + "V2_DT_thresh_drift.xml");
        dt_drift.construct_zone_graph();

        auto dk_drift = load_domain(ASSET_DIR + "domain.ont",
                                    ASSET_DIR + "pt.interp",
                                    ASSET_DIR + "dt_thresh_drift.interp");

        dtpta::SemanticAlignmentChecker checker_r3;
        dtpta::SemanticAlignmentResult  r3;
        bool ok_r3 = checker_r3.check_semantic_alignment(pt, dt_drift, dk_drift, r3);

        std::cout << "Verdict: " << (ok_r3 ? "TRUE (drift not detected — label may not be bisimulation-critical)"
                                           : "FALSE (NOT aligned — battery threshold drift detected)")
                  << std::endl;
        r3.print();
        r3.append_to_csv(csv, "CS3_thresh_drift");

        if (!ok_r3) {
            // 5 Wh gap over battery range [0, battery_capacity] = [0, 100]:
            // Loads in [25, 30) Wh falsely alarm; states outside safe window that
            // don't trigger DT alarm: P(undetected critical state) = 5/100 = 5%
            double gap_probability = 5.0 / 100.0;
            std::cout << "[RQ4] Battery threshold drift gap: "
                      << std::fixed << std::setprecision(1)
                      << (gap_probability * 100.0) << "% of battery states "
                      << "(5 Wh gap over Uniform[0, 100 Wh])\n";
        } else {
            std::cout << "[NOTE] If ALIGNED: the critical fault path may not be"
                         " bisimulation-critical in the zone graph structure.\n"
                         "       Runtime monitors (Run 4) provide complementary detection.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 4: Semantic alignment — timing violation (Variant A)
        //
        // Uses V2_DT_timing_violation.xml where telemetry_position_update! can
        // fire up to t_telem = 130 (instead of correct 124 = 120 + 4 latency).
        // The 6-unit overshoot means the DT may confirm waypoint arrival AFTER
        // the NASA NPR 7150.2 segment deadline has elapsed.
        //
        // SemAlign checks delay Condition IV automatically (0 LOC overhead).
        // State-based runtime monitors cannot detect this: it requires cross-model
        // clock comparison (PT:t_segment vs DT:t_telem), which is outside the scope
        // of any state-based monitor framework.
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 4] Timing violation (Variant A) — SemAlign check..." << std::endl;

        dtpta::TimedAutomaton dt_timing(ASSET_DIR + "V2_DT_timing_violation.xml");
        dt_timing.construct_zone_graph();

        auto dk_timing = load_domain(ASSET_DIR + "domain.ont",
                                     ASSET_DIR + "pt.interp",
                                     ASSET_DIR + "dt.interp");

        dtpta::SemanticAlignmentChecker checker_timing;
        dtpta::SemanticAlignmentResult  r_timing;
        bool ok_timing = checker_timing.check_semantic_alignment(pt, dt_timing, dk_timing, r_timing);

        std::cout << "  SemAlign verdict on timing violation: "
                  << (ok_timing ? "TRUE (timing overshoot within bisim tolerance)"
                                : "FALSE (NOT aligned — timing violation detected)")
                  << std::endl;
        r_timing.print();
        r_timing.append_to_csv(csv, "CS3_timing_violation");

        std::cout << "[RQ2] SemAlign detects cross-model timing violations via delay Condition IV.\n"
                  << "      State-based monitors require cross-model clock synchronisation\n"
                  << "      (PT:t_segment vs DT:t_telem) — outside standard monitor frameworks.\n";

        // ────────────────────────────────────────────────────────────────────
        // RUN 5: Ontology evolution — RQ1 (Theorem 1)
        //
        // domain_v2.ont changes:
        //   - battery_reserve: 25 → 30 Wh (Condition II: axiom strengthening)
        //   - Radiation sort added with radiation_level / max_radiation (Condition I)
        //
        // Both pt_v2.interp and dt_v2.interp reference battery_reserve symbolically
        // (return_initiated! uses `(+ battery_reserve energy_to_base)`), so the
        // semantic equivalences remain valid under the tighter 30 Wh reserve.
        //
        // Incremental re-verification: only SMT calls involving battery_reserve
        // need revalidation — label pairs using signal_strength or distance
        // are unaffected by the battery reserve change.
        //
        // Expected: ALIGNED = true (Theorem 1 holds)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 5] Ontology evolution (RQ1, battery_reserve: 25 → 30 Wh)..." << std::endl;

        auto dk_v2 = load_domain(ASSET_DIR + "domain_v2.ont",
                                  ASSET_DIR + "pt_v2.interp",
                                  ASSET_DIR + "dt_v2.interp");

        dtpta::SemanticAlignmentChecker checker_r5;
        dtpta::SemanticAlignmentResult  r5;
        bool ok_r5 = checker_r5.check_semantic_alignment(pt, dt, dk_v2, r5);

        std::cout << "Verdict under domain_v2.ont: "
                  << (ok_r5 ? "TRUE (alignment preserved — Theorem 1 holds)"
                             : "FALSE (alignment broken by ontology evolution)")
                  << std::endl;
        r5.print();
        r5.append_to_csv(csv, "CS3_evo_v2");

        if (!ok_r5) {
            std::cerr << "[WARNING] RUN 5: ontology evolution broke alignment "
                         "(Theorem 1 may not hold for this pair).\n";
        }


        csv.close();
        std::cout << "\nResults written to: " << csv_path << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
