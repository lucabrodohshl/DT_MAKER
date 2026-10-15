#include "resolve.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace twin::model::detail {

namespace {

template <class T>
const T* by_name(const std::vector<const T*>& items, std::string_view name) {
    for (const T* item : items) {
        if (item->name == name) return item;
    }
    return nullptr;
}

std::vector<std::string_view> split_dots(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t dot = s.find('.', start);
        out.push_back(s.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return out;
}

}  // namespace

const Property* EffectiveType::property(std::string_view name) const { return by_name(properties, name); }
const Event* EffectiveType::event(std::string_view name) const { return by_name(events, name); }
const Action* EffectiveType::action(std::string_view name) const { return by_name(actions, name); }
const Part* EffectiveType::part(std::string_view name) const { return by_name(parts, name); }

expr::Symbol::Type symbol_type(const Property& property) noexcept {
    switch (property.type) {
        case DataType::Boolean: return expr::Symbol::Type::Boolean;
        case DataType::Enum: return expr::Symbol::Type::Enum;
        case DataType::String: return expr::Symbol::Type::String;
        case DataType::Real:
        case DataType::Integer: return expr::Symbol::Type::Number;
    }
    return expr::Symbol::Type::Number;
}

std::string quantity_sort(std::string_view unit) {
    struct Row {
        std::string_view unit;
        std::string_view sort;
    };
    static constexpr std::array<Row, 34> kUnits{{
        {"degC", "Temperature"}, {"degF", "Temperature"}, {"K", "Temperature"},
        {"bar", "Pressure"}, {"mbar", "Pressure"}, {"Pa", "Pressure"}, {"kPa", "Pressure"}, {"MPa", "Pressure"}, {"psi", "Pressure"},
        {"%", "Ratio"}, {"ratio", "Ratio"},
        {"A", "Current"}, {"mA", "Current"}, {"V", "Voltage"}, {"kV", "Voltage"},
        {"W", "Power"}, {"kW", "Power"}, {"MW", "Power"}, {"Wh", "Energy"}, {"kWh", "Energy"},
        {"rpm", "RotationalSpeed"}, {"Hz", "Frequency"},
        {"m3/h", "FlowRate"}, {"l/min", "FlowRate"}, {"l/s", "FlowRate"},
        {"m", "Length"}, {"mm", "Length"}, {"cm", "Length"},
        {"m/s", "Velocity"}, {"mm/s", "Velocity"}, {"km/h", "Velocity"},
        {"s", "Duration"}, {"kg", "Mass"}, {"l", "Volume"},
    }};
    for (const Row& r : kUnits) {
        if (r.unit == unit) return std::string(r.sort);
    }
    return "Real";
}

Resolved::Resolved(const Document& doc, std::vector<Diagnostic>& diagnostics) : doc_(doc) {
    // Effective types (inheritance).
    for (const AssetType& t : doc.types) {
        EffectiveType e;
        e.type = &t;
        std::vector<const AssetType*> chain;
        const AssetType* cur = &t;
        bool cycle = false;
        while (cur != nullptr) {
            if (std::find(chain.begin(), chain.end(), cur) != chain.end()) {
                cycle = true;
                break;
            }
            chain.push_back(cur);
            cur = cur->extends.empty() ? nullptr : doc.find_type(cur->extends);
        }
        if (cycle) {
            diagnostics.push_back({"error", "TML005", "Type '" + t.name + "' inherits from itself.", "Remove one 'extends' from the cycle.",
                                   "types[" + t.name + "].extends", t.id});
            ok_ = false;
        }
        std::reverse(chain.begin(), chain.end());
        e.lineage = chain;
        for (const AssetType* a : chain) {
            for (const Property& p : a->properties) e.properties.push_back(&p);
            for (const Event& x : a->events) e.events.push_back(&x);
            for (const Action& x : a->actions) e.actions.push_back(&x);
            for (const Part& x : a->parts) e.parts.push_back(&x);
            for (const Relationship& x : a->relationships) e.relationships.push_back(&x);
            for (const Link& x : a->links) e.links.push_back(&x);
            for (const Assumption& x : a->assumptions) e.assumptions.push_back(&x);
            if (a->behavior) {
                e.behavior = &*a->behavior;
                e.behavior_owner = a;
            }
        }
        for (const Property* p : e.properties) {
            if (p->kind == PropertyKind::Fixed && p->value) e.values[p->name] = *p->value;
        }
        for (const AssetType* a : chain) {
            for (const auto& [k, v] : a->values) e.values[k] = v;
        }
        types_.emplace(t.name, std::move(e));
    }

    const EffectiveType* root_type = type(doc.root);
    if (root_type == nullptr) {
        diagnostics.push_back({"error", "TML006", doc.root.empty() ? "The model has no root type." : "The root type '" + doc.root + "' does not exist.",
                               "Set 'root' to the type that is the system.", "model.root", ""});
        ok_ = false;
        return;
    }
    auto root = std::make_unique<Instance>();
    root->type = root_type;
    root->values = root_type->values;
    root_ = root.get();
    instances_.push_back(std::move(root));
    std::vector<std::string> stack{doc.root};
    instantiate(*root_, stack, diagnostics);
}

void Resolved::instantiate(Instance& node, std::vector<std::string>& stack, std::vector<Diagnostic>& diagnostics) {
    if (node.external) return;
    for (const Part* p : node.type->parts) {
        const EffectiveType* pt = type(p->type);
        if (pt == nullptr) continue;  // reported by validation (TML004)
        if (std::find(stack.begin(), stack.end(), p->type) != stack.end()) {
            diagnostics.push_back({"error", "TML005", "Part '" + p->name + "' makes type '" + p->type + "' contain itself.",
                                   "A type cannot contain itself, directly or through its parts.", "types[" + node.type->type->name + "].parts[" + p->name + "]",
                                   p->id});
            ok_ = false;
            continue;
        }
        auto child = std::make_unique<Instance>();
        child->segments = node.segments;
        child->segments.push_back(p->name);
        for (const std::string& s : child->segments) {
            child->path += (child->path.empty() ? "" : "__") + s;
            child->dotted += (child->dotted.empty() ? "" : ".") + s;
        }
        child->type = pt;
        child->part = p;
        child->external = p->external;
        child->parent = &node;
        child->values = pt->values;
        for (const auto& [k, v] : p->values) child->values[k] = v;
        Instance* raw = child.get();
        node.children.push_back(raw);
        instances_.push_back(std::move(child));
        stack.push_back(p->type);
        instantiate(*raw, stack, diagnostics);
        stack.pop_back();
    }
}

const EffectiveType* Resolved::type(std::string_view name) const {
    const auto it = types_.find(name);
    return it == types_.end() ? nullptr : &it->second;
}

std::string Resolved::qualify(const Instance& owner, std::string_view member) {
    return owner.path.empty() ? std::string(member) : owner.path + "__" + std::string(member);
}

std::string Resolved::location_component(const Instance& owner, std::string_view state) {
    std::string s;
    for (const std::string& seg : owner.segments) s += seg + "_";
    return s + std::string(state);
}

const Instance* Resolved::part(const Instance& scope, std::string_view dotted) const {
    if (dotted.empty() || dotted == "self") return &scope;
    const Instance* cur = &scope;
    for (std::string_view seg : split_dots(dotted)) {
        const Instance* next = nullptr;
        for (const Instance* c : cur->children) {
            if (c->segments.back() == seg) {
                next = c;
                break;
            }
        }
        if (next == nullptr) return nullptr;
        cur = next;
    }
    return cur;
}

std::optional<MemberRef> Resolved::member(const Instance& scope, std::string_view dotted) const {
    const std::size_t dot = dotted.rfind('.');
    const std::string_view prefix = dot == std::string_view::npos ? std::string_view{} : dotted.substr(0, dot);
    const std::string_view name = dot == std::string_view::npos ? dotted : dotted.substr(dot + 1);
    const Instance* owner = part(scope, prefix);
    if (owner == nullptr) return std::nullopt;
    MemberRef ref;
    ref.owner = owner;
    ref.property = owner->type->property(name);
    ref.event = owner->type->event(name);
    ref.action = owner->type->action(name);
    if (ref.property == nullptr && ref.event == nullptr && ref.action == nullptr) return std::nullopt;
    return ref;
}

expr::Resolver Resolved::resolver(const Instance& scope) const {
    return [this, &scope](std::string_view path) -> std::optional<expr::Symbol> {
        const auto ref = member(scope, path);
        if (!ref || ref->property == nullptr) return std::nullopt;
        if (ref->property->kind == PropertyKind::PerTwin) {
            expr::Symbol s;
            s.type = expr::Symbol::Type::String;  // per-twin values never enter conditions
            s.smt = qualify(*ref->owner, ref->property->name);
            return s;
        }
        expr::Symbol s;
        s.type = symbol_type(*ref->property);
        s.smt = qualify(*ref->owner, ref->property->name);
        s.literals = ref->property->values;
        return s;
    };
}

expr::Resolver Resolved::type_resolver(const EffectiveType& start) const {
    return [this, &start](std::string_view path) -> std::optional<expr::Symbol> {
        const auto segs = split_dots(path);
        const EffectiveType* cur = &start;
        for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
            const Part* p = cur->part(segs[i]);
            if (p == nullptr) return std::nullopt;
            cur = type(p->type);
            if (cur == nullptr) return std::nullopt;
        }
        const Property* prop = cur->property(segs.back());
        if (prop == nullptr) return std::nullopt;
        expr::Symbol s;
        s.type = prop->kind == PropertyKind::PerTwin ? expr::Symbol::Type::String : symbol_type(*prop);
        s.smt = std::string(path);
        s.literals = prop->values;
        return s;
    };
}

std::optional<std::string> Resolved::value(const Instance& owner, const Property& property) const {
    const auto it = owner.values.find(property.name);
    if (it == owner.values.end()) return std::nullopt;
    return it->second;
}

}  // namespace twin::model::detail
