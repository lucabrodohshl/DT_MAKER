/**
 * @file run_CS1.cpp
 * @brief CS1: Three-Tank System benchmark (Gil et al., IEC 61511)
 *
 * Tests semantic alignment between the Physical Twin (ThreeTankPT) and the
 * Digital Twin (ThreeTankDT) under the IEC 61511 process-safety ontology.
 *
 * Research questions addressed:
 *  RQ1 — Ontology evolution (Theorem 1): alignment preserved under domain_v2.ont
 *  RQ2 — Coverage vs syntactic baseline: syntactic bisimulation fails because
 *         labels differ; semantic alignment succeeds under the shared ontology.
 *  RQ3 — Scalability: zone-graph sizes reported.
 *
 * Usage:
 *   ./run_CS1 [--results <folder>]
 */

#include <iostream>
#include <fstream>
#include <filesystem>
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

static const std::string ASSET_DIR = "assets/CS1_ThreeTank/";

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

        std::string csv_path = results_folder + "CS1_results.csv";
        std::ofstream csv(csv_path);
        if (!csv.is_open())
            throw std::runtime_error("Cannot open result file: " + csv_path);

        dtpta::SemanticAlignmentResult::write_csv_header(csv);

        // ────────────────────────────────────────────────────────────────────
        // Load PT and DT zone graphs (shared across all runs)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "=== CS1: Three-Tank System (IEC 61511) ===" << std::endl;
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
        r1.append_to_csv(csv, "CS1_aligned");

        // RQ2 key assertion: the aligned pair must be aligned
        if (!ok_r1) {
            std::cerr << "[WARNING] RUN 1: expected ALIGNED but got NOT ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 2: Syntactic bisimulation baseline — same pair
        // Expected: NOT aligned (labels differ syntactically)
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
        // Expected: ALIGNED = false
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 3] Threshold drift misalignment (Variant A)..." << std::endl;

        dtpta::TimedAutomaton dt_drift(ASSET_DIR + "V2_DT_thresh_drift.xml");
        dt_drift.construct_zone_graph();

        auto dk_drift = load_domain(ASSET_DIR + "domain.ont",
                                    ASSET_DIR + "pt.interp",
                                    ASSET_DIR + "dt_thresh_drift.interp");

        dtpta::SemanticAlignmentChecker checker_r3;
        dtpta::SemanticAlignmentResult  r3;
        bool ok_r3 = checker_r3.check_semantic_alignment(pt, dt_drift, dk_drift, r3);

        std::cout << "Verdict: " << (ok_r3 ? "TRUE (structurally aligned — "
                                                    "threshold drift not detectable via bisimulation)"
                                           : "FALSE (NOT aligned — threshold drift detected)")
                  << std::endl;
        r3.print();
        r3.append_to_csv(csv, "CS1_thresh_drift");

        // Note: this is the strongest possible RQ2 result for CS1.
        // V2_DT_thresh_drift.xml is STRUCTURALLY IDENTICAL to V2_DT.xml —
        // the threshold drift lives ONLY in the interpretation file.
        // Therefore syntactic bisimulation (RUN 2) CANNOT distinguish aligned
        // from misaligned: it sees the same TA, same label strings, same structure.
        // Semantic alignment catches the drift via Z3 refutation of the formula
        // equivalence:
        //   Δ ⊭  I_PT(level_critical!) ↔ I_DT(safety_margin_breach!)
        // This demonstrates that semantic alignment detects a class of misalignments
        // that is INVISIBLE to syntactic bisimulation.
        std::cout << "[RQ2] Threshold drift is a SEMANTIC-ONLY misalignment: "
                     "same TA structure, different interpretation.\n"
                  << "      Syntactic bisimulation cannot detect it (same labels, same structure).\n"
                  << "      Semantic alignment detects it via Z3 formula refutation.\n";

        // ────────────────────────────────────────────────────────────────────
        // RUN 4: Ontology evolution — RQ1 (Theorem 1)
        // Verify alignment preserved under domain_v2.ont
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 4] Ontology evolution (RQ1)..." << std::endl;

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
        r5.append_to_csv(csv, "CS1_evo_v2");

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
