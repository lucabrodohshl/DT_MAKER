/**
 * @file interpretation.cpp
 * @brief Implementation of InterpretationMap (Definition 6: I: L ∪ ΣE → FOL).
 */

#include "dtpta/interpretation.h"
#include <algorithm>

namespace dtpta {

// -------------------------------------------------------------------------- //
//  InterpretationMap – mutators
// -------------------------------------------------------------------------- //

void InterpretationMap::set_state(const std::string& location_name,
                                   const z3::expr& formula)
{
    // Overwrite if already exists
    for (auto& [k, v] : state_entries_) {
        if (k == location_name) {
            v = formula;
            return;
        }
    }
    state_entries_.emplace_back(location_name, formula);
}

void InterpretationMap::set_event(const std::string& label,
                                   const z3::expr& formula)
{
    for (auto& [k, v] : event_entries_) {
        if (k == label) {
            v = formula;
            return;
        }
    }
    event_entries_.emplace_back(label, formula);
}

// -------------------------------------------------------------------------- //
//  InterpretationMap – accessors
// -------------------------------------------------------------------------- //

std::optional<z3::expr> InterpretationMap::get_state(const std::string& location_name) const {
    for (const auto& [k, v] : state_entries_) {
        if (k == location_name) return v;
    }
    return std::nullopt;
}

std::optional<z3::expr> InterpretationMap::get_event(const std::string& label) const {
    for (const auto& [k, v] : event_entries_) {
        if (k == label) return v;
    }
    return std::nullopt;
}

bool InterpretationMap::has_state(const std::string& location_name) const {
    for (const auto& [k, v] : state_entries_) {
        if (k == location_name) return true;
    }
    return false;
}

bool InterpretationMap::has_event(const std::string& label) const {
    for (const auto& [k, v] : event_entries_) {
        if (k == label) return true;
    }
    return false;
}

std::vector<std::string> InterpretationMap::event_labels() const {
    std::vector<std::string> result;
    result.reserve(event_entries_.size());
    for (const auto& [k, v] : event_entries_) {
        result.push_back(k);
    }
    return result;
}

std::vector<std::string> InterpretationMap::state_names() const {
    std::vector<std::string> result;
    result.reserve(state_entries_.size());
    for (const auto& [k, v] : state_entries_) {
        result.push_back(k);
    }
    return result;
}

} // namespace dtpta
