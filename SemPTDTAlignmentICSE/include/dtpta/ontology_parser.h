#ifndef DTPTA_ONTOLOGY_PARSER_H
#define DTPTA_ONTOLOGY_PARSER_H

#include <string>
#include <memory>
#include "dtpta/ontology.h"
#include "dtpta/interpretation.h"

namespace dtpta {

/**
 * @class OntologyParser
 * @brief Parses an ontology + interpretation specification from a JSON file.
 *
 * Expected JSON format (all fields optional):
 * @code
 * {
 *   "sorts": ["Energy", "Hectare"],
 *   "functions": [
 *     {"name": "battery_capacity", "args": [], "return": "Int"},
 *     {"name": "consumption",      "args": ["Int"], "return": "Int"}
 *   ],
 *   "relations": [
 *     {"name": "is_valid", "args": ["Int"]}
 *   ],
 *   "axioms": [
 *     "(= battery_capacity 500)",
 *     "(forall ((h Int)) (>= (consumption h) 0))"
 *   ],
 *   "label_interpretations": {
 *     "PT": {
 *       "spray":   "(<= (consumption current_hectare) 50)",
 *       "shutoff": "(<= total_consumption battery_capacity)"
 *     },
 *     "DT": {
 *       "apply":      "(>= (- battery_capacity (+ total_consumption (consumption current_hectare))) (- battery_capacity 50))",
 *       "power_down": "(>= (- battery_capacity total_consumption) 0)"
 *     }
 *   },
 *   "state_interpretations": {
 *     "PT": { "Idle": "true", "Spraying": "(<= total_consumption battery_capacity)" },
 *     "DT": { "Idle": "true", "Working":  "(<= total_consumption battery_capacity)" }
 *   }
 * }
 * @endcode
 *
 * Axioms and formula strings are in SMT-LIB2 expression syntax and are parsed
 * via z3::context::parse_string() with the declared sorts and functions loaded
 * as context declarations.
 *
 * If the JSON file does not exist or is empty, an identity DomainKnowledge
 * (empty ontology, trivial interpretations) is returned.
 */
class OntologyParser {
public:
    OntologyParser() = default;

    /**
     * @brief Parse a JSON ontology file and produce a DomainKnowledge object.
     *
     * @param json_path Path to the JSON file.
     * @return A fully initialised DomainKnowledge (Ontology + PT + DT maps).
     * @throws std::runtime_error if the JSON is malformed or a formula fails to parse.
     */
    DomainKnowledge parse(const std::string& json_path);

    /**
     * @brief Construct a trivial (identity) DomainKnowledge with no axioms
     *        and no label interpretations.  Useful for syntactic baseline checks.
     */
    static DomainKnowledge make_trivial();
};

} // namespace dtpta

#endif // DTPTA_ONTOLOGY_PARSER_H
