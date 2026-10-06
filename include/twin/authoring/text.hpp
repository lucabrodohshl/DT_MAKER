/**
 * @file text.hpp
 * @brief TwinTA: the textual form of the canonical model (lossless for notes; layout excluded).
 * @ingroup authoring
 *
 * Grammar (EBNF):
 * @code
 *   model      = "automaton" IDENT "{" { decl } "}"
 *   decl       = clockDecl | constDecl | chanDecl | locDecl | edgeDecl
 *   clockDecl  = "clock" IDENT { "," IDENT } ";"
 *   constDecl  = "const" [ "int" ] IDENT "=" [ "-" ] INT ";"
 *   chanDecl   = "channel" IDENT { "," IDENT } ";"
 *   locDecl    = [ "initial" ] "location" IDENT ( ";" | "{" { "invariant" constr ";" } "}" )
 *   edgeDecl   = "edge" [ ID ":" ] IDENT "->" IDENT ( ";" | "{" { edgeItem } "}" )
 *   edgeItem   = "guard" constr ";" | "sync" IDENT ( "!" | "?" ) ";"
 *              | "reset" reset { "," reset } ";"            reset = IDENT [ ":=" "0" ]
 *   constr     = "true" | atom { "&&" atom }
 *   atom       = IDENT [ "-" IDENT ] ( "<" | "<=" | "==" | ">=" | ">" ) ( [ "-" ] INT | IDENT )
 *   ID         = IDENT | STRING                              (edge ids that are not identifiers are quoted)
 * @endcode
 *
 * Comments (`// ...`, `/ * ... * /`) are notes:
 *  - a comment on the line where a declaration ends belongs to that declaration;
 *  - other comments belong to the next declaration, or to the enclosing
 *    automaton if no declaration follows;
 *  - any comment inside a location or edge block belongs to that location or edge;
 *  - comments before `automaton` or after its closing brace belong to the automaton.
 * Printing writes every note as `//` lines before its declaration, so
 * formatting (parse, then print) never drops a comment and never changes the
 * semantics. Constructs outside the supported fragment are syntax errors with
 * the compiler's explanation (TWT010–TWT014).
 *
 * Syntax codes: TWT001 unexpected token, TWT002 unterminated comment, TWT003
 * integer out of range, TWT004 unterminated string, TWT005 unexpected
 * character, TWT010 disjunction, TWT011 negation, TWT012 `false`, TWT013
 * non-zero reset, TWT014 construct outside the fragment (data variables,
 * urgency, broadcast).
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/authoring/diagnostics.hpp"
#include "twin/authoring/model.hpp"
#include "twin/core/result.hpp"

namespace twin::authoring {

/// @brief A declared name with the ranges of its definition and of every use.
struct Symbol {
    std::string kind;                     ///< "model", "clock", "constant", "channel", "location" or "edge".
    std::string name;                     ///< Name, or edge id.
    SourceRange definition;               ///< Where it is declared.
    std::vector<SourceRange> references;  ///< Uses, in source order.
};

/// @brief Outcome of parsing TwinTA text.
struct ParseResult {
    /// The model, present iff there is no syntax error (validation errors may remain).
    std::optional<Model> model;
    /// Syntax errors (TWT), or else the structural diagnostics of the model (TWM), all with ranges.
    std::vector<Diagnostic> diagnostics;
    /// Declared names with definition and reference ranges (for navigation and the outline).
    std::vector<Symbol> symbols;
};

/// @brief Parse TwinTA source; edges without an id get the smallest free `e<n>`.
[[nodiscard]] ParseResult parse_text(std::string_view source);

/// @brief The canonical TwinTA text of @p model (see file documentation).
[[nodiscard]] std::string print_text(const Model& model);

/// @brief Parse, then print; ParseError (with the first syntax error) if the text has syntax errors.
[[nodiscard]] Result<std::string> format_text(std::string_view source);

/**
 * @brief Parse a standalone constraint such as `t >= 30 && x - y < 3` (`true` or empty = no atoms).
 * @return the atoms, or ParseError naming the column of the problem.
 */
[[nodiscard]] Result<Constraint> parse_constraint(std::string_view text);

/// @brief Text of a constraint as TwinTA prints it ("true" for the empty conjunction).
[[nodiscard]] std::string constraint_text(const Constraint& constraint);

/// @brief API form of symbols: [{kind, name, definition, references}].
[[nodiscard]] json::Json to_json(const std::vector<Symbol>& symbols);

}  // namespace twin::authoring
