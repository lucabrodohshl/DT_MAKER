/**
 * @file compile_command.cpp
 * @brief `twin compile`: V_D -> canonical IR + compilation manifest.
 */
#include <filesystem>
#include <fstream>
#include <iostream>

#include "cli.hpp"
#include "commands.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/json/canonical.hpp"

namespace twin::cli {
namespace {

constexpr const char* kHelp =
    "twin compile <model.xml> --id <model-id> [--version <v>] [--interp <dt.interp>]\n"
    "             [--ticks-per-unit <R>] [--out <dir>] [--legacy-system-declaration]\n\n"
    "Compiles a DT timed-automaton view (UPPAAL XML) into the canonical Twin IR.\n"
    "Writes <dir>/<id>.ir.json and <dir>/<id>.compilation.json (default dir: .).";

bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return static_cast<bool>(out);
}

}  // namespace

int run_compile(int argc, char** argv) {
    Args args(argc, argv, 0, {"id", "version", "interp", "ticks-per-unit", "out"});
    if (args.error()) return usage_error(*args.error(), kHelp);
    if (args.flag("help")) {
        std::cout << kHelp << "\n";
        return kOk;
    }
    if (args.positionals().size() != 1 || !args.option("id")) {
        return usage_error("expected exactly one model file and --id", kHelp);
    }
    compiler::CompileOptions options;
    options.model_id = *args.option("id");
    options.model_version = args.option_or("version", "1.0.0");
    if (auto interp = args.option("interp")) options.interpretation = *interp;
    options.legacy_system_declaration = args.flag("legacy-system-declaration");
    try {
        options.ticks_per_unit = std::stoll(args.option_or("ticks-per-unit", "1000"));
    } catch (const std::exception&) {
        return usage_error("--ticks-per-unit must be an integer", kHelp);
    }
    const std::filesystem::path out_dir = args.option_or("out", ".");

    auto outcome = compiler::compile_file(args.positionals()[0], options);
    if (auto* failure = std::get_if<compiler::CompileFailure>(&outcome)) {
        std::size_t errors = 0;
        for (const compiler::Diagnostic& d : failure->diagnostics) {
            std::cerr << compiler::render(d) << "\n";
            errors += d.severity == compiler::Severity::Error ? 1 : 0;
        }
        std::cerr << "compilation failed: " << errors << " error(s)\n";
        return kFailure;
    }
    auto& result = std::get<compiler::CompileResult>(outcome);
    for (const compiler::Diagnostic& d : result.manifest.diagnostics) {
        std::cerr << compiler::render(d) << "\n";
    }
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const auto ir_path = out_dir / (options.model_id + ".ir.json");
    const auto manifest_path = out_dir / (options.model_id + ".compilation.json");
    Result<std::string> manifest_text = json::canonical_dump(compiler::to_json(result.manifest));
    if (!manifest_text || !write_file(ir_path, result.canonical_ir) ||
        !write_file(manifest_path, manifest_text.value() + "\n")) {
        std::cerr << "error: cannot write output files in " << out_dir << "\n";
        return kInputError;
    }
    const auto& m = result.model;
    std::cout << "compiled " << m.info.source_template << " -> " << ir_path.string() << "\n"
              << "  locations:   " << m.locations.size() << "\n"
              << "  transitions: " << m.transitions.size() << "\n"
              << "  clocks:      " << m.clocks.size() << "\n"
              << "  source sha256: " << result.manifest.source_sha256 << "\n"
              << "  IR sha256:     " << result.manifest.ir_sha256 << "\n"
              << "  translation validation: "
              << (result.manifest.translation_validation.passed ? "passed" : "FAILED") << " ("
              << result.manifest.translation_validation.checks << " checks)\n"
              << "  event-deterministic: "
              << (result.manifest.determinism.event_deterministic ? "yes" : "no") << "\n";
    return kOk;
}

}  // namespace twin::cli
