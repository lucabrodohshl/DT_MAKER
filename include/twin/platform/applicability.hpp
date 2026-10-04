/**
 * @file applicability.hpp
 * @brief Is a piece of evidence still applicable to the artefacts a consumer uses? (VALID / STALE / INVALIDATED)
 * @ingroup platform
 *
 * Evidence is about the exact bytes it examined. Whether it still *applies*
 * depends on what a consumer (a package, a deployment, a change workspace)
 * uses now. This is computed on every request from the stored input hashes
 * and the consumer's bindings — never cached — so dependent evidence can
 * never "silently remain green" after an ontology, interpretation or model
 * changes.
 *
 * | State        | Meaning |
 * |--------------|---------|
 * | VALID        | every input role the consumer binds has the same content hash |
 * | STALE        | at least one bound role now has different content ("dependency changed") |
 * | INVALIDATED  | an input version was REJECTED, or its stored content no longer matches its hash |
 * | UNKNOWN      | the evidence shares no role with the consumer (not applicable / cannot judge) |
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "twin/platform/artifacts.hpp"
#include "twin/platform/evidence.hpp"
#include "twin/platform/twins.hpp"

namespace twin::platform {

/// @brief Applicability state (see file documentation).
enum class Applicability { Valid, Stale, Invalidated, Unknown };
/// @brief "valid" / "stale" / "invalidated" / "unknown".
[[nodiscard]] std::string_view to_string(Applicability a) noexcept;

/// @brief One input whose content differs from the consumer's binding.
struct ChangedDependency {
    std::string role;          ///< Role, e.g. "ontology".
    std::string evidence_ref;  ///< Version the evidence examined ("process-pump@1").
    std::string evidence_sha256;  ///< Hash it examined.
    std::string current_ref;   ///< Version the consumer binds now.
    std::string current_sha256;   ///< Hash bound now.
};

/// @brief Applicability with its structured explanation ("Why is this stale?").
struct ApplicabilityReport {
    Applicability state{Applicability::Unknown};  ///< State.
    std::vector<ChangedDependency> changed;       ///< Why STALE.
    std::vector<std::string> reasons;             ///< Human-readable reasons (all states).
};

/**
 * @brief Judge @p evidence against @p bindings (role → version + hash).
 *
 * Only input roles that exist in @p bindings are compared (refinement evidence
 * uses roles such as "base_ontology", which a deployment does not bind).
 * @p artifacts is consulted to detect REJECTED input versions.
 */
[[nodiscard]] Result<ApplicabilityReport> applicability(const EvidenceRecord& evidence,
                                                        const std::vector<Binding>& bindings,
                                                        const ArtifactRepository& artifacts);

/// @brief API representation.
[[nodiscard]] json::Json to_json(const ApplicabilityReport& report);

}  // namespace twin::platform
