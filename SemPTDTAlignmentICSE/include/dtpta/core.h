#ifndef DTPTA_CORE_H
#define DTPTA_CORE_H


#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <functional>
#include <fstream>
#include <iomanip>

#ifdef DTPTA_HAS_TBB
#include <tbb/concurrent_hash_map.h>
#endif

#include "threadpool.h"
#include "utils.h"
#include "timedautomaton.h"
#include "configs.h"
#include "system.h"

#include "utils.h"
namespace dtpta {

    using PairId = std::pair<int,int>;

struct PairHash {
    std::size_t operator()(const std::pair<const ZoneState*, const ZoneState*>& p) const {
        auto h1 = std::hash<const ZoneState*>{}(p.first);
        auto h2 = std::hash<const ZoneState*>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

    /**
     * Get statistics about the last refinement check
     */
    struct CheckStatistics {
    size_t refined_states;
    size_t abstract_states;
    size_t simulation_pairs;
    double check_time_ms;
    size_t memory_usage_bytes;

    // Algorithm-level metrics
    size_t symbolic_states_explored;   // states visited during tau-closure BFS
    size_t relation_pairs_seeded;      // initial seeded relation size (before fixpoint)
    size_t relation_pairs_validated;   // total validate_pair calls during fixpoint
    size_t total_weak_successors;      // cumulative weak-successor results across all queries
    size_t weak_successor_queries;     // number of weak-successor queries issued
    size_t tau_closure_total_size;     // cumulative tau-closure sizes across all queries
    size_t tau_closure_queries;        // number of tau-closure queries issued
    size_t fixpoint_iterations;        // worklist iterations in the fixpoint loop

    // Overload + operator to combine statistics
    CheckStatistics operator+(const CheckStatistics& other) const {
        return CheckStatistics{
            refined_states + other.refined_states,
            abstract_states + other.abstract_states,
            simulation_pairs + other.simulation_pairs,
            check_time_ms + other.check_time_ms,
            memory_usage_bytes + other.memory_usage_bytes,
            symbolic_states_explored + other.symbolic_states_explored,
            relation_pairs_seeded + other.relation_pairs_seeded,
            relation_pairs_validated + other.relation_pairs_validated,
            total_weak_successors + other.total_weak_successors,
            weak_successor_queries + other.weak_successor_queries,
            tau_closure_total_size + other.tau_closure_total_size,
            tau_closure_queries + other.tau_closure_queries,
            fixpoint_iterations + other.fixpoint_iterations
        };
    }

    // Overload += operator for convenience
    CheckStatistics& operator+=(const CheckStatistics& other) {
        refined_states += other.refined_states;
        abstract_states += other.abstract_states;
        simulation_pairs += other.simulation_pairs;
        check_time_ms += other.check_time_ms;
        memory_usage_bytes += other.memory_usage_bytes;
        symbolic_states_explored += other.symbolic_states_explored;
        relation_pairs_seeded += other.relation_pairs_seeded;
        relation_pairs_validated += other.relation_pairs_validated;
        total_weak_successors += other.total_weak_successors;
        weak_successor_queries += other.weak_successor_queries;
        tau_closure_total_size += other.tau_closure_total_size;
        tau_closure_queries += other.tau_closure_queries;
        fixpoint_iterations += other.fixpoint_iterations;
        return *this;
    }
    void print() const {
        std::cout << "DTPTA Check Statistics:" << std::endl;
        std::cout << "  Refined States: " << refined_states << std::endl;
        std::cout << "  Abstract States: " << abstract_states << std::endl;
        std::cout << "  Simulation Pairs (remaining): " << simulation_pairs << std::endl;
        std::cout << "  Check Time: " << check_time_ms << " ms" << std::endl;
        std::cout << "  Memory Usage: " << memory_usage_bytes / 1024 << " KB" << std::endl;
        std::cout << "  Symbolic States Explored: " << symbolic_states_explored << std::endl;
        std::cout << "  Relation Pairs Seeded: " << relation_pairs_seeded << std::endl;
        std::cout << "  Relation Pairs Validated: " << relation_pairs_validated << std::endl;
        std::cout << "  Weak Successor Queries: " << weak_successor_queries
                  << "  (total results: " << total_weak_successors << ")" << std::endl;
        std::cout << "  Tau-Closure Queries: " << tau_closure_queries
                  << "  (total size: " << tau_closure_total_size << ")" << std::endl;
        std::cout << "  Fixpoint Iterations: " << fixpoint_iterations << std::endl;
    }
    
    // Write CSV header
    static void write_csv_header(std::ofstream& file) {
        file << "model_name,refined_states,abstract_states,simulation_pairs,"
                "check_time_ms,memory_usage_bytes,memory_usage_kb,"
                "symbolic_states_explored,relation_pairs_seeded,relation_pairs_validated,"
                "total_weak_successors,weak_successor_queries,"
                "tau_closure_total_size,tau_closure_queries,fixpoint_iterations" << std::endl;
    }
    
    // Append statistics to CSV file
    void append_to_csv(std::ofstream& file, const std::string& model_name) const {
        file << model_name << ","
             << refined_states << ","
             << abstract_states << ","
             << simulation_pairs << ","
             << std::fixed << std::setprecision(3) << check_time_ms << std::defaultfloat << ","
             << memory_usage_bytes << ","
             << (memory_usage_bytes / 1024.0) << ","
             << symbolic_states_explored << ","
             << relation_pairs_seeded << ","
             << relation_pairs_validated << ","
             << total_weak_successors << ","
             << weak_successor_queries << ","
             << tau_closure_total_size << ","
             << tau_closure_queries << ","
             << fixpoint_iterations << std::endl;
    }
    //overload << operator for easy printing
    friend std::ostream& operator<<(std::ostream& os, const CheckStatistics& stats) {
        os << "DTPTA Check Statistics:" << std::endl;
        os << "  Refined States: " << stats.refined_states << std::endl;
        os << "  Abstract States: " << stats.abstract_states << std::endl;
        os << "  Simulation Pairs (remaining): " << stats.simulation_pairs << std::endl;
        os << "  Check Time: " << stats.check_time_ms << " ms" << std::endl;
        os << "  Memory Usage: " << stats.memory_usage_bytes / 1024 << " KB" << std::endl;
        os << "  Symbolic States Explored: " << stats.symbolic_states_explored << std::endl;
        os << "  Relation Pairs Seeded: " << stats.relation_pairs_seeded << std::endl;
        os << "  Relation Pairs Validated: " << stats.relation_pairs_validated << std::endl;
        os << "  Weak Successor Queries: " << stats.weak_successor_queries
           << "  (total results: " << stats.total_weak_successors << ")" << std::endl;
        os << "  Tau-Closure Queries: " << stats.tau_closure_queries
           << "  (total size: " << stats.tau_closure_total_size << ")" << std::endl;
        os << "  Fixpoint Iterations: " << stats.fixpoint_iterations << std::endl;
        return os;
    } 
};


/**
 * DTPTA (Relaxed Weak Timed Bisimulation) Equivalence Checker
 * 
 * Based on the ICSE_DT paper, this implements the refinement check for
 * Relaxed Weak Timed Bisimulation which allows:
 * - Relaxed timing constraints on received events (can be delayed)
 * - Strict timing constraints on sent events (must respect original bounds)
 * - Weak bisimulation for event transitions (abstracts tau-transitions)
 */

struct EventTransition {
    int from_state;
    int to_state;
    std::string event;
    bool is_sent;  // true for sent events, false for received events
    int time_bound; // maximum time bound for this event
    
    EventTransition(int from, int to, const std::string& evt, bool sent, int bound)
        : from_state(from), to_state(to), event(evt), is_sent(sent), time_bound(bound) {}
};

struct StateCorrespondence {
    int refined_state;
    int abstract_state;
    
    StateCorrespondence(int ref, int abs) : refined_state(ref), abstract_state(abs) {}
    
    bool operator==(const StateCorrespondence& other) const {
        return refined_state == other.refined_state && abstract_state == other.abstract_state;
    }
};

// Hash function for StateCorrespondence
struct StateCorrespondenceHash {
    std::size_t operator()(const StateCorrespondence& sc) const {
        return std::hash<int>()(sc.refined_state) ^ (std::hash<int>()(sc.abstract_state) << 1);
    }
};

class DTPTAChecker {
public:
    DTPTAChecker() : last_stats_{0,0,0,0.0,0, 0,0,0,0,0,0,0,0} {}
    bool check_weak_timed_bisim(
            const TimedAutomaton& A,
            const TimedAutomaton& B,
            const Alphabet& X
        );
    // Core equivalence check (game-based relaxed weak timed bisimulation)
    bool check_dtpta_equivalence(const TimedAutomaton& refined, const TimedAutomaton& abstract, bool use_omp = false,
                                 AlgorithmMode algo = AlgorithmMode::GFP);

    bool check_dtpta_simulation(const TimedAutomaton& refined, const TimedAutomaton& abstract,
                                AlgorithmMode algo = AlgorithmMode::GFP);
    // System-level convenience (pairwise index matching)
    bool check_dtpta_equivalence(const System& system_refined,
                                 const System& system_abstract,
                                 dtpta::RunningMode parallel_mode = dtpta::RunningMode::SERIAL,
                                 size_t num_workers = 0,
                                 long timeout_ms = -1,
                                 AlgorithmMode algo = AlgorithmMode::GFP);
    bool check_dtpta_simulation(const System& system_refined, const System& system_abstract);

    struct SystemCheckResult {
        size_t automaton_index;
        std::string template_name_refined;
        std::string template_name_abstract;
        bool is_equivalent;
        CheckStatistics statistics;
    };
    bool check_dtpta_equivalence_detailed(const System& system_refined,
                                          const System& system_abstract,
                                          std::vector<SystemCheckResult>& results);

    CheckStatistics get_last_check_statistics() const { return last_stats_; }
    void print_statistics() const { last_stats_.print(); }
    void reset() { last_stats_ = CheckStatistics{0,0,0,0.0,0, 0,0,0,0,0,0,0,0}; }


    std::vector<const ZoneState*> weak_observable_successors_raw(const TimedAutomaton& ta,
                                                             const ZoneState* start,
                                                             const std::string& action);
    std::vector<const ZoneState*> tau_closure_raw(const TimedAutomaton& ta, const ZoneState* start);
protected: // allow example subclasses to access optimisation helpers (didactic)
// ---- Cached semantic helpers ----
    std::vector<const ZoneState*> tau_closure_cached(const TimedAutomaton& ta, const ZoneState* start);
    std::vector<const ZoneState*> weak_observable_successors_cached(const TimedAutomaton& ta, const ZoneState* start, const std::string& action);
private: 
    mutable CheckStatistics last_stats_;

    // ===== Optimisation Data Structures =====

    inline size_t hash_combine(size_t a, size_t b) noexcept {
            a ^= b + 0x9e3779b9 + (a << 6) + (a >> 2);
            return a;
        }

    // Key for weak successor cache (zone state pointer + action label).
    // Uses the ZoneState pointer directly since each unique zone state has a unique address.
    struct WeakKey {
        const ZoneState* state;
        std::string action;
        bool operator==(const WeakKey& o) const noexcept { return state==o.state && action==o.action; }
    };
    struct WeakKeyHash {
        size_t operator()(const WeakKey& k) const noexcept {
            size_t h1 = std::hash<const ZoneState*>{}(k.state);
            size_t h2 = std::hash<std::string>{}(k.action);
            return h1 ^ h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        }
    };

#ifdef DTPTA_HAS_TBB
    // TBB HashCompare adapters for concurrent_hash_map
    struct TauCacheHashCompare {
        size_t hash(const ZoneState* key) const {
            return std::hash<const ZoneState*>{}(key);
        }
        bool equal(const ZoneState* a, const ZoneState* b) const {
            return a == b;
        }
    };
    struct WeakKeyHashCompare {
        size_t hash(const WeakKey& k) const {
            size_t h1 = std::hash<const ZoneState*>{}(k.state);
            size_t h2 = std::hash<std::string>{}(k.action);
            return h1 ^ h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        }
        bool equal(const WeakKey& a, const WeakKey& b) const {
            return a == b;
        }
    };

    // Thread-safe caches using TBB concurrent_hash_map (fine-grained per-bucket locking)
    tbb::concurrent_hash_map<const ZoneState*, std::vector<const ZoneState*>, TauCacheHashCompare> tau_closure_cache_;
    tbb::concurrent_hash_map<WeakKey, std::vector<const ZoneState*>, WeakKeyHashCompare> weak_succ_cache_;
#else
    // Fallback: standard unordered_map (not thread-safe, sequential only)
    std::unordered_map<const ZoneState*, std::vector<const ZoneState*>> tau_closure_cache_;
    std::unordered_map<WeakKey, std::vector<const ZoneState*>, WeakKeyHash> weak_succ_cache_;
#endif

    // Pair key used for relation + reverse dependency graph
    struct PairKey {
        int r; // refined state id
        int a; // abstract state id
        bool operator==(const PairKey& o) const noexcept { return r==o.r && a==o.a; }
    };
    struct PairKeyHash {
        //size_t operator()(const PairKey& p) const noexcept {
        //    return std::hash<size_t>{}(p.r) ^ (std::hash<size_t>{}(p.a) << 1);
        //}
        size_t operator()(const PairKey& p) const noexcept {
        size_t h1 = std::hash<int>{}(p.r);
        size_t h2 = std::hash<int>{}(p.a);
         return h1 ^ h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
    }
    };

    // Reverse dependency graph: child -> parents that relied on (child) to justify a match
    std::unordered_map<PairKey, std::vector<PairKey>, PairKeyHash> reverse_deps_;

    // Relation set (active candidate pairs)
    std::unordered_set<PairKey, PairKeyHash> relation_;

    // Worklist for localised re-validation
    std::queue<PairKey> worklist_;

    // End-to-end delay context per pair is removed; no debt tracking.

    // Reset caches & structures between top-level checks
    void clear_optimisation_state();

    // ===== On-The-Fly (Local Co-inductive DFS) =====

    /**
     * @brief Serial On-The-Fly equivalence check via recursive DFS.
     *
     * Uses co-inductive cycle detection: pairs currently on the recursion
     * stack are assumed valid (optimistic co-inductive hypothesis).
     * Memoises proven-valid and proven-invalid pairs for efficiency.
     *
     * @param refined   Refined timed automaton.
     * @param abstract  Abstract timed automaton.
     * @param rZone     Refined zone state to explore.
     * @param aZone     Abstract zone state to explore.
     * @param stack     Set of pairs on the current recursion path (co-inductive).
     * @param visited_valid   Memoisation: pairs proven equivalent.
     * @param visited_invalid Memoisation: pairs proven to fail.
     * @return true if the pair (rZone, aZone) satisfies DTPTA equivalence.
     */
    bool explore_otf_serial(
        const TimedAutomaton& refined,
        const TimedAutomaton& abstract,
        const ZoneState* rZone,
        const ZoneState* aZone,
        std::unordered_set<PairKey, PairKeyHash>& stack,
        std::unordered_set<PairKey, PairKeyHash>& visited_valid,
        std::unordered_set<PairKey, PairKeyHash>& visited_invalid);

    /**
     * @brief OpenMP On-The-Fly equivalence check via parallel task DFS.
     *
     * Similar to explore_otf_serial but spawns OpenMP tasks for
     * independent successor branches.  The recursion-path stack is
     * passed by value to each task (path-specific, not shared).
     * visited_valid / visited_invalid are shared and protected by
     * TBB concurrent_hash_map or #pragma omp critical.
     *
     * @param refined   Refined timed automaton.
     * @param abstract  Abstract timed automaton.
     * @param rZone     Refined zone state to explore.
     * @param aZone     Abstract zone state to explore.
     * @param path_stack  Copy of the recursion-path stack for this task lineage.
     * @return true if the pair (rZone, aZone) satisfies DTPTA equivalence.
     */
    bool explore_otf_omp(
        const TimedAutomaton& refined,
        const TimedAutomaton& abstract,
        const ZoneState* rZone,
        const ZoneState* aZone,
        std::unordered_set<PairKey, PairKeyHash> path_stack);

#ifdef DTPTA_HAS_TBB
    // Thread-safe visited sets for OpenMP OTF using TBB concurrent_hash_map
    struct PairKeyHashCompare {
        size_t hash(const PairKey& k) const {
            size_t h1 = std::hash<int>{}(k.r);
            size_t h2 = std::hash<int>{}(k.a);
            return h1 ^ h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        }
        bool equal(const PairKey& a, const PairKey& b) const {
            return a == b;
        }
    };
    tbb::concurrent_hash_map<PairKey, bool, PairKeyHashCompare> otf_visited_valid_tbb_;
    tbb::concurrent_hash_map<PairKey, bool, PairKeyHashCompare> otf_visited_invalid_tbb_;
#else
    // Fallback: standard sets guarded by omp critical
    std::unordered_set<PairKey, PairKeyHash> otf_visited_valid_shared_;
    std::unordered_set<PairKey, PairKeyHash> otf_visited_invalid_shared_;
#endif


    bool check_dtpta_equivalence__(const System& system_refined, const System& system_abstract, bool use_openmp,
                                   AlgorithmMode algo = AlgorithmMode::GFP);


private:
};





class ExposedChecker : public DTPTAChecker {
public:
    // Provide wrappers that call the private cached methods via a public facade.
    std::vector<const ZoneState*> tau_closure(const TimedAutomaton& ta, const ZoneState* z){
        return tau_closure_cached(ta, z); // friendship not granted: adjust visibility if needed
    }
    std::vector<const ZoneState*> weak_successors(const TimedAutomaton& ta, const ZoneState* z, const std::string& act){
        return weak_observable_successors_cached(ta, z, act);
    }
};



} // namespace dtpta

#endif // DTPTA_CORE_H
