/**
 * @file evidence.hpp
 * @brief Persistent formal evidence: validation, refinement, alignment, compilation and package checks.
 * @ingroup platform
 *
 * Every significant formal check produces an **evidence record**:
 *
 *  - the full machine-readable result document (canonical JSON, stored in the
 *    ObjectStore under its SHA-256 — the *evidence hash*);
 *  - the exact inputs it was computed from (role → artefact version + content hash);
 *  - the checker identity, timestamp and actor;
 *  - a normalised **outcome** (pass / fail / unknown / error) next to the
 *    check-specific verdict (e.g. "valid_refinement").
 *
 * Records are immutable and never overwritten: a re-run creates a new record,
 * so verification history is complete. Whether a record is still *applicable*
 * (VALID / STALE / ...) is not stored; it is computed against the artefacts a
 * consumer uses (impact.hpp), so a cache can never make stale evidence look current.
 */
#pragma once

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
#include "twin/platform/object_store.hpp"

namespace twin::platform {

/// @brief Kind of formal check.
enum class EvidenceKind { Validation, Refinement, Alignment, Compilation, Package };
/// @brief "validation" / "refinement" / "alignment" / "compilation" / "package".
[[nodiscard]] std::string_view to_string(EvidenceKind kind) noexcept;
/// @brief Inverse of to_string(EvidenceKind).
[[nodiscard]] Result<EvidenceKind> evidence_kind_from_string(std::string_view text);

/// @brief Normalised outcome of a check.
enum class Outcome { Pass, Fail, Unknown, Error };
/// @brief "pass" / "fail" / "unknown" / "error".
[[nodiscard]] std::string_view to_string(Outcome outcome) noexcept;
/// @brief Inverse of to_string(Outcome).
[[nodiscard]] Result<Outcome> outcome_from_string(std::string_view text);

/// @brief One input of a check: which artefact version (and bytes) it examined, in which role.
struct EvidenceInput {
    std::string role;     ///< e.g. "ontology", "base_ontology", "dt_interpretation", "dt_model".
    ArtifactRef ref;      ///< Artefact version.
    std::string sha256;   ///< Content hash examined (copied, so later edits cannot change it).
};

/// @brief An evidence record (see file documentation).
struct EvidenceRecord {
    std::string id;                 ///< "EV-0001".
    EvidenceKind kind{EvidenceKind::Validation};  ///< Kind.
    Outcome outcome{Outcome::Unknown};            ///< Normalised outcome.
    std::string verdict;            ///< Check-specific verdict.
    std::string summary;            ///< One-sentence explanation.
    std::string checker;            ///< Checker identity (tool + versions).
    std::string document_sha256;    ///< Evidence hash (ObjectStore key of the document).
    std::string created_at;         ///< ISO 8601 UTC.
    std::string created_by;         ///< Actor.
    std::optional<std::string> change_id;  ///< Change workspace, if run from one.
    std::vector<EvidenceInput> inputs;     ///< Inputs, sorted by role.

    /// @brief The input with @p role, if any.
    [[nodiscard]] const EvidenceInput* input(std::string_view role) const noexcept;
};

/// @brief Query filter for EvidenceRepository::list.
struct EvidenceFilter {
    std::optional<EvidenceKind> kind;           ///< Only this kind.
    std::optional<std::string> artifact_id;     ///< Only evidence with an input of this artefact.
    std::optional<std::int64_t> version;        ///< ... and this version (with artifact_id).
    std::optional<std::string> change_id;       ///< Only evidence of this change.
    std::int64_t limit{100};                    ///< Page size.
    std::int64_t offset{0};                     ///< Page offset.
};

/// @brief Evidence store (see file documentation).
class EvidenceRepository {
public:
    EvidenceRepository(Database& db, const ObjectStore& store, const Clock& clock)
        : db_(db), store_(store), clock_(clock) {}

    /// @brief Persist a new record; @p document must be canonical-safe JSON (no floats).
    [[nodiscard]] Result<EvidenceRecord> record(EvidenceKind kind, Outcome outcome, std::string_view verdict,
                                                std::string_view summary, std::string_view checker,
                                                const json::Json& document, std::vector<EvidenceInput> inputs,
                                                std::string_view actor,
                                                const std::optional<std::string>& change_id = std::nullopt);

    /// @brief One record.
    [[nodiscard]] Result<EvidenceRecord> get(std::string_view id) const;
    /// @brief The record's document (hash-verified).
    [[nodiscard]] Result<json::Json> document(const EvidenceRecord& record) const;
    /// @brief Records matching @p filter, newest first.
    [[nodiscard]] Result<std::vector<EvidenceRecord>> list(const EvidenceFilter& filter) const;
    /// @brief Number of records matching @p filter (ignoring limit/offset).
    [[nodiscard]] Result<std::int64_t> count(const EvidenceFilter& filter) const;

    /**
     * @brief Newest record of @p kind whose inputs are exactly @p inputs (same roles, same hashes).
     * This is how "is there evidence for *these exact* artefacts?" is answered.
     */
    [[nodiscard]] Result<std::optional<EvidenceRecord>> latest_for(EvidenceKind kind,
                                                                   const std::vector<EvidenceInput>& inputs) const;

private:
    [[nodiscard]] Result<std::vector<EvidenceInput>> inputs_of(std::string_view id) const;
    Database& db_;
    const ObjectStore& store_;
    const Clock& clock_;
};

/// @brief API representation of a record (without the document).
[[nodiscard]] json::Json to_json(const EvidenceRecord& record);

}  // namespace twin::platform
