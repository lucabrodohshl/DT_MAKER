/**
 * @file services.hpp
 * @brief Verified Twin Studio application services: the operations behind every API route.
 * @ingroup studio
 *
 * @defgroup studio Verified Twin Studio (application layer and HTTP API)
 * @brief Engineering lifecycle and operational read models served to the Studio UI.
 *
 * Services composes the platform repositories (persistence), the ontology
 * services (formal checks) and the existing toolchain (twin::compiler,
 * twin::alignment, twin::package_builder) into use cases such as "create a
 * draft", "run a refinement check", "compute the release pipeline of a change"
 * or "roll back a deployment".
 *
 * Authority boundaries:
 *  - **Formal conclusions** (validity, refinement, alignment, preservation,
 *    compilation, package integrity) are produced only by twin::ontology,
 *    twin::alignment, twin::compiler and twin::package — never here, never in the UI.
 *  - **Behavioural state** belongs to twin-runtime (the semantic kernel); Studio
 *    only proxies it (server.hpp).
 *  - Every mutating operation appends an engineering-audit record.
 *
 * All operations return API-ready JSON (`json::Json`) or a structured error.
 * Thread safety: all methods may be called concurrently; database access is
 * serialised internally, and long formal checks run outside the database lock.
 */
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/app_log.hpp"
#include "twin/platform/artifacts.hpp"
#include "twin/platform/assets.hpp"
#include "twin/platform/audit.hpp"
#include "twin/platform/clock.hpp"
#include "twin/platform/database.hpp"
#include "twin/platform/evidence.hpp"
#include "twin/platform/object_store.hpp"
#include "twin/platform/telemetry.hpp"
#include "twin/platform/twins.hpp"
#include "twin/studio/events.hpp"

namespace twin::studio {

/// @brief Configuration of a Studio instance.
struct StudioConfig {
    std::filesystem::path data_dir{"var/studio"};  ///< Database, objects, packages, work files, logs.
    unsigned solver_timeout_ms{10000};             ///< Per-query Z3 timeout for ontology checks.
    bool legacy_system_declaration{false};         ///< Passed to compiler/aligner (legacy corpora).
    std::filesystem::path templates_dir{"examples/templates"};  ///< Blueprint templates and tool palettes.
    std::filesystem::path bin_dir;                 ///< twin-runtime, twin-world, twin-pt-feed (empty: next to twin-studio).
    int port_range_begin{18100};                   ///< First local port the supervisor allocates to instances.
    int port_range_end{18999};                     ///< Last local port the supervisor allocates.
};

class BlueprintService;

/// @brief One global-search result (Services::search).
struct SearchHit {
    std::string kind;      ///< "twin", "blueprint", "asset", "state", "telemetry", ...
    std::string id;        ///< Object id.
    std::string title;     ///< Display title.
    std::string subtitle;  ///< Where it lives ("Indoor Inspection Drone v2 (draft) · DT view").
    std::string route;     ///< Web route that opens (and selects) the object.
    int score{3};          ///< Match quality: 0 exact, 1 prefix, 2 word start, 3 substring.
};

/// @brief Match quality of @p text for the lower-case @p query_lower: 0 exact, 1 prefix,
/// 2 word start, 3 substring, -1 no match (case-insensitive).
[[nodiscard]] int search_score(std::string_view text, std::string_view query_lower);
class Supervisor;

/// @brief Identity of the caller (for audit). Not an authentication mechanism.
struct Actor {
    std::string name{"studio-user"};  ///< Recorded in audit, evidence and versions.
};

/// @brief See file documentation.
class Services {
public:
    /// @brief Open (creating if needed) a Studio data directory.
    [[nodiscard]] static Result<std::unique_ptr<Services>> open(const StudioConfig& config,
                                                                std::unique_ptr<platform::Clock> clock = nullptr);
    ~Services();
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;
    Services(Services&&) = delete;
    Services& operator=(Services&&) = delete;

    // ------------------------------------------------------------------ artifacts
    /// @brief Artefacts of a kind (or all) with their published/open versions.
    [[nodiscard]] Result<json::Json> list_artifacts(std::optional<platform::ArtifactKind> kind);
    /// @brief One artefact with all versions (lineage) and dependants.
    [[nodiscard]] Result<json::Json> artifact(std::string_view id);
    /// @brief One version: metadata, content, structure, diagnostics, validation evidence, usages.
    [[nodiscard]] Result<json::Json> version(const platform::ArtifactRef& ref);
    /// @brief Import a new artefact (version 1, DRAFT).
    [[nodiscard]] Result<json::Json> create_artifact(platform::ArtifactKind kind, std::string_view id,
                                                     std::string_view name, std::string_view description,
                                                     std::string_view content, const json::Json& refs,
                                                     const Actor& actor);
    /// @brief New draft derived from @p from; optionally attached to a change.
    [[nodiscard]] Result<json::Json> create_draft(const platform::ArtifactRef& from, std::string_view description,
                                                  const std::optional<std::string>& change_id, const Actor& actor);
    /// @brief Save draft content (does not validate, publish or deploy anything).
    [[nodiscard]] Result<json::Json> save_draft(const platform::ArtifactRef& ref, std::string_view content,
                                                const std::optional<json::Json>& refs,
                                                const std::optional<std::string>& description, const Actor& actor);
    /// @brief Validate a version (DRAFT → VALIDATING → VERIFIED | DRAFT); records evidence.
    [[nodiscard]] Result<json::Json> validate(const platform::ArtifactRef& ref, const Actor& actor);
    /// @brief Publish a VERIFIED version (supersedes the previous one).
    [[nodiscard]] Result<json::Json> publish(const platform::ArtifactRef& ref, const Actor& actor);
    /// @brief Reject an open version (with a reason).
    [[nodiscard]] Result<json::Json> reject(const platform::ArtifactRef& ref, std::string_view reason,
                                            const Actor& actor);
    /// @brief Source texts plus structural diff of two versions of the same kind.
    [[nodiscard]] Result<json::Json> diff(const platform::ArtifactRef& from, const platform::ArtifactRef& to);
    /// @brief Definition and cross references of an ontology symbol.
    [[nodiscard]] Result<json::Json> symbol(const platform::ArtifactRef& ontology, std::string_view name);
    /**
     * @brief Three-valued evaluation of an interpretation version against observations.
     * @param interpretation The interpretation version to evaluate (its ontology comes from its refs).
     * @param observations {symbol: value-text} pairs, or empty with @p asset_id to use the
     *        latest telemetry of channels bound to ontology symbols under that asset.
     * @param asset_id Asset whose latest telemetry supplies observations not given explicitly.
     * @param keys Entries to evaluate (all entries when empty).
     */
    [[nodiscard]] Result<json::Json> evaluate(const platform::ArtifactRef& interpretation,
                                              const std::vector<std::pair<std::string, std::string>>& observations,
                                              const std::optional<std::string>& asset_id,
                                              const std::vector<std::string>& keys);

    // ------------------------------------------------------------------ formal checks
    /// @brief Domain knowledge Φ = (K, I_P, I_D) by artefact versions; interpretations optional.
    struct PhiRefs {
        platform::ArtifactRef ontology;                         ///< K.
        std::optional<platform::ArtifactRef> pt_interpretation; ///< I_P.
        std::optional<platform::ArtifactRef> dt_interpretation; ///< I_D.
    };
    /// @brief Def. 4 refinement check candidate ⊑ base; persisted as evidence.
    [[nodiscard]] Result<json::Json> run_refinement(const PhiRefs& base, const PhiRefs& candidate,
                                                    const std::optional<std::string>& change_id, const Actor& actor);
    /// @brief Run the semantic aligner on explicit bindings (all five roles); persisted as evidence.
    [[nodiscard]] Result<json::Json> run_alignment(const std::vector<platform::Binding>& bindings,
                                                   const std::optional<std::string>& change_id, const Actor& actor);
    /// @brief Compile the DT view with its interpretation; persisted as evidence.
    [[nodiscard]] Result<json::Json> run_compile(std::string_view twin_id, const std::vector<platform::Binding>& bindings,
                                                 const std::optional<std::string>& change_id, const Actor& actor);
    /// @brief Build and verify a Verified Twin Package for @p bindings.
    [[nodiscard]] Result<json::Json> build_package(std::string_view twin_id,
                                                   const std::vector<platform::Binding>& bindings,
                                                   const std::optional<std::string>& change_id, const Actor& actor);

    /// @brief What a package or a compilation is built for (a twin, or a Blueprint version).
    struct BuildTarget {
        std::string owner;                 ///< Package owner: a twin id or platform::blueprint_owner().
        std::string model_id;              ///< Model id stamped into IR and manifest.
        std::int64_t ticks_per_unit{1000}; ///< Logical-time resolution R.
        std::optional<std::string> model_version;        ///< Model version (default: "1.<n>.0" by package count).
        std::optional<std::filesystem::path> monitors;   ///< Monitor document shipped in the package.
        std::optional<json::Json> type_metadata;         ///< Type metadata shipped in the package (meta/type.json).
        bool source_models{false};         ///< Ship canonical PT/DT models (when the artefacts hold twin-ta/1 content).
    };
    /// @brief run_compile() for an explicit target (no twin record needed).
    [[nodiscard]] Result<json::Json> run_compile_for(const BuildTarget& target, const std::vector<platform::Binding>& bindings,
                                                     const std::optional<std::string>& change_id, const Actor& actor);
    /// @brief build_package() for an explicit target (no twin record needed).
    [[nodiscard]] Result<json::Json> build_package_for(const BuildTarget& target,
                                                       const std::vector<platform::Binding>& bindings,
                                                       const std::optional<std::string>& change_id, const Actor& actor);
    /// @brief Re-verify a stored package now (live integrity checks).
    [[nodiscard]] Result<json::Json> verify_package(std::string_view package_id);
    /// @brief One evidence record with its document.
    [[nodiscard]] Result<json::Json> evidence(std::string_view id);
    /// @brief Verification history (evidence records).
    [[nodiscard]] Result<json::Json> evidence_list(const platform::EvidenceFilter& filter);
    /**
     * @brief "Why is this evidence stale?" — applicability of @p evidence_id against a
     * context: a twin's current deployment, or a change's candidate bindings.
     */
    [[nodiscard]] Result<json::Json> evidence_status(std::string_view evidence_id,
                                                     const std::optional<std::string>& twin_id,
                                                     const std::optional<std::string>& change_id);

    // ------------------------------------------------------------------ twins, changes, release
    /// @brief Every twin with its current deployment (GET /twins).
    [[nodiscard]] Result<json::Json> twins();
    /// @brief A twin: definition, deployment, bindings, trust summary (all evidence-backed).
    [[nodiscard]] Result<json::Json> twin(std::string_view id);
    /// @brief Impact analysis of changing @p ref (relative to what is deployed).
    [[nodiscard]] Result<json::Json> impact(const platform::ArtifactRef& ref);
    /// @brief Opens a change workspace for a twin.
    [[nodiscard]] Result<json::Json> create_change(std::string_view twin_id, std::string_view title,
                                                   std::string_view description, const Actor& actor);
    /// @brief Change workspaces, optionally filtered by state.
    [[nodiscard]] Result<json::Json> changes(const std::optional<std::string>& state);
    /// @brief A change with its artefact versions and twin.
    [[nodiscard]] Result<json::Json> change(std::string_view id);
    /// @brief Add an artefact to a change: creates a draft from the deployed version.
    [[nodiscard]] Result<json::Json> add_to_change(std::string_view change_id, std::string_view artifact_id,
                                                   std::string_view description, const Actor& actor);
    /// @brief Abandons an open change (reason required); its drafts are kept.
    [[nodiscard]] Result<json::Json> abandon_change(std::string_view change_id, std::string_view reason,
                                                    const Actor& actor);
    /// @brief Release pipeline of a change (computed from evidence on every call).
    [[nodiscard]] Result<json::Json> pipeline(std::string_view change_id);
    /// @brief Run one pipeline stage ("validate", "refinement", "alignment", "compile", "package", "verify").
    [[nodiscard]] Result<json::Json> run_stage(std::string_view change_id, std::string_view stage, const Actor& actor);
    /// @brief Release a change: publishes its versions and releases its package (blocked unless ready).
    [[nodiscard]] Result<json::Json> release(std::string_view change_id, const Actor& actor);
    /// @brief Deploy a package to a twin after verifying its integrity (a failing package is refused).
    [[nodiscard]] Result<json::Json> deploy(std::string_view twin_id, std::string_view package_id,
                                            std::string_view reason, const Actor& actor);
    /// @brief Compare current deployment with a rollback target.
    [[nodiscard]] Result<json::Json> rollback_preview(std::string_view twin_id, std::string_view package_id);
    /// @brief Roll back to an earlier released package (reason required; nothing deleted).
    [[nodiscard]] Result<json::Json> rollback(std::string_view twin_id, std::string_view package_id,
                                              std::string_view reason, const Actor& actor);
    /**
     * @brief Initial deployment of a twin that has never been deployed.
     *
     * Requires every bound version to be PUBLISHED; builds and verifies a package
     * (compile + align, refusing unaligned views), releases it and deploys it.
     * Afterwards all evolution goes through change workspaces.
     */
    [[nodiscard]] Result<json::Json> bootstrap_twin(std::string_view twin_id, const std::vector<platform::Binding>& bindings,
                                                    const Actor& actor);
    /// @brief Packages of a twin (all when empty), newest first.
    [[nodiscard]] Result<json::Json> packages(std::string_view twin_id);
    /// @brief A package with bound versions, build evidence, integrity and deployments.
    [[nodiscard]] Result<json::Json> package(std::string_view id);
    /// @brief Deployment history of a twin (all when empty), newest first.
    [[nodiscard]] Result<json::Json> deployments(std::string_view twin_id);

    // ------------------------------------------------------------------ assets & telemetry
    /// @brief A page of assets matching @p filter.
    [[nodiscard]] Result<json::Json> assets(const platform::AssetFilter& filter);
    /// @brief An asset with ancestors, children, relationships and its (possibly inherited) twin.
    [[nodiscard]] Result<json::Json> asset(std::string_view id);
    /**
     * @brief Create an asset definition (audited). Refuses an existing id; the parent must exist.
     * Twin bindings and telemetry channels are never set here: a new asset has no data source
     * until one is bound explicitly.
     */
    [[nodiscard]] Result<json::Json> create_asset(const json::Json& body, const Actor& actor);
    /**
     * @brief Link two existing assets with a typed relationship (audited). Hierarchy ("contains")
     * is expressed by the parent, not by links; linking never duplicates an asset's identity.
     */
    [[nodiscard]] Result<json::Json> link_assets(std::string_view source, std::string_view type, std::string_view target,
                                                 const Actor& actor);
    /// @brief Bounded knowledge-graph neighbourhood around an asset.
    [[nodiscard]] Result<json::Json> neighborhood(std::string_view id, int depth,
                                                  const std::vector<std::string>& types, std::size_t max_nodes);
    /// @brief Relationship and asset types with counts, and the hierarchy roots.
    [[nodiscard]] Result<json::Json> graph_facets();
    /// @brief Channels of an asset subtree with freshness and latest value.
    [[nodiscard]] Result<json::Json> telemetry_channels(std::string_view asset_id, bool include_descendants);
    /// @brief Samples of a channel in [from, to], downsampled to buckets beyond @p max_points.
    [[nodiscard]] Result<json::Json> telemetry_series(std::string_view channel_id, std::int64_t from_ms,
                                                      std::int64_t to_ms, std::int64_t max_points);
    /// @brief Every telemetry channel (typed; used by the runtime telemetry bridge).
    [[nodiscard]] Result<std::vector<platform::TelemetryChannel>> telemetry_channels_all();
    /// @brief The canonical Twin IR stored in a package (renders the behaviour model without a runtime).
    [[nodiscard]] Result<json::Json> package_ir(std::string_view package_id);
    /// @brief Ingest samples: [{channel, observedAt|observedMs, value, quality}].
    [[nodiscard]] Result<json::Json> ingest(const json::Json& samples);

    // ------------------------------------------------------------------ cross-cutting
    /// @brief Estate overview: assets, telemetry freshness, twins and trust, engineering activity.
    [[nodiscard]] Result<json::Json> overview();
    /**
     * @brief Global search over every object kind: twins, Blueprints and their elements (asset
     * types, assets, world objects, telemetry, events, commands, data sources, states,
     * requirements, monitors, scenarios), assets, artefacts and versions ("process-pump@2"),
     * ontology symbols and axioms, interpretation entries, telemetry channels, evidence, packages,
     * deployments and changes. Results are ranked by match quality (exact, prefix, word start,
     * substring), then by kind, and cut at @p limit.
     */
    [[nodiscard]] Result<json::Json> search(std::string_view query, std::size_t limit);
    /// @brief A page of engineering-audit records, newest first.
    [[nodiscard]] Result<json::Json> audit(const platform::AuditFilter& filter);
    /// @brief Recomputes the engineering-audit hash chain.
    [[nodiscard]] Result<json::Json> verify_audit();
    /// @brief Diagnostic application-log entries matching @p filter.
    [[nodiscard]] Result<json::Json> logs(const platform::LogFilter& filter);
    /// @brief Versions of every component (for the About panel and evidence).
    [[nodiscard]] json::Json about() const;

    /// @brief Application log.
    [[nodiscard]] platform::AppLog& app_log() noexcept { return *log_; }
    /// @brief Live event hub (SSE).
    [[nodiscard]] EventHub& events() noexcept { return events_; }
    /// @brief Clock used for every record.
    [[nodiscard]] const platform::Clock& clock() const noexcept { return *clock_; }
    /// @brief Configuration.
    [[nodiscard]] const StudioConfig& config() const noexcept { return config_; }
    /// @brief Twin definitions/registry (used by the HTTP runtime proxy and the seeder).
    [[nodiscard]] Result<platform::Twin> twin_record(std::string_view id);
    /// @brief Upsert raw registry records (seeding/import).
    [[nodiscard]] Status upsert_asset(const platform::Asset& asset);
    /// @brief Adds a typed relationship between two assets (seeding/import).
    [[nodiscard]] Status relate(std::string_view source, std::string_view type, std::string_view target);
    /// @brief Creates or updates a twin definition (seeding/import).
    [[nodiscard]] Status upsert_twin(const platform::Twin& twin);
    /// @brief Creates or updates a telemetry channel (seeding/import).
    [[nodiscard]] Status upsert_channel(const platform::TelemetryChannel& channel);
    /// @brief Stores samples of one channel; returns how many were stored.
    [[nodiscard]] Result<std::int64_t> ingest_samples(std::string_view channel_id,
                                                      const std::vector<platform::TelemetrySample>& samples);
    /// @brief Current deployed bindings of a twin (empty if never deployed).
    [[nodiscard]] Result<std::vector<platform::Binding>> deployed_bindings(std::string_view twin_id);
    /// @brief Candidate bindings of a change (deployed bindings overlaid with the change's versions).
    [[nodiscard]] Result<std::vector<platform::Binding>> candidate_bindings(std::string_view change_id);
    /// @brief Bind roles to explicit versions (hashes looked up).
    [[nodiscard]] Result<std::vector<platform::Binding>> make_bindings(
        const std::vector<std::pair<std::string, platform::ArtifactRef>>& roles);

    /// @brief Opaque implementation state (defined in src/studio/services_impl.hpp).
    struct Impl;
    /// @brief The implementation state, for the other Studio services (BlueprintService, Supervisor).
    [[nodiscard]] Impl& internals() noexcept { return *impl_; }

    /// @brief Blueprint Studio (Twin Blueprints, instances, deployment).
    [[nodiscard]] BlueprintService& blueprints() noexcept { return *blueprints_; }
    /// @brief The deployment supervisor (instance processes).
    [[nodiscard]] Supervisor& supervisor() noexcept { return *supervisor_; }

private:
    Services(StudioConfig config, std::unique_ptr<platform::Clock> clock);

    StudioConfig config_;
    std::unique_ptr<platform::Clock> clock_;
    std::unique_ptr<platform::AppLog> log_;
    EventHub events_;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<BlueprintService> blueprints_;
    std::unique_ptr<Supervisor> supervisor_;
};

}  // namespace twin::studio
