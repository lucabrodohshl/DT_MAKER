/**
 * @file run_all_semantic.cpp
 * @brief Batch runner for semantic alignment: processes a directory of PT/DT/ontology triples.
 *
 * Usage:
 *   run_all_semantic --dir <directory> [--output <results.csv>] [--verbose]
 *                    [--syntactic]
 *
 * Expects the directory to contain triples named:
 *   <prefix>_PT.xml  (or <prefix>_refined.xml)
 *   <prefix>_DT.xml  (or <prefix>_abstract.xml)
 *   <prefix>.json    (optional ontology; trivial if absent)
 *
 * All matched triples are run and results are appended to the CSV.
 *
 * Alternatively, supply explicit pairs via --pair options:
 *   run_all_semantic --pair <pt.xml> <dt.xml> [<ont.json>] --pair ...
 */

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <regex>

#include "dtpta/timedautomaton.h"
#include "dtpta/core.h"
#include "dtpta/ontology_parser.h"
#include "dtpta/semantic_checker.h"

namespace fs = std::filesystem;

// -------------------------------------------------------------------------- //
//  Helper: run one PT/DT/ontology triple
// -------------------------------------------------------------------------- //

struct Triple {
    std::string pt_path;
    std::string dt_path;
    std::string ont_path;  // may be empty
    std::string name;      // label for CSV
};

static bool run_triple(const Triple& t,
                        bool syntactic,
                        bool verbose,
                        std::ofstream& csv)
{
    std::cout << "\n--- " << t.name << " ---" << std::endl;

    try {
        dtpta::TimedAutomaton ta_pt(t.pt_path);
        dtpta::TimedAutomaton ta_dt(t.dt_path);

        ta_pt.construct_zone_graph();
        ta_dt.construct_zone_graph();

        if (verbose) {
            std::cout << "  PT: " << ta_pt.get_num_states() << " states" << std::endl;
            std::cout << "  DT: " << ta_dt.get_num_states() << " states" << std::endl;
        }

        if (syntactic) {
            dtpta::SemanticAlignmentChecker base;
            dtpta::SemanticAlignmentResult base_result;
            bool ok = base.check_weak_timed_bisimulation(ta_pt, ta_dt, base_result);
            std::cout << "  Syntactic DTPTA: " << (ok ? "TRUE" : "FALSE") << std::endl;
            csv << t.name << ",syntactic,"
                << (ok ? "true" : "false") << ","
                << 0 << "," << 0 << "," << 0 << "," << 0 << ","
                << std::fixed << std::setprecision(3) << base_result.time_ms << "\n";
            return ok;
        }

        dtpta::OntologyParser parser;
        dtpta::DomainKnowledge dk = t.ont_path.empty()
                                   ? dtpta::OntologyParser::make_trivial()
                                   : parser.parse(t.ont_path);

        dtpta::SemanticAlignmentChecker checker;
        dtpta::SemanticAlignmentResult  result;
        bool ok = checker.check_semantic_alignment(ta_pt, ta_dt, dk, result);

        std::cout << "  Semantic Alignment: " << (ok ? "TRUE" : "FALSE")
                  << "  (" << std::fixed << std::setprecision(1)
                  << result.time_ms << " ms, "
                  << result.smt_calls_total << " SMT calls)" << std::endl;

        result.append_to_csv(csv, t.name);
        return ok;

    } catch (const std::exception& e) {
        std::cerr << "  ERROR: " << e.what() << std::endl;
        csv << t.name << ",error,error,0,0,0,0,0\n";
        return false;
    }
}

// -------------------------------------------------------------------------- //
//  Helper: auto-discover triples in a directory
// -------------------------------------------------------------------------- //

static std::vector<Triple> discover_triples(const std::string& dir) {
    std::vector<Triple> triples;

    // Collect all XML files whose stem ends with _PT or _refined
    std::unordered_map<std::string, Triple> by_prefix;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext  = entry.path().extension().string();
        std::string stem = entry.path().stem().string();

        if (ext == ".xml") {
            std::string prefix, role;
            // Try _PT / _DT suffixes
            if (stem.size() > 3 && stem.substr(stem.size() - 3) == "_PT") {
                prefix = stem.substr(0, stem.size() - 3);
                by_prefix[prefix].pt_path = entry.path().string();
                by_prefix[prefix].name    = prefix;
            } else if (stem.size() > 3 && stem.substr(stem.size() - 3) == "_DT") {
                prefix = stem.substr(0, stem.size() - 3);
                by_prefix[prefix].dt_path = entry.path().string();
                by_prefix[prefix].name    = prefix;
            } else if (stem.size() > 8 && stem.substr(stem.size() - 8) == "_refined") {
                prefix = stem.substr(0, stem.size() - 8);
                by_prefix[prefix].pt_path = entry.path().string();
                by_prefix[prefix].name    = prefix;
            } else if (stem.size() > 9 && stem.substr(stem.size() - 9) == "_abstract") {
                prefix = stem.substr(0, stem.size() - 9);
                by_prefix[prefix].dt_path = entry.path().string();
                by_prefix[prefix].name    = prefix;
            }
        } else if (ext == ".json") {
            // Potential ontology: stem matches some prefix
            by_prefix[stem].ont_path = entry.path().string();
            if (by_prefix[stem].name.empty()) by_prefix[stem].name = stem;
        }
    }

    for (auto& [prefix, t] : by_prefix) {
        if (!t.pt_path.empty() && !t.dt_path.empty()) {
            triples.push_back(std::move(t));
        }
    }

    std::sort(triples.begin(), triples.end(),
              [](const Triple& a, const Triple& b){ return a.name < b.name; });

    return triples;
}

// -------------------------------------------------------------------------- //
//  main
// -------------------------------------------------------------------------- //

int main(int argc, char* argv[]) {
    std::string dir_path, out_file;
    bool syntactic = false;
    bool verbose   = false;
    std::vector<Triple> explicit_triples;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto require_next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << name << " requires an argument.\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--dir")        dir_path  = require_next("--dir");
        else if (arg == "--output") out_file  = require_next("--output");
        else if (arg == "--syntactic") syntactic = true;
        else if (arg == "--verbose")   verbose   = true;
        else if (arg == "--pair") {
            Triple t;
            t.pt_path = require_next("--pair (pt)");
            t.dt_path = require_next("--pair (dt)");
            // Optional ontology: peek at next arg
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                t.ont_path = argv[++i];
            }
            t.name = fs::path(t.pt_path).stem().string()
                   + "_vs_"
                   + fs::path(t.dt_path).stem().string();
            explicit_triples.push_back(std::move(t));
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 1;
        }
    }

    // Gather triples
    std::vector<Triple> triples = std::move(explicit_triples);
    if (!dir_path.empty()) {
        if (!fs::is_directory(dir_path)) {
            std::cerr << "Error: not a directory: " << dir_path << "\n";
            return 1;
        }
        auto discovered = discover_triples(dir_path);
        triples.insert(triples.end(), discovered.begin(), discovered.end());
    }

    if (triples.empty()) {
        std::cerr << "No PT/DT triples found. Provide --dir or --pair options.\n";
        return 1;
    }

    std::cout << "=== SemAlign Batch Runner ===" << std::endl;
    std::cout << "  Triples to process: " << triples.size() << std::endl;
    std::cout << "  Mode: " << (syntactic ? "syntactic DTPTA" : "semantic alignment")
              << std::endl;

    // Open / create CSV
    bool write_header = out_file.empty() || !fs::exists(out_file);
    std::ofstream csv;
    if (!out_file.empty()) {
        csv.open(out_file, std::ios::app);
        if (!csv) {
            std::cerr << "Warning: cannot open CSV file " << out_file << "\n";
        } else if (write_header) {
            if (syntactic) {
                csv << "model_name,mode,aligned,label_pairs_E,initial_pairs,"
                       "final_pairs,smt_calls,time_ms\n";
            } else {
                dtpta::SemanticAlignmentResult::write_csv_header(csv);
            }
        }
    } else {
        // Write to /dev/null-equivalent (still call append for side effects)
        // We open a throwaway stream
        csv.setstate(std::ios::badbit); // disable writes
    }

    // Ensure CSV always has valid stream state for run_triple
    // (it checks csv before writing)
    // Actually run_triple uses the stream directly — guard with is_open()

    size_t n_aligned = 0, n_total = 0;
    auto t0 = std::chrono::high_resolution_clock::now();

    for (const auto& t : triples) {
        ++n_total;
        bool ok = run_triple(t, syntactic, verbose, csv);
        if (ok) ++n_aligned;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "  Aligned:   " << n_aligned << " / " << n_total << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(1)
              << total_ms << " ms" << std::endl;
    if (!out_file.empty()) {
        std::cout << "  Results written to: " << out_file << std::endl;
    }

    return (n_aligned == n_total) ? 0 : 2;
}
