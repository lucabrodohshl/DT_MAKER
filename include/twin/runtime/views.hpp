/**
 * @file views.hpp
 * @brief JSON views of runtime state for the API (read-only renderings).
 * @ingroup runtime
 *
 * Views render kernel snapshots and package data for observers. They compute
 * nothing semantic: every value comes from the kernel (Snapshot) or the
 * verified package. Logical times are rendered both as exact integer ticks and
 * as exact decimal strings; JSON floating-point numbers are never used.
 */
#pragma once

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/ledger/record.hpp"
#include "twin/package/package.hpp"
#include "twin/runtime/session.hpp"

namespace twin::runtime {

/// @brief {"ticks": n, "text": "12.5"}
[[nodiscard]] json::Json time_view(Ticks t, TimeBase base);

/// @brief Committed state, propositions (with ontology meaning), enabled transitions, deadline.
[[nodiscard]] json::Json state_view(const TwinSession& session, const Snapshot& snapshot);

/// @brief Outgoing transitions with delay windows.
[[nodiscard]] json::Json enabled_view(const TwinSession& session, const Snapshot& snapshot);

/// @brief Propositions holding now, with their I_D interpretations.
[[nodiscard]] json::Json propositions_view(const TwinSession& session, const Snapshot& snapshot);

/// @brief Package identity, verification checks and alignment summary.
[[nodiscard]] json::Json package_view(const package::LoadedPackage& package);

/// @brief One accepted/rejected submission, for live observers.
[[nodiscard]] json::Json submission_view(const TwinSession& session, const ledger::Input& input,
                                         const SubmitResult& result);

/// @brief Bounded exploration results as trajectories.
[[nodiscard]] json::Json prediction_view(const TwinSession& session,
                                         const std::vector<kernel::ExplorationResult>& results);

/**
 * @brief Parse an API event request:
 *   {"label": "plan_accepted!" | "transition": "<id>", "time": "12.5" | "ticks": 12500, "source": "..."}
 * or an advance request {"time": ... | "ticks": ...} when @p advance is true.
 * @return InvalidArgument for floats or malformed requests, TimeNotRepresentable off the grid.
 */
[[nodiscard]] Result<ledger::Input> input_from_request(const json::Json& request, TimeBase base, bool advance);

}  // namespace twin::runtime
