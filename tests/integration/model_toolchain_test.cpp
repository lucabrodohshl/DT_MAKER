/**
 * @file model_toolchain_test.cpp
 * @brief The twin modelling language against the real toolchain: generated V_D compiles with
 * translation validation, generated K/I_D/I_P align with an independently written physical
 * view through SemPTDTAlignmentICSE, a behavioural fault is caught, and every view of the
 * aligner corpus lifts into the language and compiles back to itself.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "twin/alignment/alignment.hpp"
#include "twin/authoring/import.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/model/compile.hpp"
#include "twin/model/lift.hpp"

namespace twin::model {
namespace {

namespace fs = std::filesystem;
using json::Json;

std::string read(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

void write(const fs::path& f, const std::string& s) {
    std::ofstream out(f, std::ios::binary | std::ios::trunc);
    out << s;
}

const fs::path kRoot(TWIN_SOURCE_DIR);

/// The tutorial's thermal chamber model with the controller's own UPPAAL model as its physical reference.
Document chamber_with_reference() {
    Decoded d = decode(Json::parse(read(kRoot / "examples/thermal-chamber/model.json")));
    EXPECT_FALSE(has_errors(d.diagnostics)) << to_json(d.diagnostics).dump(2);
    authoring::ImportOptions o;
    o.filename = "chamber_pt.xml";
    o.legacy_system_declaration = true;
    const auto pt = authoring::import_any(read(kRoot / "examples/thermal-chamber/models/chamber_pt.xml"), o);
    EXPECT_TRUE(pt.model.has_value());
    Reference ref = reference_suggestion(d.document, *pt.model);
    // The controller speaks its own vocabulary: map its channels to the model's events and actions.
    ref.labels["heater_on"] = {"start_heating", false};
    ref.labels["heater_off"] = {"stop_heating", false};
    ref.labels["overheat"] = {"high_temperature", false};
    ref.labels["latch_reset"] = {"cooled_down", false};
    d.document.reference = std::move(ref);
    return d.document;
}

struct Written {
    fs::path dir;
    fs::path dt;
    fs::path pt;
    fs::path ontology;
    fs::path dt_interp;
    fs::path pt_interp;
};

Written write_all(const Artifacts& a, const std::string& name) {
    Written w;
    w.dir = fs::temp_directory_path() / ("twin-model-" + name);
    fs::create_directories(w.dir);
    w.dt = w.dir / "dt.xml";
    w.pt = w.dir / "pt.xml";
    w.ontology = w.dir / "k.ont";
    w.dt_interp = w.dir / "dt.interp";
    w.pt_interp = w.dir / "pt.interp";
    write(w.dt, a.automaton_xml);
    write(w.pt, a.reference_xml);
    write(w.ontology, a.ontology);
    write(w.dt_interp, a.dt_interpretation);
    write(w.pt_interp, a.pt_interpretation);
    return w;
}

TEST(ModelToolchain, GeneratedViewCompilesWithTranslationValidation) {
    for (const char* f : {"examples/thermal-chamber/model.json", "examples/pump-station/model.json"}) {
        SCOPED_TRACE(f);
        const Compiled c = compile(Json::parse(read(kRoot / f)));
        ASSERT_TRUE(c.artifacts.has_value()) << to_json(c.diagnostics).dump(2);
        const Written w = write_all(*c.artifacts, fs::path(f).parent_path().filename().string());
        compiler::CompileOptions options;
        options.model_id = "generated";
        options.interpretation = w.dt_interp;
        const auto outcome = compiler::compile_file(w.dt, options);
        const auto* ok = std::get_if<compiler::CompileResult>(&outcome);
        ASSERT_NE(ok, nullptr) << "the generated V_D is rejected by the compiler";
        EXPECT_TRUE(ok->manifest.determinism.event_deterministic);
        EXPECT_EQ(ok->model.locations.size(), c.artifacts->automaton->locations.size());
        EXPECT_EQ(ok->model.transitions.size(), c.artifacts->automaton->edges.size());
    }
}

TEST(ModelToolchain, GeneratedTheoryAlignsWithTheIndependentControllerModel) {
    const Compiled c = compile(chamber_with_reference());
    ASSERT_TRUE(c.artifacts.has_value()) << to_json(c.diagnostics).dump(2);
    const Written w = write_all(*c.artifacts, "chamber-aligned");
    alignment::AlignmentInputs in{w.pt, w.dt, w.ontology, w.pt_interp, w.dt_interp, false};
    const auto evidence = alignment::check_alignment(in);
    ASSERT_TRUE(evidence) << evidence.error().to_string();
    EXPECT_TRUE(evidence.value().aligned);
    EXPECT_TRUE(evidence.value().lint_clean);
    EXPECT_EQ(evidence.value().label_pairs, 4U);
}

TEST(ModelToolchain, ABehaviouralFaultIsNotAligned) {
    Document d = chamber_with_reference();
    for (Transition& t : d.types.front().behavior->transitions) {
        if (t.id == "r3") t.to = "IDLE";  // the twin would forget the over-temperature latch
    }
    const Compiled c = compile(d);
    ASSERT_TRUE(c.artifacts.has_value()) << to_json(c.diagnostics).dump(2);
    const Written w = write_all(*c.artifacts, "chamber-fault");
    alignment::AlignmentInputs in{w.pt, w.dt, w.ontology, w.pt_interp, w.dt_interp, false};
    const auto evidence = alignment::check_alignment(in);
    ASSERT_TRUE(evidence) << evidence.error().to_string();
    EXPECT_FALSE(evidence.value().aligned);
}

TEST(ModelToolchain, EveryCorpusViewLiftsAndCompilesBackToItself) {
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(kRoot / "SemPTDTAlignmentICSE" / "assets")) {
        if (e.is_regular_file() && e.path().extension() == ".xml") files.push_back(e.path());
    }
    for (const char* f : {"models/indoor_drone/V_D_mission_supervisor.xml", "models/indoor_drone/V_P_flight_controller.xml",
                          "examples/industrial-pump/models/pump_dt.xml", "examples/industrial-pump/models/pump_pt.xml",
                          "examples/thermal-chamber/models/chamber_pt.xml"}) {
        files.push_back(kRoot / f);
    }
    std::sort(files.begin(), files.end());
    ASSERT_GE(files.size(), 35U);
    std::size_t lifted = 0;
    for (const fs::path& f : files) {
        SCOPED_TRACE(f.string());
        authoring::ImportOptions o;
        o.filename = f.filename().string();
        o.legacy_system_declaration = true;
        const auto imported = authoring::import_any(read(f), o);
        ASSERT_TRUE(imported.model.has_value());
        const Lifted l = lift(LiftInput{*imported.model, "", "", "corpus", ""});
        ASSERT_TRUE(l.document.has_value()) << to_json(l.diagnostics).dump(2);
        Document doc = *l.document;
        doc.allow_nondeterminism = true;  // corpus views may be nondeterministic; the language says so explicitly
        const Compiled c = compile(doc);
        ASSERT_TRUE(c.artifacts.has_value()) << to_json(c.diagnostics).dump(2);
        std::string why;
        EXPECT_TRUE(same_automaton(*c.artifacts->automaton, *imported.model, &why)) << why;
        ++lifted;
    }
    EXPECT_EQ(lifted, files.size());
}

}  // namespace
}  // namespace twin::model
