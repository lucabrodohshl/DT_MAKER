/**
 * @file uppaal.hpp
 * @brief UPPAAL renderings of a canonical model and its semantic digest.
 * @ingroup authoring
 *
 * Two renderings of the same model:
 *  - the **toolchain rendering** is what the compiler and the unmodified aligner
 *    read: deterministic bytes, notes and layout excluded, the initial location
 *    first (the aligner starts from the first location, TWC034), one template
 *    and the generated system line. Its SHA-256 is the **semantic digest**: two
 *    models with equal digests are byte-identical inputs to every formal tool,
 *    so evidence bound to the digest stays valid when only notes or the layout change;
 *  - the **exchange rendering** adds the diagram layout (coordinates, nails,
 *    label anchors) and the notes (comments labels and declaration comments)
 *    for UPPAAL users. Its semantics are those of the toolchain rendering
 *    (checked by tests and by export_model()).
 */
#pragma once

#include <string>

#include "twin/authoring/layout.hpp"
#include "twin/authoring/model.hpp"

namespace twin::authoring {

/// @brief The toolchain rendering (see file documentation). @p model should be valid (validate()).
[[nodiscard]] std::string render_toolchain_xml(const Model& model);

/// @brief The exchange rendering: the toolchain rendering plus layout and notes.
[[nodiscard]] std::string render_exchange_xml(const Model& model, const Layout& layout);

/// @brief SHA-256 of render_toolchain_xml(@p model): what alignment evidence and packages bind.
[[nodiscard]] std::string semantic_digest(const Model& model);

}  // namespace twin::authoring
