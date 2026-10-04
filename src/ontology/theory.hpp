/**
 * @file theory.hpp
 * @brief (private) The aligner's Z3 reading of an ontology, with tri-state entailment.
 * @ingroup ontology
 *
 * A Theory is the first-order theory K = (S, F, R, Δ) **exactly as the
 * semantic aligner reads it**: the ontology text is parsed by
 * `dtpta::OntFileParser`, and every further formula (old axioms, interpretation
 * formulas, observations) is parsed by `dtpta::InterpFileParser` against that
 * same Ontology object. Both parsers come from the unmodified
 * SemPTDTAlignmentICSE sources, so preprocessing (C-style calls, sort mapping
 * to Real, `true`/`false` handling) is identical to what alignment uses.
 *
 * The aligner's own `Ontology::entails` maps Z3 `unknown` to "not entailed".
 * That is unacceptable for refinement verdicts (UNKNOWN must never become
 * NOT A REFINEMENT), so entailment here is decided by a separate solver over
 * the same context and the same axioms, with an explicit timeout and a
 * three-valued result.
 *
 * Not thread-safe (one Z3 context per Theory). Z3 types do not escape the
 * ontology module.
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/ontology/verdict.hpp"
#include "z3++.h"

namespace dtpta {
class Ontology;
}

namespace twin::ontology::detail {

/// @brief A satisfying assignment, rendered as (symbol, value) pairs sorted by symbol.
using Model = std::vector<std::pair<std::string, std::string>>;

/// @brief Outcome of one solver query.
struct Entailment {
    Verdict3 verdict{Verdict3::Unknown};  ///< True: entailed; False: refuted; Unknown: undecided.
    std::optional<Model> counter_model;   ///< A model of Δ ∧ ¬φ when verdict == False.
    std::string reason;                   ///< Z3 reason_unknown() when Unknown.
};

/// @brief The aligner's Z3 reading of one ontology (see file documentation).
class Theory {
public:
    /**
     * @brief Parse @p ontology_text with the aligner's OntFileParser.
     * @return ErrorCode::ParseError carrying the aligner's message if it rejects the text.
     */
    [[nodiscard]] static Result<std::unique_ptr<Theory>> load(std::string_view ontology_text);

    ~Theory();
    Theory(const Theory&) = delete;
    Theory& operator=(const Theory&) = delete;
    Theory(Theory&&) = delete;
    Theory& operator=(Theory&&) = delete;

    /**
     * @brief Parse formulas over this theory's signature with the aligner's InterpFileParser.
     *
     * All formulas are parsed in one batch. A formula that does not type-check
     * (e.g. it uses a symbol this signature lacks) yields an error naming its index.
     */
    [[nodiscard]] Result<std::vector<z3::expr>> parse_formulas(const std::vector<std::string>& formulas);

    /**
     * @brief Read a whole interpretation document with the aligner's InterpFileParser.
     *
     * This is the authoritative reading used by alignment. Keys are exactly as
     * the aligner stores them (event labels keep their trailing '!'); entries
     * are sorted by key. Lines the aligner skips are absent (the strict parser
     * reports them separately).
     */
    [[nodiscard]] Result<std::vector<std::pair<std::string, z3::expr>>> read_interpretation(std::string_view text);

    /// @brief Parse a single formula (see parse_formulas()).
    [[nodiscard]] Result<z3::expr> parse_formula(const std::string& formula);

    /**
     * @brief Install Δ (the axiom formulas, as written in the source) for entailment queries.
     *
     * The aligner's parser already holds Δ internally but does not expose it;
     * re-parsing the same texts in the same context yields the same terms.
     */
    [[nodiscard]] Status set_axioms(const std::vector<std::string>& axiom_formulas);

    /// @brief Δ ⊨ φ, three-valued, with a counter-model when refuted.
    [[nodiscard]] Entailment entails(const z3::expr& phi, unsigned timeout_ms);

    /// @brief Δ ∪ extra ⊨ φ (extra assumptions, e.g. observations).
    [[nodiscard]] Entailment entails_under(const std::vector<z3::expr>& extra, const z3::expr& phi,
                                           unsigned timeout_ms);

    /// @brief Is Δ ∪ extra satisfiable? (True = sat, False = unsat, Unknown).
    [[nodiscard]] Verdict3 satisfiable(const std::vector<z3::expr>& extra, unsigned timeout_ms,
                                       std::string* reason = nullptr);

    /// @brief Names of the function and relation symbols the aligner registered.
    [[nodiscard]] std::vector<std::string> symbol_names() const;
    /// @brief Names of the sorts the aligner registered.
    [[nodiscard]] std::vector<std::string> sort_names() const;

    /// @brief The Z3 context (all expressions of this theory live in it).
    [[nodiscard]] z3::context& context();

private:
    explicit Theory(std::shared_ptr<dtpta::Ontology> ontology);
    Entailment check(const std::vector<z3::expr>& extra, const z3::expr* negated_goal, unsigned timeout_ms);

    std::shared_ptr<dtpta::Ontology> ontology_;
    std::vector<z3::expr> axioms_;
};

/// @brief "z3 <version>; SemPTDTAlignmentICSE <digest>; twin-ontology/<n>".
[[nodiscard]] std::string checker_identity();

}  // namespace twin::ontology::detail
