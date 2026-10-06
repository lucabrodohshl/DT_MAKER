/**
 * @file blueprints.hpp
 * @brief Blueprint Studio: the use cases behind building a complete twin (Studio mode).
 * @ingroup studio
 *
 * A Twin Blueprint is the reusable engineering definition of a type of twin. This service
 * implements everything Studio does with one:
 *
 *  - **catalogue** — list, templates and tool palettes, create (blank, template, clone,
 *    import of a bundle or of formal models), metadata, versions and drafts;
 *  - **authoring** — section saves with optimistic revisions; the PT/DT timed automata as
 *    canonical models (diagram layout kept in the document, never in evidence); UPPAAL
 *    import; ontology and interpretation content or pins;
 *  - **assurance** — section validation, the release gate, formal artefact validation,
 *    alignment (the unmodified aligner), compilation (translation-validated), timing windows
 *    and scenario tests on the semantic kernel, section-level impact analysis;
 *  - **release** — the Verified Core Package plus the Twin Deployment Bundle, publishing,
 *    export, instances and their deployment through the Supervisor.
 *
 * Authority is unchanged: every formal conclusion comes from the existing tools (aligner,
 * compiler, kernel, package verifier); this service composes them and records evidence.
 * Every mutating operation appends an engineering-audit record.
 */
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/studio/services.hpp"

namespace twin::studio {

class Supervisor;

/// @brief See file documentation.
class BlueprintService {
public:
    /// @brief Service over @p services; templates are read from @p templates_dir.
    BlueprintService(Services& services, std::filesystem::path templates_dir);
    ~BlueprintService();
    BlueprintService(const BlueprintService&) = delete;
    BlueprintService& operator=(const BlueprintService&) = delete;
    BlueprintService(BlueprintService&&) = delete;
    BlueprintService& operator=(BlueprintService&&) = delete;

    /// @brief Attach the deployment supervisor (deploy, stop and start instances).
    void attach(Supervisor* supervisor) noexcept { supervisor_ = supervisor; }

    // ------------------------------------------------------------------ catalogue
    /// @brief Every Blueprint with its versions summary, readiness hint and instance count.
    [[nodiscard]] Result<json::Json> list();
    /// @brief Shipped templates (metadata only).
    [[nodiscard]] Result<json::Json> templates();
    /// @brief World tool palettes by domain (data, provided by templates).
    [[nodiscard]] Result<json::Json> palettes();
    /**
     * @brief Create a Blueprint: {mode: "blank"|"template"|"clone"|"import"|"formal", id?, name, ...}.
     * blank: {domain?, description?}; template: {templateId}; clone: {from, version};
     * import: {bundle: twin-blueprint-bundle/1}; formal: {ptModel?, dtModel?: {filename, content},
     * ontology?, ptInterpretation?, dtInterpretation?: {content}}.
     */
    [[nodiscard]] Result<json::Json> create(const json::Json& body, const Actor& actor);
    /// @brief A Blueprint with all its versions (summaries) and instances.
    [[nodiscard]] Result<json::Json> get(std::string_view id);
    /// @brief Update name, description, domain or icon.
    [[nodiscard]] Result<json::Json> update_meta(std::string_view id, const json::Json& body, const Actor& actor);
    /// @brief A version: summary, document, pinned artefact versions, editable flag.
    [[nodiscard]] Result<json::Json> version(std::string_view id, std::int64_t version);
    /// @brief New draft derived from @p from (never mutates a published version).
    [[nodiscard]] Result<json::Json> create_draft(std::string_view id, std::int64_t from, std::string_view note,
                                                  const Actor& actor);
    /// @brief Save one section of a draft (StateError on a stale @p revision); returns the new revision and the section's findings.
    [[nodiscard]] Result<json::Json> save_section(std::string_view id, std::int64_t version, std::string_view section,
                                                  std::int64_t revision, const json::Json& content, const Actor& actor);

    // ------------------------------------------------------------------ formal artefacts
    /// @brief The PT ("pt") or DT ("dt") view as a canonical model with layout and structural diagnostics.
    [[nodiscard]] Result<json::Json> model(std::string_view id, std::int64_t version, std::string_view role);
    /// @brief Save a canonical model (and its layout) into the draft's editable artefact version.
    [[nodiscard]] Result<json::Json> save_model(std::string_view id, std::int64_t version, std::string_view role,
                                                std::int64_t revision, const json::Json& model, const json::Json& layout,
                                                const Actor& actor);
    /// @brief Import a model file (UPPAAL XML, twin-ta/1, TwinTA) into the role; unsupported constructs are reported, nothing is saved then.
    [[nodiscard]] Result<json::Json> import_model(std::string_view id, std::int64_t version, std::string_view role,
                                                  std::int64_t revision, std::string_view filename,
                                                  std::string_view content, const Actor& actor);
    /// @brief Ontology or interpretation of the version: pinned version, content, parse diagnostics, validation.
    [[nodiscard]] Result<json::Json> semantics(std::string_view id, std::int64_t version, std::string_view role);
    /// @brief Save content ({content}) or pin an existing version ({ref}) for "ontology", "pt_interpretation" or "dt_interpretation".
    [[nodiscard]] Result<json::Json> save_semantics(std::string_view id, std::int64_t version, std::string_view role,
                                                    std::int64_t revision, const json::Json& body, const Actor& actor);

    // ------------------------------------------------------------------ assurance and test
    /// @brief Section findings (structure, world, data, ...) and the cross-reference index.
    [[nodiscard]] Result<json::Json> validate(std::string_view id, std::int64_t version);
    /// @brief Overview: per-section status, completeness and the release gate (from evidence).
    [[nodiscard]] Result<json::Json> status(std::string_view id, std::int64_t version);
    /// @brief Run a check: "formal" (validate artefacts), "alignment", "compile", "scenarios", "package", "refinement".
    [[nodiscard]] Result<json::Json> run_check(std::string_view id, std::int64_t version, std::string_view check,
                                               const json::Json& body, const Actor& actor);
    /// @brief Rasterised simulator grids of the world (ground truth and initial knowledge).
    [[nodiscard]] Result<json::Json> world_raster(std::string_view id, std::int64_t version);
    /// @brief Kernel what-if on the compiled DT view: legal delay intervals, explanations, refusals.
    ///
    /// The request is a what-if request (`start`, `steps`), or `scenarioSteps`: the steps of a
    /// Blueprint scenario, from which the formal steps are derived as the scenario runner does
    /// (world observations generate their mapped event; `origins` names each step's source).
    [[nodiscard]] Result<json::Json> timing(std::string_view id, std::int64_t version, const json::Json& request);
    /// @brief Run one scenario (or all when empty) as a test; records scenario evidence.
    [[nodiscard]] Result<json::Json> run_scenarios(std::string_view id, std::int64_t version,
                                                   const std::optional<std::string>& scenario, const Actor& actor);
    /// @brief Section-level impact of @p version against @p against (its parent by default).
    [[nodiscard]] Result<json::Json> impact(std::string_view id, std::int64_t version, std::optional<std::int64_t> against);

    // ------------------------------------------------------------------ release
    /// @brief Package view: verified core contents vs deployment content, integrity, scope.
    [[nodiscard]] Result<json::Json> package(std::string_view id, std::int64_t version);
    /// @brief Publish a draft (release gate must pass; formal drafts are published with it).
    [[nodiscard]] Result<json::Json> publish(std::string_view id, std::int64_t version, const Actor& actor);
    /// @brief Export a version as a twin-blueprint-bundle/1 document (document + formal contents).
    [[nodiscard]] Result<json::Json> export_bundle(std::string_view id, std::int64_t version);

    // ------------------------------------------------------------------ preview
    /**
     * @brief Start an isolated Studio preview of a version (draft or published): the real runtime,
     * simulator and feed on the version's verified core (its package, or a sandbox build of the
     * pinned artefacts that is never recorded), with no twin record, deployment or stored telemetry.
     * Body: {speed?, paused?, simulator?} (simulator false: no event script, the engineer drives
     * the runtime). The runtime is reachable through the twin proxy under previewId.
     */
    [[nodiscard]] Result<json::Json> start_preview(std::string_view id, std::int64_t version, const json::Json& body,
                                                   const Actor& actor);
    /// @brief State of a version's preview ({runtime: {state: "not_started"}} if none).
    [[nodiscard]] Result<json::Json> preview(std::string_view id, std::int64_t version);
    /// @brief Stop a version's preview (idempotent).
    [[nodiscard]] Result<json::Json> stop_preview(std::string_view id, std::int64_t version, const Actor& actor);
    /// @brief The preview id of a version ("preview~<id>~v<n>"; never a valid twin id).
    [[nodiscard]] static std::string preview_id(std::string_view id, std::int64_t version);

    // ------------------------------------------------------------------ instances
    /// @brief Instances (of one Blueprint, or all), with deployment and runtime state.
    [[nodiscard]] Result<json::Json> instances(const std::optional<std::string>& blueprint);
    /// @brief One instance.
    [[nodiscard]] Result<json::Json> instance(std::string_view id);
    /**
     * @brief Create an instance of a published version: {blueprintId, version, id, name,
     * description?, assetIds?: {blueprintAsset: assetId}, placement?: {parentAssetId?, world?: {...}},
     * properties?: {...}, connectivity?: {sources?, bindings?}, target?: {kind: "local", speed?, paused?}}.
     * assetIds names the instance's assets (instance scope) or the existing estate assets the
     * Blueprint's context assets denote (reused, never modified).
     * Instantiates the Blueprint's instance-scoped assets and the telemetry channels of its data
     * contract; never copies or changes formal artefacts.
     */
    [[nodiscard]] Result<json::Json> create_instance(const json::Json& body, const Actor& actor);
    /// @brief Deploy or upgrade: {version?} (default: the instance's version); starts the runtime.
    [[nodiscard]] Result<json::Json> deploy_instance(std::string_view id, const json::Json& body, const Actor& actor);
    /// @brief Stop or start an instance's processes ({action: "stop"|"start"}).
    [[nodiscard]] Result<json::Json> control_instance(std::string_view id, std::string_view action, const Actor& actor);
    /// @brief Test a data-source binding: fetch a sample and show raw, canonical, type, unit, timestamp and quality.
    [[nodiscard]] Result<json::Json> test_binding(std::string_view id, std::int64_t version, const json::Json& body);

    /// @brief Append search hits for Blueprints and the elements of each one's working version
    /// (the draft, else the latest version) matching the lower-case @p query_lower.
    void search(std::string_view query_lower, std::vector<SearchHit>& hits);

    /// @brief Opaque implementation (src/studio/blueprints_impl.hpp).
    struct Impl;

private:
    Services& services_;
    Supervisor* supervisor_{nullptr};
    std::unique_ptr<Impl> impl_;
};

}  // namespace twin::studio
