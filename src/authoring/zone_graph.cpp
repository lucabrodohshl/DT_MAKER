/**
 * @file zone_graph.cpp
 * @brief Internal: exact forward zone-graph exploration with UDBM.
 */
#include "zone_graph.hpp"

#include <algorithm>
#include <cstdlib>
#include <queue>

#include "dbm/dbm.h"

namespace twin::authoring::detail {
namespace {

using Zone = std::vector<raw_t>;

bool constrain(Zone& z, std::size_t dim, const std::vector<compiler::SourceAtom>& atoms) {
    const auto d = static_cast<cindex_t>(dim);
    for (const compiler::SourceAtom& a : atoms) {
        for (const constraint_t& c : to_dbm(a.constraint)) {
            if (!dbm_constrain1(z.data(), d, c.i, c.j, c.value)) return false;
        }
    }
    return !dbm_isEmpty(z.data(), d);
}

/// Time elapse, invariant, extrapolation (the zone must satisfy the invariant already).
bool settle(Zone& z, std::size_t dim, const compiler::SourceLocation& location, const std::vector<std::int32_t>& max) {
    const auto d = static_cast<cindex_t>(dim);
    dbm_up(z.data(), d);
    if (!constrain(z, dim, location.invariant)) return false;
    dbm_extrapolateMaxBounds(z.data(), d, max.data());
    return true;
}

}  // namespace

std::vector<constraint_t> to_dbm(const ir::ClockConstraint& a) {
    const auto i = static_cast<cindex_t>(a.lhs);
    const auto j = static_cast<cindex_t>(a.rhs);
    const auto c = static_cast<int32_t>(a.bound);
    switch (a.op) {
        case ir::Comparison::Less: return {{i, j, dbm_bound2raw(c, dbm_STRICT)}};
        case ir::Comparison::LessEqual: return {{i, j, dbm_bound2raw(c, dbm_WEAK)}};
        case ir::Comparison::Equal: return {{i, j, dbm_bound2raw(c, dbm_WEAK)}, {j, i, dbm_bound2raw(-c, dbm_WEAK)}};
        case ir::Comparison::GreaterEqual: return {{j, i, dbm_bound2raw(-c, dbm_WEAK)}};
        case ir::Comparison::Greater: return {{j, i, dbm_bound2raw(-c, dbm_STRICT)}};
    }
    return {};
}

bool satisfiable(std::vector<raw_t> zone, std::size_t dim, const std::vector<ir::ClockConstraint>& conjunction) {
    const auto d = static_cast<cindex_t>(dim);
    for (const ir::ClockConstraint& a : conjunction) {
        for (const constraint_t& c : to_dbm(a)) {
            if (!dbm_constrain1(zone.data(), d, c.i, c.j, c.value)) return false;
        }
    }
    return !dbm_isEmpty(zone.data(), d);
}

std::vector<std::int32_t> model_max_constants(const compiler::SourceModel& model) {
    std::vector<std::int32_t> max(model.clocks.size() + 1, 0);
    const auto note = [&](const std::vector<compiler::SourceAtom>& atoms) {
        for (const compiler::SourceAtom& a : atoms) {
            const auto c = static_cast<std::int32_t>(std::llabs(a.constraint.bound));
            max[a.constraint.lhs] = std::max(max[a.constraint.lhs], c);
            if (a.constraint.rhs != ir::kReferenceClock) max[a.constraint.rhs] = std::max(max[a.constraint.rhs], c);
        }
    };
    for (const compiler::SourceLocation& l : model.locations) note(l.invariant);
    for (const compiler::SourceEdge& e : model.edges) note(e.guard);
    max[0] = 0;
    return max;
}

bool has_diagonal(const compiler::SourceModel& model) {
    const auto diagonal = [](const std::vector<compiler::SourceAtom>& atoms) {
        return std::any_of(atoms.begin(), atoms.end(),
                           [](const compiler::SourceAtom& a) { return a.constraint.rhs != ir::kReferenceClock; });
    };
    return std::any_of(model.locations.begin(), model.locations.end(),
                       [&](const compiler::SourceLocation& l) { return diagonal(l.invariant); }) ||
           std::any_of(model.edges.begin(), model.edges.end(),
                       [&](const compiler::SourceEdge& e) { return diagonal(e.guard); });
}

ZoneGraph explore(const compiler::SourceModel& model, const std::vector<std::int32_t>& max, std::size_t limit) {
    ZoneGraph g;
    g.dim = model.clocks.size() + 1;
    const auto d = static_cast<cindex_t>(g.dim);
    if (model.locations.empty()) return g;
    Zone init(g.dim * g.dim);
    dbm_zero(init.data(), d);
    if (!constrain(init, g.dim, model.locations[model.initial].invariant) ||
        !settle(init, g.dim, model.locations[model.initial], max)) {
        return g;
    }
    std::vector<std::vector<std::size_t>> passed(model.locations.size());
    g.states.push_back(ZoneState{model.initial, init, 0});
    passed[model.initial].push_back(0);
    std::queue<std::size_t> waiting;
    waiting.push(0);
    while (!waiting.empty()) {
        const std::size_t s = waiting.front();
        waiting.pop();
        for (const compiler::SourceEdge& e : model.edges) {
            if (e.source != g.states[s].location) continue;
            Zone z = g.states[s].zone;
            if (!constrain(z, g.dim, e.guard)) continue;
            for (ir::ClockIndex r : e.resets) dbm_updateValue(z.data(), d, static_cast<cindex_t>(r), 0);
            const compiler::SourceLocation& target = model.locations[e.target];
            if (!constrain(z, g.dim, target.invariant) || !settle(z, g.dim, target, max)) continue;
            const bool covered = std::any_of(passed[e.target].begin(), passed[e.target].end(), [&](std::size_t other) {
                const relation_t rel = dbm_relation(z.data(), g.states[other].zone.data(), d);
                return rel == base_SUBSET || rel == base_EQUAL;
            });
            if (covered) continue;
            if (g.states.size() >= limit) {
                g.complete = false;
                return g;
            }
            g.states.push_back(ZoneState{e.target, std::move(z), s});
            passed[e.target].push_back(g.states.size() - 1);
            waiting.push(g.states.size() - 1);
        }
    }
    return g;
}

}  // namespace twin::authoring::detail
