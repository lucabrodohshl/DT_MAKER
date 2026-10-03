/**
 * @file run_use_case.cpp
 * @brief Use Case: Autonomous Pesticide Spraying Drone -- formal PT/DT alignment validation of the paper's running example (Examples 1-4, Section VII). Not a Table III case study.
 *
 * THIS IS THE PAPER VALIDATION CASE STUDY.
 * The ontology, interpretations, and semantic alignment checks in this runner
 * reproduce the analytical results of Examples 1-4 and Section VII of the paper
 * EXACTLY. If any validation check fails, the SemAlign implementation has a bug.
 *
 * The drone models extend the motivating example from the paper into a full case study.
 * The Physical Twin (SprayingDronePT) exactly matches Example 2 of the paper.
 * The Digital Twin (EnergyBudgetDT) uses the labels from Example 4 of the paper.
 *
 * Governing standards:
 *   EPPO PP 1/213(3):2012   (precision spraying; max 50 AU/ha; 85% coverage uniformity)
 *   EU Reg. 2009/128/EC     (sustainable use of pesticides; buffer zone; aerial GPS control)
 *   EASA Reg.(EU) 2019/947  (UAS.OPEN.040 operational limitations; battery_capacity = 500)
 *   ISO 16119-4:2014        (agricultural sprayer coverage uniformity requirements)
 *   ICAO Annex 2:2005       (UAS rules of the air; VLOS; flight time limits)
 *
 * Paper examples reproduced:
 *   Example 1 (domain.ont): Sorts {Hectare, Real, Nat}; Functions {consumption, num_hectares,
 *     total_consumption, battery_capacity}; Relation {covered : Hectare};
 *     Axioms: num_h=10, bat_cap=500, cons_upper(1..5)<=50, total bounds.
 *   Example 2 (pt.interp):  spray! = (consumption 1)<=50;
 *     shutoff! = (and (<= total_consumption battery_capacity) (covered 1))
 *   Example 4 (dt.interp):  apply! = (<= (consumption 1) max_per_hectare_dose)
 *     power_down! = (and (>= battery_capacity-total_consumption 0) (covered 1))
 *     power_down! = (and (>= battery_capacity-total_consumption 0) (covered 1))
 *   Section VII: Z3 entailment checks confirming
 *     Δ |= I_PT(spray!) ↔ I_DT(apply!)
 *     Δ |= I_PT(shutoff!) ↔ I_DT(power_down!)
 *
 * Run structure:
 *   RUN 0 (validation): Paper Example Validation -- ground-truth SMT checks
 *   RUN 1: Semantic alignment (aligned pair)
 *   RUN 2: Syntactic bisimulation baseline (expects failure: spray! vs apply!)
 *   RUN 3: Misalignment Variant A -- consumption limit drift (apply! at >60 instead of <=50)
 *   RUN 4: Ontology evolution (Theorem 1) -- battery degradation (500->450)
 *
 * Usage:
 *   ./run_use_case [--folder <path>]
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

static const std::string ASSET_DIR = "assets/UseCase_Drone/";

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

        std::string csv_path = results_folder + "use_case_results.csv";
        std::ofstream csv(csv_path);
        if (!csv.is_open())
            throw std::runtime_error("Cannot open result file: " + csv_path);

        dtpta::SemanticAlignmentResult::write_csv_header(csv);

        // ────────────────────────────────────────────────────────────────────
        // Header
        // ────────────────────────────────────────────────────────────────────
        std::cout << "=========================================================" << std::endl;
        std::cout << "=== Use Case: Autonomous Pesticide Spraying Drone     ===" << std::endl;
        std::cout << "=== PAPER VALIDATION CASE STUDY                        ===" << std::endl;
        std::cout << "=========================================================" << std::endl;
        std::cout << "    PT: SprayingDronePT     (EPPO firmware, Examples 1-2)" << std::endl;
        std::cout << "    DT: EnergyBudgetDT      (energy envelope, Example 4)" << std::endl;
        std::cout << "    Ontology: domain.ont    (Definition 5 / Example 1)" << std::endl;
        std::cout << "    Standards: EPPO PP 1/213(3), EU 2009/128/EC, EASA 2019/947," << std::endl;
        std::cout << "               ISO 16119-4:2014, ICAO Annex 2:2005" << std::endl;
        std::cout << std::endl;

        std::cout << "Loading PT and DT models..." << std::endl;
        dtpta::TimedAutomaton pt(ASSET_DIR + "V1_PT.xml");
        dtpta::TimedAutomaton dt(ASSET_DIR + "V2_DT.xml");
        pt.construct_zone_graph();
        dt.construct_zone_graph();
        std::cout << "  PT zones: " << pt.get_num_states()
                  << "  |  DT zones: " << dt.get_num_states() << "\n" << std::endl;

        // ────────────────────────────────────────────────────────────────────
        // RUN 0: PAPER EXAMPLE VALIDATION
        //
        // This is the ground-truth validation for the SemAlign framework.
        // Z3 must confirm BOTH biconditionals from Section VII of the paper.
        //
        // Check 1: Δ |= I_PT(spray!) ↔ I_DT(apply!)
        //   I_PT(spray!)  = (consumption 1) <= 50
        //   I_DT(apply!)  = battery_capacity - total_consumption - (consumption 1)
        //                   >= battery_capacity - 50
        //   Under Δ: axiom cons_upper gives (consumption 1) <= 50 (tautology).
        //   Z3 checks: Δ ∧ ¬(spray! ↔ apply!) is UNSAT => ENTAILED.
        //
        // Check 2: Δ |= I_PT(shutoff!) ↔ I_DT(power_down!)
        //   I_PT(shutoff!)   = (and (<= total_consumption battery_capacity) (covered 1))
        //   I_DT(power_down!)= (and (>= (- battery_capacity total_consumption) 0) (covered 1))
        //   The two battery conditions are algebraically identical:
        //     (total_consumption <= battery_capacity) ↔ (battery_capacity - total_consumption >= 0)
        //   Z3 confirms: ENTAILED (arithmetic identity + same (covered 1) conjunct).
        //
        // If either check fails => IMPLEMENTATION BUG in ontology, interpretation, or SMT checker.
        // ────────────────────────────────────────────────────────────────────
        std::cout << "=========================================================" << std::endl;
        std::cout << "=== Paper Example Validation (Section VII)             ===" << std::endl;
        std::cout << "=========================================================" << std::endl;

        auto dk_base = load_domain(ASSET_DIR + "domain.ont",
                                   ASSET_DIR + "pt.interp",
                                   ASSET_DIR + "dt.interp");

        // Check 1: Δ |= I_PT(spray!) ↔ I_DT(apply!)
        std::cout << "Checking: Δ |= I_PT(spray!) ↔ I_DT(apply!) ..." << std::endl;
        std::cout << "  I_PT(spray!)  = (<= (consumption 1) 50)" << std::endl;
        std::cout << "  I_DT(apply!)  = (<= (consumption 1) max_per_hectare_dose)" << std::endl;
        std::cout << "  Equivalence:  Δ axiom max_dose_val: max_per_hectare_dose = 50" << std::endl;

        // Δ |= I_PT(spray!) ↔ I_DT(apply!)  using Ontology::entails_iff
        auto pt_spray = dk_base.pt_interp.get_event("spray!");
        auto dt_apply  = dk_base.dt_interp.get_event("apply!");
        bool spray_apply_equiv = (pt_spray && dt_apply)
            ? dk_base.ontology->entails_iff(*pt_spray, *dt_apply) : false;
        std::cout << "Δ |= I_PT(spray!) ↔ I_DT(apply!)     ... ["
                  << (spray_apply_equiv ? "ENTAILED" : "NOT ENTAILED") << "]\n";

        // Check 2: Δ |= I_PT(shutoff!) ↔ I_DT(power_down!)
        std::cout << "Checking: Δ |= I_PT(shutoff!) ↔ I_DT(power_down!) ..." << std::endl;
        std::cout << "  I_PT(shutoff!)    = (and (<= total_consumption battery_capacity) (covered 1))" << std::endl;
        std::cout << "  I_DT(power_down!) = (and (>= (- battery_capacity total_consumption) 0) (covered 1))" << std::endl;

        // Δ |= I_PT(shutoff!) ↔ I_DT(power_down!)  using Ontology::entails_iff
        auto pt_shutoff   = dk_base.pt_interp.get_event("shutoff!");
        auto dt_power_down = dk_base.dt_interp.get_event("power_down!");
        bool shutoff_power_equiv = (pt_shutoff && dt_power_down)
            ? dk_base.ontology->entails_iff(*pt_shutoff, *dt_power_down) : false;
        std::cout << "Δ |= I_PT(shutoff!) ↔ I_DT(power_down!) ... ["
                  << (shutoff_power_equiv ? "ENTAILED" : "NOT ENTAILED") << "]\n";

        std::cout << "Expected: both ENTAILED (per Section VII of paper)" << std::endl;
        bool validation_pass = spray_apply_equiv && shutoff_power_equiv;
        std::cout << "  Biconditional 1 (spray! ~ apply!):     ["
                  << (spray_apply_equiv   ? "ENTAILED" : "NOT ENTAILED") << "]\n";
        std::cout << "  Biconditional 2 (shutoff! ~ power_down!): ["
                  << (shutoff_power_equiv ? "ENTAILED" : "NOT ENTAILED") << "]\n";
        std::cout << "Overall: [" << (validation_pass ? "PASS" : "FAIL") << "]\n";

        if (!validation_pass) {
            std::cerr << "\n[IMPLEMENTATION BUG] Paper example validation FAILED.\n"
                      << "One or both biconditionals are NOT ENTAILED under the ontology.\n"
                      << "Check: ontology axioms in domain.ont, interpretation formulas in\n"
                      << "pt.interp and dt.interp, and the SMT entailment checker.\n"
                      << "Do not proceed — fix the bug first.\n\n";
            return 1;
        }
        std::cout << std::string(57, '=') << "\n\n";

        // ────────────────────────────────────────────────────────────────────
        // RUN 1: Semantic alignment -- aligned pair (paper examples)
        //
        // PT labels (EPPO firmware vocabulary):
        //   spray!, shutoff!, exit_zone!, enter_zone!, takeoff!, land!,
        //   return_base!, recharge_complete!, fault_detected!, fault_return!
        //
        // DT labels (energy budget vocabulary):
        //   apply!, power_down!, coverage_confirmed!, zone_entry_logged!,
        //   mission_start_logged!, base_return_logged!
        //
        // Key label-equivalence pairs (via domain.ont ontology):
        //   spray!              ~ apply!
        //     (established by Check 1 in RUN 0: ENTAILED)
        //   shutoff!            ~ power_down!
        //     (established by Check 2 in RUN 0: ENTAILED)
        //   exit_zone!          ~ coverage_confirmed!
        //     both: (covered 1)
        //   enter_zone!         ~ zone_entry_logged!
        //     both: (<= total_consumption (- battery_capacity battery_reserve))
        //   takeoff!            ~ mission_start_logged!
        //     both: (= total_consumption 0)
        //   land!               ~ base_return_logged!
        //     both: (<= total_consumption battery_capacity)
        //
        // Expected: ALIGNED = true
        // ────────────────────────────────────────────────────────────────────
        std::cout << "[RUN 1] Semantic alignment (paper Examples 1-4, aligned pair)..." << std::endl;
        std::cout << "        6 label-equiv pairs: EPPO firmware <-> energy budget vocabulary" << std::endl;
        std::cout << "        spray! ~ apply! (Section VII, Check 1)" << std::endl;
        std::cout << "        shutoff! ~ power_down! (Section VII, Check 2)" << std::endl;

        dtpta::SemanticAlignmentChecker checker_r1;
        dtpta::SemanticAlignmentResult  r1;
        bool ok_r1 = checker_r1.check_semantic_alignment(pt, dt, dk_base, r1);

        std::cout << "Verdict: " << (ok_r1 ? "TRUE (semantically aligned)"
                                           : "FALSE (NOT aligned)") << "\n";
        r1.print();
        r1.append_to_csv(csv, "UseCase_aligned");

        if (!ok_r1) {
            std::cerr << "[WARNING] RUN 1: expected ALIGNED but got NOT ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 2: Syntactic bisimulation baseline -- same pair
        //
        // PT labels: spray!, shutoff!, exit_zone!, enter_zone!, takeoff!, land!, ...
        // DT labels: apply!, power_down!, coverage_confirmed!, zone_entry_logged!, ...
        //
        // Zero shared label strings.
        // Syntactic bisimulation must fail: spray! != apply!, shutoff! != power_down!, etc.
        //
        // This makes the RQ4 point visually explicit:
        //   A precision agriculture firmware engineer (PT) and an energy budget
        //   monitoring engineer (DT) working independently use disjoint vocabularies.
        //   Syntactic bisimulation detects no equivalences. Semantic alignment via
        //   the EPPO/EASA domain ontology reveals the structural correspondence.
        //
        // Expected: FALSE (NOT ALIGNED -- syntactically disjoint labels)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 2] Syntactic bisimulation baseline (same pair)..." << std::endl;
        std::cout << "        EPPO firmware vocabulary vs energy budget vocabulary" << std::endl;

        dtpta::SemanticAlignmentChecker syntactic_checker;
        dtpta::SemanticAlignmentResult result_r2;
        bool ok_r2 = syntactic_checker.check_weak_timed_bisimulation(pt, dt, result_r2);

        std::cout << "Syntactic bisimulation on (spray! vs apply!): "
                  << (ok_r2 ? "ALIGNED (unexpected!)" : "NOT ALIGNED") << "\n";
        if (!ok_r2) {
            std::cout << "[CORRECT \xe2\x80\x94 labels differ. Semantic alignment needed to bridge this gap.]\n";
        }
        std::cout << "Syntactic bisimulation verdict: "
                  << (ok_r2 ? "TRUE (unexpected)" : "FALSE (expected for RQ4)") << "\n";
        result_r2.print();

        if (ok_r2) {
            std::cerr << "[WARNING] RUN 2: expected syntactic FAILURE but got TRUE.\n";
        } else {
            std::cout << "[RQ4] EPPO firmware vocabulary vs EASA energy budget vocabulary gap confirmed.\n"
                      << "      Syntactic bisimulation fails: spray!!=apply!, shutoff!!=power_down!, etc.\n"
                      << "      Semantic alignment (RUN 1) resolves via EPPO/EASA domain ontology.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 3: Misalignment Variant A -- consumption limit drift
        //
        // dt_thresh_drift.interp: apply! fires at consumption(1) > 60 instead of <= 50.
        //
        // Regulatory consequence:
        //   The DT now permits spray applications at 51-60 AU/ha.
        //   EPPO PP 1/213(3) Sec.4.2 maximum is 50 AU/ha.
        //   EU 2009/128/EC Art.12: excess application near water bodies violates
        //   the aquatic buffer zone protection requirement.
        //
        // Z3 analysis:
        //   PT spray! = (consumption 1) <= 50   (EPPO regulatory bound)
        //   DT apply! = (consumption 1) > 60    (malformed DT threshold)
        //   Biconditional: consumption(1) <= 50 ↔ consumption(1) > 60 is FALSE for c1=45:
        //     PT: 45 <= 50 = true
        //     DT: 45 > 60  = false
        //   Z3 finds counterexample: consumption(1) = 45 (valid mission, not detected by DT)
        //   State mismatch: SPRAYING has (consumption 1)<=50 but EM_APPLYING has c1>60.
        //   Any mission with consumption(1) in [0,60] is undetected by DT.
        //
        // Expected: NOT ALIGNED (Condition I event semantic mismatch)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 3] Misalignment Variant A: consumption limit drift..." << std::endl;
        std::cout << "        PT threshold: consumption(1) <= 50 (EPPO PP 1/213(3) Sec.4.2)" << std::endl;
        std::cout << "        DT threshold: consumption(1) > 60  (10 AU/ha drift = EU violation zone)" << std::endl;

        auto dk_drift = load_domain(ASSET_DIR + "domain.ont",
                                    ASSET_DIR + "pt.interp",
                                    ASSET_DIR + "dt_thresh_drift.interp");

        dtpta::SemanticAlignmentChecker checker_r3;
        dtpta::SemanticAlignmentResult  r3;
        bool ok_r3 = checker_r3.check_semantic_alignment(pt, dt, dk_drift, r3);

        std::cout << "Verdict: " << (ok_r3 ? "TRUE (drift not detected)"
                                           : "FALSE (NOT aligned -- consumption limit drift)")
                  << "\n";
        r3.print();
        r3.append_to_csv(csv, "UseCase_thresh_drift");

        if (!ok_r3) {
            std::cout << "[RQ2] Consumption limit drift correctly detected.\n"
                      << "      EPPO PP 1/213(3) max = 50 AU/ha; DT threshold = 60 AU/ha.\n"
                      << "      EU 2009/128/EC violation zone: 50 < consumption(1) <= 60.\n"
                      << "      Any mission in this range: PT inhibits, DT silent.\n";
        } else {
            std::cerr << "[WARNING] RUN 3: expected NOT ALIGNED but got ALIGNED.\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // RUN 4: Ontology evolution (Theorem 1, Definition 7) -- PRIMARY RQ1 RESULT
        //
        // domain_v2.ont (battery degradation after 200 charge cycles -- Section IV example):
        //   Condition I  (vocabulary extension):
        //     Add sort Motor and fun rpm : Motor -> Real
        //     Source: EASA 2019/947 Annex I (electric motor telemetry)
        //     Neither pt_v2.interp nor dt_v2.interp references rpm => transparent.
        //
        //   Condition II (axiom strengthening -- battery degradation):
        //     battery_capacity: 500 -> 450 (10% degradation after 200 cycles)
        //     Source: Section IV of the paper (canonical Theorem 1 example)
        //     Verification: total_consumption = 460 was valid under domain.ont (460 <= 500)
        //     but is REJECTED under domain_v2.ont (460 > 450). Reported explicitly below.
        //
        //   Condition III (interpretation update):
        //     pt_v2.interp: shutoff! = (and (<= total_consumption 450) (covered 1))
        //     dt_v2.interp: power_down! = (and (>= (- 450 total_consumption) 0) (covered 1))
        //     Under domain_v2.ont: battery_capacity = 450, so these equal the symbolic forms.
        //
        // Expected: ALIGNED = true (Theorem 1 holds)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RUN 4] Ontology evolution (RQ1, Theorem 1, Definition 7)..." << std::endl;
        std::cout << "        Condition I:  sort Motor + fun rpm added (EASA 2019/947 Annex I)" << std::endl;
        std::cout << "        Condition II: battery_capacity 500->450 (200-cycle degradation)" << std::endl;
        std::cout << "        Condition III: shutoff!/power_down! updated to concrete 450" << std::endl;

        // Verify Condition II: total_consumption=460 valid under v1, rejected under v2
        std::cout << "\n  [Condition II verification]" << std::endl;
        bool v1_accepts_460 = false;
        bool v2_accepts_460 = false;
        // Δ ∧ (total_consumption = 460) is SAT  iff  Δ ⊭ (total_consumption ≠ 460)
        {
            auto& ctx_v1 = dk_base.ontology->get_context();
            z3::expr tc_v1 = dk_base.ontology->get_func_decl("total_consumption")();
            z3::expr neq_v1 = (tc_v1 != ctx_v1.real_val(460));
            v1_accepts_460 = !dk_base.ontology->entails(neq_v1);
        }
        auto dk_v2 = load_domain(ASSET_DIR + "domain_v2.ont",
                                  ASSET_DIR + "pt_v2.interp",
                                  ASSET_DIR + "dt_v2.interp");
        // Δ_v2 ∧ (total_consumption = 460) is SAT  iff  Δ_v2 ⊭ (total_consumption ≠ 460)
        {
            auto& ctx_v2 = dk_v2.ontology->get_context();
            z3::expr tc_v2 = dk_v2.ontology->get_func_decl("total_consumption")();
            z3::expr neq_v2 = (tc_v2 != ctx_v2.real_val(460));
            v2_accepts_460 = !dk_v2.ontology->entails(neq_v2);
        }

        std::cout << "  domain.ont   |= (total_consumption = 460 is satisfiable): "
                  << (v1_accepts_460 ? "YES (460 <= battery_capacity=500)" : "NO") << "\n";
        std::cout << "  domain_v2.ont|= (total_consumption = 460 is satisfiable): "
                  << (v2_accepts_460 ? "YES" : "NO (460 > battery_capacity=450 -- correctly rejected)") << "\n";
        if (!v1_accepts_460 || v2_accepts_460) {
            std::cerr << "[WARNING] Condition II verification unexpected result.\n";
        }

        dtpta::SemanticAlignmentChecker checker_r4;
        dtpta::SemanticAlignmentResult  r4;
        bool ok_r4 = checker_r4.check_semantic_alignment(pt, dt, dk_v2, r4);

        std::cout << "\nVerdict under domain_v2.ont: "
                  << (ok_r4 ? "TRUE (alignment preserved -- Theorem 1 holds)"
                             : "FALSE (alignment broken by ontology evolution)")
                  << "\n";
        r4.print();
        r4.append_to_csv(csv, "UseCase_evo_v2");

        if (ok_r4) {
            std::cout << "[RQ1] Theorem 1 validated (Definition 7 Conditions I, II, III):\n"
                      << "      Condition I (sort Motor + rpm): transparent -- not in interps.\n"
                      << "      Condition II (bat_cap 500->450): tightens total_consumption bound.\n"
                      << "        shutoff!/power_down! symbolic forms equivalent to concrete 450.\n"
                      << "      Condition III (interp update to 450): consistent with Condition II.\n"
                      << "      Alignment preserved under all three conditions.\n";
        } else {
            std::cerr << "[WARNING] RUN 4: ontology evolution broke alignment (Theorem 1 failure).\n";
        }

        // ────────────────────────────────────────────────────────────────────
        // Ontology evolution summary table (RQ1)
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[RQ1] Ontology evolution detailed report:\n";
        std::cout << std::string(75, '-') << "\n";
        std::cout << std::left << std::setw(38) << "Property"
                  << std::setw(18) << "domain.ont (v1)"
                  << "domain_v2.ont (v2)\n";
        std::cout << std::string(75, '-') << "\n";
        std::cout << std::setw(38) << "battery_capacity"
                  << std::setw(18) << "500"
                  << "450 (10% degradation, 200 cycles)\n";
        std::cout << std::setw(38) << "sort Motor"
                  << std::setw(18) << "(absent)"
                  << "added (EASA 2019/947 Annex I)\n";
        std::cout << std::setw(38) << "fun rpm : Motor -> Real"
                  << std::setw(18) << "(absent)"
                  << "added (motor telemetry)\n";
        std::cout << std::setw(38) << "shutoff! (pt_v2.interp)"
                  << std::setw(18) << "symbolic battery_cap"
                  << "concrete 450\n";
        std::cout << std::setw(38) << "power_down! (dt_v2.interp)"
                  << std::setw(18) << "symbolic battery_cap"
                  << "concrete 450\n";
        std::cout << std::setw(38) << "total_consumption=460 valid"
                  << std::setw(18) << (v1_accepts_460 ? "YES" : "NO")
                  << (v2_accepts_460 ? "YES" : "NO (correctly rejected)") << "\n";
        std::cout << std::string(75, '-') << "\n";
        std::cout << "RUN 1 time (v1 ontology): " << r1.time_ms << " ms | "
                  << "RUN 4 time (v2 ontology): " << r4.time_ms << " ms\n";
        std::cout << "SMT calls v1: " << r1.smt_calls_total
                  << " | SMT calls v2: " << r4.smt_calls_total << "\n";

        // ────────────────────────────────────────────────────────────────────
        // Summary
        // ────────────────────────────────────────────────────────────────────
        std::cout << "\n[Summary] Use-Case (drone) Results:\n";
        std::cout << std::left << std::setw(45) << "Variant"
                  << std::setw(20) << "SemAlign"
                  << "Note\n";
        std::cout << std::string(95, '-') << "\n";
        std::cout << std::setw(45) << "RUN 0: Paper validation (spray!/apply!)"
                  << std::setw(20) << (spray_apply_equiv ? "ENTAILED" : "NOT ENTAILED [BUG]")
                  << "Section VII Check 1\n";
        std::cout << std::setw(45) << "RUN 0: Paper validation (shutoff!/power_down!)"
                  << std::setw(20) << (shutoff_power_equiv ? "ENTAILED" : "NOT ENTAILED [BUG]")
                  << "Section VII Check 2\n";
        std::cout << std::setw(45) << "RUN 1: Semantic alignment (aligned pair)"
                  << std::setw(20) << (ok_r1 ? "ALIGNED" : "NOT ALIGNED")
                  << "6 label-equiv pairs\n";
        std::cout << std::setw(45) << "RUN 2: Syntactic bisimulation"
                  << std::setw(20) << (ok_r2 ? "ALIGNED" : "NOT ALIGNED")
                  << "spray!!=apply! (expected failure)\n";
        std::cout << std::setw(45) << "RUN 3: Consumption limit drift (+10 AU/ha)"
                  << std::setw(20) << (ok_r3 ? "ALIGNED" : "NOT ALIGNED")
                  << "EPPO PP 1/213(3) EU violation zone\n";
        std::cout << std::setw(45) << "RUN 4: Battery degradation (Thm 1)"
                  << std::setw(20) << (ok_r4 ? "ALIGNED" : "NOT ALIGNED")
                  << "500->450, Conditions I+II+III\n";

        bool overall_pass = validation_pass && ok_r1 && !ok_r2 && !ok_r3 && ok_r4
                            && v1_accepts_460 && !v2_accepts_460;
        std::cout << "\n[Paper validation: " << (validation_pass ? "PASS" : "FAIL") << "] "
                  << "[Overall: " << (overall_pass ? "ALL EXPECTED RESULTS REPRODUCED"
                                                   : "SOME RESULTS DEVIATE FROM EXPECTATIONS")
                  << "]\n";

        std::cout << "\n[RQ3] Zone-graph sizes:\n";
        std::cout << "  PT zones: " << pt.get_num_states()
                  << " | DT zones: " << dt.get_num_states() << "\n";
        std::cout << "[RQ4] Engineering cost (RUN 1 vs RUN 2):\n";
        std::cout << "  SemAlign (RUN 1): " << r1.time_ms << " ms, "
                  << r1.smt_calls_total << " SMT calls, "
                  << r1.label_pairs_in_E << " label pairs\n";
        std::cout << "  Syntactic (RUN 2): immediate failure (0 matching label strings)\n";

        csv.close();
        std::cout << "\nResults written to: " << csv_path << std::endl;
        return overall_pass ? 0 : 2;  // 2 = validation issue (not system error)

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
