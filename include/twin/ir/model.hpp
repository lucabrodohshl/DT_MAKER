/**
 * @file model.hpp
 * @brief In-memory Twin Intermediate Representation (IR) of a DT timed-automaton view.
 * @ingroup ir
 *
 * The IR is an executable canonicalisation of the source timed automaton V_D
 * and deliberately nothing more ("intentionally boring"). It covers exactly the
 * fragment the existing semantic aligner (SemPTDTAlignmentICSE) gives semantics to:
 *
 *  - finitely many locations, one initial location;
 *  - finitely many real-valued clocks (no data variables: the aligner ignores them);
 *  - location invariants and transition guards that are conjunctions of atomic
 *    clock constraints `x ~ c` or `x - y ~ c` with ~ in {<,<=,==,>=,>} and integer c;
 *  - clock resets to zero;
 *  - actions: a channel label `a!` / `a?` or the internal action tau;
 *  - observable propositions: one per location, `at(l)`, optionally annotated
 *    with the DT interpretation I_D(l) (an ontology formula in SMT-LIB2 syntax).
 *
 * Formal semantics: proof/sections/03-ir.tex. Serialisation: twin/ir/codec.hpp.
 * The IR types are plain values; the semantic kernel (twin::kernel) executes them.
 */
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/logical_time.hpp"

namespace twin::ir {

/// @brief Index of a location in Model::locations.
using LocationIndex = std::uint32_t;
/// @brief Index of a transition in Model::transitions.
using TransitionIndex = std::uint32_t;
/// @brief Index of a proposition in Model::propositions.
using PropositionIndex = std::uint32_t;
/**
 * @brief Clock identifier. 0 denotes the reference clock (constantly 0); the
 * model clock Model::clocks[k] has identifier k + 1. This is the DBM convention
 * used by the aligner (UDBM), which makes the translation validation in the
 * compiler a direct comparison.
 */
using ClockIndex = std::uint32_t;

/// @brief The reference clock (value always 0).
inline constexpr ClockIndex kReferenceClock = 0;

/// @brief Comparison operator of an atomic clock constraint.
enum class Comparison : std::uint8_t {
    Less,          ///< <
    LessEqual,     ///< <=
    Equal,         ///< ==
    GreaterEqual,  ///< >=
    Greater        ///< >
};

/// @brief Source-level spelling of a comparison ("<", "<=", "==", ">=", ">").
[[nodiscard]] std::string_view to_string(Comparison op) noexcept;
/// @brief Parse a comparison spelling; std::nullopt if unknown.
[[nodiscard]] std::optional<Comparison> parse_comparison(std::string_view text) noexcept;

/**
 * @brief Atomic clock constraint  x_lhs - x_rhs ~ bound.
 *
 * With rhs == kReferenceClock it is the simple constraint `x ~ c`; otherwise
 * it is a diagonal (difference) constraint `x - y ~ c`. The bound is an
 * integer in model time units (scaled to ticks only by the kernel).
 */
struct ClockConstraint {
    ClockIndex lhs{1};                       ///< Clock on the left (>= 1).
    ClockIndex rhs{kReferenceClock};         ///< Subtracted clock, or reference clock.
    Comparison op{Comparison::LessEqual};    ///< Comparison operator.
    std::int64_t bound{0};                   ///< Integer constant in model time units.

    /// @brief Total order used for the canonical ordering of conjunctions.
    friend auto operator<=>(const ClockConstraint&, const ClockConstraint&) = default;
};

/// @brief A conjunction of atomic constraints; canonical form is sorted and duplicate-free.
using Conjunction = std::vector<ClockConstraint>;

/// @brief Kind of a transition action.
enum class ActionKind : std::uint8_t {
    Internal,  ///< Unsynchronised edge: the internal action tau.
    Send,      ///< `a!`
    Receive    ///< `a?`
};

/// @brief The action of a transition.
struct Action {
    ActionKind kind{ActionKind::Internal};  ///< Kind of action.
    std::string channel;                    ///< Channel name; empty iff kind == Internal.

    /// @brief Label as used by the aligner and the runtime API: "a!", "a?" or "tau".
    [[nodiscard]] std::string label() const;
    /// @brief Member-wise equality.
    friend bool operator==(const Action&, const Action&) = default;
};

/// @brief The spelling of the internal action.
inline constexpr std::string_view kTauLabel = "tau";

/// @brief Non-semantic layout hint (UPPAAL editor coordinates) used by visualisations.
struct LayoutHint {
    std::int64_t x{0};  ///< Horizontal coordinate.
    std::int64_t y{0};  ///< Vertical coordinate.
    /// @brief Member-wise equality.
    friend bool operator==(const LayoutHint&, const LayoutHint&) = default;
};

/// @brief A location with its invariant.
struct Location {
    std::string id;                     ///< Stable identifier = source location name.
    Conjunction invariant;              ///< Canonical conjunction (empty = true).
    std::optional<LayoutHint> layout;   ///< Optional editor coordinates (non-semantic).
    /// @brief Member-wise equality.
    friend bool operator==(const Location&, const Location&) = default;
};

/// @brief A discrete transition (edge).
struct Transition {
    std::string id;                    ///< Stable identifier "<source>.<label>.<target>[#k]".
    LocationIndex source{0};           ///< Source location.
    LocationIndex target{0};           ///< Target location.
    Action action;                     ///< Action label.
    Conjunction guard;                 ///< Canonical conjunction (empty = true).
    std::vector<ClockIndex> resets;    ///< Clocks reset to 0; sorted, unique, all >= 1.
    /// @brief Member-wise equality.
    friend bool operator==(const Transition&, const Transition&) = default;
};

/**
 * @brief Observable proposition `at(l)`: holds exactly in configurations at location l.
 *
 * This mirrors the aligner, whose state labelling is the location and whose
 * state interpretation I_D maps a location name to an ontology formula.
 */
struct Proposition {
    std::string id;                 ///< "at(<location id>)".
    LocationIndex location{0};      ///< The location the proposition observes.
    std::string interpretation;     ///< I_D(l) in SMT-LIB2 syntax; empty if uninterpreted.
    /// @brief Member-wise equality.
    friend bool operator==(const Proposition&, const Proposition&) = default;
};

/// @brief Ontology meaning of an action label (I_D on events); metadata only.
struct EventInterpretation {
    std::string label;    ///< "a!" / "a?"
    std::string formula;  ///< I_D(a) in SMT-LIB2 syntax.
    /// @brief Member-wise equality.
    friend bool operator==(const EventInterpretation&, const EventInterpretation&) = default;
};

/// @brief Identity and provenance of the model.
struct ModelInfo {
    std::string id;               ///< Model identifier, e.g. "indoor-drone-dt".
    std::string version;          ///< Model version, e.g. "1.0.0".
    std::string source_template;  ///< Name of the UPPAAL template compiled.
    std::string source_sha256;    ///< SHA-256 of the source document bytes.
    /// @brief Member-wise equality.
    friend bool operator==(const ModelInfo&, const ModelInfo&) = default;
};

/**
 * @brief A complete IR model.
 *
 * Invariants (checked by twin::ir::validate): identifiers unique and well-formed,
 * indices in range, conjunctions and resets canonical, propositions are exactly
 * one `at(l)` per location in location order, transition channels declared.
 */
struct Model {
    ModelInfo info;                                  ///< Identity and provenance.
    TimeBase time;                                   ///< Logical-time resolution R.
    std::vector<std::string> clocks;                 ///< Clock names; clocks[k] has id k+1.
    std::vector<std::string> channels;               ///< Declared channels, sorted.
    std::vector<Location> locations;                 ///< Locations in source order.
    LocationIndex initial{0};                        ///< Initial location.
    std::vector<Transition> transitions;             ///< Transitions in source order.
    std::vector<Proposition> propositions;           ///< One per location, same order.
    std::vector<EventInterpretation> event_interpretations;  ///< Sorted by label.

    /// @brief Member-wise equality.
    friend bool operator==(const Model&, const Model&) = default;
};

/// @brief Name of a clock id ("0" for the reference clock).
[[nodiscard]] std::string clock_name(const Model& model, ClockIndex clock);
/// @brief Find a location by id.
[[nodiscard]] std::optional<LocationIndex> find_location(const Model& model, std::string_view id);
/// @brief Find a transition by id.
[[nodiscard]] std::optional<TransitionIndex> find_transition(const Model& model,
                                                             std::string_view id);
/// @brief Find a clock by name (returns its ClockIndex >= 1).
[[nodiscard]] std::optional<ClockIndex> find_clock(const Model& model, std::string_view name);
/// @brief Render a constraint in source syntax, e.g. "t_mode <= 5" or "x - y < 3".
[[nodiscard]] std::string to_string(const Model& model, const ClockConstraint& constraint);
/// @brief Render a conjunction in source syntax ("true" when empty).
[[nodiscard]] std::string to_string(const Model& model, const Conjunction& conjunction);
/// @brief Sort and de-duplicate a conjunction into canonical form.
void canonicalize(Conjunction& conjunction);

}  // namespace twin::ir
