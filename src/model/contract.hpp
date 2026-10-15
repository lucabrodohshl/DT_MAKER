// Internal: derivation of the Blueprint sections from the model (04-compilation.md, section 4.6).
#pragma once

#include "behavior.hpp"
#include "resolve.hpp"
#include "twin/json/canonical.hpp"

namespace twin::model::detail {

/// {structure, data, presentation, assurance} as twin-blueprint/1 sections (read-only while a model section exists).
[[nodiscard]] json::Json derive_sections(const Resolved& model, const BehaviorOutput& behavior);

/// Blueprint asset id of an instance (the model name for the root, the "__"-joined part path otherwise).
[[nodiscard]] std::string asset_id(const Resolved& model, const Instance& instance);

}  // namespace twin::model::detail
