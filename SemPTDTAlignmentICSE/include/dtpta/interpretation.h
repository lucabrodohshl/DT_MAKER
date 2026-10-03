#ifndef DTPTA_INTERPRETATION_H
#define DTPTA_INTERPRETATION_H

#include <string>
#include <map>
#include <memory>
#include <optional>
#include "z3++.h"
#include "dtpta/ontology.h"

namespace dtpta {

/**
 * @class InterpretationMap
 * @brief Represents a partial interpretation I: L ∪ ΣE → FOL formulas (Definition 6).
 *
 * Maps:
 *  - Location names (strings) to Boolean Z3 formulas representing the semantic
 *    meaning of the automaton being in that location.
 *  - Event/action labels (strings) to Boolean Z3 formulas representing what
 *    the event semantically means.
 *
 * All formulas must be Boolean expressions from the same Z3 context.
 * Absent entries are treated as the trivially-true formula (⊤).
 */
class InterpretationMap {
public:
    /** Construct an empty interpretation map. */
    InterpretationMap() = default;

    // ------------------------------------------------------------------ //
    //  Mutators
    // ------------------------------------------------------------------ //

    /**
     * @brief Assign a semantic formula to a location.
     * @param location_name Location name as used in the UPPAAL XML model.
     * @param formula       Boolean Z3 expression (must share context with the Ontology).
     */
    void set_state(const std::string& location_name, const z3::expr& formula);

    /**
     * @brief Assign a semantic formula to an action/event label.
     * @param label   Action label (e.g. "spray", "apply").
     * @param formula Boolean Z3 expression.
     */
    void set_event(const std::string& label, const z3::expr& formula);

    // ------------------------------------------------------------------ //
    //  Accessors
    // ------------------------------------------------------------------ //

    /**
     * @brief Look up the formula for a location.
     * @return The registered formula, or std::nullopt if the location has no entry.
     */
    std::optional<z3::expr> get_state(const std::string& location_name) const;

    /**
     * @brief Look up the formula for an event label.
     * @return The registered formula, or std::nullopt if the label has no entry.
     */
    std::optional<z3::expr> get_event(const std::string& label) const;

    /**
     * @brief Return true if the location has an explicit formula.
     */
    bool has_state(const std::string& location_name) const;

    /**
     * @brief Return true if the action label has an explicit formula.
     */
    bool has_event(const std::string& label) const;

    /**
     * @brief Return all registered action-label keys.
     */
    std::vector<std::string> event_labels() const;

    /**
     * @brief Return all registered location-name keys.
     */
    std::vector<std::string> state_names() const;

private:
    // Use parallel vectors for name and expr to avoid default-constructor issues
    // with z3::expr in std::map::operator[].
    std::vector<std::pair<std::string, z3::expr>> state_entries_;
    std::vector<std::pair<std::string, z3::expr>> event_entries_;
};

// -------------------------------------------------------------------------- //

/**
 * @struct DomainKnowledge
 * @brief Bundles an ontology with the PT and DT interpretation maps (Definition 6).
 *
 * The ontology owns the Z3 context; all expressions in the interpretation maps
 * must come from that same context.
 */
struct DomainKnowledge {
    std::shared_ptr<Ontology> ontology;  ///< The shared FOL ontology (owns Z3 context)
    InterpretationMap         pt_interp; ///< Interpretation IP for the Physical Twin
    InterpretationMap         dt_interp; ///< Interpretation ID for the Digital Twin

    /**
     * @brief Construct DomainKnowledge with the given ontology.
     * @param ont Shared pointer to an already-built ontology.
     */
    explicit DomainKnowledge(std::shared_ptr<Ontology> ont)
        : ontology(std::move(ont)) {}
};

} // namespace dtpta

#endif // DTPTA_INTERPRETATION_H
