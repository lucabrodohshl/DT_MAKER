/**
 * @file import.hpp
 * @brief Importer framework, the UPPAAL importer, and export of canonical models.
 * @ingroup authoring
 *
 * Importers turn a file into the canonical model. Each one detects its format,
 * parses, validates, converts, reports every unsupported construct (never
 * approximating it) and records provenance. Registered importers:
 *
 * | id            | input                       | notes |
 * |---------------|-----------------------------|-------|
 * | `uppaal-xml`  | UPPAAL XML (flat, 4.x/5.x)  | strict compiler reading; layout and notes kept; preservation check |
 * | `twin-ta-json`| twin-ta/1 JSON              | the canonical format itself |
 * | `twinta-text` | TwinTA text (.tta)          | the textual form (text.hpp) |
 *
 * **Preservation check (UPPAAL).** The original document and the toolchain
 * rendering of the imported model are both compiled (with translation
 * validation against the aligner); their IRs must be identical except for the
 * source hash. Only then is the model returned (`provenance.preserved`), so an
 * imported model means exactly what the original meant to the aligner.
 *
 * A future importer (statecharts, SCXML, SysML state machines) must lower to
 * twin-ta/1 and report anything it cannot lower soundly as unsupported.
 *
 * Diagnostic codes: TWI001 unknown format, TWI002 preservation check failed,
 * TWI003 the converted model is not structurally valid, TWI004 XML not well
 * formed; TWC0xx codes of the strict reader are passed through with positions.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/authoring/diagnostics.hpp"
#include "twin/authoring/layout.hpp"
#include "twin/authoring/model.hpp"
#include "twin/core/result.hpp"

namespace twin::authoring {

/// @brief Version of the importers (recorded in provenance).
inline constexpr std::string_view kImporterVersion = "twin-authoring-import/1";

/// @brief Options of an import.
struct ImportOptions {
    std::string filename;                     ///< Original file name (provenance, format detection).
    bool legacy_system_declaration{false};    ///< Accept invalid `<system>` blocks (legacy corpora), as the aligner does.
};

/// @brief Where an imported model comes from.
struct Provenance {
    std::string original_filename;   ///< As uploaded.
    std::string original_sha256;     ///< SHA-256 of the original bytes.
    std::string imported_at;         ///< UTC time of the import.
    std::string importer;            ///< Importer id.
    std::string importer_version;    ///< kImporterVersion.
    json::Json options = json::Json::object();  ///< Options used.
    std::string content_sha256;      ///< Hash of the canonical content produced.
    std::string semantic_digest;     ///< Semantic digest of the canonical model.
    bool preserved{false};           ///< The preservation check passed (always true when a model is returned).
    std::size_t preservation_checks{0};  ///< Number of elements compared by the check.
};

/// @brief Outcome of an import.
struct ImportResult {
    std::string format;                    ///< Importer id, or "unknown".
    std::optional<Model> model;            ///< The model, absent if any error was found.
    Layout layout;                         ///< Diagram layout found in the source (may be empty).
    std::vector<Diagnostic> diagnostics;   ///< Errors and warnings, with positions where known.
    std::optional<Provenance> provenance;  ///< Present with the model.
};

/// @brief An importer (see file documentation).
class Importer {
public:
    virtual ~Importer() = default;
    /// @brief Stable id, e.g. "uppaal-xml".
    [[nodiscard]] virtual std::string id() const = 0;
    /// @brief Version recorded in provenance.
    [[nodiscard]] virtual std::string version() const = 0;
    /// @brief One-line description for the UI.
    [[nodiscard]] virtual std::string description() const = 0;
    /// @brief Whether @p content (named @p filename) is in this importer's format.
    [[nodiscard]] virtual bool detect(std::string_view filename, std::string_view content) const = 0;
    /// @brief Import @p content.
    [[nodiscard]] virtual ImportResult run(std::string_view content, const ImportOptions& options) const = 0;
};

/// @brief The registered importers, in detection order.
[[nodiscard]] const std::vector<const Importer*>& importers();

/// @brief Detect the format of @p content and import it (TWI001 if no importer recognises it).
[[nodiscard]] ImportResult import_any(std::string_view content, const ImportOptions& options);

/// @brief Import UPPAAL XML (see file documentation).
[[nodiscard]] ImportResult import_uppaal_xml(std::string_view xml, const ImportOptions& options);

/// @brief API form of provenance.
[[nodiscard]] json::Json to_json(const Provenance& provenance);
/// @brief API form: {format, model?, layout, diagnostics, provenance?}.
[[nodiscard]] json::Json to_json(const ImportResult& result);

/// @brief An exported document.
struct ExportResult {
    std::string content;              ///< Document text.
    std::string media_type;           ///< e.g. "application/xml".
    bool round_trip_verified{false};  ///< Re-importing the content gives the same semantic digest.
};

/**
 * @brief Export a valid model as "twinta", "json", "uppaal" (exchange rendering with layout
 * and notes) or "uppaal-toolchain" (the exact bytes the toolchain reads).
 *
 * Every export is re-imported before it is returned; round_trip_verified reports the
 * outcome (an export that does not round-trip is refused with IntegrityError).
 */
[[nodiscard]] Result<ExportResult> export_model(const Model& model, const Layout& layout, std::string_view target);

}  // namespace twin::authoring
