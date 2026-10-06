/**
 * @file package.hpp
 * @brief The Verified Twin Package: loading and integrity verification.
 * @ingroup package
 *
 * @defgroup package Verified Twin Package
 * @brief Deployable, content-addressed bundle of a verified DT view.
 *
 * A package is a directory:
 * @code
 *   manifest.json                 canonical JSON; package hash = SHA-256(manifest bytes)
 *   model/dt_view.xml             source V_D (exact bytes)            role "source"
 *   model/dt.interp               DT interpretation I_D               role "dt_interpretation"
 *   ir/model.ir.json              canonical Twin IR                    role "ir"
 *   semantics/domain.ont          ontology K                           role "ontology"
 *   semantics/pt_view.xml         PT view V_P (alignment input)        role "pt_model"
 *   semantics/pt.interp           PT interpretation I_P                role "pt_interpretation"
 *   evidence/alignment.json       alignment evidence                   role "alignment_evidence"
 *   evidence/compilation.json     compilation manifest                 role "compilation_manifest"
 * @endcode
 *
 * Optional files (Studio Mode; older packages simply lack them):
 * @code
 *   model/dt_view.tta.json        canonical DT model (twin-ta/1)       role "dt_source_model"
 *   semantics/pt_view.tta.json    canonical PT model (twin-ta/1)       role "pt_source_model"
 *   monitors/monitors.json        monitors (twin-monitors/1)           role "monitors"
 *   evidence/properties.json      design-time property evidence        role "property_evidence"
 *   meta/type.json                Twin Type metadata                   role "type_metadata"
 * @endcode
 * The builder checks that each canonical model renders to the shipped view
 * byte for byte; the loader checks hashes (as for every file) and that the
 * monitor document is structurally valid.
 *
 * Verification (load_and_verify) is what the runtime does at start-up; it
 * refuses a package whose bytes no longer match the manifest. Hashes give
 * provenance and integrity — they do NOT by themselves prove correctness; the
 * correctness argument is the compiler/kernel proof plus the alignment
 * evidence that the hashes bind together.
 *
 * This target links only core/json/ir, so the runtime can verify packages
 * without linking the compiler, UTAP or Z3.
 */
#pragma once

#include <optional>

#include <filesystem>
#include <string>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::package {

/// @brief One file listed in the manifest.
struct PackageFile {
    std::string path;     ///< Relative path inside the package (no "..", no absolute paths).
    std::string role;     ///< Role, e.g. "ir", "source", "alignment_evidence".
    std::string sha256;   ///< SHA-256 of the file bytes.
    std::int64_t size{0}; ///< Size in bytes.
};

/// @brief Parsed package manifest (format "twin-package/1").
struct Manifest {
    std::string format;            ///< "twin-package/1"
    std::string model_id;          ///< Model identifier.
    std::string model_version;     ///< Model version.
    std::string kernel_compat;     ///< Required kernel compatibility, e.g. "twin-kernel/1".
    std::string compiler_version;  ///< Compiler that produced the IR.
    std::string ir_format;         ///< IR format of ir/model.ir.json.
    std::string aligner_digest;    ///< Source digest of the aligner build that produced the evidence.
    std::string created_at;        ///< UTC timestamp (SOURCE_DATE_EPOCH honoured).
    bool aligned{false};           ///< Alignment verdict recorded at build time.
    bool lint_clean{false};        ///< Alignment lint verdict recorded at build time.
    bool translation_validated{false};  ///< Compiler translation validation passed.
    bool event_deterministic{false};    ///< Compiler determinism analysis.
    std::vector<PackageFile> files;     ///< All files (sorted by path).
};

/// @brief Serialise a manifest (canonical-safe).
[[nodiscard]] json::Json to_json(const Manifest& manifest);
/// @brief Parse and structurally validate a manifest.
[[nodiscard]] Result<Manifest> manifest_from_json(const json::Json& document);

/// @brief One verification check and its outcome (for reports and the UI).
struct Check {
    std::string name;    ///< e.g. "file ir/model.ir.json sha256"
    bool passed{false};  ///< Outcome.
    std::string detail;  ///< Explanation (expected/actual on failure).
};

/// @brief A package that passed verification.
struct LoadedPackage {
    std::filesystem::path directory;  ///< Where it was loaded from.
    std::string package_hash;         ///< SHA-256 of manifest.json bytes.
    Manifest manifest;                ///< The manifest.
    ir::Model model;                  ///< The decoded IR (round-trip checked).
    std::string ir_sha256;            ///< SHA-256 of the canonical IR.
    std::string source_sha256;        ///< SHA-256 of V_D.
    json::Json alignment_evidence;    ///< The evidence document.
    std::optional<json::Json> monitors;       ///< Monitor document (role "monitors"), if shipped.
    std::optional<json::Json> type_metadata;  ///< Twin Type metadata (role "type_metadata"), if shipped.
    std::vector<Check> checks;        ///< Every check performed (all passed).
};

/// @brief Verification policy.
struct VerifyOptions {
    /// Accept packages whose evidence says "not aligned" or has lint errors.
    /// Intended for development only; the runtime reports the status prominently.
    bool allow_unaligned{false};
};

/**
 * @brief Load a package directory and verify it completely.
 *
 * Checks: manifest is canonical JSON of the supported format; kernel
 * compatibility; every listed file exists with the recorded size and SHA-256
 * and no listed path escapes the package; the IR decodes canonically and its
 * hash, model id/version and source hash match; the alignment evidence is bound
 * to the shipped source, interpretations, ontology and PT view; the compilation
 * manifest records a passed translation validation for this IR; and (unless
 * allowed) the evidence verdict is ALIGNED and lint-clean.
 *
 * @return the loaded package, or an IntegrityError/ValidationError naming the
 *         first failed check (all checks are listed in the error context).
 */
[[nodiscard]] Result<LoadedPackage> load_and_verify(const std::filesystem::path& directory,
                                                    const VerifyOptions& options = {});

/// @brief Run all checks without stopping at the first failure (for `twin package verify`).
[[nodiscard]] std::vector<Check> verify_report(const std::filesystem::path& directory,
                                               const VerifyOptions& options = {});

}  // namespace twin::package
