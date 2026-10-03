#include "dtpta/benchmarks/common.h"

namespace dtpta
{


// Write CSV header
void write_csv_header(std::ofstream& file) {
    file << "system_1,system_2,refined_states,abstract_states,simulation_pairs,check_time_ms,check_time_s,memory_usage_bytes,memory_usage_kb,equivalent" << std::endl;
}

// Append statistics to CSV file
void append_to_csv(std::ofstream& file, 
    const std::string& sys1, 
    const std::string& sys2, 
    const dtpta::CheckStatistics& stats,
    bool are_equivalent) {
    file << sys1 << "," << sys2 << ","
         << stats.refined_states << ","
         << stats.abstract_states << ","
         << stats.simulation_pairs << ","
         << stats.check_time_ms << ","
         << stats.check_time_ms / 1000 << ","
         << stats.memory_usage_bytes << ","
         << (stats.memory_usage_bytes / 1024.0) << ","
         << (are_equivalent ? ",EQUIVALENT" : ",DIFFERENT")
         << std::endl;
}

void self_equivalence_checks(const std::vector<std::string>& filenames,
                            const char* benchmark_folder ,
                            const char* results_folder ,
                            const char* benchmark_prefix, 
                            dtpta::RunningMode parallel_mode,
                            size_t num_workers,
                            long timeout_ms,
                            dtpta::AlgorithmMode algo)
{
            // Generate timestamp-based filename
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << results_folder<< benchmark_prefix<< std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".csv";
        std::string csv_filename = ss.str();

        if(!std::filesystem::is_directory(results_folder)) {
            std::filesystem::create_directories(results_folder);
        }

        // Create CSV file and write header
        std::ofstream csv_file(csv_filename);
        if (!csv_file.is_open()) {
            throw std::runtime_error("Error: Could not create CSV file " + csv_filename);
        }
        


        dtpta::CheckStatistics::write_csv_header(csv_file);
        
        
        dtpta::CheckStatistics total_stats{0, 0, 0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (const auto& filename : filenames) {
            
            std::cout << "Processing " << benchmark_folder << filename << std::endl;
            dtpta::System s(benchmark_folder + filename);
            s.construct_all_zone_graphs();
            std::cout << "Running self-equivalence check..." << std::endl;
            // Fresh checker per file to avoid cross-file contamination
            dtpta::DTPTAChecker checker;
            try {
                bool equivalent = checker.check_dtpta_equivalence(s, s, parallel_mode, num_workers, timeout_ms, algo);
                std::cout << "Self-equivalence result: " << (equivalent ? "EQUIVALENT" : "DIFFERENT") << std::endl;
                if (!equivalent) {
                    
                    throw std::runtime_error("Error: System " + filename + " is not self-equivalent under DTPTA!");
                }
            } catch (const dtpta::TimeoutException& e) {
                std::cout << "Self-equivalence check for " << filename << " timed out." << std::endl;
                continue; // Skip to next file
            
            }
            
            
            // Get statistics for this run and write to CSV
            auto current_stats = checker.get_last_check_statistics();
            current_stats.append_to_csv(csv_file, filename);
            
            total_stats += current_stats;
        }

        // Write total stats to CSV
        total_stats.append_to_csv(csv_file, "TOTAL");
        csv_file.close();

        std::cout << "--------------------TOTAL STATS-------------------" << std::endl; 
        //print total stats
        total_stats.print();
        std::cout << "========================================" << std::endl;
        std::cout << "Results saved to: " << csv_filename << std::endl;
}



void comparison_checks(const std::vector<std::string>& filenames,
    const char* benchmark_folder,
    const char* results_folder ,
    const char* benchmark_prefix, dtpta::RunningMode parallel_mode, size_t n_workers, dtpta::AlgorithmMode algo)
{
            // Generate timestamp-based filename
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << results_folder<< benchmark_prefix<< std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".csv";
        std::string csv_filename = ss.str();
         if(!std::filesystem::is_directory(results_folder)) {
            std::filesystem::create_directories(results_folder);
        }

        // Create CSV file and write header
        std::ofstream csv_file(csv_filename);
        if (!csv_file.is_open()) {
            throw std::runtime_error("Error: Could not create CSV file " + csv_filename);
        }
        
        write_csv_header(csv_file);
        
        dtpta::DTPTAChecker checker;
        dtpta::CheckStatistics total_stats{0, 0, 0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        //create a map that fiven the file name gives me the system
        std::unordered_map<std::string, std::shared_ptr<dtpta::System>> systems_map;
        for (const auto& filename : filenames) {
            std::cout << "Loading system from " << benchmark_folder << filename << std::endl;
            auto system_ptr = std::make_shared<dtpta::System>(benchmark_folder + filename);
            systems_map[filename] = system_ptr;  // This copies the shared_ptr, not the System
        }
        
        for (size_t i = 0; i < filenames.size(); ++i) {
            const auto& filename_1 = filenames[i];
            std::cout << "Processing " << benchmark_folder << filename_1 << std::endl;

            for (size_t j = i+1; j < filenames.size(); ++j) {  // Start from i+1
                const auto& filename_2 = filenames[j];
                std::cout << "Comparing it to " << benchmark_folder << filename_2 << std::endl;
                
                std::cout << "Running equivalence check..." << std::endl;
                bool equivalent = checker.check_dtpta_equivalence(*systems_map[filename_1], *systems_map[filename_2], parallel_mode, n_workers, -1, algo);
                std::cout << "Equivalence result: " << (equivalent ? "EQUIVALENT" : "DIFFERENT") << std::endl;
                checker.print_statistics();
                
                // Get statistics for this run and write to CSV
                auto current_stats = checker.get_last_check_statistics();
                append_to_csv(csv_file, filename_1, filename_2, current_stats, equivalent);
                
                total_stats += current_stats;
            }
        }

        // Write total stats to CSV
        append_to_csv(csv_file, "TOTAL", "TOTAL", total_stats, false);
        csv_file.close();

        std::cout << "--------------------TOTAL STATS-------------------" << std::endl; 
        //print total stats
        total_stats.print();
        std::cout << "========================================" << std::endl;
        std::cout << "Results saved to: " << csv_filename << std::endl;
}



// Compare ONE reference system with MANY target systems.
void comparison_checks_one_vs_many(
    const std::string& reference_filename,
    const std::vector<std::string>& target_filenames,
    const std::string& benchmark_folder,
    const std::string& results_folder,
    const std::string& benchmark_prefix,
    dtpta::RunningMode parallel_mode,
    size_t n_workers,
    dtpta::AlgorithmMode algo
) {
    // Ensure results directory exists
    if (!std::filesystem::is_directory(results_folder)) {
        std::filesystem::create_directories(results_folder);
    }

    // Timestamp-based CSV filename
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << results_folder << benchmark_prefix
       << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S") << ".csv";
    std::string csv_filename = ss.str();

    // Create CSV file and write header
    std::ofstream csv_file(csv_filename);
    if (!csv_file.is_open()) {
        throw std::runtime_error("Error: Could not create CSV file " + csv_filename);
    }
    write_csv_header(csv_file);

    dtpta::DTPTAChecker checker;
    dtpta::CheckStatistics total_stats{0, 0, 0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Helper to build full path safely
    auto fullpath = [&](const std::string& fname) -> std::string {
        // If benchmark_folder already ends with '/', concatenation is fine;
        // otherwise you may want std::filesystem::path. Keeping your style:
        return benchmark_folder + fname;
    };

    // Load reference system once
    std::cout << "Loading reference system from " << fullpath(reference_filename) << std::endl;
    auto ref_system = std::make_shared<dtpta::System>(fullpath(reference_filename));

    // Load all targets once (optional; you could also load on-demand in the loop)
    std::unordered_map<std::string, std::shared_ptr<dtpta::System>> systems_map;
    systems_map.reserve(target_filenames.size());

    for (const auto& fname : target_filenames) {
        std::cout << "Loading target system from " << fullpath(fname) << std::endl;
        systems_map[fname] = std::make_shared<dtpta::System>(fullpath(fname));
    }

    // Compare reference vs each target
    for (const auto& fname : target_filenames) {
        if (fname == reference_filename) {
            // Skip if user accidentally included reference in targets
            continue;
        }

        std::cout << "Comparing reference " << fullpath(reference_filename)
                  << " to " << fullpath(fname) << std::endl;

        bool equivalent = checker.check_dtpta_equivalence(
            *ref_system, *systems_map.at(fname), parallel_mode, n_workers, -1, algo
        );

        std::cout << "Equivalence result: " << (equivalent ? "EQUIVALENT" : "DIFFERENT") << std::endl;
        checker.print_statistics();

        auto current_stats = checker.get_last_check_statistics();
        append_to_csv(csv_file, reference_filename, fname, current_stats, equivalent);

        total_stats += current_stats;
    }

    // Totals
    append_to_csv(csv_file, "TOTAL", "TOTAL", total_stats, false);
    csv_file.close();

    std::cout << "--------------------TOTAL STATS-------------------" << std::endl;
    total_stats.print();
    std::cout << "========================================" << std::endl;
    std::cout << "Results saved to: " << csv_filename << std::endl;
}



void print_help()
{
    std::cout << "Usage: benchmark_executable [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --folder <path>       Set results folder (default: " << RESULTS_FOLDER << ")" << std::endl;
    std::cout << "  --n-workers <num>    Set number of worker threads (default: 0 = auto-detect)" << std::endl;
    std::cout << "  --mode <mode>        Set parallel mode (serial, thread_pool, openmp; default: serial)" << std::endl;
    std::cout << "  --input_dir <path>   Set input directory for use case benchmarks (default: assets/use_case/)" << std::endl;
    std::cout << "  --algo <algo>        Set algorithm: gfp or on-the-fly (default: gfp)" << std::endl;
    std::cout << "  --help               Show this help message" << std::endl;
}


void parse_arguments(int argc, char* argv[], std::string* results_folder, int* n_workers, dtpta::RunningMode* parallel_mode, std::string* input_folder, bool allow_non_arguments, dtpta::AlgorithmMode* algo) {
    *results_folder = RESULTS_FOLDER;
    *n_workers = 0; // Default to 0 (auto-detect)
    *parallel_mode = dtpta::RunningMode::SERIAL; // Default to serial
    if (algo != nullptr) *algo = dtpta::AlgorithmMode::GFP; // Default to GFP
    // if no arguments, print help
    if (argc == 1 && !allow_non_arguments) {
        print_help();
        std::exit(0);
    }
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--folder" && i + 1 < argc) {
            *results_folder = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            std::exit(0);
        }
        
        else if (arg == "--n-workers" && i + 1 < argc)    {
            *n_workers = std::stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "serial") {
                *parallel_mode = dtpta::RunningMode::SERIAL;
            } else if (mode_str == "thread_pool") {
                *parallel_mode = dtpta::RunningMode::THREAD_POOL;
            } else if (mode_str == "openmp") {
                *parallel_mode = dtpta::RunningMode::OPENMP;
            } else {
                throw std::invalid_argument("Invalid mode: " + mode_str + ". Valid options are: serial, thread_pool, openmp.");
            }
        }
        else if (arg == "--input_dir" &&  input_folder != nullptr && i + 1 < argc) {
            *input_folder = argv[++i];
        }
        else if (arg == "--algo" && algo != nullptr && i + 1 < argc) {
            std::string algo_str = argv[++i];
            if (algo_str == "gfp") {
                *algo = dtpta::AlgorithmMode::GFP;
            } else if (algo_str == "on-the-fly") {
                *algo = dtpta::AlgorithmMode::ON_THE_FLY;
            } else {
                throw std::invalid_argument("Invalid algorithm: " + algo_str + ". Valid options are: gfp, on-the-fly.");
            }
        }
        else {
            print_help();
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }
    if (*n_workers < 0) *n_workers = 0; // 0 means auto-detect
    if (*n_workers > std::thread::hardware_concurrency()) *n_workers = std::thread::hardware_concurrency();
    if ((*results_folder).back() != '/') *results_folder += '/';
    if ((*results_folder).empty()) *results_folder = RESULTS_FOLDER;
    if (*n_workers >0 && *results_folder==RESULTS_FOLDER) *results_folder = std::string(RESULTS_FOLDER).substr(0, std::string(RESULTS_FOLDER).size()-1) + std::to_string(*n_workers) + "/";
    if (input_folder != nullptr && (*input_folder).back() != '/') *input_folder += '/';
    if (input_folder != nullptr && (*input_folder).empty()) *input_folder = "assets/use_case/";
}

    // Implementation of benchmark functions
} // namespace dtpta
