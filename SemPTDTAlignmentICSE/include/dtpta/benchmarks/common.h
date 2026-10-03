#ifndef DTPTA_BENCHMARK_COMMON_H
#define DTPTA_BENCHMARK_COMMON_H

#include <iostream>
#include <exception>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "../utils.h"
#include "../core.h"
#include "../system.h"

namespace dtpta {



const static char* RESULTS_FOLDER = "results/cs_results";


void write_csv_header(std::ofstream& file);
void append_to_csv(std::ofstream& file, 
    const std::string& sys1, 
    const std::string& sys2, 
    const dtpta::CheckStatistics& stats,
    bool are_equivalent);


void self_equivalence_checks(const std::vector<std::string>& filenames,
                            const char* benchmark_folder = "assets/uppaal_benchmarks/",
                            const char* results_folder = "results/",
                            const char* benchmark_prefix = "benchmark_results_",
                            dtpta::RunningMode parallel_mode = dtpta::RunningMode::SERIAL,
                            size_t num_workers = 0,
                            long timeout_ms = -1,
                            dtpta::AlgorithmMode algo = dtpta::AlgorithmMode::GFP);


void comparison_checks(const std::vector<std::string>& filenames,
    const char* benchmark_folder = "assets/uppaal_benchmarks/",
    const char* results_folder = "results/",
    const char* benchmark_prefix = "comparison_results",
    dtpta::RunningMode parallel_mode = dtpta::RunningMode::SERIAL,
    size_t num_workers = 0,
    dtpta::AlgorithmMode algo = dtpta::AlgorithmMode::GFP
);


// Compare ONE reference system with MANY target systems.
void comparison_checks_one_vs_many(
    const std::string& reference_filename,
    const std::vector<std::string>& target_filenames,
    const std::string& benchmark_folder,
    const std::string& results_folder,
    const std::string& benchmark_prefix,
    dtpta::RunningMode parallel_mode,
    size_t n_workers,
    dtpta::AlgorithmMode algo = dtpta::AlgorithmMode::GFP
);

void print_help();


void parse_arguments(int argc, char* argv[], std::string* results_folder, int* n_workers, dtpta::RunningMode* parallel_mode, std::string* input_folder = nullptr, bool allow_non_arguments = true, dtpta::AlgorithmMode* algo = nullptr);

} // namespace dtpta
#endif // DTPTA_BENCHMARK_COMMON_H