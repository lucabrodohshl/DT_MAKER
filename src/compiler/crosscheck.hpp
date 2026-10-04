/**
 * @file crosscheck.hpp
 * @brief Internal: translation validation against the aligner, and determinism analysis.
 */
#pragma once

#include <filesystem>

#include "source_model.hpp"
#include "twin/compiler/compiler.hpp"

namespace twin::compiler::detail {

/**
 * @brief Compare the compiler's reading of @p source with the aligner's.
 *
 * Builds dtpta::TimedAutomaton from the same file and checks, element by
 * element, that both readings describe the same automaton: template name,
 * clock numbering, location names and order, invariants and guards (compared
 * as closed DBM zones, i.e. semantically), resets, actions and edge order.
 */
[[nodiscard]] TranslationValidation crosscheck_with_aligner(const std::filesystem::path& source,
                                                            const SourceModel& model);

/**
 * @brief Conservative event-determinism analysis.
 *
 * A model is reported event-deterministic iff for every location l and every
 * observable label a, any two a-transitions leaving l have guards that are
 * disjoint within Inv(l) (checked exactly with DBMs). Target invariants are
 * ignored, so "not deterministic" may be reported for models that are.
 */
[[nodiscard]] DeterminismReport analyse_determinism(const SourceModel& model,
                                                    const std::vector<std::string>& transition_ids);

}  // namespace twin::compiler::detail
