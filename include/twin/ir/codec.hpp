/**
 * @file codec.hpp
 * @brief Canonical JSON encoding of the Twin IR (format "twin-ir/1").
 * @ingroup ir
 *
 * Document layout (all members required unless marked optional):
 * @code{.json}
 * {
 *   "format": "twin-ir/1",
 *   "model": {"id": "...", "version": "...", "source_template": "...", "source_sha256": "..."},
 *   "time": {"ticks_per_unit": 1000},
 *   "clocks": ["t_flight", "t_mode"],
 *   "channels": ["mission_loaded", "..."],
 *   "locations": [{"id": "READY", "invariant": [<constraint>...], "layout": {"x": 0, "y": 0}}],
 *   "initial": "INITIALIZING",
 *   "transitions": [{"id": "READY.start_mission!.TAKING_OFF", "source": "READY",
 *                    "target": "TAKING_OFF", "action": {"kind": "send", "channel": "start_mission"},
 *                    "guard": [<constraint>...], "resets": ["t_flight", "t_mode"]}],
 *   "propositions": [{"id": "at(READY)", "location": "READY", "interpretation": "..."}],
 *   "event_interpretations": [{"label": "start_mission!", "formula": "..."}]
 * }
 * <constraint> := {"clock": "x", "op": "<=", "bound": 5}            (x <= 5)
 *              |  {"clock": "x", "minus": "y", "op": "<", "bound": 3} (x - y < 3)
 * @endcode
 * `layout` is optional and non-semantic. Clocks, channels, locations and
 * transitions are referenced by name/id, never by position, so the document is
 * readable and diffable. The canonical text (twin/json/canonical.hpp) is what
 * is hashed: IR hash = SHA-256(canonical text).
 */
#pragma once

#include <string>
#include <string_view>

#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::ir {

/// @brief Encode a model as a JSON value (does not validate).
[[nodiscard]] json::Json to_json(const Model& model);

/// @brief Decode and validate a model from a JSON value.
[[nodiscard]] Result<Model> from_json(const json::Json& document);

/// @brief Canonical text of a (validated) model.
[[nodiscard]] Result<std::string> to_canonical_text(const Model& model);

/**
 * @brief Decode a model from text that must already be canonical.
 *
 * In addition to validation, the decoded model is re-encoded and compared with
 * the input bytes, so any information lost or altered by decoding is detected.
 */
[[nodiscard]] Result<Model> from_canonical_text(std::string_view text);

/// @brief SHA-256 (hex) of the canonical text of @p model.
[[nodiscard]] Result<std::string> ir_sha256(const Model& model);

}  // namespace twin::ir
