/**
 * @file mission_script.hpp
 * @brief A scripted indoor-drone mission (with deliberately inadmissible inputs).
 */
#pragma once

#include <string>
#include <vector>

#include "twin/ledger/record.hpp"

namespace twin::test {

/// @brief One scripted input and whether the kernel must accept it.
struct ScriptedInput {
    ledger::Input input;
    bool expect_accepted;
    std::string expect_location;  ///< Location after the input (if accepted), else unchanged.
};

inline ledger::Input label(const std::string& name, Ticks at, const std::string& source = "test") {
    return ledger::Input{source, ledger::InputKind::Label, name, at, json::Json::object()};
}

inline ledger::Input advance(Ticks at) {
    return ledger::Input{"test", ledger::InputKind::Advance, "", at, json::Json::object()};
}

/// @brief The scripted mission; times in ticks at R = 1000 (1 tick = 1 ms).
inline std::vector<ScriptedInput> mission_script() {
    return {
        {label("mission_loaded!", 1000), true, "READY"},
        {label("start_mission!", 2000), true, "TAKING_OFF"},
        {label("takeoff_complete!", 2500), false, "TAKING_OFF"},  // climb must take >= 2 s
        {label("takeoff_complete!", 5000), true, "NAVIGATING"},
        {label("waypoint_reached!", 9000), true, "NAVIGATING"},
        {label("path_invalidated!", 12000), true, "REPLANNING"},
        {advance(13000), true, "REPLANNING"},
        {label("plan_accepted!", 13500), true, "NAVIGATING"},
        {label("bogus_event!", 14000), false, "NAVIGATING"},       // not in the model
        {label("target_reached!", 20000), true, "INSPECTING"},
        {label("inspection_complete!", 21000), false, "INSPECTING"},  // scan takes >= 3 s
        {label("inspection_complete!", 25000), true, "NAVIGATING"},
        {label("return_requested!", 25000), true, "RETURNING"},   // same timestamp, ordered
        {label("waypoint_reached!", 19000), false, "RETURNING"},  // logical time cannot go back
        {label("home_reached!", 40000), true, "LANDING"},
        {label("landing_complete!", 43000), true, "LANDED"},
    };
}

}  // namespace twin::test
