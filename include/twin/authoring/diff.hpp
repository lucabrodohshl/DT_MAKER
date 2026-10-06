/**
 * @file diff.hpp
 * @brief Structural difference of two canonical models (release comparison, change review).
 * @ingroup authoring
 */
#pragma once

#include "twin/authoring/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::authoring {

/**
 * @brief Differences from @p from to @p to.
 *
 * @code
 * { "semanticChange": bool,                       // semantic digests differ
 *   "digests": {"from", "to"},
 *   "name": {"from","to"}?,
 *   "clocks"/"channels": {"added":[...], "removed":[...]},
 *   "constants": {"added", "removed", "changed":[{"name","from","to"}]},
 *   "locations": {"added", "removed", "changed":[{"name", "initial"?:{from,to}, "invariant"?:{from,to}}]},
 *   "edges": {"added", "removed", "changed":[{"id", "fields":{field:{from,to}}}]},   // matched by edge id
 *   "notes": [{"element":{kind,name}, "from", "to"}] }
 * @endcode
 */
[[nodiscard]] json::Json diff_models(const Model& from, const Model& to);

}  // namespace twin::authoring
