/**
 * @file model_builder.hpp
 * @brief Fluent construction of IR models in tests.
 *
 * Produces well-formed, canonical twin::ir::Model values (transition ids,
 * propositions, sorted channels and conjunctions are derived automatically),
 * so tests can describe automata concisely:
 * @code
 *   ir::Model m = test::ModelBuilder("door")
 *       .clock("x")
 *       .location("Closed").location("Open", {B::c("x", "<=", 5)})
 *       .transition("Closed", "open!", "Open", {}, {"x"})
 *       .build();
 * @endcode
 */
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "twin/ir/model.hpp"
#include "twin/ir/validate.hpp"

namespace twin::test {

/// @brief A clock constraint referring to clocks by name (resolved at build()).
struct NamedConstraint {
    std::string clock;
    std::string minus;  ///< empty for a simple constraint
    std::string op;
    std::int64_t bound{0};
};

class ModelBuilder {
public:
    explicit ModelBuilder(std::string id = "test-model") { info_.id = std::move(id); }

    /// @brief Simple constraint `clock op bound`.
    static NamedConstraint c(std::string clock, std::string op, std::int64_t bound) {
        return NamedConstraint{std::move(clock), "", std::move(op), bound};
    }
    /// @brief Diagonal constraint `x - y op bound`.
    static NamedConstraint diff(std::string x, std::string y, std::string op, std::int64_t bound) {
        return NamedConstraint{std::move(x), std::move(y), std::move(op), bound};
    }

    ModelBuilder& ticks_per_unit(std::int64_t r) {
        time_.ticks_per_unit = r;
        return *this;
    }
    ModelBuilder& clock(std::string name) {
        clocks_.push_back(std::move(name));
        return *this;
    }
    ModelBuilder& location(std::string id, std::vector<NamedConstraint> invariant = {}) {
        locations_.emplace_back(std::move(id), std::move(invariant));
        return *this;
    }
    ModelBuilder& initial(std::string id) {
        initial_ = std::move(id);
        return *this;
    }
    /// @brief Add a transition; @p label is "a!", "a?" or "tau".
    ModelBuilder& transition(std::string source, std::string label, std::string target,
                             std::vector<NamedConstraint> guard = {},
                             std::vector<std::string> resets = {}) {
        transitions_.push_back(Edge{std::move(source), std::move(label), std::move(target),
                                    std::move(guard), std::move(resets)});
        return *this;
    }
    ModelBuilder& interpret_location(std::string location, std::string formula) {
        location_interpretations_[std::move(location)] = std::move(formula);
        return *this;
    }

    /// @brief Build and validate; throws std::logic_error on a malformed description.
    ir::Model build() const {
        ir::Model m;
        m.info = info_;
        m.info.version = "1.0.0";
        m.info.source_template = "Test";
        m.time = time_;
        m.clocks = clocks_;
        for (const auto& [id, inv] : locations_) {
            ir::Location l;
            l.id = id;
            for (const NamedConstraint& nc : inv) {
                l.invariant.push_back(resolve(m, nc));
            }
            ir::canonicalize(l.invariant);
            m.locations.push_back(std::move(l));
        }
        m.initial = initial_.empty() ? 0 : index_of(m, initial_);
        std::map<std::string, int> id_count;
        std::vector<std::string> channels;
        for (const Edge& e : transitions_) {
            ir::Transition t;
            t.source = index_of(m, e.source);
            t.target = index_of(m, e.target);
            if (e.label == "tau") {
                t.action = ir::Action{ir::ActionKind::Internal, ""};
            } else {
                const char dir = e.label.back();
                const std::string channel = e.label.substr(0, e.label.size() - 1);
                t.action = ir::Action{dir == '!' ? ir::ActionKind::Send : ir::ActionKind::Receive,
                                      channel};
                channels.push_back(channel);
            }
            for (const NamedConstraint& nc : e.guard) {
                t.guard.push_back(resolve(m, nc));
            }
            ir::canonicalize(t.guard);
            for (const std::string& r : e.resets) {
                t.resets.push_back(clock_id(m, r));
            }
            std::sort(t.resets.begin(), t.resets.end());
            t.resets.erase(std::unique(t.resets.begin(), t.resets.end()), t.resets.end());
            std::string base = e.source + "." + e.label + "." + e.target;
            const int n = id_count[base]++;
            t.id = n == 0 ? base : base + "#" + std::to_string(n);
            m.transitions.push_back(std::move(t));
        }
        std::sort(channels.begin(), channels.end());
        channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
        m.channels = channels;
        for (std::size_t i = 0; i < m.locations.size(); ++i) {
            const auto it = location_interpretations_.find(m.locations[i].id);
            m.propositions.push_back(ir::Proposition{
                "at(" + m.locations[i].id + ")", static_cast<ir::LocationIndex>(i),
                it == location_interpretations_.end() ? "" : it->second});
        }
        if (Status s = ir::validate(m); !s) {
            throw std::logic_error("ModelBuilder produced an invalid model: " + s.error().to_string());
        }
        return m;
    }

private:
    struct Edge {
        std::string source, label, target;
        std::vector<NamedConstraint> guard;
        std::vector<std::string> resets;
    };

    static ir::LocationIndex index_of(const ir::Model& m, const std::string& id) {
        if (auto i = ir::find_location(m, id)) return *i;
        throw std::logic_error("unknown location " + id);
    }
    static ir::ClockIndex clock_id(const ir::Model& m, const std::string& name) {
        if (auto c = ir::find_clock(m, name)) return *c;
        throw std::logic_error("unknown clock " + name);
    }
    static ir::ClockConstraint resolve(const ir::Model& m, const NamedConstraint& nc) {
        auto op = ir::parse_comparison(nc.op);
        if (!op) throw std::logic_error("bad op " + nc.op);
        return ir::ClockConstraint{clock_id(m, nc.clock),
                                   nc.minus.empty() ? ir::kReferenceClock : clock_id(m, nc.minus),
                                   *op, nc.bound};
    }

    ir::ModelInfo info_;
    TimeBase time_{};
    std::vector<std::string> clocks_;
    std::vector<std::pair<std::string, std::vector<NamedConstraint>>> locations_;
    std::string initial_;
    std::vector<Edge> transitions_;
    std::map<std::string, std::string> location_interpretations_;
};

}  // namespace twin::test
