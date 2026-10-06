/**
 * @file validate.hpp
 * @brief Structural validation of canonical models (VALID, never VERIFIED).
 * @ingroup authoring
 *
 * | Code   | Severity | Rule |
 * |--------|----------|------|
 * | TWM001 | error    | names are identifiers ([A-Za-z_][A-Za-z0-9_]*) and not UPPAAL keywords |
 * | TWM002 | error    | clocks, constants, channels, locations and the model name share one namespace |
 * | TWM003 | error    | exactly one initial location (none found) |
 * | TWM004 | error    | exactly one initial location (several found) |
 * | TWM005 | error    | edge endpoints are declared locations |
 * | TWM006 | error    | constrained and reset clocks are declared |
 * | TWM007 | error    | constant bounds name declared constants |
 * | TWM008 | error    | synchronisations use declared channels |
 * | TWM009 | error    | no diagonal constraint in a guard (the aligner drops them) |
 * | TWM010 | error    | bounds within UDBM's range (the aligner reads larger ones as infinity) |
 * | TWM011 | error    | edge ids are unique and well formed |
 * | TWM012 | error    | at least one location |
 * | TWM013 | warning  | a clock reset twice on one edge |
 * | TWM020 | warning  | unused clock |
 * | TWM021 | warning  | unused channel |
 * | TWM022 | warning  | unused constant |
 * | TWM023 | warning  | location not reachable in the edge graph (guards ignored) |
 * | TWM024 | warning  | receive label `a?`: interpretations cannot interpret it (alignment lint TWA011) |
 *
 * The rules are exactly those that make the toolchain rendering a document the
 * strict compiler reader accepts; they never claim any behavioural property.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "twin/authoring/diagnostics.hpp"
#include "twin/authoring/model.hpp"

namespace twin::authoring {

/// @brief All structural diagnostics of @p model (empty = valid and clean).
[[nodiscard]] std::vector<Diagnostic> validate(const Model& model);

/// @brief Whether @p name is a well-formed identifier that is not a UPPAAL keyword.
[[nodiscard]] bool is_identifier(std::string_view name);

/// @brief Whether @p id is a well-formed edge id ([A-Za-z0-9_.-]+).
[[nodiscard]] bool is_edge_id(std::string_view id);

/// @brief Largest absolute bound the toolchain accepts (strictly below UDBM's infinity).
[[nodiscard]] std::int64_t max_bound() noexcept;

}  // namespace twin::authoring
