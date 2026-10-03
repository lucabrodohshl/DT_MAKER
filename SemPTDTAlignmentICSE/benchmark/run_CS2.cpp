/**
 * @file run_CS2.cpp
 * @brief CS2: Overhead Travelling Crane benchmark (Kamburjan et al., ISoLA 2022)
 *
 * Tests semantic alignment between the Physical Twin (CranePT) and the
 * Digital Twin (CraneDT) under the IEC 60204-1 / EN 13001 crane safety ontology.
 *
 * Research questions addressed:
 *  RQ1 — Ontology evolution (Theorem 1): alignment preserved under domain_v2.ont
 *         (rated_load tightened from 500 kg to 475 kg + Vibration sort added)
 *  RQ2 — Coverage vs syntactic baseline: syntactic bisimulation fails because
 *         PT/DT label sets differ; semantic alignment succeeds under Delta.
 *  RQ3 — Scalability: zone-graph sizes reported.
 *
 * Usage:
 *   ./run_CS2 [--results <folder>]
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

static const std::string ASSET_DIR = "assets/CS2_Crane/";

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

        std::string csv_path = results_folder + "CS2_results.csv";
        std::ofstream csv(csv_path);
        if (!csv.is_open())
            throw std::runtime_error("Cannot open result file: " + csv_path);

        dtpta::SemanticAlignmentResult::write_csv_header(csv);

        // ────────────────────────────────────────────────────────────────────
        // Load PT and DT zone graphs (shared across all runs)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "=== CS2: Overhead Travelling Crane (IEC 60204-1 / EN 13001) ===" << std::endl;
        std::cout << "Loading PT and DT models..." << std::endl;

        dtpta::TimedAutomaton pt(ASSET_DIR + "V1_PT.xml");
        dtpta::TimedAutomaton dt(ASSET_DIR + "V2_DT.xml");
        pt.construct_zone_graph();
        dt.construct_zone_graph();

        std::cout << "  PT zones: " << pt.get_num_states()
                  << "  |  DT zones: " << dt.get_num_states() << "\n" << std::endl;

        // ────────────────────────────────────────────────────────────────────
        // RUN 1: Semantic alignment — correctly aligned pair
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
        r1.append_to_csv(csv, "CS2_aligned");

        if (!ok_r1) {
            std::cerr << "[WARNING] RUN 1: expected ALIGNED but got NOT ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 2: Syntactic bisimulation baseline — same pair
        // Expected: NOT aligned (PT labels: hoist_up!, hoist_down!,
        //           emergency_stop!, etc.; DT labels: hoist_command!,
        //           cycle_logged!, etc. — different namespaces)
        // This is the RQ2 key result: syntactic misses semantic equivalence.
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
            std::cerr << "[WARNING] RUN 2: expected syntactic FAILURE but got TRUE "
                         "(labels may have been matched by accident).\n";
        } else {
            std::cout << "[RQ2] Syntactic baseline correctly fails — semantic alignment "
                         "is strictly more powerful.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 3: Semantic alignment — threshold drift misalignment (Variant A)
        // DT fires load_exceedance_event! at current_load > 480 instead of
        // current_load > rated_load (= 500).
        // Expected: ALIGNED = false
        // Z3 refutes: Delta |= (> current_load 480) <-> (> current_load 500)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 3] Threshold drift misalignment (Variant A, 480 vs 500 kg)..." << std::endl;

        dtpta::TimedAutomaton dt_drift(ASSET_DIR + "V2_DT_thresh_drift.xml");
        dt_drift.construct_zone_graph();

        auto dk_drift = load_domain(ASSET_DIR + "domain.ont",
                                    ASSET_DIR + "pt.interp",
                                    ASSET_DIR + "dt_thresh_drift.interp");

        dtpta::SemanticAlignmentChecker checker_r3;
        dtpta::SemanticAlignmentResult  r3;
        bool ok_r3 = checker_r3.check_semantic_alignment(pt, dt_drift, dk_drift, r3);

        std::cout << "Verdict: " << (ok_r3 ? "TRUE (aligned — threshold drift not detected)"
                                           : "FALSE (NOT aligned — threshold drift detected)")
                  << std::endl;
        r3.print();
        r3.append_to_csv(csv, "CS2_thresh_drift");

        if (ok_r3) {
            std::cerr << "[WARNING] RUN 3: expected NOT ALIGNED but got ALIGNED. "
                         "Check load_exceedance_event! formula in dt_thresh_drift.interp.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 3B: Missing emergency stop event (Variant B — RQ2)
        //
        // dt_missing_estop.interp: protective_stop_command! entry is ABSENT.
        // The DT engineer omitted the interpretation for the emergency stop event,
        // breaking the ontological bridge for the most safety-critical event.
        //
        // Expected: NOT ALIGNED (emergency_stop! is A-unmatched)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 3B] Missing emergency stop event (Variant B — protective_stop_command omitted)..." << std::endl;

        auto dk_missing = load_domain(ASSET_DIR + "domain.ont",
                                      ASSET_DIR + "pt.interp",
                                      ASSET_DIR + "dt_missing_estop.interp");

        dtpta::SemanticAlignmentChecker checker_r3b;
        dtpta::SemanticAlignmentResult  r3b;
        bool ok_r3b = checker_r3b.check_semantic_alignment(pt, dt, dk_missing, r3b);

        std::cout << "Verdict: " << (ok_r3b ? "TRUE (missing event not detected — unexpected)"
                                            : "FALSE (NOT aligned — missing event detected, as expected)")
                  << std::endl;
        r3b.print();
        r3b.append_to_csv(csv, "CS2_missing_estop");

        if (!ok_r3b) {
            std::cout << "[RQ2] Missing emergency_stop! (A-unmatched) correctly detected.\n"
                      << "      protective_stop_command! (DT) has no PT semantic counterpart.\n";
        } else {
            std::cerr << "[WARNING] RUN 3B: expected NOT ALIGNED but got ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 4: Ontology evolution — RQ1 (Theorem 1)
        // domain_v2.ont: rated_load tightened from 500 to 475 kg,
        //                Vibration sort added with vibration_level / max_vibration.
        // Both pt_v2.interp and dt_v2.interp reference rated_load symbolically,
        // so the semantic equivalences remain valid under the tighter value.
        // Expected: ALIGNED = true (Theorem 1 holds)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 4] Ontology evolution (RQ1, rated_load: 500 -> 475 kg)..." << std::endl;

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
        r5.append_to_csv(csv, "CS2_evo_v2");

        if (!ok_r5) {
            std::cerr << "[WARNING] RUN 4: ontology evolution broke alignment "
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
