/**
 * @file document.hpp
 * @brief The canonical twin model (format twin-model/1): the one document a twin is designed in.
 * @ingroup model
 *
 * @defgroup model Twin modelling language
 * @brief Model once, derive the rest.
 *
 * A twin is designed as one canonical document written in a small language:
 * asset types (with properties, events, actions, parts and an optional
 * behaviour), the assets that are the parts of the root type, relationships,
 * links from events to actions, states and transitions with time limits and
 * windows. Every formal representation is derived from it by the compilers of
 * this module (compile.hpp) with explicit, deterministic rules, and an existing
 * timed automaton can be lifted back into the language (lift.hpp).
 *
 * The language is specified in
 * docs/superpowers/specs/2026-10-15-model-once-studio/03-metamodel.md and its
 * compilation rules in 04-compilation.md.
 *
 * This file holds the typed document and its strict JSON decoding and
 * canonical encoding. Decoding collects every problem it finds (code TML001)
 * instead of stopping at the first one, so an editor can show them all.
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/authoring/model.hpp"
#include "twin/ir/model.hpp"
#include "twin/json/canonical.hpp"

namespace twin::model {

/// @brief Format tag of canonical model documents.
inline constexpr std::string_view kFormat = "twin-model/1";

/**
 * @brief A located finding about a model.
 *
 * `path` names the element in the document (e.g.
 * `types[CentrifugalPump].behavior.transitions[r3].window`), `element` is the
 * element's id where it has one (editors select it).
 */
struct Diagnostic {
    std::string severity;  ///< "error", "warning" or "info".
    std::string code;      ///< Stable code TMLnnn (04-compilation.md, section 4.7).
    std::string message;   ///< Explanation.
    std::string hint;      ///< How to fix it (may be empty).
    std::string path;      ///< Element path in the document.
    std::string element;   ///< Element id ("" when the finding concerns no single element).
    /// @brief Member-wise equality.
    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

/// @brief JSON form of a diagnostic.
[[nodiscard]] json::Json to_json(const Diagnostic& diagnostic);
/// @brief JSON array of diagnostics.
[[nodiscard]] json::Json to_json(const std::vector<Diagnostic>& diagnostics);
/// @brief True iff some diagnostic is an error.
[[nodiscard]] bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept;

/// @brief Where a property's value comes from.
enum class PropertyKind {
    Measured,  ///< Live data from the physical system.
    Fixed,     ///< Design-time constant; may appear in conditions.
    PerTwin    ///< Entered per deployed twin; descriptive only.
};

/// @brief Data type of a property.
enum class DataType { Real, Integer, Boolean, Enum, String };

/// @brief "measured", "fixed", "per-twin".
[[nodiscard]] std::string_view to_string(PropertyKind kind) noexcept;
/// @brief "real", "integer", "boolean", "enum", "string".
[[nodiscard]] std::string_view to_string(DataType type) noexcept;

/// @brief A property of an asset type.
struct Property {
    std::string id;                    ///< Immutable element id.
    std::string name;                  ///< Identifier, unique among the type's members.
    std::string label;                 ///< Display text (optional).
    std::string description;           ///< Free text.
    PropertyKind kind{PropertyKind::Measured};  ///< Value origin.
    DataType type{DataType::Real};     ///< Data type.
    std::string unit;                  ///< Unit symbol (catalogue), may be empty.
    std::vector<std::string> values;   ///< Enum literals (type == Enum).
    std::optional<std::string> value;  ///< Value of a Fixed property (exact decimal, true/false, literal).
    std::optional<std::string> min;    ///< Range minimum (exact decimal).
    std::optional<std::string> max;    ///< Range maximum (exact decimal).
    std::optional<std::string> period; ///< Expected update period in seconds (Measured).
    std::string sort;                  ///< Domain-theory sort override (advanced; lifting).
    /// @brief Member-wise equality.
    friend bool operator==(const Property&, const Property&) = default;
};

/// @brief What happens when a trigger occurs and no transition accepts it.
enum class Unexpected {
    Nonconformance,  ///< Recorded as a conformance violation (default).
    Ignore           ///< Accepted without effect in states that have no transition on it.
};

/// @brief Origin of an event.
enum class EventOrigin {
    Reported,   ///< Reported by the physical system.
    Condition   ///< Derived by the platform when a condition on properties becomes true.
};

/// @brief An event of an asset type.
struct Event {
    std::string id;                 ///< Immutable element id.
    std::string name;               ///< Identifier.
    std::string label;              ///< Display text.
    std::string description;        ///< Free text.
    EventOrigin origin{EventOrigin::Reported};  ///< Origin.
    std::string when;               ///< Condition (origin Condition).
    std::string rearm;              ///< Re-arm condition (origin Condition; default: not when).
    std::string meaning;            ///< What holds when it is reported (origin Reported; optional).
    Unexpected unexpected{Unexpected::Nonconformance};  ///< Handling when no transition accepts it.
    bool uninterpreted{false};      ///< Advanced (lifting): no interpretation entry (tau for the aligner).
    char direction{'!'};            ///< Channel direction of its label ('!' or '?'; advanced).
    /// @brief Member-wise equality.
    friend bool operator==(const Event&, const Event&) = default;
};

/// @brief An action of an asset type.
struct Action {
    std::string id;             ///< Immutable element id.
    std::string name;           ///< Identifier.
    std::string label;          ///< Display text.
    std::string description;    ///< Free text.
    std::string meaning;        ///< Precondition claim conjoined with "requested" (optional).
    Unexpected unexpected{Unexpected::Nonconformance};  ///< Handling when it is not admissible.
    bool requestable{true};     ///< False: only reachable through links.
    char direction{'!'};        ///< Channel direction of its label ('!' or '?'; advanced).
    /// @brief Member-wise equality.
    friend bool operator==(const Action&, const Action&) = default;
};

/// @brief An asset: a named occurrence of a type inside another type (a part).
struct Part {
    std::string id;                              ///< Immutable element id.
    std::string name;                            ///< Identifier, unique among the type's members.
    std::string label;                           ///< Display text.
    std::string description;                     ///< Free text.
    std::string type;                            ///< Asset type name.
    bool external{false};                        ///< Reference to an asset outside the twin.
    std::map<std::string, std::string> values;   ///< Fixed-property overrides for this occurrence.
    /// @brief Member-wise equality.
    friend bool operator==(const Part&, const Part&) = default;
};

/// @brief A relationship between two assets in the scope of a type.
struct Relationship {
    std::string id;    ///< Immutable element id.
    std::string type;  ///< Relationship type name.
    std::string from;  ///< Part path ("pump_01", "pump_01.motor") or "self".
    std::string to;    ///< Part path or "self".
    /// @brief Member-wise equality.
    friend bool operator==(const Relationship&, const Relationship&) = default;
};

/// @brief A link: whenever the event occurs, the action occurs in the same step.
struct Link {
    std::string id;    ///< Immutable element id.
    std::string from;  ///< Event path ("tank_01.level_high").
    std::string to;    ///< Action path ("pump_01.stop").
    /// @brief Member-wise equality.
    friend bool operator==(const Link&, const Link&) = default;
};

/// @brief An assumption: an extra axiom of the domain theory (advanced).
struct Assumption {
    std::string id;           ///< Immutable element id.
    std::string name;         ///< Identifier.
    std::string expr;         ///< Expression, or "smt:" followed by SMT-LIB2 text.
    std::string description;  ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const Assumption&, const Assumption&) = default;
};

/// @brief When a timer is reset.
enum class Restart {
    OnStateChange,  ///< On every transition whose target differs from its source.
    Explicit        ///< Only where a transition lists it.
};

/// @brief A timer (a clock of the behaviour).
struct Timer {
    std::string id;           ///< Immutable element id.
    std::string name;         ///< Identifier.
    Restart restart{Restart::OnStateChange};  ///< Reset policy.
    std::string description;  ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const Timer&, const Timer&) = default;
};

/// @brief A named integer duration (advanced).
struct Constant {
    std::string id;           ///< Immutable element id.
    std::string name;         ///< Identifier.
    std::int64_t value{0};    ///< Value in model time units.
    std::string description;  ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const Constant&, const Constant&) = default;
};

/// @brief A time constraint `timer op bound` or `timer - minus op bound`.
struct TimeAtom {
    std::string timer;                     ///< Constrained timer.
    std::optional<std::string> minus;      ///< Subtracted timer (diagonal; limits only).
    ir::Comparison op{ir::Comparison::LessEqual};  ///< Comparison.
    authoring::Bound bound;                ///< Integer or constant name.
    /// @brief Member-wise equality.
    friend bool operator==(const TimeAtom&, const TimeAtom&) = default;
};

/// @brief Presentation tone of a state (no formal effect).
enum class Tone { Normal, Warning, Critical };
/// @brief "normal", "warning", "critical".
[[nodiscard]] std::string_view to_string(Tone tone) noexcept;

/// @brief A state of a behaviour.
struct State {
    std::string id;                 ///< Immutable element id.
    std::string name;               ///< Identifier.
    std::string label;              ///< Display text.
    std::string description;        ///< Free text.
    bool initial{false};            ///< Exactly one per behaviour.
    std::string meaning;            ///< Condition that holds in the state (optional).
    std::vector<TimeAtom> limits;   ///< Invariant atoms (<, <=).
    Tone tone{Tone::Normal};        ///< Presentation tone.
    /// @brief Member-wise equality.
    friend bool operator==(const State&, const State&) = default;
};

/// @brief A named set of states.
struct Group {
    std::string id;                   ///< Immutable element id.
    std::string name;                 ///< Identifier.
    std::string label;                ///< Display text.
    std::vector<std::string> states;  ///< Member state names (order = expansion order).
    std::string meaning;              ///< Conjoined to the members' meanings (optional).
    /// @brief Member-wise equality.
    friend bool operator==(const Group&, const Group&) = default;
};

/// @brief Kind of a transition trigger.
enum class TriggerKind { Event, Action, Timeout, Internal };

/// @brief What makes a transition happen.
struct Trigger {
    TriggerKind kind{TriggerKind::Event};  ///< Kind.
    std::string ref;                       ///< Event or action path (Event, Action).
    authoring::Bound after;                ///< Timeout duration (Timeout).
    std::string timer;                     ///< Timer of the timeout ("" = the state timer).
    /// @brief Member-wise equality.
    friend bool operator==(const Trigger&, const Trigger&) = default;
};

/// @brief A transition.
struct Transition {
    std::string id;                    ///< Immutable element id.
    std::vector<std::string> from;     ///< Source states and/or groups.
    std::string to;                    ///< Target state.
    Trigger trigger;                   ///< Trigger.
    std::vector<TimeAtom> window;      ///< Guard (time only).
    std::vector<std::string> restart;  ///< Timers reset in addition to the policy.
    std::string description;           ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const Transition&, const Transition&) = default;
};

/// @brief The behaviour (state machine) of an asset type.
struct Behavior {
    std::vector<Timer> timers;            ///< Timers.
    std::vector<Constant> constants;      ///< Named durations.
    std::vector<State> states;            ///< States.
    std::vector<Group> groups;            ///< State groups.
    std::vector<Transition> transitions;  ///< Transitions.
    /// @brief Member-wise equality.
    friend bool operator==(const Behavior&, const Behavior&) = default;
};

/// @brief An asset type.
struct AssetType {
    std::string id;                             ///< Immutable element id.
    std::string name;                           ///< Identifier, unique among types.
    std::string label;                          ///< Display text.
    std::string description;                    ///< Free text.
    std::string extends;                        ///< Supertype name ("" = none).
    std::map<std::string, std::string> values;  ///< Overrides of inherited fixed values.
    std::vector<Property> properties;           ///< Own properties.
    std::vector<Event> events;                  ///< Own events.
    std::vector<Action> actions;                ///< Own actions.
    std::vector<Part> parts;                    ///< Own parts.
    std::vector<Relationship> relationships;    ///< Relationships among its parts.
    std::vector<Link> links;                    ///< Links among its parts.
    std::vector<Assumption> assumptions;        ///< Extra axioms.
    std::optional<Behavior> behavior;           ///< State machine.
    /// @brief Member-wise equality.
    friend bool operator==(const AssetType&, const AssetType&) = default;
};

/// @brief A relationship type (vocabulary entry).
struct RelationshipType {
    std::string id;                 ///< Immutable element id.
    std::string name;               ///< Identifier.
    std::string label;              ///< Display text.
    std::string inverse;            ///< Name of the inverse (optional).
    std::vector<std::string> from;  ///< Allowed source types (empty = any).
    std::vector<std::string> to;    ///< Allowed target types (empty = any).
    bool symmetric{false};          ///< Symmetric relation.
    std::string description;        ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const RelationshipType&, const RelationshipType&) = default;
};

/// @brief Meaning of one location of the physical reference.
struct ReferenceState {
    std::vector<std::string> states;  ///< Model state paths ("Running", "pump_01.Running").
    std::string meaning;              ///< Or an own condition (used when states is empty).
    /// @brief Member-wise equality.
    friend bool operator==(const ReferenceState&, const ReferenceState&) = default;
};

/// @brief Meaning of one channel of the physical reference.
struct ReferenceLabel {
    std::string trigger;    ///< Event or action path in the root scope.
    bool internal{false};   ///< Unobservable for alignment (no interpretation).
    /// @brief Member-wise equality.
    friend bool operator==(const ReferenceLabel&, const ReferenceLabel&) = default;
};

/// @brief The physical reference V_P with its mapping (advanced; alignment input).
struct Reference {
    authoring::Model automaton;                     ///< The imported PT view (twin-ta/1).
    std::map<std::string, ReferenceState> states;   ///< PT location -> meaning.
    std::map<std::string, ReferenceLabel> labels;   ///< PT channel -> meaning.
    /// @brief Member-wise equality.
    friend bool operator==(const Reference&, const Reference&) = default;
};

/// @brief A requirement checked by a monitor (advanced).
struct Requirement {
    std::string id;           ///< Immutable element id.
    std::string name;         ///< Identifier.
    std::string title;        ///< Display text.
    std::string kind;         ///< "never" (a state path is never reached) or "property".
    std::string state;        ///< State path (kind never).
    std::string property;     ///< Property-language text over generated names (kind property).
    std::string severity{"warning"};  ///< "info", "warning" or "critical".
    std::string description;  ///< Free text.
    /// @brief Member-wise equality.
    friend bool operator==(const Requirement&, const Requirement&) = default;
};

/// @brief A complete model document.
struct Document {
    std::string name;                       ///< Model identifier (also the KG root asset name).
    std::string label;                      ///< Display name.
    std::string description;                ///< Free text.
    std::string time_unit{"s"};             ///< Model time unit.
    std::int64_t ticks_per_unit{1000};      ///< Logical-time resolution (power of ten).
    std::string root;                       ///< Name of the root type (the system).
    std::string iri_base;                   ///< IRI base ("" = urn:vts:<name>:).
    bool allow_nondeterminism{false};       ///< Advanced: overlapping windows are warnings.
    std::vector<AssetType> types;           ///< Asset types (root included).
    std::vector<RelationshipType> relationship_types;  ///< Vocabulary.
    std::optional<Reference> reference;     ///< Physical reference (advanced).
    std::vector<Requirement> requirements;  ///< Requirements (advanced).
    std::vector<std::string> declarations;  ///< Verbatim extra ontology declarations (lifting).
    json::Json layout = json::Json::object();  ///< Non-semantic editor layout.
    /// @brief Member-wise equality.
    friend bool operator==(const Document&, const Document&) = default;

    /// @brief Type by name.
    [[nodiscard]] const AssetType* find_type(std::string_view type_name) const noexcept;
    /// @brief Relationship type by name.
    [[nodiscard]] const RelationshipType* find_relationship_type(std::string_view type_name) const noexcept;
};

/// @brief Result of decoding: the best-effort document and every structural problem (TML001).
struct Decoded {
    Document document;                     ///< Best effort (also when there are errors).
    std::vector<Diagnostic> diagnostics;   ///< Structural problems.
};

/// @brief Decode a twin-model/1 document strictly (unknown keys and wrong types are TML001).
[[nodiscard]] Decoded decode(const json::Json& json);

/// @brief Canonical JSON encoding (decode(encode(d)).document == d).
[[nodiscard]] json::Json encode(const Document& document);

/// @brief The document without layout, labels, descriptions and ids: what the semantics depends on.
[[nodiscard]] json::Json semantic_projection(const Document& document);

/// @brief SHA-256 of the canonical semantic projection.
[[nodiscard]] std::string semantic_digest(const Document& document);

/// @brief True for identifiers the language accepts as names ([A-Za-z][A-Za-z0-9_]*, no "__", no keyword).
[[nodiscard]] bool is_name(std::string_view text) noexcept;

}  // namespace twin::model
