/**
 * @file lift.hpp
 * @brief Lifting: from a timed automaton (with its ontology and interpretation) to the modelling language.
 * @ingroup model
 *
 * lift() is the inverse of the behaviour and domain-theory compilers
 * (03-metamodel.md, section 3.9). Every automaton of the supported fragment
 * lifts; the result is a model with one machine on the root type whose
 * compilation is the given automaton up to the order of edges and
 * declarations, edge ids and notes (same_automaton()).
 *
 * Without an interpretation every channel becomes an uninterpreted reported
 * event (as invisible to the aligner as it was). With the formats this
 * compiler generates (`ℓ_requested`, `ℓ_reported`, `ℓ_elapsed`, `<State>_timeout`)
 * actions and timeouts are recovered, so lift(compile(M)) gives M back for
 * single-machine models.
 */
#pragma once

#include <string>
#include <vector>

#include "twin/authoring/model.hpp"
#include "twin/json/canonical.hpp"
#include "twin/model/document.hpp"

namespace twin::model {

/// @brief What to lift.
struct LiftInput {
    authoring::Model automaton;   ///< The timed automaton (twin-ta/1).
    std::string ontology;         ///< Its domain theory (.ont text), optional.
    std::string interpretation;   ///< Its interpretation (.interp text), optional.
    std::string name;             ///< Model name ("" = derived from the automaton name).
    std::string label;            ///< Display name (optional).
};

/// @brief Result of lifting.
struct Lifted {
    std::optional<Document> document;      ///< The model, absent on error.
    std::vector<Diagnostic> diagnostics;   ///< Errors (unliftable names) and notes (what was kept verbatim).
};

/// @brief Lift an automaton (and optionally its ontology and interpretation) into a model.
[[nodiscard]] Lifted lift(const LiftInput& input);

/**
 * @brief A physical reference for @p model with the mapping suggested by equal names.
 *
 * A reference location maps to the state of the model with the same name
 * (case-insensitive; root behaviour first, then the parts in order); a
 * reference channel maps to the event or action with the same name. Nothing
 * else is guessed: unmatched entries stay unmapped and the compiler reports
 * them (TML070) until the user maps them.
 */
[[nodiscard]] Reference reference_suggestion(const Document& model, const authoring::Model& automaton);

/**
 * @brief Whether two automata are the same up to the order of edges and declarations, edge ids and notes.
 * @param why Receives the first difference found (may be nullptr).
 */
[[nodiscard]] bool same_automaton(const authoring::Model& a, const authoring::Model& b, std::string* why = nullptr);

}  // namespace twin::model
