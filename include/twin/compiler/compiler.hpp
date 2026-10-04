/**
 * @file compiler.hpp
 * @brief The Twin Model Compiler: V_D (UPPAAL XML timed automaton) -> IR(V_D).
 * @ingroup compiler
 *
 * @defgroup compiler Twin model compiler
 * @brief Deterministic, validating translation of a DT view into the Twin IR.
 *
 * Pipeline (see docs/architecture.md, "Compilation"):
 *
 *  1. **Parse** the document with UTAP — the same parser the semantic aligner
 *     uses — and fail on any UTAP syntax *or type* error.
 *  2. **Strict extraction**: walk UTAP's abstract syntax and accept only the
 *     fragment the aligner gives semantics to (clocks, conjunctive clock
 *     constraints, zero resets, plain channels, one template). Every other
 *     construct is rejected with a located diagnostic — nothing is dropped,
 *     weakened or strengthened silently.
 *  3. **Translation validation**: build the aligner's own representation
 *     (dtpta::TimedAutomaton) of the same file and check that locations,
 *     clock indices, invariants, guards (as DBM constraints), resets, actions
 *     and the initial location coincide exactly with the extracted model.
 *     Hence the compiled model is provably *the model the aligner verified*.
 *  4. **Emit** canonical IR: stable ids, sorted conjunctions, propositions
 *     from the DT interpretation I_D, and the SHA-256 of the canonical text.
 *  5. **Analyse** (informational): event-determinism of the model.
 *
 * The compiler is deterministic: the IR depends only on the source bytes, the
 * interpretation file and the options (never on time, paths or environment).
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "twin/compiler/diagnostics.hpp"
#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::compiler {

/// @brief Compiler inputs other than the source document.
struct CompileOptions {
    std::string model_id;                                ///< IR model id (required).
    std::string model_version{"1.0.0"};                  ///< IR model version.
    std::int64_t ticks_per_unit{1000};                   ///< Logical-time resolution R.
    std::optional<std::filesystem::path> interpretation; ///< DT interpretation (.interp), optional.
    /**
     * Downgrade UTAP errors located *only* in the `<system>` declaration to
     * warnings. The aligner ignores the system declaration and analyses the
     * single template; legacy corpora (including SemPTDTAlignmentICSE/assets)
     * contain system declarations that UPPAAL rejects. Errors anywhere else
     * remain fatal. The choice is recorded in the manifest diagnostics.
     */
    bool legacy_system_declaration{false};
};

/// @brief Outcome of the translation validation against the aligner's reading.
struct TranslationValidation {
    bool passed{false};               ///< All checks agreed.
    std::size_t checks{0};            ///< Number of individual comparisons performed.
    std::vector<std::string> mismatches;  ///< Human-readable disagreements (empty if passed).
};

/// @brief Event-determinism analysis result.
struct DeterminismReport {
    bool event_deterministic{false};  ///< No two same-label transitions can be enabled together.
    /// Pairs of transition ids whose guards may overlap (only if not deterministic).
    std::vector<std::pair<std::string, std::string>> overlapping;
};

/**
 * @brief Compilation manifest (provenance record written next to the IR).
 *
 * Unlike the IR it contains a timestamp, so it is not part of the IR hash.
 * The timestamp honours SOURCE_DATE_EPOCH for reproducible builds.
 */
struct CompilationManifest {
    std::string source_path;          ///< Path as given (informational).
    std::string source_sha256;        ///< SHA-256 of the source bytes.
    std::string interpretation_sha256;  ///< SHA-256 of the interpretation file, or empty.
    std::string ir_sha256;            ///< SHA-256 of the canonical IR text.
    std::string compiler_version;     ///< twin::version::kCompiler.
    std::string ir_format;            ///< twin::version::kIrFormat.
    std::string compiled_at;          ///< UTC timestamp, ISO 8601.
    std::string model_id;             ///< Model id.
    std::string model_version;        ///< Model version.
    std::string source_template;      ///< UPPAAL template compiled.
    TranslationValidation translation_validation;  ///< Cross-check with the aligner.
    DeterminismReport determinism;    ///< Event-determinism analysis.
    std::vector<Diagnostic> diagnostics;  ///< Warnings and notes (errors abort compilation).
};

/// @brief Serialise a manifest as a JSON value (canonical-safe: integers and strings only).
[[nodiscard]] json::Json to_json(const CompilationManifest& manifest);

/// @brief Successful compilation result.
struct CompileResult {
    ir::Model model;                 ///< The IR model.
    std::string canonical_ir;        ///< Canonical IR text (what is hashed and shipped).
    CompilationManifest manifest;    ///< Provenance record.
};

/// @brief Failed compilation: all diagnostics (at least one error).
struct CompileFailure {
    std::vector<Diagnostic> diagnostics;  ///< Errors, warnings and notes.
};

/**
 * @brief Compile a UPPAAL XML document into the Twin IR.
 * @return the result, or a CompileFailure listing every problem found.
 */
[[nodiscard]] std::variant<CompileResult, CompileFailure> compile_file(
    const std::filesystem::path& source, const CompileOptions& options);

}  // namespace twin::compiler
