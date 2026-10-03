/**
 * @file instance.cpp
 * @brief Transactional stateful facade over the pure kernel semantics.
 */
#include "twin/kernel/instance.hpp"

#include <algorithm>
#include <set>

namespace twin::kernel {

Result<KernelInstance> KernelInstance::initialize(std::shared_ptr<const Model> model) {
    if (!model) {
        return make_error(ErrorCode::InvalidArgument, "null model");
    }
    Result<Configuration> init = initial_configuration(*model);
    if (!init) {
        return std::move(init).error();
    }
    return KernelInstance(std::move(model), StateSet::of(std::move(init).value()));
}

Status KernelInstance::restore_state(const StateSet& snapshot) {
    for (const Configuration& c : snapshot.members()) {
        if (Status s = check_configuration(*model_, c); !s) {
            return s;
        }
    }
    state_ = snapshot;
    return ok_status();
}

Result<StateSet> KernelInstance::advance_time(Ticks delta) {
    if (delta < 0) {
        return make_error(ErrorCode::InvalidArgument, "negative delay");
    }
    Result<Ticks> to = checked_add(state_.time(), delta);
    if (!to) {
        return std::move(to).error();
    }
    return advance_to(to.value());
}

Result<StateSet> KernelInstance::advance_to(Ticks time) {
    Result<StateSet> next = kernel::advance_to(*model_, state_, time);
    if (next) {
        state_ = next.value();  // commit only on success
    }
    return next;
}

std::vector<EnabledTransition> KernelInstance::enabled_transitions() const {
    std::vector<EnabledTransition> out;
    const auto& members = state_.members();
    for (std::size_t i = 0; i < members.size(); ++i) {
        for (ir::TransitionIndex e : model_->outgoing(members[i].location)) {
            if (std::optional<DelayWindow> w = enabling_window(*model_, members[i], e)) {
                out.push_back(EnabledTransition{static_cast<std::uint32_t>(i), e, *w, w->earliest == 0});
            }
        }
    }
    return out;
}

Result<ObservationOutcome> KernelInstance::apply_event(std::string_view label, Ticks at) {
    std::optional<LabelId> id = model_->label_id(label);
    if (!id) {
        return make_error(ErrorCode::IncompatibleObservation,
                          "the model has no transition with this action label")
            .with("label", std::string(label));
    }
    Result<ObservationOutcome> outcome = observe(*model_, state_, at, Selector::label(*id));
    if (outcome) {
        state_ = outcome.value().after;  // commit only on success
    }
    return outcome;
}

Result<ObservationOutcome> KernelInstance::apply_transition(ir::TransitionIndex transition,
                                                            Ticks at) {
    Result<ObservationOutcome> outcome =
        observe(*model_, state_, at, Selector::transition(transition));
    if (outcome) {
        state_ = outcome.value().after;
    }
    return outcome;
}

PropositionStatus KernelInstance::evaluate_propositions() const {
    PropositionStatus status;
    std::set<ir::PropositionIndex> possible;
    std::vector<ir::PropositionIndex> certain;
    bool first = true;
    for (const Configuration& c : state_.members()) {
        std::vector<ir::PropositionIndex> here = propositions(*model_, c);
        possible.insert(here.begin(), here.end());
        if (first) {
            certain = here;
            first = false;
        } else {
            std::vector<ir::PropositionIndex> both;
            std::set_intersection(certain.begin(), certain.end(), here.begin(), here.end(),
                                  std::back_inserter(both));
            certain = std::move(both);
        }
    }
    status.certain = std::move(certain);
    status.possible.assign(possible.begin(), possible.end());
    return status;
}

std::vector<std::vector<DiscreteSuccessor>> KernelInstance::successors() const {
    std::vector<std::vector<DiscreteSuccessor>> out;
    for (const Configuration& c : state_.members()) {
        out.push_back(discrete_successors(*model_, c));
    }
    return out;
}

std::vector<ExplorationResult> KernelInstance::predict(const ExplorationLimits& limits) const {
    std::vector<ExplorationResult> out;
    for (const Configuration& c : state_.members()) {
        out.push_back(explore(*model_, c, limits));
    }
    return out;
}

Result<std::vector<ObservationOutcome>> KernelInstance::simulate(
    std::span<const ScheduledObservation> schedule) const {
    return kernel::simulate(*model_, state_, schedule);
}

}  // namespace twin::kernel
