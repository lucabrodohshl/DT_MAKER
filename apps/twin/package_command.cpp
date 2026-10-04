/**
 * @file package_command.cpp
 * @brief `twin package build|verify|inspect`.
 */
#include <iostream>

#include "cli.hpp"
#include "commands.hpp"
#include "twin/package/builder.hpp"
#include "twin/package/package.hpp"

namespace twin::cli {
namespace {

constexpr const char* kHelp =
    "twin package build --pt <V_P.xml> --dt <V_D.xml> --ontology <K.ont>\n"
    "                   --pt-interp <I_P.interp> --dt-interp <I_D.interp>\n"
    "                   --id <model-id> [--version <v>] [--ticks-per-unit <R>]\n"
    "                   --out <dir> [--overwrite] [--allow-unaligned] [--legacy-system-declaration]\n"
    "twin package verify <dir> [--allow-unaligned]\n"
    "twin package inspect <dir>\n\n"
    "build   compile V_D, run the aligner, bundle and hash every artefact, self-verify\n"
    "verify  re-check every hash and binding (what twin-runtime does at start-up)\n"
    "inspect print the manifest summary";

int build(int argc, char** argv) {
    Args args(argc, argv, 0,
              {"pt", "dt", "ontology", "pt-interp", "dt-interp", "id", "version", "ticks-per-unit", "out"});
    if (args.error()) return usage_error(*args.error(), kHelp);
    for (const char* required : {"pt", "dt", "ontology", "pt-interp", "dt-interp", "id", "out"}) {
        if (!args.option(required)) return usage_error(std::string("missing --") + required, kHelp);
    }
    package::BuildInputs in;
    in.pt_model = *args.option("pt");
    in.dt_model = *args.option("dt");
    in.ontology = *args.option("ontology");
    in.pt_interpretation = *args.option("pt-interp");
    in.dt_interpretation = *args.option("dt-interp");
    in.model_id = *args.option("id");
    in.model_version = args.option_or("version", "1.0.0");
    try {
        in.ticks_per_unit = std::stoll(args.option_or("ticks-per-unit", "1000"));
    } catch (const std::exception&) {
        return usage_error("--ticks-per-unit must be an integer", kHelp);
    }
    in.allow_unaligned = args.flag("allow-unaligned");
    in.legacy_system_declaration = args.flag("legacy-system-declaration");
    in.overwrite = args.flag("overwrite");
    Result<package::BuildResult> r = package::build_package(in, *args.option("out"));
    if (!r) {
        std::cerr << "package build failed: " << r.error().to_string() << "\n";
        return kFailure;
    }
    const package::LoadedPackage& p = r.value().package;
    std::cout << "built package " << p.directory.string() << "\n"
              << "  model:        " << p.manifest.model_id << " " << p.manifest.model_version << "\n"
              << "  package hash: " << p.package_hash << "\n"
              << "  IR hash:      " << p.ir_sha256 << "\n"
              << "  alignment:    " << (p.manifest.aligned ? "ALIGNED" : "NOT ALIGNED")
              << (p.manifest.lint_clean ? ", lint clean" : ", lint errors") << "\n"
              << "  verification: " << p.checks.size() << " checks passed\n";
    return kOk;
}

int verify(int argc, char** argv) {
    Args args(argc, argv, 0, {});
    if (args.positionals().size() != 1) return usage_error("expected a package directory", kHelp);
    package::VerifyOptions opts{args.flag("allow-unaligned")};
    std::vector<package::Check> checks = package::verify_report(args.positionals()[0], opts);
    std::size_t failed = 0;
    for (const package::Check& c : checks) {
        std::cout << (c.passed ? "  [ok]   " : "  [FAIL] ") << c.name;
        if (!c.passed && !c.detail.empty()) std::cout << "  -- " << c.detail;
        std::cout << "\n";
        failed += c.passed ? 0 : 1;
    }
    std::cout << (failed == 0 ? "PACKAGE VALID" : "PACKAGE INVALID") << " (" << checks.size() - failed
              << "/" << checks.size() << " checks passed)\n";
    return failed == 0 ? kOk : kFailure;
}

int inspect(int argc, char** argv) {
    Args args(argc, argv, 0, {});
    if (args.positionals().size() != 1) return usage_error("expected a package directory", kHelp);
    Result<package::LoadedPackage> p =
        package::load_and_verify(args.positionals()[0], package::VerifyOptions{true});
    if (!p) {
        std::cerr << "error: " << p.error().to_string() << "\n";
        return kFailure;
    }
    const package::Manifest& m = p.value().manifest;
    std::cout << "package " << p.value().package_hash << "\n"
              << "  format " << m.format << ", kernel " << m.kernel_compat << "\n"
              << "  model " << m.model_id << " " << m.model_version << " (template "
              << p.value().model.info.source_template << ")\n"
              << "  created " << m.created_at << " by compiler " << m.compiler_version << "\n"
              << "  aligner digest " << m.aligner_digest << "\n"
              << "  aligned " << (m.aligned ? "yes" : "no") << ", lint clean "
              << (m.lint_clean ? "yes" : "no") << ", translation validated "
              << (m.translation_validated ? "yes" : "no") << ", event-deterministic "
              << (m.event_deterministic ? "yes" : "no") << "\n"
              << "  files:\n";
    for (const package::PackageFile& f : m.files) {
        std::cout << "    " << f.sha256.substr(0, 16) << "  " << f.path << "  (" << f.role << ", "
                  << f.size << " bytes)\n";
    }
    return kOk;
}

}  // namespace

int run_package(int argc, char** argv) {
    if (argc < 1) return usage_error("missing subcommand", kHelp);
    const std::string sub = argv[0];
    if (sub == "build") return build(argc - 1, argv + 1);
    if (sub == "verify") return verify(argc - 1, argv + 1);
    if (sub == "inspect") return inspect(argc - 1, argv + 1);
    if (sub == "--help" || sub == "help") {
        std::cout << kHelp << "\n";
        return kOk;
    }
    return usage_error("unknown subcommand '" + sub + "'", kHelp);
}

}  // namespace twin::cli
