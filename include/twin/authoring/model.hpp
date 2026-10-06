/**
 * @file model.hpp
 * @brief The canonical timed-automaton model of Studio authoring (format twin-ta/1).
 * @ingroup authoring
 *
 * @defgroup authoring Twin authoring
 * @brief One canonical model behind every way of authoring a PT or DT view.
 *
 * A UPPAAL import, the diagram editor and the TwinTA text editor all produce
 * and edit this model. Its toolchain rendering (uppaal.hpp) is the only form
 * the existing compiler and the unmodified semantic aligner ever read, so a
 * model has exactly one meaning: the timed-automaton semantics of the
 * supported fragment (docs/supported-model-fragment.md).
 *
 * The model is a plain value. It mirrors the fragment one to one:
 *  - clocks, integer constants and plain channels;
 *  - locations with an invariant (a conjunction of atoms) and one initial location;
 *  - edges with a guard (a conjunction of atoms), an optional synchronisation
 *    `a!` / `a?` (none = the internal action tau) and clock resets to zero.
 *
 * Notes are free text kept with the declarations. They are part of the stored
 * content (content_sha256) but never part of the semantics: the semantic
 * digest (uppaal.hpp) excludes them, as it excludes the diagram layout
 * (layout.hpp), which is stored separately.
 *
 * Structural validity is checked by validate() (validate.hpp). A valid model
 * is only *well formed*; formal properties come from the aligner, the compiler
 * and the property checker, never from this module's structural checks.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/ir/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::authoring {

/// @brief Format tag of canonical model documents.
inline constexpr std::string_view kModelFormat = "twin-ta/1";

/// @brief Bound of a clock constraint: an integer literal or the name of an integer constant.
struct Bound {
    std::variant<std::int64_t, std::string> value;  ///< Literal value or constant name.
    /// @brief Member-wise equality.
    friend bool operator==(const Bound&, const Bound&) = default;
};

/**
 * @brief Atomic clock constraint `clock op bound`, or `clock - minus op bound` (diagonal).
 *
 * Diagonal constraints are part of the fragment only in invariants: the aligner's
 * guard parser drops them (validate() reports TWM009 for a diagonal guard).
 */
struct Atom {
    std::string clock;                  ///< Constrained clock.
    std::optional<std::string> minus;   ///< Subtracted clock of a diagonal constraint.
    ir::Comparison op{ir::Comparison::LessEqual};  ///< Comparison.
    Bound bound;                        ///< Bound in model time units.
    /// @brief Member-wise equality.
    friend bool operator==(const Atom&, const Atom&) = default;
};

/// @brief A conjunction of atoms; empty means `true`.
using Constraint = std::vector<Atom>;

/// @brief A clock declaration.
struct ClockDecl {
    std::string name;  ///< Identifier.
    std::string note;  ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const ClockDecl&, const ClockDecl&) = default;
};

/// @brief An integer constant declaration (`const int NAME = value;` in UPPAAL).
struct ConstantDecl {
    std::string name;      ///< Identifier.
    std::int64_t value{0}; ///< Value.
    std::string note;      ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const ConstantDecl&, const ConstantDecl&) = default;
};

/// @brief A plain (not urgent, not broadcast) channel declaration.
struct ChannelDecl {
    std::string name;  ///< Identifier.
    std::string note;  ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const ChannelDecl&, const ChannelDecl&) = default;
};

/// @brief A location.
struct LocationDecl {
    std::string name;      ///< Identifier (also the location's stable id).
    bool initial{false};   ///< Exactly one location of a valid model is initial.
    Constraint invariant;  ///< Invariant (empty = true).
    std::string note;      ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const LocationDecl&, const LocationDecl&) = default;
};

/// @brief A synchronisation: send `a!` or receive `a?`.
struct Sync {
    std::string channel;   ///< Channel name.
    char direction{'!'};   ///< '!' (send) or '?' (receive).
    /// @brief Member-wise equality.
    friend bool operator==(const Sync&, const Sync&) = default;
};

/// @brief An edge (transition).
struct EdgeDecl {
    std::string id;                  ///< Stable edge id (editor selection, layout, source maps).
    std::string source;              ///< Source location name.
    std::string target;              ///< Target location name.
    std::optional<Sync> sync;        ///< Synchronisation; std::nullopt = internal action tau.
    Constraint guard;                ///< Guard (empty = true).
    std::vector<std::string> resets; ///< Clocks reset to zero.
    std::string note;                ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const EdgeDecl&, const EdgeDecl&) = default;
};

/// @brief A complete canonical model (one automaton).
struct Model {
    std::string name;                     ///< Automaton (UPPAAL template) name.
    std::string note;                     ///< Free text.
    std::vector<ClockDecl> clocks;        ///< Clocks, declaration order.
    std::vector<ConstantDecl> constants;  ///< Integer constants, declaration order.
    std::vector<ChannelDecl> channels;    ///< Channels, declaration order.
    std::vector<LocationDecl> locations;  ///< Locations, declaration order.
    std::vector<EdgeDecl> edges;          ///< Edges, declaration order (the toolchain order).
    /// @brief Member-wise equality.
    friend bool operator==(const Model&, const Model&) = default;
};

/// @brief The label of an edge as the aligner and the runtime spell it: "a!", "a?" or "tau".
[[nodiscard]] std::string edge_label(const EdgeDecl& edge);

/// @brief The initial location, if exactly one is marked initial.
[[nodiscard]] const LocationDecl* initial_location(const Model& model) noexcept;

/// @brief Encode a model as a twin-ta/1 document (canonical-safe: strings, integers, booleans, null).
[[nodiscard]] json::Json to_json(const Model& model);

/**
 * @brief Decode a twin-ta/1 document strictly.
 *
 * Unknown keys, a wrong format tag, wrong types, unknown comparisons and bad
 * synchronisation directions are refused (ValidationError naming the path).
 * Structural rules (names, references) are checked by validate(), not here.
 */
[[nodiscard]] Result<Model> model_from_json(const json::Json& document);

/// @brief Encode a conjunction as in twin-ta/1: [{clock, op, bound, minus?}].
[[nodiscard]] json::Json constraint_to_json(const Constraint& constraint);
/// @brief Decode a conjunction encoded by constraint_to_json() (ValidationError naming the path).
[[nodiscard]] Result<Constraint> constraint_from_json(const json::Json& atoms);

/// @brief SHA-256 of the canonical JSON encoding (the stored content hash; includes notes).
[[nodiscard]] std::string content_sha256(const Model& model);

}  // namespace twin::authoring
