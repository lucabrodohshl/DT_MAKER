/**
 * @file run_CS8.cpp
 * @brief CS8: Precision Irrigation System (Author-Constructed / ISO 11783 ISOBUS / FAO-56)
 *
 * Tests semantic alignment between the Physical Twin (IrrigationControllerPT) and the
 * Digital Twin (CropWaterBudgetDT) under the precision irrigation domain ontology.
 *
 * Author-constructed case study for the smart farming / precision agriculture domain.
 * Ontology construction methodology:
 *   (1) Governing standards identified: ISO 11783-7:2015, ISO 11783-10:2015,
 *       FAO-56:1998, EN 13101:2003, EU 2009/128/EC
 *   (2) Vocabulary extracted from data dictionaries:
 *       ISO 11783-7 PGN 0xFE49 (soil moisture sensor), PGN 0xFE4E (flow meter, section valve)
 *       ISO 11783-10 Annex B.2 (zone prescription rate limits)
 *       FAO-56 Table 1 (soil hydraulic properties), Table 12 (crop Kc coefficients)
 *       FAO-56 Eq.1 (ETc = ETo*Kc), Eq.82 (TAW), Eq.84 (RAW), Eq.85 (depletion fraction)
 *   (3) Normative requirement clauses formalised as FOL axioms in SMT-LIB2 prefix notation
 *
 * The PT models the physical irrigation controller from the precision agriculture engineer
 * perspective: ISOBUS section control events, zone valve commands, sensor threshold crossings.
 * The DT models a crop water budget monitoring twin from the agronomist perspective:
 * FAO-56 evapotranspiration vocabulary (water_application_*, deficit_threshold_breach!, etc.)
 *
 * Standards addressed:
 *   ISO 11783-7:2015  (ISOBUS implement messages: sensor response <=3 TU, overflow <=1 TU)
 *   ISO 11783-10:2015 (prescription maps: zone windows Z1=[10,60], Z2=[8,45], Z3=[6,30])
 *   FAO-56:1998       (Penman-Monteith ET; depletion threshold p=50%; critical deficit 48 TU)
 *   EN 13101:2003     (underground reservoir constraints: application rate limits)
 *   EU 2009/128/EC Art.12 (precision application timing, spray inhibit logic)
 *
 * Research questions addressed:
 *  RQ1 - Ontology evolution (Theorem 1) -- PROMINENTLY REPORTED:
 *         Condition I:  Add rainfall_forecast function (FAO-56 forecast-based scheduling).
 *         Condition II: Tighten depletion_fraction from 50 to 45% (drought management,
 *                       FAO-56 Sec.8.2 / EU 2009/128/EC water scarcity provisions).
 *                       raw(1): 15->13mm, raw(2): 13->12mm, raw(3): 11->10mm.
 *         Both interps reference (raw i) symbolically -- Z3 re-evaluates at new thresholds.
 *         Expected: alignment preserved -- Theorem 1 holds (Condition I + II both passed).
 *  RQ2 - Misalignment coverage:
 *         Variant A (FAO-56 deficit threshold drift): deficit_threshold_breach! fires at
 *           irrigation_deficit > 20 instead of > raw(1) = 15mm.
 *           The 5mm gap is the crop water stress zone where stomatal closure begins and
 *           yield penalty accumulates (FAO-56 Sec.8.3.2, Doorenbos & Kassam 1979).
 *           Z3 counterexample: irrigation_deficit = 18:
 *             PT: (> 18 (raw 1)) = (> 18 15) = true  -- deficit alarm fires
 *             DT: (> 18 20)                  = false -- DT does not detect deficit
 *           Expected: NOT ALIGNED (Condition I state + event inconsistency).
 *  RQ3 - Zone-graph sizes and check times.
 *  RQ4 - Engineering cost: syntactic bisimulation vs semantic alignment.
 *         ISOBUS firmware vocabulary vs FAO-56 agronomist vocabulary gap:
 *         zero shared label strings. Syntactic baseline fails immediately.
 *         Semantic alignment bridges the gap via the ISO/FAO domain ontology.
 *
 * Usage:
 *   ./run_CS8 [--folder <path>]
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

static const std::string ASSET_DIR = "assets/CS8_Irrigation/";

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

        std::string csv_path = results_folder + "CS8_results.csv";
        std::ofstream csv(csv_path);
        if (!csv.is_open())
            throw std::runtime_error("Cannot open result file: " + csv_path);

        dtpta::SemanticAlignmentResult::write_csv_header(csv);

        // ────────────────────────────────────────────────────────────────────
        // Load PT and DT zone graphs (shared across all runs)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "=== CS8: Precision Irrigation System ===" << std::endl;
        std::cout << "    (ISO 11783-7:2015 ISOBUS / ISO 11783-10:2015 / FAO-56:1998)" << std::endl;
        std::cout << "    PT: IrrigationControllerPT  (ISOBUS firmware, precision agriculture engineer)" << std::endl;
        std::cout << "    DT: CropWaterBudgetDT        (FAO-56 ET water budget, agronomist)" << std::endl;
        std::cout << "    Ontology methodology: ISO standards -> data dictionary -> FOL axioms" << std::endl;
        std::cout << "Loading PT and DT models..." << std::endl;

        dtpta::TimedAutomaton pt(ASSET_DIR + "V1_PT.xml");
        dtpta::TimedAutomaton dt(ASSET_DIR + "V2_DT.xml");
        pt.construct_zone_graph();
        dt.construct_zone_graph();

        std::cout << "  PT zones: " << pt.get_num_states()
                  << "  |  DT zones: " << dt.get_num_states() << "\n" << std::endl;

        // ────────────────────────────────────────────────────────────────────
        // RUN 1: Semantic alignment -- correctly aligned pair
        //
        // PT labels (ISOBUS firmware vocabulary):
        //   irrigation_start_z1!, irrigation_start_z2!, irrigation_start_z3!,
        //   irrigation_complete!, drain_start!, deficit_alarm!, emergency_irrigation!,
        //   overflow_alarm!, overflow_cleared!, maintenance_start!, maintenance_complete!
        //
        // DT labels (FAO-56 agronomist vocabulary):
        //   water_application_zone1!, water_application_zone2!, water_application_zone3!,
        //   water_application_event!, drainage_cycle_start!, deficit_threshold_breach!,
        //   emergency_water_application!, field_capacity_exceeded!, soil_moisture_nominal!,
        //   system_check_start!, system_check_complete!
        //
        // Zero shared label strings. Semantic alignment bridges the vocabulary gap via
        // the ISO 11783 / FAO-56 domain ontology.
        //
        // Key label equivalences established via ontology (11 label-equiv pairs):
        //   irrigation_start_z1!  ~ water_application_zone1!
        //     both: (and (< (soil_moisture 1) (raw 1)) (<= et_crop max_application_rate))
        //   irrigation_start_z2!  ~ water_application_zone2!
        //     both: (and (< (soil_moisture 2) (raw 2)) (<= et_crop max_application_rate))
        //   irrigation_start_z3!  ~ water_application_zone3!
        //     both: (and (< (soil_moisture 3) (raw 3)) (<= et_crop max_application_rate))
        //   irrigation_complete!  ~ water_application_event!
        //     both: (and (>= (irrigation_applied 1) min_application_per_zone) ...)
        //   drain_start!          ~ drainage_cycle_start!
        //     both: (>= (soil_moisture 1) 0)  [always true -- unconditional drain]
        //   deficit_alarm!        ~ deficit_threshold_breach!
        //     both: (> irrigation_deficit (raw 1))
        //   emergency_irrigation! ~ emergency_water_application!
        //     both: (> irrigation_deficit (raw 1))
        //   overflow_alarm!       ~ field_capacity_exceeded!
        //     both: (>= (soil_moisture 1) (field_capacity 1))
        //   overflow_cleared!     ~ soil_moisture_nominal!
        //     both: (< (soil_moisture 1) (field_capacity 1))
        //   maintenance_start!    ~ system_check_start!
        //     both: (>= (soil_moisture 1) 0)  [always true -- diagnostic entry]
        //   maintenance_complete! ~ system_check_complete!
        //     both: (>= (soil_moisture 1) 0)  [always true -- diagnostic exit]
        //
        // Expected: ALIGNED = true
        // ────────────────────────────────────────────────────────────────────
        std::cout << "[RUN 1] Semantic alignment (aligned pair)..." << std::endl;
        std::cout << "        11 label-equiv pairs: ISOBUS firmware <-> FAO-56 agronomist vocab" << std::endl;

        auto dk_base = load_domain(ASSET_DIR + "domain.ont",
                                   ASSET_DIR + "pt.interp",
                                   ASSET_DIR + "dt.interp");

        dtpta::SemanticAlignmentChecker checker_r1;
        dtpta::SemanticAlignmentResult  r1;
        bool ok_r1 = checker_r1.check_semantic_alignment(pt, dt, dk_base, r1);

        std::cout << "Verdict: " << (ok_r1 ? "TRUE (semantically aligned)"
                                           : "FALSE (NOT aligned)") << std::endl;
        r1.print();
        r1.append_to_csv(csv, "CS8_aligned");

        if (!ok_r1) {
            std::cerr << "[WARNING] RUN 1: expected ALIGNED but got NOT ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 2: Syntactic bisimulation baseline -- same pair
        //
        // PT labels: irrigation_start_z1!, irrigation_complete!, deficit_alarm!, ...
        // DT labels: water_application_zone1!, water_application_event!, deficit_threshold_breach!, ...
        //
        // Zero shared label strings. Syntactic bisimulation must fail.
        //
        // Core RQ4 result: the ISOBUS firmware vocabulary (ISO 11783-7 section control events)
        // and the FAO-56 agronomist vocabulary (evapotranspiration and water budget events) are
        // deliberately disjoint -- they describe the same irrigation operation from different
        // professional perspectives. An ISOBUS firmware engineer and an FAO-certified agronomist
        // writing their models independently will produce non-bisimilar automata. Semantic
        // alignment via the ISO/FAO ontology resolves the vocabulary gap.
        //
        // Expected: FALSE
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 2] Syntactic bisimulation baseline (same pair)..." << std::endl;
        std::cout << "        ISOBUS firmware vocabulary vs FAO-56 agronomist vocabulary" << std::endl;

        dtpta::SemanticAlignmentChecker syntactic_checker;
        dtpta::SemanticAlignmentResult result_r2;
        bool ok_r2 = syntactic_checker.check_weak_timed_bisimulation(pt, dt, result_r2);

        std::cout << "Verdict: " << (ok_r2 ? "TRUE (syntactically bisimilar)"
                                           : "FALSE (ISOBUS vs FAO-56 vocabulary gap -- expected for RQ4)")
                  << std::endl;
        result_r2.print();

        if (ok_r2) {
            std::cerr << "[WARNING] RUN 2: expected syntactic FAILURE but got TRUE.\n";
        } else {
            std::cout << "[RQ4] ISOBUS firmware (ISO 11783-7) vs FAO-56 agronomist vocabulary gap confirmed.\n"
                      << "      Syntactic bisimulation fails with zero matching label strings.\n"
                      << "      Semantic alignment (RUN 1) resolves via ISO/FAO domain ontology.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 3: Misalignment Variant A -- FAO-56 deficit threshold drift
        //
        // dt_thresh_drift.interp: deficit_threshold_breach! fires at irrigation_deficit > 20mm
        // instead of > raw(1) = 15mm (FAO-56 Eq.84: RAW = 0.50 * TAW = 0.50 * 30 = 15mm).
        //
        // Agronomic consequence:
        //   The 5mm gap (15mm to 20mm) is the crop water stress zone:
        //     - At irrigation_deficit > 15mm: FAO-56 Eq.85 says irrigate NOW
        //     - At irrigation_deficit > 20mm: DT only detects the problem
        //   In this zone, crops experience stomatal closure (FAO-56 Fig.40), measurable
        //   leaf water potential drop, and growth rate reduction. For winter wheat,
        //   FAO-56 Table 24 gives Ky (yield response factor) = 0.5 for vegetative stage,
        //   meaning a 33% relative evapotranspiration deficit causes ~17% yield penalty.
        //
        //   Z3 counterexample: irrigation_deficit = 18mm, raw(1) = 15mm
        //     PT formula: (> 18 (raw 1)) = (> 18 15) = true  -- alarm fires, irrigation starts
        //     DT formula: (> 18 20)                 = false -- DT silent, no water applied
        //     Counterexample also breaks state consistency:
        //       PT ALARM_DEFICIT: (> 18 15) = true
        //       DT DT_DEFICIT_ALERT: (> 18 20) = false
        //       States are semantically inequivalent at irrigation_deficit = 18mm
        //
        // Expected: NOT ALIGNED (Condition I state inconsistency + event semantic mismatch)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 3] Misalignment Variant A: FAO-56 deficit threshold drift..." << std::endl;
        std::cout << "        PT threshold: irrigation_deficit > raw(1) = 15mm (FAO-56 Eq.84)" << std::endl;
        std::cout << "        DT threshold: irrigation_deficit > 20mm (5mm drift = water stress zone)" << std::endl;

        auto dk_drift = load_domain(ASSET_DIR + "domain.ont",
                                    ASSET_DIR + "pt.interp",
                                    ASSET_DIR + "dt_thresh_drift.interp");

        dtpta::SemanticAlignmentChecker checker_r3;
        dtpta::SemanticAlignmentResult  r3;
        bool ok_r3 = checker_r3.check_semantic_alignment(pt, dt, dk_drift, r3);

        std::cout << "Verdict: " << (ok_r3 ? "TRUE (drift not bisimulation-critical)"
                                           : "FALSE (NOT aligned -- deficit threshold drift detected)")
                  << std::endl;
        r3.print();
        r3.append_to_csv(csv, "CS8_thresh_drift");

        if (!ok_r3) {
            double gap_mm = 20.0 - 15.0;
            std::cout << "[RQ2] FAO-56 deficit threshold drift correctly detected.\n"
                      << "      Gap: " << std::fixed << std::setprecision(0)
                      << gap_mm << "mm = water stress zone (stomatal closure onset to DT alarm).\n"
                      << "      Agricultural risk: yield penalty in irrigation_deficit range [15, 20)mm.\n"
                      << "      FAO-56 Ky factor for wheat suggests ~17% yield reduction possible.\n";
        } else {
            std::cerr << "[WARNING] RUN 3: expected NOT ALIGNED but got ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 4: Ontology evolution -- RQ1 (Theorem 1) -- PRIMARY RQ1 RESULT
        //
        // domain_v2.ont changes (Variant B):
        //   Condition I (vocabulary extension):
        //     Add fun rainfall_forecast : Volume_mm (FAO-56 forecast-based scheduling)
        //     Add axiom rainfall_nonneg : (>= rainfall_forecast 0)
        //     Source: FAO-56 Sec.6.3 effective rainfall estimation; EN 13101 Clause 5.3
        //
        //   Condition II (axiom strengthening -- drought management):
        //     depletion_fraction: 50 -> 45 percent
        //     Source: FAO-56 Sec.8.2 tighter management + EU 2009/128/EC water scarcity
        //     Updated RAW thresholds:
        //       raw(1): 15mm -> 13mm  (floor(0.45 * 30) = 13)
        //       raw(2): 13mm -> 12mm  (floor(0.45 * 27) = 12)
        //       raw(3): 11mm -> 10mm  (floor(0.45 * 23) = 10)
        //
        // Theorem 1 analysis:
        //   pt_v2.interp uses (raw 1), (raw 2), (raw 3) symbolically throughout.
        //   dt_v2.interp uses (raw 1), (raw 2), (raw 3) symbolically throughout.
        //   Z3 sees: raw(1) = 13 (updated), evaluates all formulas at new threshold.
        //   Label equivalences:
        //     irrigation_start_z1! ~ water_application_zone1!:
        //       PT: (< (soil_moisture 1) (raw 1)) at raw(1)=13
        //       DT: (< (soil_moisture 1) (raw 1)) at raw(1)=13  -- still equivalent!
        //     deficit_alarm! ~ deficit_threshold_breach!:
        //       PT: (> irrigation_deficit (raw 1)) at raw(1)=13
        //       DT: (> irrigation_deficit (raw 1)) at raw(1)=13  -- still equivalent!
        //   Both conditions tighten equally and symmetrically => alignment preserved.
        //
        //   Condition I (rainfall_forecast): new function not referenced by either interp
        //     => transparent addition, does not affect label equivalences.
        //
        // Expected: ALIGNED = true (Theorem 1 holds for Conditions I and II)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 4] Ontology evolution (RQ1, Theorem 1) -- VARIANT B..." << std::endl;
        std::cout << "        Condition I:  rainfall_forecast function added (FAO-56 forecast scheduling)" << std::endl;
        std::cout << "        Condition II: depletion_fraction 50->45%% (drought management)" << std::endl;
        std::cout << "        Updated RAW thresholds: raw(1)=15->13mm, raw(2)=13->12mm, raw(3)=11->10mm" << std::endl;
        std::cout << "        pt_v2.interp and dt_v2.interp use (raw i) symbolically => Theorem 1" << std::endl;

        auto dk_v2 = load_domain(ASSET_DIR + "domain_v2.ont",
                                  ASSET_DIR + "pt_v2.interp",
                                  ASSET_DIR + "dt_v2.interp");

        dtpta::SemanticAlignmentChecker checker_r4;
        dtpta::SemanticAlignmentResult  r4;
        bool ok_r4 = checker_r4.check_semantic_alignment(pt, dt, dk_v2, r4);

        std::cout << "Verdict under domain_v2.ont: "
                  << (ok_r4 ? "TRUE (alignment preserved -- Theorem 1 holds for Conditions I and II)"
                             : "FALSE (alignment broken by ontology evolution)")
                  << std::endl;
        r4.print();
        r4.append_to_csv(csv, "CS8_evo_v2");

        if (ok_r4) {
            std::cout << "[RQ1] Theorem 1 validated:\n"
                      << "      Condition I (vocabulary extension): rainfall_forecast added.\n"
                      << "        Neither interp references rainfall_forecast => transparent.\n"
                      << "      Condition II (axiom strengthening): depletion_fraction 50->45%%.\n"
                      << "        raw(1) 15->13mm; both interps use (raw 1) symbolically.\n"
                      << "        Z3 re-evaluates label equivalences at new thresholds -- still equivalent.\n"
                      << "      Alignment preserved under both ontology evolution conditions.\n";
        } else {
            std::cerr << "[WARNING] RUN 4: ontology evolution broke alignment (Theorem 1 failure).\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // Detailed ontology evolution report (RQ1)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RQ1] Ontology evolution detailed report:\n";
        std::cout << std::string(85, '-') << "\n";
        std::cout << std::left << std::setw(40) << "Property"
                  << std::setw(22) << "domain.ont (v1)"
                  << "domain_v2.ont (v2)\n";
        std::cout << std::string(85, '-') << "\n";
        std::cout << std::setw(40) << "depletion_fraction"
                  << std::setw(22) << "50 %"
                  << "45 % (drought tightening)\n";
        std::cout << std::setw(40) << "raw(1) [Zone 1 loam]"
                  << std::setw(22) << "15 mm"
                  << "13 mm\n";
        std::cout << std::setw(40) << "raw(2) [Zone 2 sandy loam]"
                  << std::setw(22) << "13 mm"
                  << "12 mm\n";
        std::cout << std::setw(40) << "raw(3) [Zone 3 sandy]"
                  << std::setw(22) << "11 mm"
                  << "10 mm\n";
        std::cout << std::setw(40) << "rainfall_forecast"
                  << std::setw(22) << "(absent)"
                  << "fun added (>= 0)\n";
        std::cout << std::string(85, '-') << "\n";
        std::cout << "RUN 1 time (v1 ontology): " << r1.time_ms << " ms | "
                  << "RUN 4 time (v2 ontology): " << r4.time_ms << " ms\n";
        std::cout << "SMT calls v1: " << r1.smt_calls_total
                  << " | SMT calls v2: " << r4.smt_calls_total << "\n";
        std::cout << "Label pairs v1: " << r1.label_pairs_in_E
                  << " | Label pairs v2: " << r4.label_pairs_in_E << "\n";

        // ────────────────────────────────────────────────────────────────────
        // Summary
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RQ2/RQ4] CS8 Results summary:\n";
        std::cout << std::left << std::setw(45) << "Variant"
                  << std::setw(20) << "SemAlign"
                  << "Note\n";
        std::cout << std::string(95, '-') << "\n";
        std::cout << std::setw(45) << "RUN 1: Aligned pair"
                  << std::setw(20) << (ok_r1 ? "ALIGNED" : "NOT ALIGNED")
                  << "11 label-equiv pairs via ISO/FAO ontology\n";
        std::cout << std::setw(45) << "RUN 2: Syntactic bisimulation"
                  << std::setw(20) << (ok_r2 ? "ALIGNED" : "NOT ALIGNED")
                  << "zero shared labels (ISOBUS vs FAO-56 vocab)\n";
        std::cout << std::setw(45) << "RUN 3: Deficit threshold drift (+5mm)"
                  << std::setw(20) << (ok_r3 ? "ALIGNED" : "NOT ALIGNED")
                  << "FAO-56 water stress zone undetected\n";
        std::cout << std::setw(45) << "RUN 4: Ontology evolution (Thm 1)"
                  << std::setw(20) << (ok_r4 ? "ALIGNED" : "NOT ALIGNED")
                  << "depletion_fraction 50->45%% + forecast sort\n";

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
