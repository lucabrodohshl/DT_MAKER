// Internal: the resolved model — effective types (inheritance applied), the
// instance tree (parts instantiated from the root type), qualified names and
// scopes for expressions. Shared by validation and every compiler.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/model/document.hpp"
#include "twin/model/expression.hpp"

namespace twin::model::detail {

/// A type with its inherited members (supertype members first).
struct EffectiveType {
    const AssetType* type{nullptr};
    std::vector<const AssetType*> lineage;   // root ancestor first, the type last
    std::vector<const Property*> properties;
    std::vector<const Event*> events;
    std::vector<const Action*> actions;
    std::vector<const Part*> parts;
    std::vector<const Relationship*> relationships;
    std::vector<const Link*> links;
    std::vector<const Assumption*> assumptions;
    const Behavior* behavior{nullptr};       // own, else inherited
    const AssetType* behavior_owner{nullptr};
    std::map<std::string, std::string> values;  // effective fixed values (defaults, then overrides)

    [[nodiscard]] const Property* property(std::string_view name) const;
    [[nodiscard]] const Event* event(std::string_view name) const;
    [[nodiscard]] const Action* action(std::string_view name) const;
    [[nodiscard]] const Part* part(std::string_view name) const;
};

/// An asset of the model: the root, or a part (recursively).
struct Instance {
    std::vector<std::string> segments;     // part names from the root ([] for the root)
    std::string path;                      // segments joined by "__" ("" for the root)
    std::string dotted;                    // segments joined by "." ("" for the root)
    const EffectiveType* type{nullptr};
    const Part* part{nullptr};             // nullptr for the root
    bool external{false};
    Instance* parent{nullptr};
    std::vector<Instance*> children;       // in part order
    std::map<std::string, std::string> values;  // effective fixed values for this occurrence
};

/// What a dotted member path denotes in a scope.
struct MemberRef {
    const Instance* owner{nullptr};
    const Property* property{nullptr};
    const Event* event{nullptr};
    const Action* action{nullptr};
};

class Resolved {
public:
    /// Resolve @p doc; problems that prevent resolution are appended to @p diagnostics.
    Resolved(const Document& doc, std::vector<Diagnostic>& diagnostics);

    [[nodiscard]] const Document& doc() const noexcept { return doc_; }
    [[nodiscard]] const Instance* root() const noexcept { return root_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Instance>>& instances() const noexcept { return instances_; }
    [[nodiscard]] const EffectiveType* type(std::string_view name) const;
    [[nodiscard]] bool ok() const noexcept { return ok_; }

    /// `path__member`, or `member` for the root.
    [[nodiscard]] static std::string qualify(const Instance& owner, std::string_view member);
    /// Location-name component of an instance's state: `<segments joined by _>_<State>`, or `<State>` at the root.
    [[nodiscard]] static std::string location_component(const Instance& owner, std::string_view state);

    /// The descendant (or self) named by a dotted part path; "self" and "" are the scope itself.
    [[nodiscard]] const Instance* part(const Instance& scope, std::string_view dotted) const;
    /// A member (property, event or action) named by `[part.]*member` in the scope of @p scope.
    [[nodiscard]] std::optional<MemberRef> member(const Instance& scope, std::string_view dotted) const;
    /// Expression resolver for the scope of @p scope (properties of the instance and its parts).
    [[nodiscard]] expr::Resolver resolver(const Instance& scope) const;
    /// Type-level resolver: same rules, symbols are dotted paths (used for validation).
    [[nodiscard]] expr::Resolver type_resolver(const EffectiveType& type) const;

    /// Fixed value of a property for an instance, if any.
    [[nodiscard]] std::optional<std::string> value(const Instance& owner, const Property& property) const;

private:
    void instantiate(Instance& node, std::vector<std::string>& stack, std::vector<Diagnostic>& diagnostics);

    const Document& doc_;
    std::map<std::string, EffectiveType, std::less<>> types_;
    std::vector<std::unique_ptr<Instance>> instances_;  // pre-order
    Instance* root_{nullptr};
    bool ok_{true};
};

/// Symbol type of a property for expressions.
[[nodiscard]] expr::Symbol::Type symbol_type(const Property& property) noexcept;

/// The quantity sort of a unit ("degC" -> "Temperature"), or "Real".
[[nodiscard]] std::string quantity_sort(std::string_view unit);

}  // namespace twin::model::detail
