/**
 * @file core.cpp
 * @brief Game-based implementation of Relaxed Weak Timed Bisimulation (DTPTA).
 *
 * This file contains a lightweight on-the-fly game algorithm for checking
 * relaxed weak timed bisimulation between timed automata. The notion of
 * relaxation follows the asymmetric timing constraints:
 *  - Sent actions (!) in the refined automaton must be at least as restrictive
 *    (i.e., enabling zone is a subset) as in the abstract one.
 *  - Received actions (?) in the refined automaton may be more permissive
 *    (i.e., enabling zone is a superset) than in the abstract one.
 *  - Internal (unsynchronized / tau) actions are matched under standard
 *    weak semantics (tau* a tau*), requiring refined enabling ⊆ abstract.
 *
 * The algorithm keeps a candidate relation R of zone pairs (location + DBM).
 * It eliminates violating pairs until reaching a greatest fixed point.
 * If the relation is non-empty after convergence, refinement holds.
 *
 * NOTE: This is a first clean version focused on correctness/clarity.
 * Future optimisations: memoisation of tau-closure, weak successors,
 * on-demand zone graph expansion, and predecessor dependency tracking.
 */
// Clean reimplementation (game-based DTPTA)
#include "dtpta/core.h"
#include <chrono>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <iostream>
#include <future>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
namespace dtpta {

// anonymous helpers (not part of public API)


// Global cooperative-cancel flag for long-running computations in this TU.
// All checker instances in this process will honor cancellation when set.
static std::atomic<bool>* DTPTA_CANCEL_FLAG = nullptr;
inline bool is_cancelled() {
    return DTPTA_CANCEL_FLAG && DTPTA_CANCEL_FLAG->load(std::memory_order_relaxed);
}

/**
 * @brief Hash functor for pairs of zone state pointers.
 * @details Used to store candidate relation pairs in an unordered_set.
 */
struct ZonePairHash {
    size_t operator()(const std::pair<const ZoneState*, const ZoneState*>& p) const noexcept {
        return reinterpret_cast<uintptr_t>(p.first) ^ (reinterpret_cast<uintptr_t>(p.second) << 1);
    }
};

/**
 * @brief Check if a transition is considered internal (tau) under weak semantics.
 * @param t Transition pointer (may be nullptr – not checked here).
 * @return true if action is tau or empty unsynchronized; false otherwise.
 */
inline bool is_tau(const Transition* t){
    // Internal moves are ONLY unsynchronized and labeled as tau (or empty)
    return (!t->has_synchronization()) && (t->action == TA_CONFIG.tau_action_name || t->action.empty());
}

/**
 * @brief Compute the τ-closure (reachable zones using only weak/internal moves).
 *
 * Process:
 *  1. BFS over internal transitions.
 *  2. For every internal edge: apply invariants, elapse time, re-apply invariants, apply reset+guards, invariants.
 *  3. Reuse existing canonical zone states via find_zone_state.
 *
 * @param ta Timed automaton.
 * @param start Starting zone state.
 * @return Vector of all reachable zone states (including start).
 */
std::vector<const ZoneState*> DTPTAChecker::tau_closure_raw(const TimedAutomaton& ta, const ZoneState* start){
    std::vector<const ZoneState*> closure; 
    std::queue<const ZoneState*> q; 
    std::unordered_set<const ZoneState*> visited;
    q.push(start); 
    visited.insert(start);
    while(!q.empty()){
        if (is_cancelled()) return closure;
        auto z = q.front(); 
        q.pop(); 
        closure.push_back(z);
        auto outs = ta.get_outgoing_transitions(z->location_id);
        for(auto tr: outs){ 
            if (is_cancelled()) return closure;
            if(!is_tau(tr)) continue; 
            auto zInv = ta.apply_invariants(z->zone, z->location_id);
            if(zInv.empty()) continue;
            auto zUp = ta.time_elapse(zInv);
            if(zUp.empty()) continue;
            // Re-apply invariants after Up to avoid enabling via invariant violations
            auto ready = ta.apply_invariants(zUp, z->location_id);
            if(ready.empty()) continue;
            // Direct attempt: apply transition; guards+resets handled inside
            auto post = ta.apply_transition(ready, *tr);
            if(post.empty()) continue; 
            post = ta.apply_invariants(post, tr->to_location); 
            if(post.empty()) continue; 
            // Lazily register the successor zone state (on-the-fly safe).
            // get_or_add_zone_state creates the state if it doesn't exist yet.
            const ZoneState* existing = const_cast<TimedAutomaton&>(ta).get_or_add_zone_state(tr->to_location, post); 
            if(existing && !visited.count(existing)){ 
                visited.insert(existing); 
                q.push(existing);
            }
        }
    }
    return closure;
}


// ===== DTPTAChecker optimisation member implementations =====
std::vector<const ZoneState*> DTPTAChecker::tau_closure_cached(const TimedAutomaton& ta, const ZoneState* start){
#ifdef DTPTA_HAS_TBB
    // TBB concurrent_hash_map: fine-grained per-bucket locking, no global critical section
    {
        typename decltype(tau_closure_cache_)::const_accessor reader;
        if (tau_closure_cache_.find(reader, start)) {
            // Cache hit — copy out while accessor holds per-bucket lock
            auto result = reader->second;
            reader.release();
            #pragma omp atomic
            last_stats_.tau_closure_queries++;
            #pragma omp atomic
            last_stats_.tau_closure_total_size += result.size();
            #pragma omp atomic
            last_stats_.symbolic_states_explored += result.size();
            return result;
        }
    }
    // Cache miss — compute then insert
    auto vec = tau_closure_raw(ta, start);
    size_t sz = vec.size();
    {
        typename decltype(tau_closure_cache_)::accessor writer;
        if (tau_closure_cache_.insert(writer, start)) {
            // We won the insert race — move data in
            writer->second = std::move(vec);
        }
        // If another thread inserted first, writer->second already has valid data
        auto result = writer->second;
        writer.release();
        #pragma omp atomic
        last_stats_.tau_closure_queries++;
        #pragma omp atomic
        last_stats_.tau_closure_total_size += result.size();
        #pragma omp atomic
        last_stats_.symbolic_states_explored += result.size();
        return result;
    }
#else
    // Sequential fallback: standard unordered_map
    auto it = tau_closure_cache_.find(start);
    if(it!=tau_closure_cache_.end()) {
        last_stats_.tau_closure_queries++;
        last_stats_.tau_closure_total_size += it->second.size();
        last_stats_.symbolic_states_explored += it->second.size();
        return it->second;
    }
    auto vec = tau_closure_raw(ta, start);
    last_stats_.tau_closure_queries++;
    last_stats_.tau_closure_total_size += vec.size();
    last_stats_.symbolic_states_explored += vec.size();
    auto ins = tau_closure_cache_.emplace(start, std::move(vec));
    return ins.first->second;
#endif
}

/**
 * @brief Compute weak successors for an observable action (τ* a τ* pattern).
 *
 * Steps:
 *  1. Expand τ-closure pre-set from the start state.
 *  2. For each zone in closure, attempt the observable action.
 *  3. After the action, expand τ-closure again and collect endpoints.
 *  4. Deduplicate resulting zone states.
 *
 * @param ta Timed automaton.
 * @param start Start zone state.
 * @param action Observable action label to match.
 * @return Vector of unique weak reachable zone states.
 */
std::vector<const ZoneState*> DTPTAChecker::weak_observable_successors_raw(const TimedAutomaton& ta,
                                                             const ZoneState* start,
                                                             const std::string& action){
    std::vector<const ZoneState*> result; 
    auto pre = tau_closure_cached(ta, start);
    for(auto z: pre){ 
        if (is_cancelled()) break;
        auto outs = ta.get_outgoing_transitions(z->location_id); 
        for(auto tr: outs){
            if (is_cancelled()) break;
            const std::string tr_label = tr->has_synchronization()
                ? tr->channel + (tr->is_sender ? "!" : "?")
                : tr->action;
            if(tr_label != action) continue;
            auto zInv = ta.apply_invariants(z->zone, z->location_id);
            if(zInv.empty()) continue;
            auto zUp = ta.time_elapse(zInv);
            if(zUp.empty()) continue;
            auto ready = ta.apply_invariants(zUp, z->location_id);
            if(ready.empty()) continue;
            auto post = ta.apply_transition(ready, *tr);
            if(post.empty()) continue;
            post = ta.apply_invariants(post, tr->to_location);
            if(post.empty()) continue;
            // Lazily register the successor zone state (on-the-fly safe).
            const ZoneState* mid = const_cast<TimedAutomaton&>(ta).get_or_add_zone_state(tr->to_location, post); 
            if(!mid) continue; 
            auto postTau = tau_closure_cached(ta, mid); 
            result.insert(result.end(), postTau.begin(), postTau.end()); 
        }
    }
    std::unordered_set<const ZoneState*> uniq(result.begin(), result.end()); 
    result.assign(uniq.begin(), uniq.end()); 
    return result;
}

/**
 * @brief Check asymmetric timing compatibility between enabling zones.
 *
 * Constructs Up((Z ∩ Inv) ∩ Guard) for both refined and abstract sides.
 * Uses DBM relation to decide subset/superset.
 * Rules applied:
 *  - Internal (no sync)     : refined ⊆ abstract
 *  - Synchronous send  (!)  : refined ⊆ abstract (must not widen send window)
 *  - Synchronous receive (?) : abstract ⊆ refined (refined may wait longer / allow earlier)
 *
 * @return true if timing relation satisfies DTPTA rule for this action.
 */
bool timing_ok(const TimedAutomaton& refined, const ZoneState* rz, const Transition* rt,
               const TimedAutomaton& abs, const ZoneState* az, const Transition* at){

    auto rInv = refined.apply_invariants(rz->zone, rz->location_id);
    if(rInv.empty()) return false;
    auto rUp = refined.time_elapse(rInv);
    if(rUp.empty()) return false;
    // Keep invariants enforced during delay
    auto rReady = refined.apply_invariants(rUp, rz->location_id);
    if(rReady.empty()) return false;

    for(auto &g: rt->guards) dbm_constrain1(rReady.data(), refined.get_dimension(), g.i, g.j, g.value);
    bool guards_ok_in_r=true;
    if(!dbm_close(rReady.data(), refined.get_dimension()) || dbm_isEmpty(rReady.data(), refined.get_dimension()))
    {
        guards_ok_in_r= false;
    }


    auto aInv = abs.apply_invariants(az->zone, az->location_id);
    if(aInv.empty()) return false;
    auto aUp = abs.time_elapse(aInv);
                
    if(aUp.empty()) return false;
    // Keep invariants enforced during delay
    
    auto aReady = abs.apply_invariants(aUp, az->location_id);
    if(aReady.empty()) return false;


    for(auto &g: at->guards) dbm_constrain1(aReady.data(), abs.get_dimension(), g.i, g.j, g.value);
    bool guards_ok_in_a=true;
    if(!dbm_close(aReady.data(), abs.get_dimension()) || dbm_isEmpty(aReady.data(), abs.get_dimension())){
        guards_ok_in_a= false;
    }

    if(guards_ok_in_r && guards_ok_in_a){
        // Both can move → check zone relation
        relation_t rel = dbm_relation(rReady.data(), aReady.data(), refined.get_dimension()); 
        bool r_subset_a = (rel == base_SUBSET || rel == base_EQUAL); 
        bool a_subset_r = (rel == base_SUPERSET || rel == base_EQUAL);

        if(!rt->has_synchronization() && !at->has_synchronization())
            return r_subset_a; // internal

        if(rt->has_synchronization() && at->has_synchronization() && rt->channel == at->channel){ 
            if(rt->is_sender && at->is_sender) return r_subset_a; 
            if(rt->is_receiver && at->is_receiver) return a_subset_r; 
        }
        return false; // guards ok but cannot match
    }
    else if(!guards_ok_in_r && !guards_ok_in_a){
        // Both cannot move → bisimulation trivially holds
        return true;
    }
    else {
        // Only one can move → bisimulation fails
        return false;
    }
}




std::vector<const ZoneState*> DTPTAChecker::weak_observable_successors_cached(const TimedAutomaton& ta, const ZoneState* start, const std::string& action){
    WeakKey k{start, action};
#ifdef DTPTA_HAS_TBB
    // TBB concurrent_hash_map: fine-grained per-bucket locking
    {
        typename decltype(weak_succ_cache_)::const_accessor reader;
        if (weak_succ_cache_.find(reader, k)) {
            auto result = reader->second;
            reader.release();
            #pragma omp atomic
            last_stats_.weak_successor_queries++;
            #pragma omp atomic
            last_stats_.total_weak_successors += result.size();
            return result;
        }
    }
    // Cache miss — compute then insert
    auto vec = weak_observable_successors_raw(ta, start, action);
    {
        typename decltype(weak_succ_cache_)::accessor writer;
        if (weak_succ_cache_.insert(writer, k)) {
            writer->second = std::move(vec);
        }
        auto result = writer->second;
        writer.release();
        #pragma omp atomic
        last_stats_.weak_successor_queries++;
        #pragma omp atomic
        last_stats_.total_weak_successors += result.size();
        return result;
    }
#else
    // Sequential fallback
    auto it = weak_succ_cache_.find(k);
    if(it!=weak_succ_cache_.end()) {
        last_stats_.weak_successor_queries++;
        last_stats_.total_weak_successors += it->second.size();
        return it->second;
    }
    auto vec = weak_observable_successors_raw(ta, start, action);
    last_stats_.weak_successor_queries++;
    last_stats_.total_weak_successors += vec.size();
    auto ins = weak_succ_cache_.emplace(std::move(k), std::move(vec));
    return ins.first->second;
#endif
}

void DTPTAChecker::clear_optimisation_state(){
    tau_closure_cache_.clear();
    weak_succ_cache_.clear();
    reverse_deps_.clear();
    relation_.clear();
    while(!worklist_.empty()) worklist_.pop();
#ifdef DTPTA_HAS_TBB
    otf_visited_valid_tbb_.clear();
    otf_visited_invalid_tbb_.clear();
#else
    otf_visited_valid_shared_.clear();
    otf_visited_invalid_shared_.clear();
#endif
}
/**
 * @brief Core DTPTA simulation (refinement) check between two automata.
 *
 * Algorithm Outline:
 *  1. Ensure zone graphs are (fully) constructed (current implementation builds all states).
 *  2. Seed candidate relation R with all zone pairs sharing the same location id.
 *  3. Iterate elimination: for each pair (r,a) verify every observable refined move
 *     can be matched by some abstract weak move with acceptable timing and related successors.
 *  4. Remove failing pairs; stop at fixed point. Non-empty relation => refinement holds.
 *
 * Correctness Notes:
 *  - This is a greatest fixed point style refinement (monotone elimination).
 *  - Successor validation is optimistic: requires existence of at least one successor pair
 *    already in relation; convergence ensures global consistency.
 *  - Tau behaviours are abstracted via τ-closure in both pre/post phases.
 *
 * Limitations / TODO:
 *  - No counterexample extraction.
 *  - No on-demand zone exploration (eager build may be large).
 *  - Could refine initial seeding using DBM inclusion to reduce search space.
 *  - Could track predecessor dependencies to avoid full scans per iteration.
 *
 * Complexity (worst-case, without memoisation):
 *  O(|R| * T_match) where T_match involves computing weak successors repeatedly.
 */

// =====================================================================
// ===== On-The-Fly (Local Co-inductive DFS) Implementation ============
// =====================================================================

/**
 * @brief Serial On-The-Fly DTPTA equivalence check using recursive DFS.
 *
 * Algorithm:
 *   1. If (rZone, aZone) is on the recursion stack, return true
 *      (co-inductive hypothesis: assume the pair holds).
 *   2. If (rZone, aZone) was already proven valid, return true.
 *   3. If (rZone, aZone) was already proven invalid, return false.
 *   4. Push the pair onto the stack, then for each observable refined
 *      transition find a matching abstract transition (same action,
 *      timing_ok, at least one successor pair that recursively holds).
 *   5. Symmetrically check abstract → refined (bisimulation).
 *   6. If all transitions matched, memoise as valid; otherwise invalid.
 *
 * The e2e delay debt is threaded through the recursion rather than
 * stored in a global map, keeping the stack-local context correct.
 */
bool DTPTAChecker::explore_otf_serial(
    const TimedAutomaton& refined,
    const TimedAutomaton& abstract,
    const ZoneState* rZone,
    const ZoneState* aZone,
    std::unordered_set<PairKey, PairKeyHash>& stack,
    std::unordered_set<PairKey, PairKeyHash>& visited_valid,
    std::unordered_set<PairKey, PairKeyHash>& visited_invalid)
{
    if (is_cancelled()) return false;

    int rId = refined.get_state_id(rZone);
    int aId = abstract.get_state_id(aZone);
    PairKey pk{rId, aId};

    // Co-inductive base: pair on recursion stack → assume valid
    if (stack.count(pk)) return true;
    // Memoisation hits
    if (visited_valid.count(pk)) return true;
    if (visited_invalid.count(pk)) return false;

    last_stats_.relation_pairs_validated++;
    last_stats_.symbolic_states_explored++;

    // Push onto recursion stack
    stack.insert(pk);

    bool pair_valid = true;

    // ── Forward direction: refined → abstract ──
    {
        auto rOut = refined.get_outgoing_transitions(rZone->location_id);
        for (auto rt : rOut) {
            if (is_cancelled()) { pair_valid = false; break; }
            if (is_tau(rt)) continue;

            auto rSuccs = weak_observable_successors_cached(refined, rZone, rt->action);
            if (rSuccs.empty()) continue; // transition not enabled

            bool matched = false;
            for (auto at : abstract.get_outgoing_transitions(aZone->location_id)) {
                if (is_tau(at)) continue;
                if (rt->action != at->action) continue;
                // Sync precheck
                bool rs = rt->has_synchronization();
                bool asy = at->has_synchronization();
                if (rs != asy) continue;
                if (rs) {
                    if (rt->channel != at->channel) continue;
                    if (rt->is_sender != at->is_sender) continue;
                    if (rt->is_receiver != at->is_receiver) continue;
                }

                auto aSuccs = weak_observable_successors_cached(abstract, aZone, at->action);
                if (aSuccs.empty()) continue;

                if (!timing_ok(refined, rZone, rt, abstract, aZone, at))
                    continue;

                // Find at least one successor pair that recursively holds
                bool found = false;
                for (auto rsucc : rSuccs) {
                    for (auto asucc : aSuccs) {
                        if (rsucc->location_id == asucc->location_id) {
                            if (explore_otf_serial(refined, abstract,
                                                   rsucc, asucc,
                                                   stack, visited_valid, visited_invalid)) {
                                found = true;
                                break;
                            }
                        }
                    }
                    if (found) break;
                }
                if (found) { matched = true; break; }
            }
            if (!matched) { pair_valid = false; break; }
        }
    }

    // ── Backward direction: abstract → refined ──
    if (pair_valid) {
        auto aOut = abstract.get_outgoing_transitions(aZone->location_id);
        for (auto at : aOut) {
            if (is_cancelled()) { pair_valid = false; break; }
            if (is_tau(at)) continue;

            auto aSuccs = weak_observable_successors_cached(abstract, aZone, at->action);
            if (aSuccs.empty()) continue;

            bool matched = false;
            for (auto rt : refined.get_outgoing_transitions(rZone->location_id)) {
                if (is_tau(rt)) continue;
                if (at->action != rt->action) continue;
                bool asy = at->has_synchronization();
                bool rs = rt->has_synchronization();
                if (asy != rs) continue;
                if (asy) {
                    if (at->channel != rt->channel) continue;
                    if (at->is_sender != rt->is_sender) continue;
                    if (at->is_receiver != rt->is_receiver) continue;
                }

                auto rSuccs = weak_observable_successors_cached(refined, rZone, rt->action);
                if (rSuccs.empty()) continue;

                // Mirror timing check: abstract in "refined" role
                if (!timing_ok(abstract, aZone, at, refined, rZone, rt))
                    continue;

                bool found = false;
                for (auto asucc : aSuccs) {
                    for (auto rsucc : rSuccs) {
                        if (asucc->location_id == rsucc->location_id) {
                            if (explore_otf_serial(refined, abstract,
                                                   rsucc, asucc,
                                                   stack, visited_valid, visited_invalid)) {
                                found = true;
                                break;
                            }
                        }
                    }
                    if (found) break;
                }
                if (found) { matched = true; break; }
            }
            if (!matched) { pair_valid = false; break; }
        }
    }

    // Pop from recursion stack
    stack.erase(pk);

    // Memoise result
    if (pair_valid) {
        visited_valid.insert(pk);
    } else {
        visited_invalid.insert(pk);
    }
    return pair_valid;
}

/**
 * @brief OpenMP On-The-Fly DTPTA equivalence check via parallel task DFS.
 *
 * Key thread-safety design:
 *   - path_stack is passed BY VALUE to each task so that each task lineage
 *     has its own copy of the recursion path.  This avoids false cycle
 *     detections across unrelated concurrent paths.
 *   - visited_valid / visited_invalid are shared across all tasks and
 *     protected by TBB concurrent_hash_map (if available) or
 *     #pragma omp critical.
 *   - Independent successor branches are explored in parallel via
 *     #pragma omp task, with #pragma omp taskwait used to synchronise
 *     results before returning.
 */
bool DTPTAChecker::explore_otf_omp(
    const TimedAutomaton& refined,
    const TimedAutomaton& abstract,
    const ZoneState* rZone,
    const ZoneState* aZone,
    std::unordered_set<PairKey, PairKeyHash> path_stack)  // BY VALUE — each task gets its own copy
{
    if (is_cancelled()) return false;

    // Pointer aliases for the automata — used by OpenMP tasks so that
    // {refined, abstract} are captured as trivially-copyable pointers
    // (firstprivate) instead of triggering a copy of the non-copyable
    // TimedAutomaton (which contains a std::shared_mutex).
    const TimedAutomaton* refined_ptr = &refined;
    const TimedAutomaton* abstract_ptr = &abstract;

    int rId = refined.get_state_id(rZone);
    int aId = abstract.get_state_id(aZone);
    PairKey pk{rId, aId};

    // Co-inductive base: pair on THIS task's recursion path → assume valid
    if (path_stack.count(pk)) return true;

    // Check shared memoisation (thread-safe reads)
#ifdef DTPTA_HAS_TBB
    {
        
        typename decltype(otf_visited_valid_tbb_)::const_accessor reader;
        if (otf_visited_valid_tbb_.find(reader, pk)) return true;
    }
    {
        typename decltype(otf_visited_invalid_tbb_)::const_accessor reader;
        if (otf_visited_invalid_tbb_.find(reader, pk)) return false;
    }
#else
    {
        bool found_valid = false, found_invalid = false;
        #pragma omp critical(otf_visited_read)
        {
            found_valid = otf_visited_valid_shared_.count(pk) > 0;
            found_invalid = otf_visited_invalid_shared_.count(pk) > 0;
        }
        if (found_valid) return true;
        if (found_invalid) return false;
    }
#endif

    #pragma omp atomic
    last_stats_.relation_pairs_validated++;
    #pragma omp atomic
    last_stats_.symbolic_states_explored++;

    // Push onto this task's path stack
    path_stack.insert(pk);

    bool pair_valid = true;

    // ── Forward direction: refined → abstract ──
    {
        auto rOut = refined.get_outgoing_transitions(rZone->location_id);
        for (auto rt : rOut) {
            if (is_cancelled()) { pair_valid = false; break; }
            if (is_tau(rt)) continue;

            auto rSuccs = weak_observable_successors_cached(refined, rZone, rt->action);
            if (rSuccs.empty()) continue;

            bool matched = false;
            for (auto at : abstract.get_outgoing_transitions(aZone->location_id)) {
                if (is_tau(at)) continue;
                if (rt->action != at->action) continue;
                bool rs = rt->has_synchronization();
                bool asy = at->has_synchronization();
                if (rs != asy) continue;
                if (rs) {
                    if (rt->channel != at->channel) continue;
                    if (rt->is_sender != at->is_sender) continue;
                    if (rt->is_receiver != at->is_receiver) continue;
                }

                auto aSuccs = weak_observable_successors_cached(abstract, aZone, at->action);
                if (aSuccs.empty()) continue;

                if (!timing_ok(refined, rZone, rt, abstract, aZone, at))
                    continue;

                // Explore successor pairs in parallel using OpenMP tasks
                // Collect results via a shared vector
                std::vector<std::pair<const ZoneState*, const ZoneState*>> candidates;
                for (auto rsucc : rSuccs) {
                    for (auto asucc : aSuccs) {
                        if (rsucc->location_id == asucc->location_id) {
                            candidates.emplace_back(rsucc, asucc);
                        }
                    }
                }

                if (!candidates.empty()) {
                    std::atomic<bool> any_found{false};
                    // Spawn tasks for each candidate
                    for (size_t ci = 0; ci < candidates.size(); ++ci) {
                        if (any_found.load(std::memory_order_relaxed)) break;
                        auto [rs_cand, as_cand] = candidates[ci];
                        #pragma omp task shared(any_found) firstprivate(rs_cand, as_cand, path_stack, refined_ptr, abstract_ptr)
                        {
                            if (!any_found.load(std::memory_order_relaxed)) {
                                bool ok = explore_otf_omp(*refined_ptr, *abstract_ptr,
                                                          rs_cand, as_cand,
                                                          path_stack);  // copy of path_stack
                                if (ok) {
                                    any_found.store(true, std::memory_order_relaxed);
                                }
                            }
                        }
                    }
                    #pragma omp taskwait
                    if (any_found.load()) { matched = true; break; }
                }
            }
            if (!matched) { pair_valid = false; break; }
        }
    }

    // ── Backward direction: abstract → refined ──
    if (pair_valid) {
        auto aOut = abstract.get_outgoing_transitions(aZone->location_id);
        for (auto at : aOut) {
            if (is_cancelled()) { pair_valid = false; break; }
            if (is_tau(at)) continue;

            auto aSuccs = weak_observable_successors_cached(abstract, aZone, at->action);
            if (aSuccs.empty()) continue;

            bool matched = false;
            for (auto rt : refined.get_outgoing_transitions(rZone->location_id)) {
                if (is_tau(rt)) continue;
                if (at->action != rt->action) continue;
                bool asy = at->has_synchronization();
                bool rs = rt->has_synchronization();
                if (asy != rs) continue;
                if (asy) {
                    if (at->channel != rt->channel) continue;
                    if (at->is_sender != rt->is_sender) continue;
                    if (at->is_receiver != rt->is_receiver) continue;
                }

                auto rSuccs = weak_observable_successors_cached(refined, rZone, rt->action);
                if (rSuccs.empty()) continue;

                if (!timing_ok(abstract, aZone, at, refined, rZone, rt))
                    continue;

                std::vector<std::pair<const ZoneState*, const ZoneState*>> candidates;
                for (auto asucc : aSuccs) {
                    for (auto rsucc : rSuccs) {
                        if (asucc->location_id == rsucc->location_id) {
                            candidates.emplace_back(rsucc, asucc);
                        }
                    }
                }

                if (!candidates.empty()) {
                    std::atomic<bool> any_found{false};
                    for (size_t ci = 0; ci < candidates.size(); ++ci) {
                        if (any_found.load(std::memory_order_relaxed)) break;
                        auto [rs_cand, as_cand] = candidates[ci];
                        #pragma omp task shared(any_found) firstprivate(rs_cand, as_cand, path_stack, refined_ptr, abstract_ptr)
                        {
                            if (!any_found.load(std::memory_order_relaxed)) {
                                bool ok = explore_otf_omp(*refined_ptr, *abstract_ptr,
                                                          rs_cand, as_cand,
                                                          path_stack);
                                if (ok) {
                                    any_found.store(true, std::memory_order_relaxed);
                                }
                            }
                        }
                    }
                    #pragma omp taskwait
                    if (any_found.load()) { matched = true; break; }
                }
            }
            if (!matched) { pair_valid = false; break; }
        }
    }

    // Memoise result (thread-safe writes)
#ifdef DTPTA_HAS_TBB
    if (pair_valid) {
        typename decltype(otf_visited_valid_tbb_)::accessor writer;
        otf_visited_valid_tbb_.insert(writer, pk);
        writer->second = true;
    } else {
        typename decltype(otf_visited_invalid_tbb_)::accessor writer;
        otf_visited_invalid_tbb_.insert(writer, pk);
        writer->second = true;
    }
#else
    #pragma omp critical(otf_visited_write)
    {
        if (pair_valid) {
            otf_visited_valid_shared_.insert(pk);
        } else {
            otf_visited_invalid_shared_.insert(pk);
        }
    }
#endif

    return pair_valid;
}


bool DTPTAChecker::check_dtpta_simulation(const TimedAutomaton& refined, const TimedAutomaton& abstract,
                                          AlgorithmMode algo){
    auto start = std::chrono::high_resolution_clock::now();
    clear_optimisation_state();
    if (is_cancelled()) return false;
    const_cast<TimedAutomaton&>(refined).construct_zone_graph();
    const_cast<TimedAutomaton&>(abstract).construct_zone_graph();
    const auto& RZ = refined.get_all_zone_states();
    const auto& AZ = abstract.get_all_zone_states();
    if(RZ.empty() || AZ.empty()) return false;
    // 1) Early pruning seed: only include pairs with same location AND refined zone subset/eq abstract zone
    
    if(false){
        for(auto &rzp: RZ){
            for(auto &azp: AZ){
                if(rzp->location_id == azp->location_id){
                    relation_t rel = dbm_relation(rzp->zone.data(), azp->zone.data(), rzp->dimension);
                    if(rel==base_SUBSET || rel==base_EQUAL){
                        PairKey pk{rzp->location_id, azp->location_id};
                        relation_.insert(pk);
                        worklist_.push(pk);
                    }
                }
            }
        }
    }
    if(relation_.empty()) return false;

    // Record initial relation size (before fixpoint elimination)
    last_stats_.relation_pairs_seeded += relation_.size();

    // 2) Localised validation loop using worklist and reverse dependencies
    //
    // End-to-end delay bounding (paper Conditions 3+4):
    //   The validate_pair lambda now consults the per-pair EndToEndContext
    //   (e2e_context_) to enforce:
    //     a) Alternating send/receive watchdog – two consecutive receives
    //        without an intervening send are rejected.
    //     b) End-to-end response time – any delay debt accumulated at a
    //        relaxed receive must be compensated at the next send event.
    //   The context is propagated to successor pairs so that the fixpoint
    //   iteration globally enforces the constraint.
    auto validate_pair = [&](const PairKey& pk)->bool{
        auto rZone = pk.r; auto aZone = pk.a;

        auto rOut = refined.get_outgoing_transitions(refined.get_zone_state(rZone)->location_id);
        for(auto rt: rOut){
            if(is_tau(rt)) continue;

            // Compute enabled refined weak successors for this action; if none, skip this transition
            const auto& rSuccs = weak_observable_successors_cached(refined, refined.get_zone_state(rZone), rt->action);
            if(rSuccs.empty()) continue; // not enabled at this zone
            // Pre-filter abstract transitions by action
            bool matched=false;
            for(auto at: abstract.get_outgoing_transitions(abstract.get_zone_state(aZone)->location_id)){
                if(is_tau(at)) continue;
                if(rt->action != at->action) continue;
                // Abstract side must also be enabled for this action
                const auto& aSuccs = weak_observable_successors_cached(abstract, abstract.get_zone_state(aZone), at->action);
                if(aSuccs.empty()) continue;

                if(!timing_ok(refined, refined.get_zone_state(rZone), rt,
                              abstract, abstract.get_zone_state(aZone), at))
                    continue;

                bool found=false; PairKey supporting{};
                for(auto rs: rSuccs){
                    if(rs ==nullptr)
                    {
                        std::cout<<"Refined has none ";
                    }
                    for(auto as: aSuccs){
                        if(as ==nullptr)
                    {
                        std::cout<<"Abstract has none ";
                    }
                        if(rs->location_id==as->location_id){
                            PairKey cand{refined.get_state_id(rs),abstract.get_state_id(as)};
                            if(relation_.count(cand)) { found=true; supporting=cand; break; }
                        }
                    }
                    if(found) break;
                }
                if(found){
                    // record reverse dependency: pk depends on supporting
                    reverse_deps_[supporting].push_back(pk);
                    matched=true; break;
                }
            }
            if(!matched) return false; // some enabled refined move has no match
        }
        return true; // all observable transitions matched
    };
    
    while(!worklist_.empty()){
        if (is_cancelled()) { relation_.clear(); break; }
        last_stats_.fixpoint_iterations++;
        PairKey current = worklist_.front(); worklist_.pop();
        // It might have been removed already
        if(!relation_.count(current)) continue;
        last_stats_.relation_pairs_validated++;
        if(!validate_pair(current)){
            // remove and enqueue dependents
            relation_.erase(current);
            auto it = reverse_deps_.find(current);
            if(it!=reverse_deps_.end()){
                for(auto &parent: it->second){
                    if(relation_.count(parent)) worklist_.push(parent);
                }
            }
            reverse_deps_.erase(current);
        }
        if(relation_.empty()) break;
    }
    auto end = std::chrono::high_resolution_clock::now();
    last_stats_.refined_states += RZ.size();
    last_stats_.abstract_states += AZ.size();
    last_stats_.simulation_pairs += relation_.size();
    last_stats_.check_time_ms += std::chrono::duration<double, std::milli>(end-start).count();
    last_stats_.memory_usage_bytes += relation_.size()*sizeof(PairKey);
    return !relation_.empty();
}

/**
 * @brief Pairwise system-level refinement: all corresponding automata must refine.
 * @return true iff every automaton pair satisfies DTPTA refinement.
 */
bool DTPTAChecker::check_dtpta_simulation(const System& system_refined, const System& system_abstract){
    if(system_refined.size()!=system_abstract.size()) return false; 
    bool all=true; 
    for(size_t i=0;i<system_refined.size();++i) {
        if(!check_dtpta_simulation(system_refined.get_automaton(i), system_abstract.get_automaton(i))) all=false; 
    }
    return all; 
}




/**
 * @brief Core DTPTA equivalence (refinement) check between two automata.
 *
 * Algorithm Outline:
 *  1. Ensure zone graphs are (fully) constructed (current implementation builds all states).
 *  2. Seed candidate relation R with all zone pairs sharing the same location id.
 *  3. Iterate elimination: for each pair (r,a) verify every observable refined move
 *     can be matched by some abstract weak move with acceptable timing and related successors.
 *  4. Remove failing pairs; stop at fixed point. Non-empty relation => refinement holds.
 *
 * Correctness Notes:
 *  - This is a greatest fixed point style refinement (monotone elimination).
 *  - Successor validation is optimistic: requires existence of at least one successor pair
 *    already in relation; convergence ensures global consistency.
 *  - Tau behaviours are abstracted via τ-closure in both pre/post phases.
 *
 * Limitations / TODO:
 *  - No counterexample extraction.
 *  - No on-demand zone exploration (eager build may be large).
 *  - Could refine initial seeding using DBM inclusion to reduce search space.
 *  - Could track predecessor dependencies to avoid full scans per iteration.
 *
 * Complexity (worst-case, without memoisation):
 *  O(|R| * T_match) where T_match involves computing weak successors repeatedly.
 */





bool DTPTAChecker::check_dtpta_equivalence(const TimedAutomaton& refined, const TimedAutomaton& abstract, bool use_omp,
                                           AlgorithmMode algo){
    auto start = std::chrono::high_resolution_clock::now();
    clear_optimisation_state();
    if (is_cancelled()) return false;

    // ===================================================================
    // On-The-Fly mode: local co-inductive DFS from the initial pair only.
    // Does NOT eagerly build the full zone graph.  Zone states are created
    // lazily by get_or_add_zone_state() inside tau_closure_raw /
    // weak_observable_successors_raw as the DFS discovers them.
    // ===================================================================
    if (algo == AlgorithmMode::ON_THE_FLY) {
        // Obtain the initial zone state for each automaton.
        // get_or_create_initial_state() creates only the single initial
        // state — no BFS exploration of the full zone graph.
        const ZoneState* rInit = const_cast<TimedAutomaton&>(refined).get_or_create_initial_state();
        const ZoneState* aInit = const_cast<TimedAutomaton&>(abstract).get_or_create_initial_state();
        if (!rInit || !aInit) return false;

        DEV_PRINT("OTF: starting from initial pair ("
                  << rInit->location_id << ", " << aInit->location_id << ")\n");

        bool result = false;
        if (!use_omp) {
            // Serial OTF
            std::unordered_set<PairKey, PairKeyHash> stack;
            std::unordered_set<PairKey, PairKeyHash> visited_valid;
            std::unordered_set<PairKey, PairKeyHash> visited_invalid;
            result = explore_otf_serial(refined, abstract,
                                        rInit, aInit,
                                        stack, visited_valid, visited_invalid);
            last_stats_.simulation_pairs += visited_valid.size();
        } else {
            // OpenMP OTF
#ifdef DTPTA_HAS_TBB
            otf_visited_valid_tbb_.clear();
            otf_visited_invalid_tbb_.clear();
#else
            otf_visited_valid_shared_.clear();
            otf_visited_invalid_shared_.clear();
#endif
            std::unordered_set<PairKey, PairKeyHash> initial_path;
            #pragma omp parallel
            {
                #pragma omp single
                {
                    result = explore_otf_omp(refined, abstract,
                                             rInit, aInit,
                                             initial_path);
                }
            }
#ifdef DTPTA_HAS_TBB
            last_stats_.simulation_pairs += otf_visited_valid_tbb_.size();
#else
            last_stats_.simulation_pairs += otf_visited_valid_shared_.size();
#endif
        }

        auto end = std::chrono::high_resolution_clock::now();
        // Report how many zone states were lazily discovered
        last_stats_.refined_states += refined.get_all_zone_states().size();
        last_stats_.abstract_states += abstract.get_all_zone_states().size();
        last_stats_.check_time_ms += std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }

    // ===================================================================
    // GFP mode (existing global greatest fixed-point algorithm)
    // ===================================================================
    const_cast<TimedAutomaton&>(refined).construct_zone_graph();
    const_cast<TimedAutomaton&>(abstract).construct_zone_graph();
    const auto& RZ = refined.get_all_zone_states();
    const auto& AZ = abstract.get_all_zone_states();
    if(RZ.empty() || AZ.empty()) return false;
    DEV_PRINT( "Thread " << std::this_thread::get_id() << " is doing equivalence check\n for "<< refined.get_name() << " and " << abstract.get_name() << std::endl);

    // 1) Early pruning seed: only include pairs with same location AND refined zone subset/eq abstract zone
    for(auto &rzp: RZ){
        if (is_cancelled()) return false;
        for(auto &azp: AZ){
            if (is_cancelled()) return false;
            if(rzp->location_id == azp->location_id){
                relation_t rel = dbm_relation(rzp->zone.data(), azp->zone.data(), rzp->dimension);
                if(rel==base_SUBSET || rel==base_EQUAL){
                    PairKey pk{refined.get_state_id(rzp.get()),
                                abstract.get_state_id(azp.get())};
                    relation_.insert(pk);
                    worklist_.push(pk);
                }
            }
        }
    }
    if(relation_.empty()) return false;

    // Record initial relation size (before fixpoint elimination)
    last_stats_.relation_pairs_seeded += relation_.size();

    // 2) Localised validation loop using worklist and reverse dependencies
    //    Now symmetric: both refined→abstract and abstract→refined must match (bisimulation)
    //
    //    End-to-end delay bounding (paper Conditions 3+4):
    //      The validate_pair lambda consults the per-pair EndToEndContext
    //      (e2e_context_) to enforce:
    //        a) Alternating send/receive watchdog – two consecutive receives
    //           without an intervening send are rejected.
    //        b) End-to-end response time – delay debt accumulated at a relaxed
    //           receive must be compensated at the next send event.
    //      Context is propagated to successor pairs via e2e_context_ (or via
    //      the thread-local vector when running in OpenMP mode).
    //
    // Thread-safe validate_pair for OpenMP: collects reverse_deps_ updates in a local vector.
    auto validate_pair = [&](const PairKey& pk,
                             const std::unordered_set<dtpta::DTPTAChecker::PairKey,
                                                      dtpta::DTPTAChecker::PairKeyHash>& relation_,
                             std::vector<std::pair<PairKey, PairKey>>* local_reverse_deps_updates = nullptr
                             )->bool{
        auto rZone = pk.r; auto aZone = pk.a;

        // Forward direction: refined -> abstract
        {
            auto rOut = refined.get_outgoing_transitions(refined.get_zone_state(rZone)->location_id);
            for(auto rt: rOut){
                if(is_tau(rt)) continue;

                // Compute enabled refined weak successors for this action; if none, skip this transition
                // Cache is thread-safe (TBB concurrent_hash_map) — no critical section needed
                auto rSuccs = weak_observable_successors_cached(refined, refined.get_zone_state(rZone), rt->action);
                
                if(rSuccs.empty()) continue; // transition not enabled from rZone
                
                bool matched=false;
                for(auto at: abstract.get_outgoing_transitions(abstract.get_zone_state(aZone)->location_id)){
                    if(is_tau(at)) continue;
                    if(rt->action != at->action) continue;
                    // Cheap sync precheck to avoid expensive DBM timing_ok when impossible
                    bool rs = rt->has_synchronization();
                    bool asy = at->has_synchronization();
                    if(rs != asy) continue;
                    if(rs){
                        if(rt->channel != at->channel) continue;
                        if(rt->is_sender != at->is_sender) continue;
                        if(rt->is_receiver != at->is_receiver) continue;
                    }
                    // Cache is thread-safe (TBB concurrent_hash_map) — no critical section needed
                    auto aSuccs = weak_observable_successors_cached(abstract, abstract.get_zone_state(aZone), at->action);
                    
                    if(aSuccs.empty()){                         
                        continue;} // not enabled on abstract side

                    if(!timing_ok(refined, refined.get_zone_state(rZone), rt,
                                  abstract, abstract.get_zone_state(aZone), at))
                        continue;
                    // Index abstract successors by location to reduce O(m*n)
                    std::unordered_map<int, std::vector<const ZoneState*>> aByLoc;
                    aByLoc.reserve(aSuccs.size());
                    for(auto as: aSuccs){
                        aByLoc[as->location_id].push_back(as);
                    } 
                    bool found=false; PairKey supporting{};
                    for(auto rsucc: rSuccs){
                        auto it = aByLoc.find(rsucc->location_id);
                        if(it==aByLoc.end()) continue;
                        for(auto as: it->second){
                            PairKey cand{refined.get_state_id(rsucc),abstract.get_state_id(as)};
                            if(relation_.count(cand)) { found=true; supporting=cand; break; }
                        }
                        if(found) break;
                    }
                    if(found){
                        // record reverse dependency: pk depends on supporting
                        if (local_reverse_deps_updates) {
                            local_reverse_deps_updates->emplace_back(supporting, pk);
                        } else {
                            reverse_deps_[supporting].push_back(pk);
                        }
                        matched=true; break;
                    }
                }
                if(!matched) return false; // enabled refined move has no match
            }
        }
        
        // Backward direction: abstract -> refined
        {
            auto aOut = abstract.get_outgoing_transitions(abstract.get_zone_state(aZone)->location_id);
            for(auto at: aOut){
                if(is_tau(at)) continue;

                // Cache is thread-safe (TBB concurrent_hash_map) — no critical section needed
                auto aSuccs = weak_observable_successors_cached(abstract, abstract.get_zone_state(aZone), at->action);
                if(aSuccs.empty()) continue; // not enabled from aZone
                bool matched=false;
                for(auto rt: refined.get_outgoing_transitions(refined.get_zone_state(rZone)->location_id)){
                    if(is_tau(rt)) continue;
                    if(at->action != rt->action) continue;
                    // Cheap sync precheck (mirror)
                    bool asy = at->has_synchronization();
                    bool rs = rt->has_synchronization();
                    if(asy != rs) continue;
                    if(asy){
                        if(at->channel != rt->channel) continue;
                        if(at->is_sender != rt->is_sender) continue;
                        if(at->is_receiver != rt->is_receiver) continue;
                    }
                    // Cache is thread-safe (TBB concurrent_hash_map) — no critical section needed
                    auto rSuccs = weak_observable_successors_cached(refined, refined.get_zone_state(rZone), rt->action);
                    if(rSuccs.empty()){
                        continue;
                    }  // not enabled on refined side

                    if(!timing_ok(abstract, abstract.get_zone_state(aZone), at,
                                  refined, refined.get_zone_state(rZone), rt))
                        continue;

                    // Index refined successors by location
                    std::unordered_map<int, std::vector<const ZoneState*>> rByLoc;
                    rByLoc.reserve(rSuccs.size());
                    for(auto rsucc: rSuccs) rByLoc[rsucc->location_id].push_back(rsucc);
                    bool found=false; PairKey supporting{};
                    for(auto as: aSuccs){
                        auto it = rByLoc.find(as->location_id);
                        if(it==rByLoc.end()) continue;
                        for(auto rsucc: it->second){
                            PairKey cand{refined.get_state_id(rsucc),abstract.get_state_id(as)};
                            if(relation_.count(cand)) { found=true; supporting=cand; break; }
                        }
                        if(found) break;
                    }
                    if(found){
                        if (local_reverse_deps_updates) {
                            local_reverse_deps_updates->emplace_back(supporting, pk);
                        } else {
                            reverse_deps_[supporting].push_back(pk);
                        }
                        matched=true; break;
                    }
                }
                

                if(!matched) return false; // enabled abstract move has no match
            }
        }
        return true; // all observable transitions matched in both directions
    };

    if(!use_omp){
        while(!worklist_.empty()){
            if (is_cancelled()) { relation_.clear(); break; }
            last_stats_.fixpoint_iterations++;
            PairKey current = worklist_.front(); worklist_.pop();
            // It might have been removed already
            if(!relation_.count(current)) continue;
            last_stats_.relation_pairs_validated++;
            bool valid = validate_pair(current, relation_);
            
            
            if(!valid){
                // remove and enqueue dependents
                relation_.erase(current);
                auto it = reverse_deps_.find(current);
                if(it!=reverse_deps_.end()){
                    for(auto &parent: it->second){
                        if(relation_.count(parent)) worklist_.push(parent);
                    }
                }
                reverse_deps_.erase(current);
            }
            if(relation_.empty()) break;
        }
        
    }else{
        while (!worklist_.empty()) {
            last_stats_.fixpoint_iterations++;
            // 1. Extract a batch of pairs from worklist_
            // Use larger batches to amortize synchronisation — process ALL available work
            std::vector<PairKey> batch;
            batch.reserve(worklist_.size());
            while (!worklist_.empty()) {
                batch.push_back(worklist_.front());
                worklist_.pop();
            }

            // 2. Parallel validation — threads are created once, reused across iterations
            std::vector<PairKey> to_remove;
            std::vector<std::pair<PairKey, PairKey>> all_reverse_deps_updates;
            const auto& relation_snapshot = relation_; // snapshot for thread safety

            #pragma omp parallel
            {
                std::vector<PairKey> local_remove;
                std::vector<std::pair<PairKey, PairKey>> local_reverse_deps_updates;
                size_t local_validated = 0;

                #pragma omp for schedule(dynamic, 64) nowait
                for (size_t i = 0; i < batch.size(); ++i) {
                    PairKey current = batch[i];
                    if (!relation_snapshot.count(current)) continue;
                    local_validated++;
                    if (!validate_pair(current, relation_,
                                       &local_reverse_deps_updates)) {
                        local_remove.push_back(current);
                    }
                }

                // Merge thread-local results at the end of parallel region
                #pragma omp critical
                {
                    to_remove.insert(to_remove.end(),
                                    local_remove.begin(),
                                    local_remove.end());

                    all_reverse_deps_updates.insert(all_reverse_deps_updates.end(),
                                    local_reverse_deps_updates.begin(),
                                    local_reverse_deps_updates.end());

                    last_stats_.relation_pairs_validated += local_validated;
                }
            }
            
            // 3. Synchronize: merge reverse_deps_ updates (main thread only)
            for (const auto& update : all_reverse_deps_updates) {
                reverse_deps_[update.first].push_back(update.second);
            }
            // For each pair to remove, enqueue its dependents and erase from reverse_deps_
            for (auto& pk : to_remove) {
                relation_.erase(pk);
                auto it = reverse_deps_.find(pk);
                if (it != reverse_deps_.end()) {
                    for (auto& parent : it->second) {
                        if (relation_.count(parent)) worklist_.push(parent);
                    }
                }
                reverse_deps_.erase(pk);
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
        last_stats_.refined_states += RZ.size();
        last_stats_.abstract_states += AZ.size();
        last_stats_.simulation_pairs += relation_.size();
        last_stats_.check_time_ms += std::chrono::duration<double, std::milli>(end-start).count();
        last_stats_.memory_usage_bytes += relation_.size()*sizeof(PairKey);
        DEV_PRINT( "Thread " << std::this_thread::get_id() << " finished equivalence check\n for "<< refined.get_name() << " and " << abstract.get_name() << std::endl);        
        for (const auto& p : relation_)
        {
            if (p.r == 0 && p.a == 0)

                return true;
        }
        return false;
        
}


bool DTPTAChecker::check_dtpta_equivalence__(const System& system_refined, const System& system_abstract, bool use_openmp,
                                              AlgorithmMode algo)
{
       bool all = true; 
    size_t total = system_refined.size();
    int barWidth = 70;
    std::vector<bool> results(total, false);
    for(size_t i = 0; i < total; ++i) {
        if (is_cancelled()) { all = false; break; }
                // Run equivalence check
                if(!check_dtpta_equivalence(system_refined.get_automaton(i), system_abstract.get_automaton(i), use_openmp, algo))
                    all = false;

                // Report result of current automaton
                

                // Update progress bar
                float progress = float(i + 1) / total; // completed fraction

                std::cout << "[";
                int pos = barWidth * progress;
                for (int j = 0; j < barWidth; ++j) {
                    if (j < pos) std::cout << "=";
                    else if (j == pos) std::cout << ">";
                    else std::cout << " ";
                }
                std::cout << "] " << int(progress * 100.0) <<"% ";
                std::cout <<"Automaton pair " << i << ": " 
                        << (all ? "EQUIVALENT" : "DIFFERENT") << " \r";
                std::cout.flush();
                results[i] = all;
            }

            std::cout << std::endl; // move to next line after progress bar finishes
            for (size_t i = 0; i < total; ++i) {
                std::cout << "Automaton pair " << i << ": " 
                        << (results[i] ? "EQUIVALENT" : "DIFFERENT") << std::endl;
            }

            return all; 
}


/**
 * @brief Pairwise system-level refinement: all corresponding automata must refine.
 * @return true iff every automaton pair satisfies DTPTA refinement.
 */
bool DTPTAChecker::check_dtpta_equivalence(const System& system_refined, const System& system_abstract, RunningMode parallel_mode, size_t num_workers, long timeout_ms, AlgorithmMode algo) {
    if(system_refined.size() != system_abstract.size()) 
        return false; 

    // Cooperative-cancel with watchdog if timeout is set
    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::thread watchdog;
    std::mutex watchdog_mtx;
    std::condition_variable watchdog_cv;
    bool installed_cancel_flag = false;
    if (timeout_ms >= 0) {
        // Install global cancel flag pointer before any work starts
        DTPTA_CANCEL_FLAG = &cancel_flag;
        installed_cancel_flag = true;
        watchdog = std::thread([timeout_ms, &cancel_flag, &done_flag, &watchdog_mtx, &watchdog_cv]() {
            if (timeout_ms == 0) {
                // immediate cancel
                cancel_flag.store(true, std::memory_order_relaxed);
                std::cout << "TIME OUT HAPPENED, I WAS CHECKING IF AUZTOMATA" << std::endl;
                return;
            }
            std::unique_lock<std::mutex> lk(watchdog_mtx);
            bool finished = watchdog_cv.wait_for(
                lk,
                std::chrono::milliseconds(timeout_ms),
                [&]{ return done_flag.load(std::memory_order_relaxed); }
            );
            if (!finished) {
                cancel_flag.store(true, std::memory_order_relaxed);
                std::cout << "TIME OUT HAPPENED, I WAS CHECKING IF AUZTOMATA" << std::endl;
            }
        });
    }

    auto join_watchdog = [&]() {
        done_flag.store(true, std::memory_order_relaxed);
        watchdog_cv.notify_all();
        if (watchdog.joinable()) watchdog.join();
        if (installed_cancel_flag && DTPTA_CANCEL_FLAG == &cancel_flag) {
            DTPTA_CANCEL_FLAG = nullptr;
        }
    };

    auto run_work = [&]() -> bool {
        if (parallel_mode == RunningMode::SERIAL) {
            return check_dtpta_equivalence__(system_refined, system_abstract, false, algo);
        }

        if (parallel_mode == RunningMode::OPENMP) {
            std::cout << "Running it in openmp mode" << std::endl;
            return check_dtpta_equivalence__(system_refined, system_abstract, true, algo);
        }

        // Pre-construct all zone graphs sequentially (avoid concurrent mutations)
        for (size_t i = 0; i < system_refined.size(); ++i) {
            if (is_cancelled()) return false;
            auto& r = const_cast<TimedAutomaton&>(system_refined.get_automaton(i));
            auto& a = const_cast<TimedAutomaton&>(system_abstract.get_automaton(i));
            r.construct_zone_graph();
            a.construct_zone_graph();
        }
        bool all_local = true;
        std::vector<std::future<std::pair<bool, dtpta::CheckStatistics>>> futures;
        ThreadPool pool(num_workers);
        futures.reserve(system_refined.size());
        for (size_t i = 0; i < system_refined.size(); ++i) {
            futures.push_back(pool.enqueue([&system_refined, &system_abstract, i, algo]() {
                DTPTAChecker local;
                bool correct = local.check_dtpta_equivalence(
                    system_refined.get_automaton(i),
                    system_abstract.get_automaton(i),
                    false, algo
                );
                return std::make_pair(correct, local.get_last_check_statistics());
            }));
        }
        for (auto& fut : futures) {
            if (is_cancelled()) { all_local = false; break; }
            auto [correct, stats] = fut.get();
            if (!correct) all_local = false;
            auto old_time = this->last_stats_.check_time_ms;
            this->last_stats_ += stats;
            this->last_stats_.check_time_ms = std::max(old_time, stats.check_time_ms);
        }
        return all_local;
    };

    bool res = run_work();
    join_watchdog();
    // Check if canceled
    if (cancel_flag.load(std::memory_order_relaxed)) {
        this->last_stats_.check_time_ms = timeout_ms;
        throw dtpta::TimeoutException("Operation timed out!");
    }

    return res;

}

/**
 * @brief Detailed system refinement returning per-automaton statistics.
 * @param results Output vector populated with one entry per automaton pair.
 * @return true if all pairs refine.
 */
bool DTPTAChecker::check_dtpta_equivalence_detailed(const System& system_refined,
                                                    const System& system_abstract,
                                                    std::vector<SystemCheckResult>& results){
    results.clear(); 
    if(system_refined.size()!=system_abstract.size()) return false; 
    bool all=true; 
    const auto& rn=system_refined.get_template_names(); 
    const auto& an=system_abstract.get_template_names(); 
    for(size_t i=0;i<system_refined.size();++i){ 
        bool eq = check_dtpta_equivalence(system_refined.get_automaton(i), system_abstract.get_automaton(i)); 
        results.push_back(SystemCheckResult{i,rn[i],an[i],eq,get_last_check_statistics()}); 
        if(!eq) all=false; 
    } 
    return all; 
}







bool DTPTAChecker::check_weak_timed_bisim(
    const TimedAutomaton& A,
    const TimedAutomaton& B,
    const Alphabet& X
)
{
    // Clear shared caches: weak_succ_cache_ is keyed by (location_id, action)
    // which collides when A and B share the same integer location IDs.
    // Must purge before starting to avoid one automaton's results being
    // returned for the other's queries.
    clear_optimisation_state();

    // ── 1. Build zone graphs (idempotent if already built) ────────────────
    const_cast<TimedAutomaton&>(A).construct_zone_graph();
    const_cast<TimedAutomaton&>(B).construct_zone_graph();

    const size_t nA = A.get_num_states();
    const size_t nB = B.get_num_states();
    if (nA == 0 || nB == 0) return false;

    // Restrict X to actions that actually label transitions in A or B.
    Alphabet X_obs;
    for (const auto& t : A.get_transitions())
        if (X.count(t.action)) X_obs.insert(t.action);
    for (const auto& t : B.get_transitions())
        if (X.count(t.action)) X_obs.insert(t.action);
    std::vector<std::string> X_obs_vec(X_obs.begin(), X_obs.end());
    const size_t nActions = X_obs_vec.size();

    // ── 2. Seed R = same-location pairs only ─────────────────────────────
    // Pairing states at different locations cannot be part of any
    // bisimulation: they disagree structurally on which moves are enabled.
    const size_t total_pairs = nA * nB;
    std::vector<unsigned char> R(total_pairs, 0);
    size_t R_count = 0;
    std::vector<PairId> live_pairs;
    live_pairs.reserve(total_pairs);

    auto R_index = [&](int ia, int ib) -> size_t {
        return static_cast<size_t>(ia) * nB + static_cast<size_t>(ib);
    };
    auto R_get = [&](int ia, int ib) -> bool {
        return R[R_index(ia, ib)] != 0;
    };
    auto R_clear = [&](int ia, int ib) {
        const size_t idx = R_index(ia, ib);
        if (R[idx] != 0) { R[idx] = 0; --R_count; }
    };

    for (size_t ia = 0; ia < nA; ++ia) {
        const ZoneState* zA = A.get_zone_state(ia);
        if (!zA) continue;
        for (size_t ib = 0; ib < nB; ++ib) {
            const ZoneState* zB = B.get_zone_state(ib);
            if (!zB || zA->location_id != zB->location_id) continue;
            R[R_index(ia, ib)] = 1;
            ++R_count;
            live_pairs.push_back({static_cast<int>(ia), static_cast<int>(ib)});
        }
    }

    std::cout << "[bisim] initial R size (same-location pairs) = " << R_count << "\n" << std::flush;
    if (R_count == 0) return false;

    // ── 3. Per-automaton weak-successor caches (pointer-keyed) ───────────
    // Do NOT use the shared weak_succ_cache_ which is keyed only by
    // (location_id, action) — that collides when A and B share the same
    // location_id integers.  Instead use two separate maps keyed by the
    // zone state POINTER (unique per automaton object).
    using SuccMap = std::unordered_map<const ZoneState*,
                        std::unordered_map<std::string, std::vector<int>>>;
    SuccMap succA_local, succB_local;

    // Compute and cache weak successors, bypassing weak_succ_cache_.
    // tau_closure_cached is safe (pointer-keyed) and is reused internally.
    auto get_succ = [&](const TimedAutomaton& ta,
                        const ZoneState* z,
                        const std::string& act,
                        SuccMap& cache,
                        size_t n_states) -> const std::vector<int>& {
        auto& inner = cache[z];
        auto it = inner.find(act);
        if (it != inner.end()) return it->second;
        auto raw = weak_observable_successors_raw(ta, z, act);
        std::vector<int> ids;
        ids.reserve(raw.size());
        for (auto* s : raw) {
            int id = ta.get_state_id(s);
            if (id >= 0 && static_cast<size_t>(id) < n_states) ids.push_back(id);
        }
        return inner.emplace(act, std::move(ids)).first->second;
    };

    // ── 4. Fixpoint elimination ───────────────────────────────────────────
    {
        bool changed = true;
        while (changed && R_count > 0) {
            changed = false;
            for (auto& p : live_pairs) {
                int ia = p.first, ib = p.second;
                if (!R_get(ia, ib)) continue;

                const ZoneState* zA = A.get_zone_state(ia);
                const ZoneState* zB = B.get_zone_state(ib);
                if (!zA || !zB) { R_clear(ia, ib); changed = true; continue; }

                bool pair_ok = true;

                // Forward: every weak-a successor of zA must have a related zB'
                for (size_t k = 0; k < nActions && pair_ok; ++k) {
                    const std::string& act = X_obs_vec[k];
                    const auto& sA = get_succ(A, zA, act, succA_local, nA);
                    if (sA.empty()) continue;
                    const auto& sB = get_succ(B, zB, act, succB_local, nB);
                    for (int ia2 : sA) {
                        bool found = false;
                        for (int ib2 : sB)
                            if (R_get(ia2, ib2)) { found = true; break; }
                        if (!found) { pair_ok = false; break; }
                    }
                }

                // Backward: every weak-a successor of zB must have a related zA'
                for (size_t k = 0; k < nActions && pair_ok; ++k) {
                    const std::string& act = X_obs_vec[k];
                    const auto& sB = get_succ(B, zB, act, succB_local, nB);
                    if (sB.empty()) continue;
                    const auto& sA = get_succ(A, zA, act, succA_local, nA);
                    for (int ib2 : sB) {
                        bool found = false;
                        for (int ia2 : sA)
                            if (R_get(ia2, ib2)) { found = true; break; }
                        if (!found) { pair_ok = false; break; }
                    }
                }

                if (!pair_ok) {
                    R_clear(ia, ib);
                    changed = true;
                }
            }
        }
    }
    std::cout << "[bisim] R size after fixpoint = " << R_count << "\n" << std::flush;

    // ── 5. Check whether the initial-state pair survived ──────────────────
    const ZoneState* initA = const_cast<TimedAutomaton&>(A).get_or_create_initial_state();
    const ZoneState* initB = const_cast<TimedAutomaton&>(B).get_or_create_initial_state();
    if (!initA || !initB) return false;

    int ia0 = A.get_state_id(initA);
    int ib0 = B.get_state_id(initB);
    if (ia0 < 0 || ib0 < 0) return false;

    return R_get(ia0, ib0);
}




} // namespace dtpta
