/**
 * @file main.cpp
 * @brief Entry point of the `twin` command-line tool.
 *
 * `twin` bundles the offline toolchain of the Verified Twin architecture:
 * compilation, semantic alignment, packaging, ledger verification and replay.
 * The online runtime is a separate executable (twin-runtime).
 */
#include <iostream>
#include <string>

#include "cli.hpp"
#include "commands.hpp"
#include "twin/core/version.hpp"

namespace {

constexpr const char* kHelp = R"(usage: twin <command> [options]

Commands:
  compile   Compile a DT timed-automaton view (UPPAAL XML) into the canonical Twin IR
  align     Run the semantic aligner on PT/DT views and write alignment evidence
  package   Build, verify or inspect a Verified Twin Package
  ledger    Verify, anchor or show a tamper-evident execution ledger
  replay    Deterministically replay a ledger against its package
  version   Print tool and format versions

Run 'twin <command> --help' for the options of a command.)";

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << kHelp << "\n";
        return twin::cli::kUsageError;
    }
    const std::string command = argv[1];
    if (command == "compile") return twin::cli::run_compile(argc - 2, argv + 2);
    if (command == "align") return twin::cli::run_align(argc - 2, argv + 2);
    if (command == "package") return twin::cli::run_package(argc - 2, argv + 2);
    if (command == "ledger") return twin::cli::run_ledger(argc - 2, argv + 2);
    if (command == "replay") return twin::cli::run_replay(argc - 2, argv + 2);
    if (command == "version" || command == "--version") {
        std::cout << "twin " << twin::version::kProject << "\n"
                  << "  compiler " << twin::version::kCompiler << "\n"
                  << "  kernel   " << twin::version::kKernel << " (" << twin::version::kKernelCompat << ")\n"
                  << "  formats  " << twin::version::kIrFormat << ", " << twin::version::kPackageFormat
                  << ", " << twin::version::kLedgerSchema << "\n";
        return twin::cli::kOk;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << kHelp << "\n";
        return twin::cli::kOk;
    }
    std::cerr << "error: unknown command '" << command << "'\n\n" << kHelp << "\n";
    return twin::cli::kUsageError;
}
