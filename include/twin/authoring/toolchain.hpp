/**
 * @file toolchain.hpp
 * @brief Bridge from stored model artefacts to the existing toolchain (compiler, aligner, packages).
 * @ingroup authoring
 *
 * A PT/DT model artefact holds either canonical content (twin-ta/1 JSON, written
 * by Studio's editors and importers) or legacy UPPAAL XML (artefacts created
 * before Studio Mode). toolchain_source() returns the bytes every formal tool
 * reads in both cases, so the platform has one call to make wherever it writes
 * a model file for the compiler or the aligner.
 */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

#include "twin/authoring/model.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::authoring {

/// @brief "twin-ta/1", "uppaal-xml" or "unknown".
[[nodiscard]] std::string_view content_format(std::string_view artifact_content);

/**
 * @brief The bytes the compiler and the aligner read for a model artefact:
 * the toolchain rendering of twin-ta/1 content, or legacy UPPAAL XML unchanged.
 * @return ValidationError if canonical content does not decode or is not structurally
 *         valid, InvalidArgument for content of an unknown format.
 */
[[nodiscard]] Result<std::string> toolchain_source(std::string_view artifact_content);

/// @brief A successful compilation of a canonical model.
struct CompiledModel {
    compiler::CompileResult result;  ///< The compiler's result (IR, canonical text, manifest).
    json::Json source_map;           ///< {locations:{irId:name}, transitions:{irId:edgeId}, edges:{edgeId:irId}}.
    std::string semantic_digest;     ///< Digest of the compiled rendering (= manifest source hash).
};

/**
 * @brief Validate @p model, write its toolchain rendering into @p work_dir and compile it.
 *
 * Structural errors (validate()) are returned as a CompileFailure with their TWM
 * codes; compiler diagnostics are returned unchanged otherwise.
 */
[[nodiscard]] std::variant<CompiledModel, compiler::CompileFailure> compile_model(
    const Model& model, const compiler::CompileOptions& options, const std::filesystem::path& work_dir);

/**
 * @brief Write @p bytes to @p file atomically (unique temporary file, then rename).
 *
 * Concurrent writers of identical content (e.g. two requests compiling the same
 * model) never expose a partially written file to a concurrent reader.
 */
[[nodiscard]] bool write_file_atomically(const std::filesystem::path& file, std::string_view bytes);

/// @brief Correspondence between IR elements and canonical model elements (IR transitions follow edge order).
[[nodiscard]] json::Json source_map(const Model& model, const ir::Model& ir);

}  // namespace twin::authoring
