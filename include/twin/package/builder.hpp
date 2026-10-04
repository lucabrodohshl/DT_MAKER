/**
 * @file builder.hpp
 * @brief Building Verified Twin Packages (offline toolchain).
 * @ingroup package
 *
 * build_package() is the release pipeline of a Digital Twin:
 *   1. compile V_D strictly (translation-validated) into the canonical IR;
 *   2. run the semantic aligner on (V_P, V_D, K, I_P, I_D) and record evidence;
 *   3. refuse to package an unaligned or lint-failing pair (unless allowed);
 *   4. write every artefact, hash each file, write the canonical manifest;
 *   5. re-verify the result with the same loader the runtime uses.
 *
 * Lives in the separate target twin::package_builder so that the runtime's
 * twin::package does not link the compiler, UTAP or Z3.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "twin/compiler/diagnostics.hpp"
#include "twin/core/result.hpp"
#include "twin/package/package.hpp"

namespace twin::package {

/// @brief Inputs of a package build.
struct BuildInputs {
    std::filesystem::path pt_model;           ///< V_P (UPPAAL XML).
    std::filesystem::path dt_model;           ///< V_D (UPPAAL XML) — the model that is deployed.
    std::filesystem::path ontology;           ///< K (.ont).
    std::filesystem::path pt_interpretation;  ///< I_P (.interp).
    std::filesystem::path dt_interpretation;  ///< I_D (.interp).
    std::string model_id;                     ///< Model identifier.
    std::string model_version{"1.0.0"};       ///< Model version.
    std::int64_t ticks_per_unit{1000};        ///< Logical-time resolution R.
    bool allow_unaligned{false};              ///< Package even if not aligned (development only).
    bool legacy_system_declaration{false};    ///< See compiler::CompileOptions.
    bool overwrite{false};                    ///< Replace an existing output directory.
};

/// @brief Result of a successful build.
struct BuildResult {
    LoadedPackage package;                         ///< The verified package.
    std::vector<compiler::Diagnostic> diagnostics; ///< Compiler warnings/notes.
};

/// @brief Build and verify a package into @p output_directory.
[[nodiscard]] Result<BuildResult> build_package(const BuildInputs& inputs,
                                                const std::filesystem::path& output_directory);

}  // namespace twin::package
