/**
 * @file text_lexer.hpp
 * @brief Internal: tokens of the TwinTA text format, with 1-based positions.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "twin/authoring/diagnostics.hpp"

namespace twin::authoring::detail {

/// @brief Token kinds.
enum class Tok {
    Ident,    ///< Identifier or keyword (keywords are recognised by the parser).
    Int,      ///< Unsigned integer literal.
    String,   ///< Double-quoted string (edge ids); text is the unescaped content.
    Punct,    ///< Punctuation: { } ; , : -> - ! ? && || < <= == >= > = := ( )
    Comment,  ///< `//` or block comment; text is the normalised content (lines joined by '\n').
    End       ///< End of input.
};

/// @brief One token.
struct Token {
    Tok kind{Tok::End};  ///< Kind.
    std::string text;    ///< Spelling (or normalised content for comments and strings).
    SourceRange range;   ///< Position (end exclusive).
};

/// @brief Result of lexing: tokens (always ending with End) and lexical errors.
struct LexResult {
    std::vector<Token> tokens;            ///< Tokens including comments.
    std::vector<Diagnostic> diagnostics;  ///< TWT002, TWT004, TWT005.
};

/// @brief Split @p source into tokens. Columns count Unicode code points.
[[nodiscard]] LexResult lex(std::string_view source);

}  // namespace twin::authoring::detail
