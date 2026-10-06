/**
 * @file property.hpp
 * @brief The property language of Studio monitors and requirements, and its evaluation on kernel states.
 * @ingroup monitoring
 *
 * @defgroup monitoring Monitors and properties
 * @brief Monitor documents and the property language, light enough for the runtime.
 *
 * Grammar (UPPAAL-like):
 * @code
 *   property = "A[]" state | "E<>" state | "A<>" state | "E[]" state | state "-->" state
 *   state    = or [ "->" state ]                                   (implication, right associative)
 *   or       = and { "||" and }            and = unary { "&&" unary }
 *   unary    = "!" unary | "(" state ")" | atom
 *   atom     = "true" | "false" | LOCATION | [PROCESS "."] LOCATION
 *            | CLOCK [ "-" CLOCK ] CMP INT | INT CMP CLOCK [ "-" CLOCK ]
 *            | "sem(" SMT-LIB2 formula over ontology symbols ")"
 *   CMP      = "<" | "<=" | "==" | ">=" | ">"       ("and", "or", "not", "imply" are accepted as aliases)
 * @endcode
 * Clock bounds are integers in model time units. Which properties can be checked
 * at design time or monitored at run time is decided by the backend
 * (twin::authoring::analyse_property), never by the UI.
 *
 * evaluate() gives the verdict of a state formula on a committed state set of
 * the kernel: Satisfied if it holds in every configuration, Violated if in
 * none, Inconclusive if in some (the twin's nondeterminism is preserved, never
 * resolved). Semantic atoms are not evaluated here (they need observations and
 * the ontology; Studio evaluates them).
 */
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"
#include "twin/kernel/configuration.hpp"
#include "twin/kernel/model.hpp"
#include "twin/kernel/state_set.hpp"

namespace twin::monitoring {

/// @brief Path quantifier of a property.
enum class Quantifier {
    Always,       ///< A[] φ — φ holds in every reachable state.
    Eventually,   ///< E<> φ — some reachable state satisfies φ.
    Inevitably,   ///< A<> φ — every run eventually satisfies φ.
    Potentially,  ///< E[] φ — some run satisfies φ forever.
    LeadsTo       ///< φ --> ψ — every φ-state is eventually followed by a ψ-state.
};

/// @brief "A[]", "E<>", "A<>", "E[]" or "-->".
[[nodiscard]] std::string_view to_string(Quantifier quantifier) noexcept;

/// @brief A state formula (see file documentation).
struct StateFormula {
    /// @brief Node kind.
    enum class Kind { True, False, Location, Clock, Semantic, Not, And, Or, Implies };
    Kind kind{Kind::True};                    ///< Kind.
    std::string name;                         ///< Location or clock name.
    std::optional<std::string> minus;         ///< Subtracted clock of a diagonal clock atom.
    ir::Comparison op{ir::Comparison::LessEqual};  ///< Clock atom comparison.
    std::int64_t bound{0};                    ///< Clock atom bound (model time units).
    std::string formula;                      ///< SMT-LIB2 text of a semantic atom.
    std::vector<StateFormula> children;       ///< Operands (Not: 1; And/Or/Implies: 2).
    /// @brief Structural equality.
    friend bool operator==(const StateFormula&, const StateFormula&) = default;
};

/// @brief A property.
struct Property {
    Quantifier quantifier{Quantifier::Always};  ///< Quantifier.
    StateFormula phi;                           ///< φ (the left side of "-->").
    std::optional<StateFormula> psi;            ///< ψ of "φ --> ψ".
    /// @brief Structural equality.
    friend bool operator==(const Property&, const Property&) = default;
};

/// @brief Parse @p text (ParseError with context "column" on failure).
[[nodiscard]] Result<Property> parse_property(std::string_view text);
/// @brief Canonical text of a property (parse(print(p)) == p).
[[nodiscard]] std::string print_property(const Property& property);
/// @brief Canonical text of a state formula.
[[nodiscard]] std::string print_state(const StateFormula& formula);

/// @brief Names used by a formula.
struct Atoms {
    std::set<std::string> locations;    ///< Location atoms.
    std::set<std::string> clocks;       ///< Clocks of clock atoms.
    std::vector<std::string> semantic;  ///< SMT-LIB2 texts of semantic atoms, in order.
};
/// @brief Collect the atoms of @p formula.
[[nodiscard]] Atoms atoms(const StateFormula& formula);

/// @brief Verdict of a state formula on a set of configurations.
enum class Verdict { Satisfied, Violated, Inconclusive };
/// @brief "satisfied", "violated" or "inconclusive".
[[nodiscard]] std::string_view to_string(Verdict verdict) noexcept;

/**
 * @brief Whether @p formula holds in configuration @p c of @p model.
 * @return NotFound for unknown locations/clocks, InvalidArgument for semantic atoms.
 */
[[nodiscard]] Result<bool> holds(const StateFormula& formula, const kernel::Model& model, const kernel::Configuration& c);

/// @brief Verdict of @p formula on every configuration of @p states (see file documentation).
[[nodiscard]] Result<Verdict> evaluate(const StateFormula& formula, const kernel::Model& model,
                                       const kernel::StateSet& states);

}  // namespace twin::monitoring
