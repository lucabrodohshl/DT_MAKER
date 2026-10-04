/**
 * @file source.hpp
 * @brief Strict, located source model of formal ontologies (.ont) and interpretations (.interp).
 * @ingroup ontology
 *
 * @defgroup ontology Formal ontology services
 * @brief Validation, structural diff, refinement (Def. 4) and interpretation
 * evaluation for the domain knowledge Φ = (K, {I_P, I_D}) of the semantic aligner.
 *
 * The **authoritative** reading of an ontology is the one of the existing
 * aligner (`dtpta::OntFileParser`, SemPTDTAlignmentICSE): it determines the
 * Z3 theory that alignment and refinement are decided over (see theory.hpp).
 * The aligner's parser is, however, *lenient*: it silently skips unknown
 * keywords and malformed interpretation lines, and it does not report where an
 * error is. An ontology engineer needs the opposite, so this file provides a
 * **strict** structural parser of the very same line-oriented formats that
 *
 *  - accepts exactly the constructs the aligner gives meaning to,
 *  - rejects everything the aligner would ignore (so nothing is dropped
 *    silently from what the engineer believes the theory to be),
 *  - attaches a source span to every declaration and diagnostic (for editor
 *    markers, go-to-definition and cross references), and
 *  - keeps the comments the formats use as documentation.
 *
 * validation.hpp cross-checks the two readings (same symbol tables), so the
 * structure shown to engineers is provably the theory the aligner uses.
 *
 * Formats (one declaration per line; `;` starts a comment outside parentheses):
 * @code
 *   sort  Name
 *   fun   name : Ret                 ; nullary constant
 *   fun   name : Arg1 Arg2 -> Ret
 *   rel   name :                     ; 0-ary relation (proposition)
 *   rel   name : Arg1 Arg2
 *   axiom id   : <SMT-LIB2 formula>
 *
 *   Location : <SMT-LIB2 formula>    ; .interp: location (state) interpretation
 *   label!   : <SMT-LIB2 formula>    ; .interp: event interpretation
 * @endcode
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace twin::ontology {

/// @brief A source location: 1-based line and column, length in bytes (0 = whole line).
struct Span {
    std::uint32_t line{0};    ///< 1-based line number (0 = no location).
    std::uint32_t column{0};  ///< 1-based byte column.
    std::uint32_t length{0};  ///< Length of the highlighted text in bytes.
};

/// @brief Severity of a source diagnostic. Any Error makes a document invalid.
enum class Severity { Error, Warning, Note };

/// @brief "error" / "warning" / "note".
[[nodiscard]] const char* to_string(Severity severity) noexcept;

/**
 * @brief A located, machine-readable diagnostic.
 *
 * Codes are stable (documented in docs/studio/ontology-diagnostics.md):
 * `ONT0xx` structural ontology errors, `ONT1xx` theory-level findings,
 * `INT0xx` interpretation errors, `INT1xx` interpretation theory findings.
 */
struct SourceDiagnostic {
    std::string code;                     ///< Stable code, e.g. "ONT001".
    Severity severity{Severity::Error};   ///< Severity.
    std::string message;                  ///< Human-readable explanation.
    Span span;                            ///< Where (line 0 = document level).
};

/// @brief `sort Name` — a value domain S (Def. 2). The aligner reads every user sort as Real.
struct SortDecl {
    std::string name;     ///< Sort name.
    std::string comment;  ///< Trailing comment (documentation), without the `;`.
    Span span;            ///< Span of the name.
};

/// @brief `fun name : A1 .. An -> R` — a function symbol of F (Def. 2).
struct FunctionDecl {
    std::string name;                     ///< Symbol name.
    std::vector<std::string> arg_sorts;   ///< Declared argument sorts (as written).
    std::string return_sort;              ///< Declared result sort (as written).
    std::string comment;                  ///< Trailing comment.
    Span span;                            ///< Span of the name.
};

/// @brief `rel name : A1 .. An` — a relation symbol of R (Def. 2).
struct RelationDecl {
    std::string name;                     ///< Symbol name.
    std::vector<std::string> arg_sorts;   ///< Declared argument sorts (as written).
    std::string comment;                  ///< Trailing comment.
    Span span;                            ///< Span of the name.
};

/// @brief `axiom id : φ` — an element of Δ (Def. 2).
struct Axiom {
    std::string id;        ///< Axiom identifier (unique within the ontology).
    std::string formula;   ///< SMT-LIB2 formula text, exactly as written.
    std::string comment;   ///< Trailing comment.
    Span span;             ///< Span of the identifier.
    Span formula_span;     ///< Span of the formula text.
};

/// @brief Structured content of an ontology document K = (S, F, R, Δ).
struct OntologySource {
    std::string header_comment;            ///< Leading comment block (documentation).
    std::vector<SortDecl> sorts;           ///< S, in document order.
    std::vector<FunctionDecl> functions;   ///< F, in document order.
    std::vector<RelationDecl> relations;   ///< R, in document order.
    std::vector<Axiom> axioms;             ///< Δ, in document order.

    /// @brief Find a declared sort.
    [[nodiscard]] const SortDecl* find_sort(std::string_view name) const noexcept;
    /// @brief Find a declared function.
    [[nodiscard]] const FunctionDecl* find_function(std::string_view name) const noexcept;
    /// @brief Find a declared relation.
    [[nodiscard]] const RelationDecl* find_relation(std::string_view name) const noexcept;
    /// @brief Find an axiom by id.
    [[nodiscard]] const Axiom* find_axiom(std::string_view id) const noexcept;
};

/// @brief One line `key : φ` of an interpretation; `key!` denotes an event label.
struct InterpretationEntry {
    std::string key;        ///< Location name or event label (events keep the trailing '!').
    bool is_event{false};   ///< True iff the key ends with '!'.
    std::string formula;    ///< SMT-LIB2 formula text.
    std::string comment;    ///< Trailing comment.
    Span span;              ///< Span of the key.
    Span formula_span;      ///< Span of the formula.
};

/// @brief Structured content of an interpretation document I : AP ∪ Σ_E → L(K).
struct InterpretationSource {
    std::string header_comment;                 ///< Leading comment block.
    std::vector<InterpretationEntry> entries;   ///< Entries in document order.

    /// @brief Find an entry by key (events include the '!').
    [[nodiscard]] const InterpretationEntry* find(std::string_view key) const noexcept;
};

/// @brief Result of parsing an ontology: the structure plus every diagnostic.
struct ParsedOntology {
    OntologySource source;                     ///< Best-effort structure (also when invalid).
    std::vector<SourceDiagnostic> diagnostics; ///< All findings, in document order.
    /// @brief True iff there is no Error diagnostic.
    [[nodiscard]] bool ok() const noexcept;
};

/// @brief Result of parsing an interpretation.
struct ParsedInterpretation {
    InterpretationSource source;               ///< Best-effort structure.
    std::vector<SourceDiagnostic> diagnostics; ///< All findings.
    /// @brief True iff there is no Error diagnostic.
    [[nodiscard]] bool ok() const noexcept;
};

/**
 * @brief Parse an ontology document strictly.
 *
 * Diagnostics: ONT001 unknown keyword, ONT002 missing ':', ONT003 missing
 * name, ONT004 duplicate symbol, ONT005 undeclared sort (warning: the aligner
 * reads it as Real), ONT006 malformed formula, ONT007 undeclared symbol in an
 * axiom, ONT008 duplicate axiom id, ONT009 invalid identifier, ONT010
 * redeclared built-in.
 *
 * Never throws; accepts LF and CRLF line endings.
 */
[[nodiscard]] ParsedOntology parse_ontology(std::string_view text);

/**
 * @brief Parse an interpretation document strictly.
 *
 * Diagnostics: INT001 malformed line, INT002 duplicate key, INT003 malformed
 * formula, INT004 undeclared symbol (only when @p ontology is given), INT005
 * invalid key.
 */
[[nodiscard]] ParsedInterpretation parse_interpretation(std::string_view text,
                                                        const OntologySource* ontology = nullptr);

/**
 * @brief The free (non-built-in, non-bound) identifiers of an SMT-LIB2 formula.
 *
 * Applies the aligner's C-style call rewriting (`f(x)` → `(f x)`) first.
 * Excludes SMT-LIB2 operators/keywords, numerals and variables bound by
 * `forall`/`exists`/`let`; sort names used in binders are excluded as well.
 * The result is sorted and duplicate-free.
 *
 * @return std::nullopt if the formula is not a well-formed s-expression.
 */
[[nodiscard]] std::optional<std::vector<std::string>> formula_symbols(std::string_view smt2);

/// @brief True for SMT-LIB2 built-in sorts the formats accept (Int, Real, Bool).
[[nodiscard]] bool is_builtin_sort(std::string_view name) noexcept;

}  // namespace twin::ontology
