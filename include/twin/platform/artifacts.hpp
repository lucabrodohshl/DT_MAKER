/**
 * @file artifacts.hpp
 * @brief Versioned formal artefacts (ontologies, interpretations, PT/DT models) and their lifecycle.
 * @ingroup platform
 *
 * An artefact (e.g. ontology `process-pump`) has numbered versions
 * (`process-pump@1`, `@2`, ...). Each version names its content by SHA-256
 * (ObjectStore) and has a lifecycle state:
 *
 * @dot
 * digraph lifecycle {
 *   rankdir=LR; node [shape=box, fontname=Helvetica, fontsize=10];
 *   DRAFT -> VALIDATING -> VERIFIED -> PUBLISHED -> SUPERSEDED;
 *   VALIDATING -> DRAFT [label="invalid"];
 *   VERIFIED -> DRAFT [label="edited"];
 *   DRAFT -> REJECTED; VERIFIED -> REJECTED;
 * }
 * @enddot
 *
 * Rules (enforced here, not in the UI):
 *  - only DRAFT and VERIFIED versions accept new content; saving changed
 *    content into a VERIFIED version returns it to DRAFT (its validation no
 *    longer applies to the new hash);
 *  - PUBLISHED, SUPERSEDED and REJECTED versions are immutable;
 *  - publishing requires VERIFIED and supersedes the previously published version;
 *  - at most one open (DRAFT/VALIDATING/VERIFIED) version per artefact;
 *  - a new version is always created from an existing one (parent), so the
 *    lineage is a chain that history and diffs can follow.
 *
 * Nothing is ever deleted: historical packages and executions keep resolving
 * the exact versions (and bytes) they were built from.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/clock.hpp"
#include "twin/platform/database.hpp"
#include "twin/platform/object_store.hpp"

namespace twin::platform {

/// @brief Kind of formal artefact.
enum class ArtifactKind { Ontology, Interpretation, PtModel, DtModel };
/// @brief "ontology" / "interpretation" / "pt_model" / "dt_model".
[[nodiscard]] std::string_view to_string(ArtifactKind kind) noexcept;
/// @brief Inverse of to_string(ArtifactKind).
[[nodiscard]] Result<ArtifactKind> artifact_kind_from_string(std::string_view text);

/// @brief Lifecycle state of a version (see file documentation).
enum class Lifecycle { Draft, Validating, Verified, Published, Superseded, Rejected };
/// @brief "draft" / "validating" / "verified" / "published" / "superseded" / "rejected".
[[nodiscard]] std::string_view to_string(Lifecycle state) noexcept;
/// @brief Inverse of to_string(Lifecycle).
[[nodiscard]] Result<Lifecycle> lifecycle_from_string(std::string_view text);
/// @brief Whether the transition @p from → @p to is allowed.
[[nodiscard]] bool transition_allowed(Lifecycle from, Lifecycle to) noexcept;

/// @brief Reference to one version: "<artifact_id>@<version>".
struct ArtifactRef {
    std::string artifact_id;  ///< Artefact id.
    std::int64_t version{0};  ///< Version number (1-based).
    [[nodiscard]] std::string str() const { return artifact_id + "@" + std::to_string(version); }
    friend bool operator==(const ArtifactRef&, const ArtifactRef&) = default;
};
/// @brief Parse "id@n".
[[nodiscard]] Result<ArtifactRef> parse_ref(std::string_view text);

/// @brief Artefact identity.
struct Artifact {
    std::string id;           ///< Stable id (lowercase, digits, '-', '_', '.').
    ArtifactKind kind{ArtifactKind::Ontology};  ///< Kind.
    std::string name;         ///< Display name.
    std::string description;  ///< Description.
    std::string created_at;   ///< ISO 8601 UTC.
    std::string created_by;   ///< Actor.
};

/// @brief One version of an artefact.
struct ArtifactVersion {
    std::string artifact_id;               ///< Owning artefact.
    ArtifactKind kind{ArtifactKind::Ontology};  ///< Kind (denormalised for convenience).
    std::int64_t version{0};               ///< Version number.
    std::optional<std::int64_t> parent;    ///< Version it was derived from.
    Lifecycle state{Lifecycle::Draft};     ///< Lifecycle state.
    std::string content_sha256;            ///< Content hash (ObjectStore key).
    json::Json refs = json::Json::object();  ///< References, e.g. {"ontology":"process-pump@1"}.
    std::string change_description;        ///< Why this version exists.
    std::string created_at;                ///< ISO 8601 UTC.
    std::string created_by;                ///< Actor.
    std::string updated_at;                ///< Last content/state change.
    std::optional<std::string> published_at;  ///< When published.
    std::optional<std::string> published_by;  ///< Who published.
    [[nodiscard]] ArtifactRef ref() const { return {artifact_id, version}; }
    /// @brief DRAFT, VALIDATING or VERIFIED.
    [[nodiscard]] bool is_open() const noexcept;
};

/// @brief Repository of artefacts and versions (see file documentation).
class ArtifactRepository {
public:
    ArtifactRepository(Database& db, const ObjectStore& store, const Clock& clock)
        : db_(db), store_(store), clock_(clock) {}

    /// @brief Create an artefact with version 1 in state DRAFT.
    [[nodiscard]] Result<ArtifactVersion> create(ArtifactKind kind, std::string_view id, std::string_view name,
                                                 std::string_view description, std::string_view content,
                                                 const json::Json& refs, std::string_view actor,
                                                 std::string_view change_description);

    /// @brief One artefact.
    [[nodiscard]] Result<Artifact> get(std::string_view id) const;
    /// @brief All artefacts (optionally of one kind), ordered by id.
    [[nodiscard]] Result<std::vector<Artifact>> list(std::optional<ArtifactKind> kind = std::nullopt) const;
    /// @brief All versions of an artefact, newest first.
    [[nodiscard]] Result<std::vector<ArtifactVersion>> versions(std::string_view id) const;
    /// @brief One version.
    [[nodiscard]] Result<ArtifactVersion> version(const ArtifactRef& ref) const;
    /// @brief The currently PUBLISHED version, if any.
    [[nodiscard]] Result<std::optional<ArtifactVersion>> published(std::string_view id) const;
    /// @brief The open (DRAFT/VALIDATING/VERIFIED) version, if any.
    [[nodiscard]] Result<std::optional<ArtifactVersion>> open_version(std::string_view id) const;
    /// @brief Content bytes of a version (hash-verified).
    [[nodiscard]] Result<std::string> content(const ArtifactRef& ref) const;

    /// @brief New DRAFT version derived from @p from (content and refs copied).
    [[nodiscard]] Result<ArtifactVersion> create_draft(const ArtifactRef& from, std::string_view actor,
                                                       std::string_view change_description);

    /**
     * @brief Save new content (and optionally refs / description) into an open version.
     * @return StateError if the version is not DRAFT or VERIFIED.
     */
    [[nodiscard]] Result<ArtifactVersion> save(const ArtifactRef& ref, std::string_view content,
                                               const std::optional<json::Json>& refs,
                                               const std::optional<std::string>& change_description,
                                               std::string_view actor);

    /// @brief Change the lifecycle state (transition table enforced). Not for PUBLISHED: use publish().
    [[nodiscard]] Result<ArtifactVersion> set_state(const ArtifactRef& ref, Lifecycle to, std::string_view actor);

    /// @brief VERIFIED → PUBLISHED; the previous PUBLISHED version becomes SUPERSEDED.
    [[nodiscard]] Result<ArtifactVersion> publish(const ArtifactRef& ref, std::string_view actor);

private:
    [[nodiscard]] Result<std::vector<ArtifactVersion>> query_versions(std::string_view where, std::string_view a,
                                                                      std::optional<std::int64_t> b) const;
    Database& db_;
    const ObjectStore& store_;
    const Clock& clock_;
};

/// @brief JSON rendering (API representation) of an artefact.
[[nodiscard]] json::Json to_json(const Artifact& artifact);
/// @brief JSON rendering of a version.
[[nodiscard]] json::Json to_json(const ArtifactVersion& version);

}  // namespace twin::platform
