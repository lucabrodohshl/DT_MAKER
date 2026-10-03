/**
 * @file model.cpp
 * @brief Construction of the immutable executable kernel model.
 */
#include "twin/kernel/model.hpp"

#include <algorithm>
#include <map>

#include "twin/ir/validate.hpp"

namespace twin::kernel {
namespace {

Result<std::vector<TickConstraint>> scale(const ir::Conjunction& conjunction, TimeBase base) {
    std::vector<TickConstraint> out;
    out.reserve(conjunction.size());
    for (const ir::ClockConstraint& c : conjunction) {
        Result<Ticks> bound = units_to_ticks(c.bound, base);
        if (!bound) {
            return std::move(bound).error();
        }
        out.push_back(TickConstraint{c.lhs, c.rhs, c.op, bound.value()});
    }
    return out;
}

}  // namespace

Result<std::shared_ptr<const Model>> Model::create(ir::Model ir) {
    if (Status s = ir::validate(ir); !s) {
        return s.error();
    }
    // std::make_shared cannot reach the private constructor.
    std::shared_ptr<Model> m(new Model());  // NOLINT(cppcoreguidelines-owning-memory)
    m->invariants_.reserve(ir.locations.size());
    for (const ir::Location& l : ir.locations) {
        Result<std::vector<TickConstraint>> inv = scale(l.invariant, ir.time);
        if (!inv) {
            return std::move(inv).error().with("location", l.id);
        }
        m->invariants_.push_back(std::move(inv).value());
    }
    m->outgoing_.resize(ir.locations.size());
    std::map<std::string, LabelId> label_ids;
    // Label ids are assigned in ascending lexicographic label order, so they do
    // not depend on transition order (deterministic and canonical).
    for (const ir::Transition& t : ir.transitions) {
        label_ids.emplace(t.action.label(), 0);
    }
    for (auto& [label, id] : label_ids) {
        id = static_cast<LabelId>(m->labels_.size());
        m->labels_.push_back(label);
    }
    m->guards_.reserve(ir.transitions.size());
    for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
        const ir::Transition& t = ir.transitions[i];
        Result<std::vector<TickConstraint>> g = scale(t.guard, ir.time);
        if (!g) {
            return std::move(g).error().with("transition", t.id);
        }
        m->guards_.push_back(std::move(g).value());
        m->outgoing_[t.source].push_back(static_cast<ir::TransitionIndex>(i));
        m->transition_label_.push_back(label_ids.at(t.action.label()));
    }
    m->ir_ = std::move(ir);
    return std::shared_ptr<const Model>(std::move(m));
}

std::span<const TickConstraint> Model::invariant(ir::LocationIndex location) const {
    return invariants_.at(location);
}

std::span<const TickConstraint> Model::guard(ir::TransitionIndex transition) const {
    return guards_.at(transition);
}

const ir::Transition& Model::transition(ir::TransitionIndex transition) const {
    return ir_.transitions.at(transition);
}

std::span<const ir::TransitionIndex> Model::outgoing(ir::LocationIndex location) const {
    return outgoing_.at(location);
}

std::optional<LabelId> Model::label_id(std::string_view label) const {
    const auto it = std::lower_bound(labels_.begin(), labels_.end(), label);
    if (it != labels_.end() && *it == label) {
        return static_cast<LabelId>(it - labels_.begin());
    }
    return std::nullopt;
}

const std::string& Model::label_name(LabelId label) const { return labels_.at(label); }

LabelId Model::label_of(ir::TransitionIndex transition) const {
    return transition_label_.at(transition);
}

}  // namespace twin::kernel
