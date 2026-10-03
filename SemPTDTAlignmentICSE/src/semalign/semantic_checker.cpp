/**
 * @file semantic_checker.cpp
 * @brief Implementation of Algorithm 1 (K-bisimulation semantic alignment check).
 *
 * References:
 *  - Definition 8 (K-bisimulation)
 *  - Algorithm 1 (Semantic Alignment Fixpoint)
 */

#include "dtpta/semantic_checker.h"
#include "dtpta/configs.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <queue>
#include <cassert>
#include <fstream>
#include <string>
#ifdef _OPENMP
#  include <omp.h>
#endif

namespace dtpta {

// -------------------------------------------------------------------------- //
//  RSS helper – reads current resident set size from /proc/self/status (Linux)
// -------------------------------------------------------------------------- //

static long read_rss_kb() {
#if defined(__linux__)
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            auto pos = line.find_first_of("0123456789");
            if (pos != std::string::npos)
                return std::stol(line.substr(pos));
        }
    }
#endif
    return 0;
}

// -------------------------------------------------------------------------- //
//  SemanticAlignmentResult – output helpers
// -------------------------------------------------------------------------- //

void SemanticAlignmentResult::print() const {
    std::cout << "Semantic Alignment Result:" << std::endl;
    std::cout << "  Aligned:                " << (aligned ? "TRUE" : "FALSE") << std::endl;
    std::cout << "  Label pairs in E:       " << label_pairs_in_E    << std::endl;
    std::cout << "  Initial state pairs:    " << initial_state_pairs << std::endl;
    std::cout << "  Final relation size:    " << final_relation_size << std::endl;
    std::cout << "  SMT calls total:        " << smt_calls_total     << std::endl;
    std::cout << "  Fixpoint iterations:    " << fixpoint_iterations  << std::endl;
    std::cout << "  Time:                   " << std::fixed << std::setprecision(3)
              << time_ms << " ms" << std::endl;
    std::cout << "  Peak memory (delta):    " << peak_memory_kb << " kB" << std::endl;
    if (counterexample_labels) {
        std::cout << "  Counterexample labels:  PT='"
                  << counterexample_labels->first
                  << "'  DT='" << counterexample_labels->second << "'" << std::endl;
    }
}

void SemanticAlignmentResult::write_csv_header(std::ofstream& file) {
    file << "model_name,aligned,label_pairs_E,initial_pairs,"
            "final_pairs,smt_calls,fixpoint_iters,time_ms,peak_memory_kb" << std::endl;
}

void SemanticAlignmentResult::append_to_csv(std::ofstream& file,
                                              const std::string& model_name) const
{
    file << model_name << ","
         << (aligned ? "true" : "false") << ","
         << label_pairs_in_E    << ","
         << initial_state_pairs << ","
         << final_relation_size << ","
         << smt_calls_total     << ","
         << fixpoint_iterations  << ","
         << std::fixed << std::setprecision(3) << time_ms << ","
         << peak_memory_kb
         << std::endl;
}

// -------------------------------------------------------------------------- //
//  SemanticAlignmentChecker – static helpers
// -------------------------------------------------------------------------- //

std::unordered_set<std::string>
SemanticAlignmentChecker::observable_labels(const TimedAutomaton& ta) {
    std::unordered_set<std::string> labels;
    for (const auto& t : ta.get_transitions()) {
        std::string a;
        if (t.has_synchronization()) {
            a = t.channel + (t.is_sender ? "!" : "?");
        } else {
            a = t.action;
        }
        if (!a.empty() && a != TA_CONFIG.tau_action_name) {
            labels.insert(a);
        }
    }
    return labels;
}

z3::expr SemanticAlignmentChecker::formula_for_event(const InterpretationMap& imap,
                                                       const std::string& label,
                                                       z3::context& ctx)
{
    auto opt = imap.get_event(label);
    return opt.has_value() ? *opt : ctx.bool_val(true);
}

z3::expr SemanticAlignmentChecker::formula_for_state(const InterpretationMap& imap,
                                                       const std::string& location_name,
                                                       z3::context& ctx)
{
    auto opt = imap.get_state(location_name);
    return opt.has_value() ? *opt : ctx.bool_val(true);
}

// -------------------------------------------------------------------------- //
//  Step 1: Build label equivalence relation E
// -------------------------------------------------------------------------- //

void SemanticAlignmentChecker::build_label_equivalence(
    const TimedAutomaton& pt,
    const TimedAutomaton& dt,
    const DomainKnowledge& phi,
    std::unordered_map<std::string, std::unordered_set<std::string>>& E)
{
    Ontology& ont = *phi.ontology;
    z3::context& ctx = ont.get_context();

    // A = dom(I_PT), B = dom(I_DT)  — Definition 8 of the paper.
    // Labels NOT in the interp file are tau-transitions and must be
    // invisible to the bisimulation conditions.
    auto pt_label_vec = phi.pt_interp.event_labels();
    auto dt_label_vec = phi.dt_interp.event_labels();
    std::unordered_set<std::string> pt_labels(pt_label_vec.begin(), pt_label_vec.end());
    std::unordered_set<std::string> dt_labels(dt_label_vec.begin(), dt_label_vec.end());

    // --- Tautology pre-check ---
    // A label whose interpretation Δ ⊨ I(a) is always true under the domain
    // axioms provides no discriminatory power.  Such a label is treated as tau:
    // we remove it from dom(I_PT) / dom(I_DT) so it never enters E and is
    // invisible to both Condition II and Condition III.
    {
        std::vector<std::string> taut_pt, taut_dt;
        for (const auto& a : pt_labels) {
            if (phi.pt_interp.has_event(a)) {
                z3::expr fa = formula_for_event(phi.pt_interp, a, ctx);
                if (ont.entails(fa)) {
                    std::cout << "[WARNING] PT label '" << a
                              << "' has a tautological interpretation under Δ"
                                 " — treating as tau (removed from dom(I_PT))\n";
                    taut_pt.push_back(a);
                }
            }
        }
        for (const auto& ap : dt_labels) {
            if (phi.dt_interp.has_event(ap)) {
                z3::expr fap = formula_for_event(phi.dt_interp, ap, ctx);
                if (ont.entails(fap)) {
                    std::cout << "[WARNING] DT label '" << ap
                              << "' has a tautological interpretation under Δ"
                                 " — treating as tau (removed from dom(I_DT))\n";
                    taut_dt.push_back(ap);
                }
            }
        }
        for (const auto& a  : taut_pt) pt_labels.erase(a);
        for (const auto& ap : taut_dt) dt_labels.erase(ap);
    }

    for (const auto& a : pt_labels) {
        E[a]; // ensure entry exists even if empty
        z3::expr fa = formula_for_event(phi.pt_interp, a, ctx);
        bool a_has_interp = phi.pt_interp.has_event(a);

        for (const auto& a_prime : dt_labels) {
            bool ap_has_interp = phi.dt_interp.has_event(a_prime);
            z3::expr fap = formula_for_event(phi.dt_interp, a_prime, ctx);

            bool eq = false;
            if (!a_has_interp && !ap_has_interp) {
                // Both uninterpreted: syntactic equality
                eq = (a == a_prime);
            } else {
                // At least one has an interpretation: use SMT entailment
                eq = ont.entails_iff(fa, fap);
            }

            if (eq) {
                E[a].insert(a_prime);
            }
        }
    }
}

// -------------------------------------------------------------------------- //
//  Step 2 + 3 combined: BFS-based on-demand K-bisimulation check
//
//  Instead of seeding the full cross-product of zone states and then
//  refining, we explore the relation lazily starting from (init_pt, init_dt).
//  Only pairs reachable via observable successors are ever generated, which
//  is typically orders of magnitude smaller than the full cross product.
// -------------------------------------------------------------------------- //

// Internal helper: check whether a single pair (zp, zd) satisfies all
// K-bisimulation conditions given the current candidate relation kbisim.
// Newly discovered successor pairs that are not yet in kbisim or the todo
// set are appended to `new_pairs`.
bool SemanticAlignmentChecker::check_pair(
    const TimedAutomaton& pt,
    const TimedAutomaton& dt,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& E,
    const std::unordered_set<std::string>& dt_observable,
    const std::unordered_map<std::string, std::vector<std::string>>& pt_equiv_of,
    const std::unordered_set<std::string>& a_unmatched,
    const std::unordered_set<std::string>& b_unmatched,
    const ZoneState* zp,
    const ZoneState* zd,
    const RelationSet& kbisim,
    const RelationSet& rejected,
    std::vector<ZonePair>& new_pairs,
    SemanticAlignmentResult& result)
{
    // ── Condition II (A-unmatched) ───────────────────────────────────────── //
    // Check structurally: does any location reachable via tau from zp have
    // an outgoing transition labeled 'a'? Use location-level check, not
    // zone-level, because guard satisfiability is clock-valuation-dependent
    // and a different clock valuation at this location would enable the transition.
    for (const auto& a : a_unmatched) {
        auto pre_pt = tau_closure_cached(pt, zp);
        for (const ZoneState* z : pre_pt) {
            for (const Transition* tr : pt.get_outgoing_transitions(z->location_id)) {
                const std::string tr_label = tr->has_synchronization()
                    ? tr->channel + (tr->is_sender ? "!" : "?")
                    : tr->action;
                if (tr_label == a) {
                    if (!result.counterexample_labels)
                        result.counterexample_labels = {a, "<A-unmatched>"};
                    return false;
                }
            }
        }
    }

    // ── Condition III (B-unmatched) ──────────────────────────────────────── //
    for (const auto& ap : b_unmatched) {
        auto pre_dt = tau_closure_cached(dt, zd);
        for (const ZoneState* z : pre_dt) {
            for (const Transition* tr : dt.get_outgoing_transitions(z->location_id)) {
                const std::string tr_label = tr->has_synchronization()
                    ? tr->channel + (tr->is_sender ? "!" : "?")
                    : tr->action;
                if (tr_label == ap) {
                    if (!result.counterexample_labels)
                        result.counterexample_labels = {"<B-unmatched>", ap};
                    return false;
                }
            }
        }
    }

    // ── Condition II: every PT observable move must be matched in DT ─────── //
    {
        auto pre_pt = tau_closure_cached(pt, zp);
        std::unordered_set<int> seen_locs;
        for (const ZoneState* z : pre_pt) {
            if (!seen_locs.insert(z->location_id).second) continue;
            for (const Transition* tr : pt.get_outgoing_transitions(z->location_id)) {
                const std::string a = tr->has_synchronization()
                    ? tr->channel + (tr->is_sender ? "!" : "?")
                    : tr->action;
                auto e_it = E.find(a);
                if (e_it == E.end()) continue;

                const auto& dt_equiv = e_it->second;
                auto pt_succs = weak_observable_successors_cached(pt, zp, a);

                for (const ZoneState* zp_prime : pt_succs) {
                    bool found_match = false;
                    bool any_candidate = false;

                    for (const auto& a_prime : dt_equiv) {
                        if (found_match) break;
                        auto dt_succs = weak_observable_successors_cached(dt, zd, a_prime);
                        for (const ZoneState* zd_prime : dt_succs) {
                            if (kbisim.count({zp_prime, zd_prime})) {
                                found_match = true;
                                break;
                            }
                            // Only enqueue as candidate if not already known rejected
                            if (!rejected.count({zp_prime, zd_prime})) {
                                new_pairs.push_back({zp_prime, zd_prime});
                                any_candidate = true;
                            }
                        }
                    }

                    if (!found_match && (dt_equiv.empty() || !any_candidate)) {
                        // No current witness and no viable candidates remain
                        if (!result.counterexample_labels)
                            result.counterexample_labels = {a, "?"};
                        return false;
                    }
                }
            }
        }
    }

    // ��─ Condition III: every DT observable move must be matched in PT ─────── //
    {
        auto pre_dt = tau_closure_cached(dt, zd);
        std::unordered_set<int> seen_locs;
        for (const ZoneState* z : pre_dt) {
            if (!seen_locs.insert(z->location_id).second) continue;
            for (const Transition* tr : dt.get_outgoing_transitions(z->location_id)) {
                const std::string a_prime = tr->has_synchronization()
                    ? tr->channel + (tr->is_sender ? "!" : "?")
                    : tr->action;
                if (!dt_observable.count(a_prime)) continue;

                auto dt_succs = weak_observable_successors_cached(dt, zd, a_prime);

                for (const ZoneState* zd_prime : dt_succs) {
                    bool found_match = false;
                    bool any_candidate = false;

                    auto peq_it = pt_equiv_of.find(a_prime);
                    if (peq_it != pt_equiv_of.end()) {
                        for (const auto& a : peq_it->second) {
                            if (found_match) break;
                            auto pt_succs = weak_observable_successors_cached(pt, zp, a);
                            for (const ZoneState* zp_prime : pt_succs) {
                                if (kbisim.count({zp_prime, zd_prime})) {
                                    found_match = true;
                                    break;
                                }
                                if (!rejected.count({zp_prime, zd_prime})) {
                                    new_pairs.push_back({zp_prime, zd_prime});
                                    any_candidate = true;
                                }
                            }
                        }
                    }

                    if (!found_match && (peq_it == pt_equiv_of.end() || peq_it->second.empty() || !any_candidate)) {
                        if (!result.counterexample_labels)
                            result.counterexample_labels = {"?", a_prime};
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

// -------------------------------------------------------------------------- //
//  Step 2: seed_relation — kept for API compatibility, seeds only init pair
// -------------------------------------------------------------------------- //

void SemanticAlignmentChecker::seed_relation(
    const TimedAutomaton& pt,
    const TimedAutomaton& dt,
    const DomainKnowledge& phi,
    RelationSet& kbisim)
{
    // BFS mode: we only seed the initial pair here.
    // The full exploration happens in fixpoint_refine (BFS traversal).
    const ZoneState* init_pt = const_cast<TimedAutomaton&>(pt).get_or_create_initial_state();
    const ZoneState* init_dt = const_cast<TimedAutomaton&>(dt).get_or_create_initial_state();
    if (init_pt && init_dt)
        kbisim.insert({init_pt, init_dt});
}

// -------------------------------------------------------------------------- //
//  Step 3: BFS-based fixpoint refinement
//
//  We use a worklist (todo) of pairs that need to be verified.  When a pair
//  passes the K-bisimulation conditions we keep it and enqueue any newly
//  discovered successor pairs.  When a pair fails we remove it and re-check
//  all pairs that used it as a witness (via a reverse-dependency map).
// -------------------------------------------------------------------------- //

void SemanticAlignmentChecker::fixpoint_refine(
    const TimedAutomaton& pt,
    const TimedAutomaton& dt,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& E,
    const std::unordered_set<std::string>& dt_observable,
    RelationSet& kbisim,
    SemanticAlignmentResult& result)
{
    // ── Precompute unmatched label sets ──────────────────────────────────── //
    std::unordered_set<std::string> a_unmatched;
    for (const auto& [a, equiv_set] : E) {
        if (equiv_set.empty()) a_unmatched.insert(a);
    }

    std::unordered_set<std::string> b_unmatched;
    for (const auto& a_prime : dt_observable) {
        bool has_equiv = false;
        for (const auto& [a, equiv_set] : E) {
            if (equiv_set.count(a_prime)) { has_equiv = true; break; }
        }
        if (!has_equiv) b_unmatched.insert(a_prime);
    }

    // Build reverse map: dt_label -> PT equivalent labels
    std::unordered_map<std::string, std::vector<std::string>> pt_equiv_of;
    for (const auto& [a, equiv_set] : E) {
        for (const auto& a_prime : equiv_set) {
            pt_equiv_of[a_prime].push_back(a);
        }
    }

    // ── BFS exploration ──────────────────────────────────────────────────── //
    // todo: pairs that need to be verified (not yet in kbisim)
    // kbisim: pairs that have been verified and accepted
    // rejected: pairs known to be invalid (avoid re-checking)

    RelationSet todo;
    RelationSet rejected;

    // Move the initial seed from kbisim into todo for processing
    for (const auto& pr : kbisim) todo.insert(pr);
    kbisim.clear();

    // For re-checking: reverse dependency map.
    // deps[q] = set of pairs (p,d) that used q as a witness.
    // If q is removed from kbisim, all dependents must be re-checked.
    std::unordered_map<ZonePair, std::vector<ZonePair>, ZonePairHash> dependents;

    size_t progress_accepted = 0;
    size_t progress_rejected = 0;
    size_t progress_print_next = 10000;

    while (!todo.empty()) {
        // Pick next pair to check
        ZonePair pr = *todo.begin();
        todo.erase(todo.begin());

        if (kbisim.count(pr) || rejected.count(pr)) continue;

        const ZoneState* zp = pr.first;
        const ZoneState* zd = pr.second;

        // Temporarily admit this pair so that successor lookups can see it
        kbisim.insert(pr);

        std::vector<ZonePair> new_pairs;
        bool valid = check_pair(pt, dt, E, dt_observable, pt_equiv_of,
                                a_unmatched, b_unmatched,
                                zp, zd, kbisim, rejected, new_pairs, result);

        if (!valid) {
            kbisim.erase(pr);
            rejected.insert(pr);
            ++progress_rejected;

            // Re-check all pairs that depended on this one
            auto dep_it = dependents.find(pr);
            if (dep_it != dependents.end()) {
                for (const auto& dep : dep_it->second) {
                    if (kbisim.count(dep)) {
                        kbisim.erase(dep);
                        todo.insert(dep);
                    }
                }
                dependents.erase(dep_it);
            }
        } else {
            ++progress_accepted;
            // Record reverse dependencies for newly discovered pairs
            for (const auto& succ : new_pairs) {
                if (!kbisim.count(succ) && !rejected.count(succ)) {
                    todo.insert(succ);
                }
                dependents[succ].push_back(pr);
            }
        }

        ++result.fixpoint_iterations;

        // Periodic progress print
        size_t total_seen = progress_accepted + progress_rejected;
        if (total_seen >= progress_print_next) {
            std::cout << "  BFS: accepted=" << progress_accepted
                      << "  rejected=" << progress_rejected
                      << "  todo=" << todo.size()
                      << std::endl;
            progress_print_next = total_seen + 10000;
        }
    }

    std::cout << "  BFS done: accepted=" << progress_accepted
              << "  rejected=" << progress_rejected << std::endl;
}

// -------------------------------------------------------------------------- //
//  Standard weak timed bisimulation (syntactic label matching)
// -------------------------------------------------------------------------- //

bool SemanticAlignmentChecker::check_weak_timed_bisimulation(
    const TimedAutomaton& pt_view,
    const TimedAutomaton& dt_view,
    SemanticAlignmentResult& result)
{
    auto t_start = std::chrono::high_resolution_clock::now();

    if (pt_view.get_num_states() == 0) pt_view.construct_zone_graph();
    if (dt_view.get_num_states() == 0) dt_view.construct_zone_graph();

    assert(pt_view.get_num_states() > 0 && "PT zone graph could not be constructed");
    assert(dt_view.get_num_states() > 0 && "DT zone graph could not be constructed");

    this->reset();

    // Build syntactic E: pair each label with itself iff it appears in both automata.
    // PT-only labels get an empty DT-set (Condition II blocks them).
    // DT-only labels are in dt_observable only (Condition III blocks them).
    auto pt_labels = observable_labels(pt_view);
    auto dt_labels = observable_labels(dt_view);

    std::unordered_map<std::string, std::unordered_set<std::string>> E;
    E.reserve(pt_labels.size());
    for (const auto& a : pt_labels) {
        E[a]; // ensure entry exists so Condition II is checked
        if (dt_labels.count(a))
            E[a].insert(a);
    }

    size_t e_size = 0;
    for (const auto& [a, eqs] : E) e_size += eqs.size();
    result.label_pairs_in_E = e_size;

    std::cout << "WTB: " << pt_view.get_num_states() << " PT zones, "
              << dt_view.get_num_states() << " DT zones, "
              << E.size() << " PT labels, "
              << e_size << " matched pairs" << std::endl;

    // Seed only the initial pair.  fixpoint_refine uses BFS and discovers all
    // reachable pairs on demand — seeding the full location-compatible
    // cross-product would fill the worklist with unreachable pairs for nothing.
    const ZoneState* init_pt = const_cast<TimedAutomaton&>(pt_view).get_or_create_initial_state();
    const ZoneState* init_dt = const_cast<TimedAutomaton&>(dt_view).get_or_create_initial_state();

    if (!init_pt || !init_dt) {
        result.aligned = false;
        result.time_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t_start).count();
        return false;
    }

    RelationSet kbisim;
    kbisim.insert({init_pt, init_dt});
    result.initial_state_pairs = 1;

    fixpoint_refine(pt_view, dt_view, E, dt_labels, kbisim, result);

    result.final_relation_size = kbisim.size();
    result.smt_calls_total     = 0; // no Z3 needed

    result.aligned = kbisim.count({init_pt, init_dt}) > 0;
    if (result.aligned) result.counterexample_labels = std::nullopt;

    auto t_end = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    return result.aligned;
}

// -------------------------------------------------------------------------- //
//  Public entry point: check_semantic_alignment
// -------------------------------------------------------------------------- //

bool SemanticAlignmentChecker::check_semantic_alignment(
    const TimedAutomaton& pt_view,
    const TimedAutomaton& dt_view,
    const DomainKnowledge& phi,
    SemanticAlignmentResult& result)
{
    auto t_start = std::chrono::high_resolution_clock::now();
    long mem_baseline_kb = read_rss_kb();

    // ---- Ensure zone graphs are built ----
    if (pt_view.get_num_states() == 0) {
        pt_view.construct_zone_graph();
    }
    if (dt_view.get_num_states() == 0) {
        dt_view.construct_zone_graph();
    }

    assert(pt_view.get_num_states() > 0 && "PT zone graph could not be constructed");
    assert(dt_view.get_num_states() > 0 && "DT zone graph could not be constructed");

    // Reset caches from any previous run
    this->reset();

    // ---- Step 1: Build label equivalence E ----
    std::unordered_map<std::string, std::unordered_set<std::string>> E;
    build_label_equivalence(pt_view, dt_view, phi, E);



    size_t e_size = 0;
    for (const auto& [a, eqs] : E) e_size += eqs.size();
    result.label_pairs_in_E = e_size;

    // ---- Print label equivalence E and unmatched sets ----
    {
        // Collect B-unmatched (DT labels with no PT equivalent) for printing.
        // We need dt_observable early here just for the print; it is rebuilt
        // properly below before fixpoint_refine.
        auto dt_lv = phi.dt_interp.event_labels();
        std::unordered_set<std::string> dt_obs_tmp(dt_lv.begin(), dt_lv.end());

        std::cout << "\n=== Label Equivalence E ==="  << std::endl;
        bool any_a_unmatched = false;
        for (const auto& [a, eqs] : E) {
            if (eqs.empty()) {
                std::cout << "  [A-UNMATCHED] " << a << "  →  (no DT equivalent)" << std::endl;
                any_a_unmatched = true;
            } else {
                std::cout << "  " << a << "  →  {";
                bool first = true;
                for (const auto& ap : eqs) { if (!first) std::cout << ", "; std::cout << ap; first = false; }
                std::cout << "}" << std::endl;
            }
        }
        bool any_b_unmatched = false;
        for (const auto& ap : dt_obs_tmp) {
            bool has_equiv = false;
            for (const auto& [a, eqs] : E)
                if (eqs.count(ap)) { has_equiv = true; break; }
            if (!has_equiv) {
                std::cout << "  [B-UNMATCHED]              ←  " << ap << "  (no PT equivalent)" << std::endl;
                any_b_unmatched = true;
            }
        }
        if (!any_a_unmatched && !any_b_unmatched)
            std::cout << "  (all labels matched)" << std::endl;
        std::cout << "  Total: " << E.size() << " PT labels, "
                  << e_size << " matched pairs, "
                  << pt_view.get_num_states() << " PT zones, "
                  << dt_view.get_num_states() << " DT zones"
                  << std::endl;
    }

    // ---- Step 2: Seed the candidate relation K~ ----
    RelationSet kbisim;
    seed_relation(pt_view, dt_view, phi, kbisim);
    result.initial_state_pairs = kbisim.size();

    // ---- Step 3: Greatest-fixed-point refinement ----
    // Build dom(I_DT) for Condition III (Definition 8).
    // Filter out any tautological DT labels (same logic as build_label_equivalence)
    // so that unobservable-by-design transitions are not required to be matched.
    auto dt_label_vec = phi.dt_interp.event_labels();
    std::unordered_set<std::string> dt_observable(dt_label_vec.begin(), dt_label_vec.end());
    {
        Ontology& ont = *phi.ontology;
        z3::context& ctx = ont.get_context();
        std::vector<std::string> taut_dt;
        for (const auto& ap : dt_observable) {
            if (phi.dt_interp.has_event(ap)) {
                z3::expr fap = formula_for_event(phi.dt_interp, ap, ctx);
                if (ont.entails(fap)) taut_dt.push_back(ap);
            }
        }
        for (const auto& ap : taut_dt) dt_observable.erase(ap);
    }
    fixpoint_refine(pt_view, dt_view, E, dt_observable, kbisim, result);

    // Measure peak RSS while kbisim (the dominant allocation) is still live.
    long mem_peak_kb = read_rss_kb();
    result.peak_memory_kb = std::max(0L, mem_peak_kb - mem_baseline_kb);

    result.final_relation_size = kbisim.size();

    // ---- Step 4: Check if initial pair is in K~ ----
    const ZoneState* init_pt = const_cast<TimedAutomaton&>(pt_view).get_or_create_initial_state();
    const ZoneState* init_dt = const_cast<TimedAutomaton&>(dt_view).get_or_create_initial_state();

    result.aligned = (init_pt != nullptr)
                  && (init_dt != nullptr)
                  && kbisim.count({init_pt, init_dt}) > 0;

    // Clear the counterexample if we actually are aligned
    if (result.aligned) {
        result.counterexample_labels = std::nullopt;
    }

    result.smt_calls_total = phi.ontology->get_smt_call_count();

    auto t_end = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    return result.aligned;
}

} // namespace dtpta
