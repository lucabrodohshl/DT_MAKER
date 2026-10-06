/**
 * @file authoring_fixtures.hpp
 * @brief Small canonical models shared by the authoring unit tests.
 */
#pragma once

#include "twin/authoring/model.hpp"

namespace twin::authoring::test {

/// @brief Atom `clock op bound` with an integer bound.
inline Atom atom(std::string clock, ir::Comparison op, std::int64_t bound) {
    return Atom{std::move(clock), std::nullopt, op, Bound{bound}};
}

/// @brief Atom `clock op NAME` with a constant bound.
inline Atom atom(std::string clock, ir::Comparison op, std::string constant) {
    return Atom{std::move(clock), std::nullopt, op, Bound{std::move(constant)}};
}

/**
 * @brief A valid model exercising every construct of the fragment:
 * two clocks, a constant, a diagonal invariant, a tau edge, constant bounds.
 *
 *   STOPPED --restart!/t:=0--> DEGRADED --start_cooling! [t>=COOL_MIN]/t,u:=0--> COOLING
 *   DEGRADED --tau [t>2]--> STOPPED          COOLING --cooling_complete! [u>=1]--> STOPPED
 */
inline Model pump_like_model() {
    using ir::Comparison;
    Model m;
    m.name = "PumpLike";
    m.note = "test model";
    m.clocks = {ClockDecl{"t", "time in mode"}, ClockDecl{"u", ""}};
    m.constants = {ConstantDecl{"COOL_MIN", 30, "minimum cooling time"}};
    m.channels = {ChannelDecl{"restart", ""}, ChannelDecl{"start_cooling", ""}, ChannelDecl{"cooling_complete", ""}};
    m.locations = {
        LocationDecl{"STOPPED", true, {}, "at rest"},
        LocationDecl{"DEGRADED", false, {atom("t", Comparison::LessEqual, 600)}, ""},
        LocationDecl{"COOLING", false, {Atom{"t", std::string("u"), Comparison::LessEqual, Bound{std::int64_t{5}}}}, ""},
    };
    m.edges = {
        EdgeDecl{"e1", "STOPPED", "DEGRADED", Sync{"restart", '!'}, {}, {"t"}, ""},
        EdgeDecl{"e2", "DEGRADED", "COOLING", Sync{"start_cooling", '!'},
                 {atom("t", Comparison::GreaterEqual, std::string("COOL_MIN"))}, {"t", "u"}, "start the cooler"},
        EdgeDecl{"e3", "DEGRADED", "STOPPED", std::nullopt, {atom("t", Comparison::Greater, 2)}, {}, ""},
        EdgeDecl{"e4", "COOLING", "STOPPED", Sync{"cooling_complete", '!'}, {atom("u", Comparison::GreaterEqual, 1)}, {}, ""},
    };
    return m;
}

/// @brief examples/industrial-pump/models/pump_dt.xml written in TwinTA (same model, same edge order).
inline constexpr const char* kPumpDtText = R"(// DT view of process pump P-101.
automaton ProcessPumpDT {
    clock t;
    channel condition_degraded, condition_recovered, controlled_stop, cooling_complete, fault_reset;
    channel increase_load, pump_stopped, restart, start_cooling, trip;

    initial location STOPPED;
    location NORMAL;
    location DEGRADED { invariant t <= 600; }
    location COOLING { invariant t <= 300; }
    location STOPPING { invariant t <= 60; }
    location FAULT;

    edge e1: STOPPED -> NORMAL { sync restart!; reset t; }
    edge e2: NORMAL -> NORMAL { sync increase_load!; }
    edge e3: NORMAL -> DEGRADED { sync condition_degraded!; reset t; }
    edge e4: DEGRADED -> NORMAL { sync condition_recovered!; reset t; }
    edge e5: DEGRADED -> COOLING { sync start_cooling!; reset t; }
    edge e6: COOLING -> NORMAL { guard t >= 30; sync cooling_complete!; reset t; }
    edge e7: NORMAL -> STOPPING { sync controlled_stop!; reset t; }
    edge e8: DEGRADED -> STOPPING { sync controlled_stop!; reset t; }
    edge e9: COOLING -> STOPPING { sync controlled_stop!; reset t; }
    edge e10: STOPPING -> STOPPED { guard t >= 5; sync pump_stopped!; reset t; }
    edge e11: NORMAL -> FAULT { sync trip!; reset t; }
    edge e12: DEGRADED -> FAULT { sync trip!; reset t; }
    edge e13: COOLING -> FAULT { sync trip!; reset t; }
    edge e14: FAULT -> STOPPED { guard t >= 60; sync fault_reset!; reset t; }
}
)";

}  // namespace twin::authoring::test
