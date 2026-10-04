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
};

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
     * @param observations {symbol: value-text} pairs, or empty with @p asset_id to use the
     *        latest telemetry of channels bound to ontology symbols under that asset.
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
    [[nodiscard]] Result<json::Json> twins();
    /// @brief A twin: definition, deployment, bindings, trust summary (all evidence-backed).
    [[nodiscard]] Result<json::Json> twin(std::string_view id);
    /// @brief Impact analysis of changing @p ref (relative to what is deployed).
    [[nodiscard]] Result<json::Json> impact(const platform::ArtifactRef& ref);
    [[nodiscard]] Result<json::Json> create_change(std::string_view twin_id, std::string_view title,
                                                   std::string_view description, const Actor& actor);
    [[nodiscard]] Result<json::Json> changes(const std::optional<std::string>& state);
    [[nodiscard]] Result<json::Json> change(std::string_view id);
    /// @brief Add an artefact to a change: creates a draft from the deployed version.
    [[nodiscard]] Result<json::Json> add_to_change(std::string_view change_id, std::string_view artifact_id,
                                                   std::string_view description, const Actor& actor);
    [[nodiscard]] Result<json::Json> abandon_change(std::string_view change_id, std::string_view reason,
                                                    const Actor& actor);
    /// @brief Release pipeline of a change (computed from evidence on every call).
    [[nodiscard]] Result<json::Json> pipeline(std::string_view change_id);
    /// @brief Run one pipeline stage ("validate", "refinement", "alignment", "compile", "package", "verify").
    [[nodiscard]] Result<json::Json> run_stage(std::string_view change_id, std::string_view stage, const Actor& actor);
    /// @brief Release a change: publishes its versions and releases its package (blocked unless ready).
    [[nodiscard]] Result<json::Json> release(std::string_view change_id, const Actor& actor);
    /// @brief Deploy a released package to its twin.
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
    [[nodiscard]] Result<json::Json> packages(std::string_view twin_id);
    [[nodiscard]] Result<json::Json> package(std::string_view id);
    [[nodiscard]] Result<json::Json> deployments(std::string_view twin_id);

    // ------------------------------------------------------------------ assets & telemetry
    [[nodiscard]] Result<json::Json> assets(const platform::AssetFilter& filter);
    [[nodiscard]] Result<json::Json> asset(std::string_view id);
    [[nodiscard]] Result<json::Json> neighborhood(std::string_view id, int depth,
                                                  const std::vector<std::string>& types, std::size_t max_nodes);
    [[nodiscard]] Result<json::Json> graph_facets();
    /// @brief Channels of an asset subtree with freshness and latest value.
    [[nodiscard]] Result<json::Json> telemetry_channels(std::string_view asset_id, bool include_descendants);
    [[nodiscard]] Result<json::Json> telemetry_series(std::string_view channel_id, std::int64_t from_ms,
                                                      std::int64_t to_ms, std::int64_t max_points);
    /// @brief Ingest samples: [{channel, observedAt|observedMs, value, quality}].
    [[nodiscard]] Result<json::Json> ingest(const json::Json& samples);

    // ------------------------------------------------------------------ cross-cutting
    [[nodiscard]] Result<json::Json> overview();
    [[nodiscard]] Result<json::Json> search(std::string_view query, std::size_t limit);
    [[nodiscard]] Result<json::Json> audit(const platform::AuditFilter& filter);
    [[nodiscard]] Result<json::Json> verify_audit();
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
    [[nodiscard]] Status relate(std::string_view source, std::string_view type, std::string_view target);
    [[nodiscard]] Status upsert_twin(const platform::Twin& twin);
    [[nodiscard]] Status upsert_channel(const platform::TelemetryChannel& channel);
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

private:
    Services(StudioConfig config, std::unique_ptr<platform::Clock> clock);

    StudioConfig config_;
    std::unique_ptr<platform::Clock> clock_;
    std::unique_ptr<platform::AppLog> log_;
    EventHub events_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace twin::studio
