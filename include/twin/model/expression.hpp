/**
 * @file expression.hpp
 * @brief The expression language of conditions, meanings and assumptions, and its SMT-LIB2 translation.
 * @ingroup model
 *
 * Grammar (03-metamodel.md, section 3.5):
 * @code
 *   expr    := or
 *   or      := and ( "or" and )*
 *   and     := not ( "and" not )*
 *   not     := "not" not | cmp
 *   cmp     := sum ( ( "<" | "<=" | ">" | ">=" | "==" | "!=" ) sum )?
 *   sum     := product ( ( "+" | "-" ) product )*
 *   product := unary ( "*" unary )*
 *   unary   := "-" unary | atom
 *   atom    := NUMBER | "true" | "false" | path | "(" expr ")"
 *   path    := IDENT ( "." IDENT )*
 * @endcode
 * A text starting with `smt:` is raw SMT-LIB2 (advanced; produced by lifting
 * for formulas outside the grammar) and is passed through verbatim.
 *
 * Translation (04-compilation.md, section 4.4) targets the QF_LRA fragment the
 * aligner decides: n-ary `and`/`or`/`+`/`-`/`*` for chains, `(not (= a b))`
 * for `!=`, exact decimals with negatives written `(- n)`.
 * from_smt() is the inverse used by lifting: to_smt(from_smt(s)) reproduces s
 * up to whitespace for every s inside the grammar.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace twin::model::expr {

/// @brief An expression node.
struct Node {
    /// @brief Node kind.
    enum class Kind { Number, Boolean, Path, Not, And, Or, Compare, Add, Sub, Mul, Neg, Raw };
    Kind kind{Kind::Boolean};      ///< Kind.
    std::string text;              ///< Number text, path, comparison operator, or raw SMT.
    bool value{false};             ///< Boolean literal value.
    std::vector<Node> children;    ///< Operands.
    std::uint32_t column{1};       ///< 1-based column in the source text.
    /// @brief Structural equality (columns ignored).
    [[nodiscard]] bool same(const Node& other) const;
};

/// @brief Result of parsing.
struct Parsed {
    std::optional<Node> node;  ///< The tree, absent on a syntax error.
    std::string error;         ///< Syntax error message.
    std::uint32_t column{0};   ///< Column of the error.
};

/// @brief Parse an expression (or a raw `smt:` text).
[[nodiscard]] Parsed parse(std::string_view text);

/// @brief Canonical text of an expression (minimal parentheses; parse(print(n)) is structurally n).
[[nodiscard]] std::string print(const Node& node);

/// @brief What a path denotes.
struct Symbol {
    /// @brief Kind of value.
    enum class Type { Number, Boolean, Enum, String };
    Type type{Type::Number};               ///< Kind.
    std::string smt;                       ///< SMT symbol.
    std::vector<std::string> literals;     ///< Enum literals (code = index).
};

/// @brief Resolves a dotted path in the current scope; std::nullopt if unknown.
using Resolver = std::function<std::optional<Symbol>(std::string_view path)>;

/// @brief A failed check: code TML011/TML021/TML022/TML023 and a message.
struct CheckError {
    std::string code;          ///< Diagnostic code.
    std::string message;       ///< Explanation.
    std::uint32_t column{0};   ///< Column.
};

/// @brief A checked condition translated to SMT-LIB2.
struct Translated {
    std::string smt;                     ///< SMT-LIB2 formula.
    std::vector<std::string> symbols;    ///< SMT symbols used, in first-use order.
    std::optional<CheckError> error;     ///< Set on failure (smt is then empty).
};

/**
 * @brief Type-check @p node as a condition (it must be Boolean) and translate it.
 *
 * Raw nodes are passed through without checking (their symbols are not
 * listed).
 */
[[nodiscard]] Translated to_smt(const Node& node, const Resolver& resolve);

/// @brief Parse and translate @p text in one step (syntax errors become TML020).
[[nodiscard]] Translated condition(std::string_view text, const Resolver& resolve);

/**
 * @brief The expression whose translation is @p smt, if @p smt is inside the grammar.
 *
 * Symbols become paths verbatim; `(- n)` becomes the number -n;
 * `(not (= a b))` becomes `a != b`. Returns std::nullopt for anything else
 * (quantifiers, `=>`, `ite`, `distinct`, non-linear terms, C-style calls…).
 */
[[nodiscard]] std::optional<Node> from_smt(std::string_view smt);

/// @brief The SMT text with whitespace normalised (single spaces, no space inside parentheses).
[[nodiscard]] std::string normalize_smt(std::string_view smt);

/// @brief The top-level arguments of an SMT conjunction "(and a b ...)", or {smt} for anything else.
[[nodiscard]] std::vector<std::string> conjuncts(std::string_view smt);

/// @brief True for an exact decimal: -?[0-9]+(\.[0-9]+)?
[[nodiscard]] bool is_decimal(std::string_view text) noexcept;

}  // namespace twin::model::expr
