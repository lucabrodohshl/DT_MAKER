/**
 * @file audit.hpp
 * @brief Engineering change audit trail: append-only and hash-chained (tamper-evident).
 * @ingroup platform
 *
 * The engineering audit records *artefact and lifecycle* operations (draft
 * created, ontology saved, validation/refinement/alignment run, package built,
 * release, deployment, rollback). It is deliberately separate from the
 * runtime's **execution ledger**, which records semantic behaviour, and from
 * application logs, which are diagnostics.
 *
 * Each record carries `hash = SHA-256(prev_hash || "\n" || canonical(record))`
 * where canonical(record) is the canonical JSON of every field except `hash`.
 * verify() recomputes the chain and reports the first record whose content,
 * order or link does not match. This is *tamper-evident*, not tamper-proof:
 * someone with write access to the database can rewrite the whole chain; the
 * head hash can be exported and anchored elsewhere to detect that.
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

namespace twin::platform {

/// @brief One audit record.
struct AuditRecord {
    std::int64_t seq{0};         ///< 1-based sequence number.
    std::string at;              ///< ISO 8601 UTC.
    std::string actor;           ///< Who.
    std::string operation;       ///< e.g. "ontology.draft.create", "refinement.run", "deployment.rollback".
    std::string outcome;         ///< "success" | "failure" | a check outcome ("pass", "fail", ...).
    std::string subject;         ///< Primary subject, e.g. "process-pump@2", "CHG-0003", "pump-p101".
    json::Json details;          ///< Artefact refs, hashes, evidence ids, reason, ... (canonical-safe).
    std::string prev_hash;       ///< Hash of the previous record (64 zeros for the first).
    std::string hash;            ///< This record's hash.
};

/// @brief Result of verifying the chain.
struct AuditVerification {
    bool valid{false};                          ///< Whole chain verified.
    std::int64_t records{0};                    ///< Number of records examined.
    std::optional<std::int64_t> first_invalid;  ///< First bad sequence number.
    std::string reason;                         ///< Why it is invalid.
    std::string head_hash;                      ///< Hash of the last record (for anchoring).
    std::string verified_at;                    ///< When this verification ran.
};

/// @brief Filter for listing.
struct AuditFilter {
    std::optional<std::string> operation_prefix;  ///< e.g. "ontology." .
    std::optional<std::string> subject;           ///< Exact subject or prefix "id@" match.
    std::optional<std::string> actor;             ///< Exact actor.
    std::int64_t limit{100};                      ///< Page size.
    std::int64_t offset{0};                       ///< Page offset.
};

/// @brief The audit log (see file documentation).
class AuditLog {
public:
    /// @brief Audit trail stored in @p db; @p clock stamps records.
    AuditLog(Database& db, const Clock& clock) : db_(db), clock_(clock) {}

    /// @brief Append a record. @p details must not contain floating-point numbers.
    [[nodiscard]] Result<AuditRecord> append(std::string_view operation, std::string_view outcome,
                                             std::string_view subject, const json::Json& details,
                                             std::string_view actor);
    /// @brief Records matching @p filter, newest first.
    [[nodiscard]] Result<std::vector<AuditRecord>> list(const AuditFilter& filter) const;
    /// @brief Total number of records matching @p filter.
    [[nodiscard]] Result<std::int64_t> count(const AuditFilter& filter) const;
    /// @brief Recompute and check the whole chain.
    [[nodiscard]] Result<AuditVerification> verify() const;

    /// @brief The canonical text hashed for @p record (exposed for tests and external verifiers).
    [[nodiscard]] static Result<std::string> canonical_payload(const AuditRecord& record);

private:
    Database& db_;
    const Clock& clock_;
};

/// @brief API representation.
[[nodiscard]] json::Json to_json(const AuditRecord& record);
/// @brief API representation.
[[nodiscard]] json::Json to_json(const AuditVerification& verification);

}  // namespace twin::platform
