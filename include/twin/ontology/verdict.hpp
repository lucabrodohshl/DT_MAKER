/**
 * @file verdict.hpp
 * @brief Three-valued solver verdicts and solver configuration shared by the ontology services.
 * @ingroup ontology
 */
#pragma once

#include <string>
#include <string_view>

namespace twin::ontology {

/**
 * @brief A three-valued answer of the SMT solver.
 *
 * Unknown means the solver could not decide (timeout, incompleteness of the
 * logic fragment). It is never to be reported as False.
 */
enum class Verdict3 { True, False, Unknown };

/// @brief "true" / "false" / "unknown".
[[nodiscard]] std::string_view to_string(Verdict3 v) noexcept;

/// @brief Solver limits applied to every individual query.
struct SolverConfig {
    unsigned timeout_ms{10000};  ///< Per-query timeout; exceeding it yields Verdict3::Unknown.
};

/// @brief Version of the ontology services (recorded in every evidence document).
inline constexpr std::string_view kOntologyServicesVersion = "twin-ontology/1";

/**
 * @brief Identity of the formal checker used for ontology evidence:
 * Z3 version, digest of the aligner sources (parsers + Ontology), and
 * kOntologyServicesVersion. Recorded in validation and refinement evidence.
 */
[[nodiscard]] std::string checker_identity();

}  // namespace twin::ontology
