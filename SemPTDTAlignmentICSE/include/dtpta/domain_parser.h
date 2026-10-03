#ifndef DTPTA_DOMAIN_PARSER_H
#define DTPTA_DOMAIN_PARSER_H

/**
 * @file dtpta/domain_parser.h
 * @brief Parsers for the custom .ont and .interp text file formats used by
 *        the CS1 Three-Tank benchmark.
 *
 * These parsers complement dtpta::OntologyParser (which reads JSON) with
 * support for the human-readable domain description formats defined in the
 * CS1 benchmark assets.
 *
 * .ont format:
 *   ; comment
 *   sort  Name
 *   fun   name : [Arg ...] -> Ret   (Ret defaults to Real if unknown sort)
 *   fun   name : Ret                (nullary constant)
 *   rel   name : [Arg ...]          (predicate, returns Bool)
 *   rel   name :                    (0-ary proposition)
 *   axiom id   : smt2-expr
 *
 * All user-declared sorts are mapped to the SMT-LIB2 Real sort so that
 * numeric literals in axioms type-check without coercions.
 *
 * .interp format:
 *   ; comment
 *   LocationName   : smt2-expr
 *   event_label!   : smt2-expr
 *
 * C-style function application "f(n)" is auto-converted to "(f n)".
 */

#include "dtpta/ontology.h"
#include "dtpta/interpretation.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace dtpta {

/**
 * @class OntFileParser
 * @brief Parses a .ont domain-ontology file into an dtpta::Ontology.
 *
 * Usage:
 * @code
 *   dtpta::OntFileParser p;
 *   auto ontology = p.parse("domain.ont");
 * @endcode
 */
class OntFileParser {
public:
    OntFileParser() = default;

    /**
     * @brief Parse a .ont file and return a populated Ontology.
     * @throws std::runtime_error on file or parse errors.
     */
    std::shared_ptr<Ontology> parse(const std::string& ont_path);
};

/**
 * @class InterpFileParser
 * @brief Parses a .interp interpretation file into an dtpta::InterpretationMap.
 *
 * Requires the Ontology to have been fully built first, so that Z3 sort and
 * function declarations are available for formula parsing.
 *
 * Usage:
 * @code
 *   dtpta::InterpFileParser p;
 *   auto imap = p.parse("pt.interp", ontology);
 * @endcode
 */
class InterpFileParser {
public:
    InterpFileParser() = default;

    /**
     * @brief Parse a .interp file against an already-built Ontology.
     * @param interp_path  Path to the .interp file.
     * @param ontology     Shared ontology providing the Z3 context.
     * @return Populated InterpretationMap (Z3 expressions, not raw strings).
     * @throws std::runtime_error on file or Z3 parse errors.
     */
    InterpretationMap parse(const std::string& interp_path,
                            std::shared_ptr<Ontology> ontology);
};

} // namespace dtpta

#endif // DTPTA_DOMAIN_PARSER_H
