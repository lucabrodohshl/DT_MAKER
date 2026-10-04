/**
 * @file align_command.cpp
 * @brief `twin align`: run the semantic aligner and write hash-bound evidence.
 */
#include <filesystem>
#include <fstream>
#include <iostream>

#include "cli.hpp"
#include "commands.hpp"
#include "twin/alignment/alignment.hpp"
#include "twin/json/canonical.hpp"

namespace twin::cli {
namespace {

constexpr const char* kHelp =
    "twin align --pt <V_P.xml> --dt <V_D.xml> --ontology <K.ont>\n"
    "           --pt-interp <I_P.interp> --dt-interp <I_D.interp>\n"
    "           [--out <alignment.json>] [--legacy-system-declaration]\n\n"
    "Runs the existing semantic aligner (Algorithm 1, Z3) on the PT/DT views and writes\n"
    "a machine-readable alignment evidence document bound to the SHA-256 of every input.\n"
    "Exit code 0: aligned and lint-clean; 1: not aligned or lint errors.";

}  // namespace

int run_align(int argc, char** argv) {
    Args args(argc, argv, 0, {"pt", "dt", "ontology", "pt-interp", "dt-interp", "out"});
    if (args.error()) return usage_error(*args.error(), kHelp);
    if (args.flag("help")) {
        std::cout << kHelp << "\n";
        return kOk;
    }
    for (const char* required : {"pt", "dt", "ontology", "pt-interp", "dt-interp"}) {
        if (!args.option(required)) {
            return usage_error(std::string("missing --") + required, kHelp);
        }
    }
    alignment::AlignmentInputs inputs{*args.option("pt"),        *args.option("dt"),
                                      *args.option("ontology"),  *args.option("pt-interp"),
                                      *args.option("dt-interp"), args.flag("legacy-system-declaration")};
    Result<alignment::AlignmentEvidence> evidence = alignment::check_alignment(inputs);
    if (!evidence) {
        std::cerr << "error: " << evidence.error().to_string() << "\n";
        return kInputError;
    }
    const alignment::AlignmentEvidence& ev = evidence.value();
    Result<std::string> text = json::canonical_dump(alignment::to_json(ev));
    if (!text) {
        std::cerr << "error: " << text.error().to_string() << "\n";
        return kInputError;
    }
    if (auto out = args.option("out")) {
        std::ofstream file(*out, std::ios::binary | std::ios::trunc);
        file << text.value() << "\n";
        if (!file) {
            std::cerr << "error: cannot write " << *out << "\n";
            return kInputError;
        }
    }

    std::cout << "semantic alignment (V_P ~Phi V_D): " << (ev.aligned ? "ALIGNED" : "NOT ALIGNED") << "\n"
              << "  zone graphs: PT " << ev.pt_zones << " zones, DT " << ev.dt_zones << " zones\n"
              << "  label pairs in E: " << ev.label_pairs << ", SMT calls: " << ev.smt_calls
              << ", relation size: " << ev.final_relation_size << "\n"
              << "  syntactic WTB baseline: " << (ev.syntactic_baseline_aligned ? "aligned" : "not aligned")
              << "\n";
    if (!ev.aligned && (!ev.counterexample_pt.empty() || !ev.counterexample_dt.empty())) {
        std::cout << "  counterexample: PT '" << ev.counterexample_pt << "' / DT '" << ev.counterexample_dt << "'\n";
    }
    std::cout << "  label equivalence E:\n";
    for (const alignment::LabelEquivalence& row : ev.label_equivalence) {
        std::cout << "    " << row.pt_label << "  ~  ";
        if (row.dt_labels.empty()) std::cout << "(none)";
        for (std::size_t i = 0; i < row.dt_labels.size(); ++i) std::cout << (i ? ", " : "") << row.dt_labels[i];
        std::cout << "\n";
    }
    for (const alignment::LintFinding& f : ev.lint) {
        std::cout << "  " << f.severity << "[" << f.code << "] " << f.message << "\n";
    }
    std::cout << "  lint: " << (ev.lint_clean ? "clean" : "ERRORS") << "\n";
    return ev.aligned && ev.lint_clean ? kOk : kFailure;
}

}  // namespace twin::cli
