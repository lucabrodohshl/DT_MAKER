/**
 * @file twins.hpp
 * @brief Twins, verified packages, deployments and change workspaces.
 * @ingroup platform
 *
 * - A **twin** is a deployable Digital Twin definition bound to an asset
 *   (e.g. the DT of Pump P-101), identified by its model id.
 * - A **package** record points at an immutable Verified Twin Package
 *   directory (built by twin::package_builder) and at the exact artefact
 *   versions + hashes it was built from (its *bindings*).
 * - A **deployment** record states that a twin runs a package from a point
 *   in time. Deployments are append-only: the current deployment is the
 *   latest record; a rollback is a new record pointing at an older package.
 *   Nothing is ever deleted, so every historical execution can be resolved
 *   to the exact artefacts it ran with.
 * - A **change** is an engineering workspace grouping coordinated draft
 *   versions (ontology + interpretation + model) of one twin.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/artifacts.hpp"
#include "twin/platform/clock.hpp"
#include "twin/platform/database.hpp"

namespace twin::platform {

/// @brief Artefact roles of a twin (the inputs of Def. 5 alignment and of the package).
inline constexpr std::array<std::string_view, 5> kBindingRoles = {"pt_model", "dt_model", "ontology",
                                                                  "pt_interpretation", "dt_interpretation"};

/// @brief An artefact version in a role.
struct Binding {
    std::string role;     ///< One of kBindingRoles.
    ArtifactRef ref;      ///< Artefact version.
    std::string sha256;   ///< Content hash of that version.
};

/// @brief A twin definition.
struct Twin {
    std::string id;                         ///< e.g. "pump-p101-dt".
    std::string name;                       ///< Display name.
    std::optional<std::string> asset_id;    ///< Asset it is the twin of.
    std::string description;                ///< Free text.
    std::string model_id;                   ///< Model id used for packages.
    std::int64_t ticks_per_unit{1000};      ///< Logical-time resolution R used when packaging (semantic, not presentation).
    std::optional<std::string> runtime_url; ///< twin-runtime base URL, if connected.
    json::Json presentation = json::Json::object();  ///< Display-only metadata (variables, labels, plugin hints).
    std::string created_at;                 ///< ISO 8601 UTC.
};

/// @brief A package record.
struct PackageRecord {
    std::string id;                          ///< "PKG-0001".
    std::string twin_id;                     ///< Twin.
    std::string directory;                   ///< Package directory (absolute).
    std::string package_hash;                ///< SHA-256 of manifest.json.
    std::string ir_sha256;                   ///< IR hash.
    std::string model_version;               ///< Model version stamped into the package.
    std::vector<Binding> bindings;           ///< Exact inputs.
    std::optional<std::string> evidence_id;  ///< Package build/verification evidence.
    std::string state;                       ///< "built" | "released".
    std::string created_at;                  ///< ISO 8601 UTC.
    std::string created_by;                  ///< Actor.
    std::optional<std::string> released_at;  ///< Release time.
    std::optional<std::string> change_id;    ///< Change it was built for.
    /// @brief The binding in @p role, if any.
    [[nodiscard]] const Binding* binding(std::string_view role) const noexcept;
};

/// @brief A deployment record.
struct Deployment {
    std::int64_t seq{0};                          ///< Global order.
    std::string id;                               ///< "DEP-0001".
    std::string twin_id;                          ///< Twin.
    std::string package_id;                       ///< Package now running.
    std::optional<std::string> previous_package_id;  ///< Package it replaced.
    std::string kind;                             ///< "deploy" | "rollback".
    std::string reason;                           ///< Why (required for rollback).
    std::string deployed_at;                      ///< ISO 8601 UTC.
    std::string deployed_by;                      ///< Actor.
};

/// @brief A change workspace.
struct Change {
    std::string id;                          ///< "CHG-0001".
    std::string twin_id;                     ///< Twin being changed.
    std::string title;                       ///< Short title.
    std::string description;                 ///< Motivation.
    std::string state;                       ///< "open" | "released" | "abandoned".
    std::vector<ArtifactRef> artifacts;      ///< Draft versions in this change.
    std::string created_at;                  ///< ISO 8601 UTC.
    std::string created_by;                  ///< Actor.
    std::optional<std::string> closed_at;    ///< When released/abandoned.
};

/// @brief Twins, packages, deployments and changes (see file documentation).
class TwinRepository {
public:
    /// @brief Repository over @p db; @p clock stamps deployments and changes.
    TwinRepository(Database& db, const Clock& clock) : db_(db), clock_(clock) {}

    /// @name Twins
    /// @{
    /// @brief Creates or updates a twin definition.
    [[nodiscard]] Status upsert_twin(const Twin& twin);
    /// @brief The twin @p id (NotFound otherwise).
    [[nodiscard]] Result<Twin> twin(std::string_view id) const;
    /// @brief All twins, ordered by name.
    [[nodiscard]] Result<std::vector<Twin>> twins() const;
    /// @}

    /// @name Packages
    /// @{
    /// @brief Stores a newly built package (state "built"), assigning its id.
    [[nodiscard]] Result<PackageRecord> add_package(PackageRecord record);
    /// @brief The package @p id (NotFound otherwise).
    [[nodiscard]] Result<PackageRecord> package(std::string_view id) const;
    /// @brief Packages of a twin (all twins when empty), newest first.
    [[nodiscard]] Result<std::vector<PackageRecord>> packages(std::string_view twin_id) const;
    /// @brief Marks a built package released (packages are otherwise immutable).
    [[nodiscard]] Result<PackageRecord> mark_released(std::string_view id);
    /// @}

    /// @name Deployments
    /// @{
    /// @brief Append a deployment; @p kind "rollback" requires a non-empty reason.
    [[nodiscard]] Result<Deployment> deploy(std::string_view twin_id, std::string_view package_id, std::string_view kind,
                                            std::string_view reason, std::string_view actor);
    /// @brief The twin's latest deployment, or nullopt if never deployed.
    [[nodiscard]] Result<std::optional<Deployment>> current_deployment(std::string_view twin_id) const;
    /// @brief Append-only deployment history (all twins when empty), newest first.
    [[nodiscard]] Result<std::vector<Deployment>> deployments(std::string_view twin_id) const;
    /// @brief The deployment @p id (NotFound otherwise).
    [[nodiscard]] Result<Deployment> deployment(std::string_view id) const;
    /// @}

    /// @name Changes
    /// @{
    /// @brief Opens a change workspace for a twin.
    [[nodiscard]] Result<Change> create_change(std::string_view twin_id, std::string_view title,
                                               std::string_view description, std::string_view actor);
    /// @brief The change @p id (NotFound otherwise).
    [[nodiscard]] Result<Change> change(std::string_view id) const;
    /// @brief Changes, optionally only those in @p state (open / released / abandoned).
    [[nodiscard]] Result<std::vector<Change>> changes(std::optional<std::string> state = std::nullopt) const;
    /// @brief Replaces the artefact versions coordinated by an open change.
    [[nodiscard]] Result<Change> set_change_artifacts(std::string_view id, const std::vector<ArtifactRef>& artifacts);
    /// @brief Closes an open change as "released" or "abandoned".
    [[nodiscard]] Result<Change> close_change(std::string_view id, std::string_view state);
    /// @}

private:
    [[nodiscard]] Result<std::vector<PackageRecord>> query_packages(std::string_view where, std::string_view arg) const;
    [[nodiscard]] Result<std::vector<Deployment>> query_deployments(std::string_view where, std::string_view arg) const;
    [[nodiscard]] Result<std::vector<Change>> query_changes(std::string_view where, std::string_view arg) const;
    Database& db_;
    const Clock& clock_;
};

/// @name JSON
/// @{
/// @brief API form of a binding: {role, ref, artifactId, version, sha256}.
[[nodiscard]] json::Json to_json(const Binding& binding);
/// @brief API form of a list of bindings.
[[nodiscard]] json::Json to_json(const std::vector<Binding>& bindings);
/// @brief Parses bindings from their API form.
[[nodiscard]] Result<std::vector<Binding>> bindings_from_json(const json::Json& j);
/// @brief API form of a twin definition (TwinSummary without deployment).
[[nodiscard]] json::Json to_json(const Twin& twin);
/// @brief API form of a package record.
[[nodiscard]] json::Json to_json(const PackageRecord& package);
/// @brief API form of a deployment record.
[[nodiscard]] json::Json to_json(const Deployment& deployment);
/// @brief API form of a change workspace.
[[nodiscard]] json::Json to_json(const Change& change);
/// @}

}  // namespace twin::platform
