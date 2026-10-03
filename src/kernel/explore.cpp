/**
 * @file explore.cpp
 * @brief Prediction functions built only from the pure kernel semantics.
 */
#include "twin/kernel/explore.hpp"

#include <algorithm>
#include <deque>

namespace twin::kernel {

std::vector<TimedSuccessor> timed_successors(const Model& model, const Configuration& c) {
    std::vector<TimedSuccessor> out;
    for (ir::TransitionIndex e : model.outgoing(c.location)) {
        std::optional<DelayWindow> w = enabling_window(model, c, e);
        if (!w) {
            continue;
        }
        // By Lemma "enabling windows", the timed step at w->earliest is defined.
        Result<Configuration> next = timed_step(model, c, w->earliest, e);
        if (next) {
            out.push_back(TimedSuccessor{e, *w, std::move(next).value()});
        }
    }
    return out;
}

ExplorationResult explore(const Model& model, const Configuration& root,
                          const ExplorationLimits& limits) {
    ExplorationResult result;
    result.nodes.push_back(ExplorationNode{root, std::nullopt, 0, DelayWindow{}, 0});
    std::deque<std::uint32_t> frontier{0};
    while (!frontier.empty()) {
        const std::uint32_t current = frontier.front();
        frontier.pop_front();
        if (result.nodes[current].depth >= limits.max_depth) {
            continue;
        }
        // Copy: nodes may reallocate while children are appended.
        const Configuration config = result.nodes[current].config;
        const std::uint32_t depth = result.nodes[current].depth;
        for (TimedSuccessor& s : timed_successors(model, config)) {
            if (limits.horizon && s.earliest.time > *limits.horizon) {
                result.truncated = true;
                continue;
            }
            if (result.nodes.size() >= limits.max_nodes) {
                result.truncated = true;
                return result;
            }
            result.nodes.push_back(
                ExplorationNode{std::move(s.earliest), current, s.transition, s.window, depth + 1});
            frontier.push_back(static_cast<std::uint32_t>(result.nodes.size() - 1));
        }
    }
    return result;
}

std::vector<TrajectoryStep> trajectory_to(const ExplorationResult& result, std::uint32_t node) {
    std::vector<TrajectoryStep> steps;
    std::optional<std::uint32_t> cursor = node;
    while (cursor && *cursor < result.nodes.size() && result.nodes[*cursor].parent) {
        const ExplorationNode& n = result.nodes[*cursor];
        steps.push_back(TrajectoryStep{n.via, n.window, n.config});
        cursor = n.parent;
    }
    std::reverse(steps.begin(), steps.end());
    return steps;
}

Result<std::vector<ObservationOutcome>> simulate(const Model& model, const StateSet& start,
                                                 std::span<const ScheduledObservation> schedule) {
    std::vector<ObservationOutcome> outcomes;
    StateSet current = start;  // a copy: the caller's state is never touched
    for (std::size_t i = 0; i < schedule.size(); ++i) {
        Result<ObservationOutcome> step = observe(model, current, schedule[i].at, schedule[i].selector);
        if (!step) {
            return std::move(step).error().with("step", std::to_string(i));
        }
        current = step.value().after;
        outcomes.push_back(std::move(step).value());
    }
    return outcomes;
}

}  // namespace twin::kernel
