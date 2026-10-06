/**
 * @file diagnostics_test.cpp
 * @brief Alignment workspace support: strong/weak modes, failure categories linked to model
 * elements, and label-pair explanations — all from the unmodified aligner's machinery.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <fstream>
#include <set>
#include <sstream>

#include "twin/alignment/alignment.hpp"
#include "twin/alignment/diagnostics.hpp"

namespace twin::alignment {
namespace {

namespace fs = std::filesystem;

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

/// A copy of the pump example's five inputs, with optional text patches.
struct PumpCase {
    fs::path dir;
    AlignmentInputs inputs;

    explicit PumpCase(const std::string& name) {
        dir = fs::temp_directory_path() / ("twin-align-diag-" + name);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path src = fs::path(TWIN_SOURCE_DIR) / "examples/industrial-pump/models";
        for (const char* f : {"pump_pt.xml", "pump_dt.xml", "process-pump.ont", "pt.interp", "dt.interp"}) {
            fs::copy_file(src / f, dir / f);
        }
        inputs = AlignmentInputs{dir / "pump_pt.xml", dir / "pump_dt.xml", dir / "process-pump.ont", dir / "pt.interp",
                                 dir / "dt.interp", false};
    }
    void patch(const char* file, const std::string& from, const std::string& to) const {
        std::string s = read(dir / file);
        const std::size_t p = s.find(from);
        ASSERT_NE(p, std::string::npos) << from;
        s.replace(p, from.size(), to);
        std::ofstream(dir / file, std::ios::binary | std::ios::trunc) << s;
    }
    /// Remove the <transition> from @p src to @p dst from a UPPAAL file.
    void remove_transition(const char* file, const std::string& src, const std::string& dst) const {
        const std::string s = read(dir / file);
        const std::regex t("<transition>\\s*<source ref=\"" + src + "\"/><target ref=\"" + dst + "\"/>[\\s\\S]*?</transition>");
        const std::string out = std::regex_replace(s, t, "", std::regex_constants::format_first_only);
        ASSERT_NE(out, s) << src << " -> " << dst;
        std::ofstream(dir / file, std::ios::binary | std::ios::trunc) << out;
    }
    void drop_line_starting(const char* file, const std::string& prefix) const {
        std::stringstream in(read(dir / file));
        std::string out;
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(prefix, 0) != 0) out += line + "\n";
        }
        std::ofstream(dir / file, std::ios::binary | std::ios::trunc) << out;
    }
    [[nodiscard]] std::vector<AlignmentDiagnostic> diagnostics(const AlignmentEvidence& ev) const {
        return diagnose(to_json(ev), read(inputs.pt_model), read(inputs.dt_model));
    }
};

std::vector<AlignmentDiagnostic> of(const std::vector<AlignmentDiagnostic>& ds, const std::string& category) {
    std::vector<AlignmentDiagnostic> out;
    std::copy_if(ds.begin(), ds.end(), std::back_inserter(out),
                 [&](const AlignmentDiagnostic& d) { return d.category == category; });
    return out;
}

bool links_to(const AlignmentDiagnostic& d, const std::string& view, const std::string& kind, const std::string& text) {
    return std::any_of(d.links.begin(), d.links.end(), [&](const ElementLink& l) {
        return l.view == view && l.kind == kind && l.name.find(text) != std::string::npos;
    });
}

TEST(AlignmentModes, PumpIsAlignedWeaklyAndStronglyBecauseItHasNoTau) {
    PumpCase c("aligned");
    Result<AlignmentEvidence> ev = check_alignment(c.inputs);
    ASSERT_TRUE(ev) << ev.error().to_string();
    EXPECT_TRUE(ev.value().aligned);
    EXPECT_EQ(ev.value().pt_internal_transitions, 0u);
    EXPECT_EQ(ev.value().dt_internal_transitions, 0u);
    const json::Json modes = to_json(ev.value()).at("modes");
    EXPECT_EQ(modes.at("weak"), "aligned");
    EXPECT_EQ(modes.at("strong"), "aligned");
    EXPECT_FALSE(modes.at("strong_reason").get<std::string>().empty());
    const auto ds = c.diagnostics(ev.value());
    EXPECT_TRUE(std::none_of(ds.begin(), ds.end(), [](const AlignmentDiagnostic& d) { return d.severity == "error"; }))
        << to_json(ds).dump(2);
}

TEST(AlignmentModes, StrongIsNotDecidableWithInternalTransitions) {
    const fs::path m = fs::path(TWIN_SOURCE_DIR) / "models/indoor_drone";
    Result<AlignmentEvidence> ev = check_alignment(AlignmentInputs{m / "V_P_flight_controller.xml", m / "V_D_mission_supervisor.xml",
                                                                   m / "domain.ont", m / "pt.interp", m / "dt.interp", false});
    ASSERT_TRUE(ev) << ev.error().to_string();
    EXPECT_TRUE(ev.value().aligned);
    EXPECT_EQ(ev.value().pt_internal_transitions, 2u);
    const json::Json modes = to_json(ev.value()).at("modes");
    EXPECT_EQ(modes.at("weak"), "aligned");
    EXPECT_EQ(modes.at("strong"), "not_decidable");
}

TEST(AlignmentDiagnostics, UnmatchedEventsAreLinkedToEdgesAndInterpretations) {
    PumpCase c("unmatched");
    c.patch("pt.interp", "start_cmd!        : start_requested", "start_cmd!        : (and start_requested load_increase_requested)");
    Result<AlignmentEvidence> ev = check_alignment(c.inputs);
    ASSERT_TRUE(ev) << ev.error().to_string();
    EXPECT_FALSE(ev.value().aligned);
    EXPECT_EQ(to_json(ev.value()).at("modes").at("strong"), "not_aligned");
    const auto ds = c.diagnostics(ev.value());
    const auto pt = of(ds, "unmatched_pt_event");
    ASSERT_EQ(pt.size(), 1u) << to_json(ds).dump(2);
    EXPECT_EQ(pt[0].severity, "error");
    EXPECT_TRUE(links_to(pt[0], "pt", "edge", "start_cmd!")) << to_json(pt[0]).dump();
    EXPECT_TRUE(links_to(pt[0], "pt_interpretation", "entry", "start_cmd!"));
    const auto dt = of(ds, "unmatched_dt_event");
    ASSERT_EQ(dt.size(), 1u) << to_json(ds).dump(2);
    EXPECT_TRUE(links_to(dt[0], "dt", "edge", "restart!"));
}

TEST(AlignmentDiagnostics, MissingInterpretationIsReported) {
    PumpCase c("missing");
    c.drop_line_starting("dt.interp", "trip!");
    Result<AlignmentEvidence> ev = check_alignment(c.inputs);
    ASSERT_TRUE(ev) << ev.error().to_string();
    const auto ds = c.diagnostics(ev.value());
    const auto missing = of(ds, "missing_interpretation");
    ASSERT_EQ(missing.size(), 1u) << to_json(ds).dump(2);
    EXPECT_TRUE(links_to(missing[0], "dt", "edge", "trip!"));
    EXPECT_TRUE(links_to(missing[0], "dt_interpretation", "entry", "trip!"));
    // A label without interpretation is not also reported as "unmatched".
    for (const auto& d : of(ds, "unmatched_dt_event")) EXPECT_FALSE(links_to(d, "dt", "edge", "trip!"));
}

TEST(AlignmentDiagnostics, BranchingMismatchBetweenEquivalentLabels) {
    // PT RUN_ALARM can ramp down; the DT's DEGRADED no longer can (edge removed).
    PumpCase c("branching");
    c.remove_transition("pump_dt.xml", "DEGRADED", "STOPPING");
    Result<AlignmentEvidence> ev = check_alignment(c.inputs);
    ASSERT_TRUE(ev) << ev.error().to_string();
    ASSERT_FALSE(ev.value().aligned);
    const auto ds = c.diagnostics(ev.value());
    const auto mismatch = of(ds, "timing_or_branching");
    ASSERT_EQ(mismatch.size(), 1u) << to_json(ds).dump(2) << "\ncounterexample: " << ev.value().counterexample_pt
                                   << " / " << ev.value().counterexample_dt;
    EXPECT_EQ(mismatch[0].severity, "error");
    EXPECT_FALSE(mismatch[0].links.empty());
}

TEST(AlignmentDiagnostics, AlignerDoesNotCompareGuardBoundsOfEquivalentLabels) {
    // Documented behaviour of the unmodified aligner (docs/existing-aligner-integration.md,
    // finding 14): delays are folded into zone-graph successors, so a DT guard t >= 40 against
    // the PT's t >= 30 on equivalent labels is still ALIGNED. The workspace must not claim that
    // alignment checks such bounds; this test pins the behaviour so a change of the aligner is noticed.
    PumpCase c("timing");
    c.patch("pump_dt.xml", "t &gt;= 30", "t &gt;= 40");
    Result<AlignmentEvidence> ev = check_alignment(c.inputs);
    ASSERT_TRUE(ev) << ev.error().to_string();
    EXPECT_TRUE(ev.value().aligned);
}

TEST(AlignmentDiagnostics, LocationsWithoutEquivalentAreInformational) {
    PumpCase c("states");
    // Under the axioms (not motor_running) already implies shaft_speed = 0, so add a condition
    // that no PT location has.
    c.patch("dt.interp", "STOPPED   : (and (not motor_running) (= shaft_speed 0))",
            "STOPPED   : (and (not motor_running) cooling_active)");
    Result<AlignmentEvidence> ev = check_alignment(c.inputs);
    ASSERT_TRUE(ev) << ev.error().to_string();
    const auto states = of(c.diagnostics(ev.value()), "state_inconsistency");
    ASSERT_EQ(states.size(), 1u);
    EXPECT_EQ(states[0].severity, "info");
    EXPECT_TRUE(links_to(states[0], "dt", "location", "STOPPED"));
}

TEST(PairExplanation, DirectionsOfEntailment) {
    PumpCase c("pairs");
    Result<PairExplanation> same = explain_pair(c.inputs, "start_cmd!", "restart!", "event");
    ASSERT_TRUE(same) << same.error().to_string();
    EXPECT_TRUE(same.value().equivalent);
    Result<PairExplanation> unrelated = explain_pair(c.inputs, "start_cmd!", "trip!", "event");
    ASSERT_TRUE(unrelated);
    EXPECT_FALSE(unrelated.value().equivalent);
    EXPECT_FALSE(unrelated.value().pt_implies_dt);
    EXPECT_FALSE(unrelated.value().dt_implies_pt);
    Result<PairExplanation> locations = explain_pair(c.inputs, "RUN_NORMAL", "NORMAL", "location");
    ASSERT_TRUE(locations);
    EXPECT_TRUE(locations.value().equivalent);

    PumpCase s("pairs-strong");
    s.patch("pt.interp", "start_cmd!        : start_requested", "start_cmd!        : (and start_requested load_increase_requested)");
    Result<PairExplanation> one_way = explain_pair(s.inputs, "start_cmd!", "restart!", "event");
    ASSERT_TRUE(one_way);
    EXPECT_FALSE(one_way.value().equivalent);
    EXPECT_TRUE(one_way.value().pt_implies_dt);
    EXPECT_FALSE(one_way.value().dt_implies_pt);
    EXPECT_FALSE(explain_pair(c.inputs, "no_such!", "restart!", "event"));
}

}  // namespace
}  // namespace twin::alignment
