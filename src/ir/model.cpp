/**
 * @file model.cpp
 * @brief Helpers of the in-memory IR model.
 */
#include "twin/ir/model.hpp"

#include <algorithm>

namespace twin::ir {

std::string_view to_string(Comparison op) noexcept {
    switch (op) {
        case Comparison::Less: return "<";
        case Comparison::LessEqual: return "<=";
        case Comparison::Equal: return "==";
        case Comparison::GreaterEqual: return ">=";
        case Comparison::Greater: return ">";
    }
    return "?";
}

std::optional<Comparison> parse_comparison(std::string_view text) noexcept {
    if (text == "<") return Comparison::Less;
    if (text == "<=") return Comparison::LessEqual;
    if (text == "==") return Comparison::Equal;
    if (text == ">=") return Comparison::GreaterEqual;
    if (text == ">") return Comparison::Greater;
    return std::nullopt;
}

std::string Action::label() const {
    switch (kind) {
        case ActionKind::Internal: return std::string(kTauLabel);
        case ActionKind::Send: return channel + "!";
        case ActionKind::Receive: return channel + "?";
    }
    return std::string(kTauLabel);
}

std::string clock_name(const Model& model, ClockIndex clock) {
    if (clock == kReferenceClock) {
        return "0";
    }
    if (clock - 1U < model.clocks.size()) {
        return model.clocks[clock - 1U];
    }
    return "<clock#" + std::to_string(clock) + ">";
}

std::optional<LocationIndex> find_location(const Model& model, std::string_view id) {
    for (std::size_t i = 0; i < model.locations.size(); ++i) {
        if (model.locations[i].id == id) {
            return static_cast<LocationIndex>(i);
        }
    }
    return std::nullopt;
}

std::optional<TransitionIndex> find_transition(const Model& model, std::string_view id) {
    for (std::size_t i = 0; i < model.transitions.size(); ++i) {
        if (model.transitions[i].id == id) {
            return static_cast<TransitionIndex>(i);
        }
    }
    return std::nullopt;
}

std::optional<ClockIndex> find_clock(const Model& model, std::string_view name) {
    for (std::size_t i = 0; i < model.clocks.size(); ++i) {
        if (model.clocks[i] == name) {
            return static_cast<ClockIndex>(i + 1);
        }
    }
    return std::nullopt;
}

std::string to_string(const Model& model, const ClockConstraint& constraint) {
    std::string out = clock_name(model, constraint.lhs);
    if (constraint.rhs != kReferenceClock) {
        out += " - ";
        out += clock_name(model, constraint.rhs);
    }
    out += ' ';
    out += to_string(constraint.op);
    out += ' ';
    out += std::to_string(constraint.bound);
    return out;
}

std::string to_string(const Model& model, const Conjunction& conjunction) {
    if (conjunction.empty()) {
        return "true";
    }
    std::string out;
    for (std::size_t i = 0; i < conjunction.size(); ++i) {
        if (i > 0) {
            out += " && ";
        }
        out += to_string(model, conjunction[i]);
    }
    return out;
}

void canonicalize(Conjunction& conjunction) {
    std::sort(conjunction.begin(), conjunction.end());
    conjunction.erase(std::unique(conjunction.begin(), conjunction.end()), conjunction.end());
}

}  // namespace twin::ir
