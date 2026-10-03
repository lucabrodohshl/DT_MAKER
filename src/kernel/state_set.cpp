/**
 * @file state_set.cpp
 * @brief Subset-construction (monitoring) semantics over the kernel transition relation.
 */
#include "twin/kernel/state_set.hpp"

#include <algorithm>

#include "twin/kernel/semantics.hpp"

namespace twin::kernel {

StateSet StateSet::of(Configuration c) { return StateSet(std::vector<Configuration>{std::move(c)}); }

Result<StateSet> StateSet::of(std::vector<Configuration> members) {
    if (members.empty()) {
        return make_error(ErrorCode::Internal, "a state set cannot be empty");
    }
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    const Ticks t = members.front().time;
    if (std::any_of(members.begin(), members.end(),
                    [t](const Configuration& c) { return c.time != t; })) {
        return make_error(ErrorCode::Internal, "members of a state set must share one logical time");
    }
    return StateSet(std::move(members));
}

Result<StateSet> advance_to(const Model& model, const StateSet& states, Ticks to) {
    if (to < states.time()) {
        return make_error(ErrorCode::TimeRegression, "logical time cannot move backwards")
            .with("now", format_time(states.time(), model.time_base()))
            .with("requested", format_time(to, model.time_base()));
    }
    const Ticks d = to - states.time();
    std::vector<Configuration> kept;
    std::optional<Error> last_error;
    for (const Configuration& c : states.members()) {
        Result<Configuration> next = delay(model, c, d);
        if (next) {
            kept.push_back(std::move(next).value());
        } else {
            last_error = next.error();
        }
    }
    if (kept.empty()) {
        Error e = make_error(ErrorCode::IncompatibleObservation,
                             "no possible configuration admits this passage of logical time");
        if (last_error) {
            e.with("reason", last_error->message);
            for (const auto& [k, v] : last_error->context) {
                e.with(k, v);
            }
        }
        return e;
    }
    return StateSet::of(std::move(kept));
}

Result<ObservationOutcome> observe(const Model& model, const StateSet& states, Ticks at,
                                   Selector selector) {
    if (selector.by_transition && selector.id >= model.transition_count()) {
        return make_error(ErrorCode::InvalidArgument, "unknown transition");
    }
    if (!selector.by_transition && selector.id >= model.labels().size()) {
        return make_error(ErrorCode::InvalidArgument, "unknown action label");
    }
    Result<StateSet> delayed = advance_to(model, states, at);
    if (!delayed) {
        return std::move(delayed).error();
    }
    struct Pending {
        std::uint32_t from;
        ir::TransitionIndex transition;
        Configuration target;
    };
    std::vector<Pending> pending;
    const std::vector<Configuration>& pre = delayed.value().members();
    for (std::size_t i = 0; i < pre.size(); ++i) {
        for (const DiscreteSuccessor& s : discrete_successors(model, pre[i])) {
            const bool matches = selector.by_transition
                                     ? s.transition == selector.id
                                     : model.label_of(s.transition) == selector.id;
            if (matches) {
                pending.push_back(Pending{static_cast<std::uint32_t>(i), s.transition, s.target});
            }
        }
    }
    if (pending.empty()) {
        const std::string what = selector.by_transition
                                     ? model.transition(selector.id).id
                                     : model.label_name(selector.id);
        return make_error(ErrorCode::IncompatibleObservation,
                          "the observed step is not enabled in any possible configuration")
            .with("observed", what)
            .with("at", format_time(at, model.time_base()));
    }
    std::vector<Configuration> targets;
    targets.reserve(pending.size());
    for (const Pending& p : pending) {
        targets.push_back(p.target);
    }
    Result<StateSet> after = StateSet::of(targets);
    if (!after) {
        return std::move(after).error();
    }
    ObservationOutcome out{states, at - states.time(), pre, after.value(), {}};
    const std::vector<Configuration>& post = out.after.members();
    for (const Pending& p : pending) {
        const auto it = std::lower_bound(post.begin(), post.end(), p.target);
        out.branches.push_back(
            Branch{p.from, p.transition, static_cast<std::uint32_t>(it - post.begin())});
    }
    return out;
}

}  // namespace twin::kernel
