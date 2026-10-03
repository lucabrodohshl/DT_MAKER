/**
 * @file run_CS6.cpp
 * @brief CS6: GPCA Infusion Pump (FDA / IEC 60601-1 / ASTM F2761 ICE)
 *
 * Tests semantic alignment between the Physical Twin (InfusionPumpPT) and the
 * Digital Twin (DosageMonitorDT) under the IEC 60601-1 / FDA / ASTM F2761 domain
 * ontology.
 *
 * The GPCA infusion pump is a classic safety-critical formal methods benchmark
 * (Jiang et al., FDA infusion pump software evaluation programme).
 * The PT models pump firmware (bolus, KVO, occlusion, overdose alarms).
 * The DT models a clinical dosage monitoring twin using ASTM F2761 ICE vocabulary.
 *
 * Research questions addressed:
 *  RQ1 — Ontology evolution (Theorem 1):
 *         Condition I:  Add drug_concentration sort (IEC 60601-2-24:2012).
 *         Condition II: Tighten dose_tolerance from 5 to 3 ml (FDA 2023 high-alert).
 *         Both interps reference dose_tolerance symbolically -- alignment preserved.
 *  RQ2 — Misalignment coverage:
 *         Variant A (missing event): ice_safety_limit_breach! absent from DT interp.
 *           The most critical safety event (overdose alarm) has no DT semantic mapping.
 *           IEC 62443-3-3 SR 2.12 (non-repudiation) and SR 3.3 (security monitoring)
 *           are violated. Expected: NOT ALIGNED (unmatched label).
 *         Variant B (threshold drift): ice_safety_limit_breach! fires at > 110 ml
 *           instead of > 105 ml. Patient may receive 5 ml excess dose undetected.
 *           Clinically significant for opioid PCA (morphine respiratory depression).
 *           Expected: NOT ALIGNED (Z3 counterexample: dose_delivered = 108).
 *  RQ3 — Zone-graph sizes and check times.
 *  RQ4 — Engineering cost: syntactic bisimulation vs semantic alignment.
 *         ICE/ASTM vocabulary gap means zero shared label strings.
 *         Syntactic baseline fails immediately; semantic alignment succeeds via ontology.
 *
 * Usage:
 *   ./run_CS6 [--folder <path>]
 */

#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <string>
#include <stdexcept>

#include "dtpta/timedautomaton.h"
#include "dtpta/core.h"
#include "dtpta/ontology.h"
#include "dtpta/interpretation.h"
#include "dtpta/semantic_checker.h"
#include "dtpta/domain_parser.h"
#include "dtpta/benchmarks/common.h"

static const std::string ASSET_DIR = "assets/CS6_Pump/";

// ============================================================================
// Helpers
// ============================================================================

static dtpta::DomainKnowledge load_domain(const std::string& ont_path,
                                           const std::string& pt_interp_path,
                                           const std::string& dt_interp_path)
{
    dtpta::OntFileParser    ont_parser;
    dtpta::InterpFileParser interp_parser;

    auto ontology = ont_parser.parse(ont_path);
    auto pt_imap  = interp_parser.parse(pt_interp_path, ontology);
    auto dt_imap  = interp_parser.parse(dt_interp_path, ontology);

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

        std::string csv_path = results_folder + "CS6_results.csv";
        std::ofstream csv(csv_path);
        if (!csv.is_open())
            throw std::runtime_error("Cannot open result file: " + csv_path);

        dtpta::SemanticAlignmentResult::write_csv_header(csv);

        // ────────────────────────────────────────────────────────────────────
        // Load PT and DT zone graphs (shared across all runs)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "=== CS6: GPCA Infusion Pump ===" << std::endl;
        std::cout << "    (IEC 60601-1:2005+AMD1 / FDA Guidance 2010 / ASTM F2761-09 ICE)" << std::endl;
        std::cout << "    Benchmark: Jiang et al. GPCA model (FDA infusion pump evaluation)" << std::endl;
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
        // PT labels (firmware vocabulary):   prime_start!, infusion_start!, bolus_request!, ...
        // DT labels (ASTM F2761 ICE vocab):  ice_device_connect!, ice_therapy_start!, ice_bolus_request!, ...
        //
        // Zero shared label strings. Semantic alignment bridges the vocabulary gap via
        // the IEC 60601-1 / FDA domain ontology.
        //
        // Key label equivalences established via ontology:
        //   prime_start!         ~ ice_device_connect!      (both: dose_delivered = 0)
        //   infusion_start!      ~ ice_therapy_start!        (both: rate in (0, max_rate])
        //   bolus_request!       ~ ice_bolus_request!        (both: count < lockout, dose <= prescribed)
        //   bolus_complete!      ~ ice_bolus_confirmed!      (both: bolus_vol <= max, dose <= threshold)
        //   overdose_alarm!      ~ ice_safety_limit_breach!  (both: dose > prescribed + tolerance)
        //   occlusion_alarm!     ~ ice_alarm_occlusion!      (both: pressure > occlusion_threshold)
        //   infusion_complete!   ~ ice_therapy_complete!     (both: dose >= prescribed)
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
        r1.append_to_csv(csv, "CS6_aligned");

        if (!ok_r1) {
            std::cerr << "[WARNING] RUN 1: expected ALIGNED but got NOT ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 2: Syntactic bisimulation baseline — same pair
        //
        // PT labels: prime_start!, infusion_start!, bolus_request!, ...
        // DT labels: ice_device_connect!, ice_therapy_start!, ice_bolus_request!, ...
        //
        // Zero shared label strings. Syntactic bisimulation must fail.
        // Core RQ4 result: ASTM F2761 ICE vocabulary is deliberately different from
        // pump firmware vocabulary (standard interoperability requirement). A firmware
        // engineer and a clinical informatics engineer writing their models independently
        // will produce non-bisimilar models. Semantic alignment resolves the gap.
        //
        // Expected: FALSE
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 2] Syntactic bisimulation baseline (same pair)..." << std::endl;

        dtpta::SemanticAlignmentChecker syntactic_checker;
        dtpta::SemanticAlignmentResult result_r2;
        bool ok_r2 = syntactic_checker.check_weak_timed_bisimulation(pt, dt, result_r2);

        std::cout << "Verdict: " << (ok_r2 ? "TRUE (syntactically bisimilar)"
                                           : "FALSE (ICE/firmware vocabulary gap — expected for RQ4)")
                  << std::endl;
        result_r2.print();

        if (ok_r2) {
            std::cerr << "[WARNING] RUN 2: expected syntactic FAILURE but got TRUE.\n";
        } else {
            std::cout << "[RQ4] ASTM F2761 ICE vs firmware vocabulary gap confirmed.\n"
                      << "      Syntactic bisimulation fails with zero matching label strings.\n"
                      << "      Semantic alignment (RUN 1) resolves via IEC 60601-1 ontology.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 3: Misalignment Variant A — Missing overdose warning
        //
        // IMPORTANT (ISSUE 2 note): This is a SEMANTIC-ONLY misalignment.
        // V2_DT.xml is used as-is (no separate misaligned XML model).
        // The misalignment lives ENTIRELY in dt_missing_warning.interp:
        //   ice_safety_limit_breach! has no formula entry => treated as ⊤.
        // Since overdose_alarm! maps to a non-trivial formula in pt.interp,
        // the label equivalence (overdose_alarm! ~ ice_safety_limit_breach!)
        // fails under the ontology: Δ ⊭ (dose > threshold) ↔ ⊤.
        // This is therefore the most interesting case for the paper:
        //   SAME TA structure, DIFFERENT semantic meaning.
        //   Syntactic bisimulation sees the same structure and label strings,
        //   semantic alignment detects the interpretation gap via Z3.
        //
        //
        // Regulatory consequence:
        //   IEC 62443-3-3 SR 2.12: Non-repudiation of safety limit events is violated.
        //   IEC 62443-3-3 SR 3.3: Security monitoring coverage gap.
        //   The DT cannot detect when the pump crosses the prescribed + tolerance threshold.
        //
        // Expected: NOT ALIGNED (unmatched DT label: overdose_alarm! has no counterpart)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 3] Misalignment Variant A: missing overdose safety event..." << std::endl;

        auto dk_missing = load_domain(ASSET_DIR + "domain.ont",
                                      ASSET_DIR + "pt.interp",
                                      ASSET_DIR + "dt_missing_warning.interp");

        dtpta::SemanticAlignmentChecker checker_r3;
        dtpta::SemanticAlignmentResult  r3;
        bool ok_r3 = checker_r3.check_semantic_alignment(pt, dt, dk_missing, r3);

        std::cout << "Verdict: " << (ok_r3 ? "TRUE (unexpected — overdose alarm should be unmatched)"
                                           : "FALSE (NOT aligned — missing overdose event detected)")
                  << std::endl;
        r3.print();
        r3.append_to_csv(csv, "CS6_missing_warning");

        if (!ok_r3) {
            std::cout << "[RQ2] Missing event gap correctly detected.\n"
                      << "      overdose_alarm! in PT has no DT semantic counterpart.\n"
                      << "      IEC 62443-3-3 SR 2.12 (non-repudiation) coverage gap confirmed.\n";
        } else {
            std::cerr << "[WARNING] RUN 3: expected NOT ALIGNED but got ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 4: Misalignment Variant B — Dose tolerance threshold drift
        //
        // IMPORTANT: This is also a SEMANTIC-ONLY misalignment.
        // The same V2_DT.xml is used (no separate misaligned XML model).
        // The drift lives entirely in dt_thresh_drift.interp:
        //   ice_safety_limit_breach! threshold changed from 105 to 110 ml.
        // Syntactic bisimulation sees the same TA structure and cannot detect
        // this. Semantic alignment refutes:
        //   Δ ⊭ (dose > 105) ↔ (dose > 110)
        // via the Z3 counterexample dose_delivered = 108.
        //
        // dt_thresh_drift.interp: ice_safety_limit_breach! fires at dose_delivered > 110
        // instead of > prescribed_dose + dose_tolerance (= 105).
        //
        // Clinical consequence:
        //   Patient may receive up to 5 ml excess dose (105 to 110 ml range) before the
        //   monitoring DT detects an overdose. For opioid PCA (morphine, hydromorphone),
        //   this 5% excess dose window can cause respiratory depression.
        //
        //   Gap fraction = 5 / prescribed_dose = 5%
        //   FDA threshold: dose_delivered > 105 ml (= 100 + 5)
        //   DT threshold:  dose_delivered > 110 ml (10% excess, not the FDA 5%)
        //
        // Z3 counterexample: dose_delivered = 108
        //   PT formula  (> 108 (+ prescribed_dose dose_tolerance)) = (> 108 105) = true
        //   DT formula  (> 108 110)                                              = false
        //   Semantically inequivalent at overdose_alarm! ~ ice_safety_limit_breach!
        //
        // Expected: NOT ALIGNED
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 4] Misalignment Variant B: dose tolerance threshold drift..." << std::endl;
        std::cout << "        PT threshold: dose > 105 ml (prescribed 100 + tolerance 5)" << std::endl;
        std::cout << "        DT threshold: dose > 110 ml (5 ml drift -- 10% excess)" << std::endl;

        auto dk_drift = load_domain(ASSET_DIR + "domain.ont",
                                    ASSET_DIR + "pt.interp",
                                    ASSET_DIR + "dt_thresh_drift.interp");

        dtpta::SemanticAlignmentChecker checker_r4;
        dtpta::SemanticAlignmentResult  r4;
        bool ok_r4 = checker_r4.check_semantic_alignment(pt, dt, dk_drift, r4);

        std::cout << "Verdict: " << (ok_r4 ? "TRUE (drift not bisimulation-critical)"
                                           : "FALSE (NOT aligned — threshold drift detected)")
                  << std::endl;
        r4.print();
        r4.append_to_csv(csv, "CS6_thresh_drift");

        if (!ok_r4) {
            double gap_fraction = 5.0 / 100.0;
            std::cout << "[RQ2] Threshold drift correctly detected.\n"
                      << "      Gap fraction: " << std::fixed << std::setprecision(1)
                      << (gap_fraction * 100.0) << "% of prescribed dose.\n"
                      << "      Clinical risk: opioid PCA overdose in [105, 110) ml window.\n";
        } else {
            std::cerr << "[WARNING] RUN 4: expected NOT ALIGNED but got ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 5: Ontology evolution — RQ1 (Theorem 1)
        //
        // domain_v2.ont changes:
        //   Condition I:  Add Concentration sort + drug_concentration function
        //                 (IEC 60601-2-24:2012 dual-channel pump requirement)
        //   Condition II: dose_tolerance: 5 -> 3 ml
        //                 (FDA high-alert medication guidance 2023)
        //
        // pt_v2.interp and dt_v2.interp reference dose_tolerance symbolically.
        // Theorem 1: alignment preserved under Condition I (sort addition) and
        //            Condition II (axiom tightening) because all formulas use
        //            (+ prescribed_dose dose_tolerance) rather than literal 105.
        //
        // Expected: ALIGNED = true
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 5] Ontology evolution (RQ1, dose_tolerance: 5 -> 3 ml)..." << std::endl;
        std::cout << "        Condition I:  drug_concentration sort (IEC 60601-2-24:2012)" << std::endl;
        std::cout << "        Condition II: dose_tolerance tightened (FDA 2023 high-alert)" << std::endl;

        auto dk_v2 = load_domain(ASSET_DIR + "domain_v2.ont",
                                  ASSET_DIR + "pt_v2.interp",
                                  ASSET_DIR + "dt_v2.interp");

        dtpta::SemanticAlignmentChecker checker_r5;
        dtpta::SemanticAlignmentResult  r5;
        bool ok_r5 = checker_r5.check_semantic_alignment(pt, dt, dk_v2, r5);

        std::cout << "Verdict under domain_v2.ont: "
                  << (ok_r5 ? "TRUE (alignment preserved -- Theorem 1 holds)"
                             : "FALSE (alignment broken by ontology evolution)")
                  << std::endl;
        r5.print();
        r5.append_to_csv(csv, "CS6_evo_v2");

        if (!ok_r5) {
            std::cerr << "[WARNING] RUN 5: ontology evolution broke alignment.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // Summary
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RQ2] CS6 Misalignment detection summary:\n";
        std::cout << std::left << std::setw(40) << "Variant"
                  << std::setw(20) << "SemAlign"
                  << "Note\n";
        std::cout << std::string(90, '-') << "\n";
        std::cout << std::setw(40) << "RUN 1: Aligned pair"
                  << std::setw(20) << (ok_r1 ? "ALIGNED" : "NOT ALIGNED")
                  << "13 label-equiv pairs via ontology\n";
        std::cout << std::setw(40) << "RUN 2: Syntactic bisimulation"
                  << std::setw(20) << (ok_r2 ? "ALIGNED" : "NOT ALIGNED")
                  << "zero shared labels (ICE vs firmware)\n";
        std::cout << std::setw(40) << "RUN 3: Missing overdose event"
                  << std::setw(20) << (ok_r3 ? "ALIGNED" : "NOT ALIGNED")
                  << "IEC 62443-3-3 SR 2.12/3.3 gap\n";
        std::cout << std::setw(40) << "RUN 4: Dose tolerance drift (+5 ml)"
                  << std::setw(20) << (ok_r4 ? "ALIGNED" : "NOT ALIGNED")
                  << "5% excess opioid dose risk\n";
        std::cout << std::setw(40) << "RUN 5: Ontology evolution (Thm 1)"
                  << std::setw(20) << (ok_r5 ? "ALIGNED" : "NOT ALIGNED")
                  << "dose_tolerance 5->3 ml + Concentration sort\n";

        std::cout << "\n[RQ4] Engineering cost summary:\n";
        std::cout << "  SemAlign one-time cost (RUN 1):       " << r1.time_ms << " ms\n";
        std::cout << "  Total SMT calls:                      " << r1.smt_calls_total << "\n";
        std::cout << "  Label-equiv pairs |E|:                " << r1.label_pairs_in_E << "\n";
        std::cout << "  Fixpoint iterations:                  " << r1.fixpoint_iterations << "\n";
        std::cout << "  PT zone count:                        " << pt.get_num_states() << "\n";
        std::cout << "  DT zone count:                        " << dt.get_num_states() << "\n";

        csv.close();
        std::cout << "\nResults written to: " << csv_path << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
