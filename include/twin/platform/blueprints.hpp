/**
 * @file blueprints.hpp
 * @brief Twin Blueprints: versioned engineering definitions of a type of twin, and their bundles.
 * @ingroup platform
 *
 * A **Blueprint** (e.g. "CentrifugalPump") has numbered **versions**. A version holds:
 *  - the Blueprint document (format twin-blueprint/1): every non-formal section of the
 *    definition — identity, structure, world, data contract, connectivity, presentation,
 *    diagram layouts, assurance (requirements, monitors, alerts), simulation and scenarios;
 *  - **pins**: the formal artefact versions it uses, by role (pt_model, dt_model, ontology,
 *    pt_interpretation, dt_interpretation), kept in the artefact store with their own
 *    lifecycle and evidence.
 *
 * Rules (enforced here, not in the UI):
 *  - a version is `draft`, `published` or `deprecated`; only drafts change, and every
 *    change must name the revision it was based on (optimistic concurrency: a stale
 *    revision is a StateError, so two editors never silently overwrite each other);
 *  - at most one draft per Blueprint; a new draft is always derived from an existing
 *    version, so the lineage is a chain;
 *  - publishing freezes the version (document, pins, package and bundle references).
 *
 * Nothing is deleted: instances keep resolving the exact version they were created from.
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/clock.hpp"
#include "twin/platform/database.hpp"

namespace twin::platform {

/// @brief Format tag of Blueprint documents.
inline constexpr std::string_view kBlueprintFormat = "twin-blueprint/1";

/// @brief Sections of a Blueprint document, in authoring order.
inline constexpr std::string_view kBlueprintSections[] = {
    "identity", "structure", "world", "data", "connectivity", "presentation",
    "behavior", "assurance", "simulation", "scenarios"};

/// @brief Formal roles a Blueprint version pins.
inline constexpr std::string_view kBlueprintRoles[] = {"pt_model", "dt_model", "ontology", "pt_interpretation",
                                                       "dt_interpretation"};

/// @brief Blueprint identity.
struct Blueprint {
    std::string id;           ///< Stable id (lowercase letters, digits, '-').
    std::string name;         ///< Display name.
    std::string description;  ///< Free text.
    std::string domain{"generic"};  ///< Domain (selects tool palettes), e.g. "mobile-robot", "process".
    std::string icon{"boxes"};      ///< Icon name.
    std::optional<std::string> template_id;  ///< Template it was created from.
    std::optional<std::string> cloned_from;  ///< "<blueprint>@<version>" it was cloned from.
    std::string created_at;   ///< ISO 8601 UTC.
    std::string created_by;   ///< Actor.
};

/// @brief One version of a Blueprint.
struct BlueprintVersion {
    std::string blueprint_id;           ///< Blueprint.
    std::int64_t version{0};            ///< 1-based.
    std::string state{"draft"};         ///< "draft", "published" or "deprecated".
    std::int64_t revision{1};           ///< Incremented on every save (optimistic concurrency).
    std::optional<std::int64_t> parent; ///< Version it was derived from.
    json::Json document = json::Json::object();  ///< twin-blueprint/1 document.
    std::string document_sha256;        ///< SHA-256 of the canonical document.
    std::map<std::string, std::string> pins;  ///< role -> "artifact@version".
    std::optional<std::string> package_id;    ///< Verified core package built for this version.
    std::optional<std::string> bundle_id;     ///< Deployment bundle built for this version.
    std::string note;                   ///< What this version changes.
    std::string created_at;             ///< ISO 8601 UTC.
    std::string created_by;             ///< Actor.
    std::string updated_at;             ///< ISO 8601 UTC.
    std::string updated_by;             ///< Actor of the last save.
    std::optional<std::string> published_at;  ///< When it was published.
};

/// @brief A deployment bundle record (twin-bundle/1 directory).
struct BundleRecord {
    std::string id;            ///< "BND-0001".
    std::string blueprint_id;  ///< Blueprint.
    std::int64_t version{0};   ///< Blueprint version.
    std::string package_id;    ///< Verified core package it carries.
    std::string directory;     ///< Absolute directory.
    std::string bundle_hash;   ///< SHA-256 of bundle.json.
    std::string created_at;    ///< ISO 8601 UTC.
    std::string created_by;    ///< Actor.
};

/// @brief See file documentation.
class BlueprintRepository {
public:
    /// @brief Repository over @p db; @p clock stamps records.
    BlueprintRepository(Database& db, const Clock& clock) : db_(db), clock_(clock) {}

    /// @brief Creates a Blueprint and its version 1 (draft) with @p document (see without_nulls()) and @p pins.
    [[nodiscard]] Result<BlueprintVersion> create(const Blueprint& blueprint, const json::Json& document,
                                                  const std::map<std::string, std::string>& pins, std::string_view actor);
    /// @brief The Blueprint @p id (NotFound otherwise).
    [[nodiscard]] Result<Blueprint> blueprint(std::string_view id) const;
    /// @brief All Blueprints, by name.
    [[nodiscard]] Result<std::vector<Blueprint>> blueprints() const;
    /// @brief Updates display metadata (name, description, domain, icon).
    [[nodiscard]] Result<Blueprint> update_meta(const Blueprint& blueprint);

    /// @brief One version (NotFound otherwise).
    [[nodiscard]] Result<BlueprintVersion> version(std::string_view id, std::int64_t version) const;
    /// @brief All versions of a Blueprint, newest first.
    [[nodiscard]] Result<std::vector<BlueprintVersion>> versions(std::string_view id) const;
    /// @brief The open draft of a Blueprint, if any.
    [[nodiscard]] Result<std::optional<BlueprintVersion>> draft(std::string_view id) const;
    /// @brief The newest published version, if any.
    [[nodiscard]] Result<std::optional<BlueprintVersion>> latest_published(std::string_view id) const;

    /// @brief New draft derived from @p from (document and pins copied). Refused if a draft exists.
    [[nodiscard]] Result<BlueprintVersion> create_draft(std::string_view id, std::int64_t from, std::string_view note,
                                                        std::string_view actor);
    /**
     * @brief Saves a draft's document (see without_nulls()) and pins if @p expected_revision is current.
     * @return the saved version (revision + 1); StateError for a published version or a stale revision.
     */
    [[nodiscard]] Result<BlueprintVersion> save(std::string_view id, std::int64_t version, std::int64_t expected_revision,
                                                const json::Json& document,
                                                const std::map<std::string, std::string>& pins, std::string_view actor);
    /// @brief Records the package and bundle built for a version (drafts and published versions).
    [[nodiscard]] Status set_build(std::string_view id, std::int64_t version, const std::optional<std::string>& package_id,
                                   const std::optional<std::string>& bundle_id);
    /// @brief Freezes a draft as published.
    [[nodiscard]] Result<BlueprintVersion> publish(std::string_view id, std::int64_t version, std::string_view actor);
    /// @brief Marks a published version deprecated (instances keep running it).
    [[nodiscard]] Result<BlueprintVersion> deprecate(std::string_view id, std::int64_t version);

    /// @name Bundles
    /// @{
    /// @brief Stores a bundle record, assigning its id.
    [[nodiscard]] Result<BundleRecord> add_bundle(BundleRecord record);
    /// @brief The bundle @p id (NotFound otherwise).
    [[nodiscard]] Result<BundleRecord> bundle(std::string_view id) const;
    /// @brief Moves a bundle's directory (after it was finalised).
    [[nodiscard]] Status set_bundle_directory(std::string_view id, std::string_view directory);
    /// @}

private:
    [[nodiscard]] Result<std::vector<BlueprintVersion>> query(std::string_view where, std::string_view id,
                                                              std::optional<std::int64_t> version) const;
    Database& db_;
    const Clock& clock_;
};

/// @brief @p j with every object member whose value is null removed, recursively. In twin-blueprint/1
/// an absent member and a null one mean the same; documents are stored without nulls so that
/// readers never meet a null where they expect an object, array or string.
[[nodiscard]] json::Json without_nulls(const json::Json& j);

/// @brief Whether @p id is a valid Blueprint or instance id (1-64 of [a-z0-9-], starting with a letter or digit).
[[nodiscard]] bool valid_blueprint_id(std::string_view id) noexcept;

/// @brief API form of a Blueprint.
[[nodiscard]] json::Json to_json(const Blueprint& blueprint);
/// @brief API form of a version without its document ({blueprintId, version, state, revision, pins, ...}).
[[nodiscard]] json::Json version_summary(const BlueprintVersion& version);
/// @brief API form of a bundle record.
[[nodiscard]] json::Json to_json(const BundleRecord& bundle);

}  // namespace twin::platform
