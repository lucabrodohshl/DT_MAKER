/**
 * @file import_test.cpp
 * @brief Importer framework: UPPAAL XML (strict, preserved, located), TwinTA text, JSON; export; model diff.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "authoring_fixtures.hpp"
#include "twin/authoring/diff.hpp"
#include "twin/authoring/import.hpp"
#include "twin/authoring/text.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/core/sha256.hpp"

namespace twin::authoring {
namespace {

namespace fs = std::filesystem;

std::string read(const fs::path& relative) {
    std::ifstream in(fs::path(TWIN_SOURCE_DIR) / relative, std::ios::binary);
    std::stringstream b;
    b << in.rdbuf();
    return b.str();
}

Model without_notes(Model m) {
    m.note.clear();
    for (auto& c : m.clocks) c.note.clear();
    for (auto& c : m.constants) c.note.clear();
    for (auto& c : m.channels) c.note.clear();
    for (auto& l : m.locations) l.note.clear();
    for (auto& e : m.edges) e.note.clear();
    return m;
}

const Diagnostic* find_code(const std::vector<Diagnostic>& ds, const std::string& code) {
    const auto it = std::find_if(ds.begin(), ds.end(), [&](const Diagnostic& d) { return d.code == code; });
    return it == ds.end() ? nullptr : &*it;
}

TEST(ImportUppaal, PumpDtGivesTheSameModelAsItsTwinTaText) {
    const std::string xml = read("examples/industrial-pump/models/pump_dt.xml");
    const ImportResult r = import_any(xml, ImportOptions{"pump_dt.xml", false});
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump(2);
    EXPECT_EQ(r.format, "uppaal-xml");
    const ParseResult expected = parse_text(test::kPumpDtText);
    ASSERT_TRUE(expected.model.has_value());
    EXPECT_EQ(without_notes(*r.model), without_notes(*expected.model));
    EXPECT_NE(r.model->note.find("Digital Twin view of centrifugal process pump P-101"), std::string::npos)
        << r.model->note;

    ASSERT_TRUE(r.provenance.has_value());
    const Provenance& p = *r.provenance;
    EXPECT_EQ(p.original_filename, "pump_dt.xml");
    EXPECT_EQ(p.original_sha256, sha256_hex(xml));
    EXPECT_EQ(p.importer, "uppaal-xml");
    EXPECT_FALSE(p.importer_version.empty());
    EXPECT_FALSE(p.imported_at.empty());
    EXPECT_TRUE(p.preserved);
    EXPECT_GT(p.preservation_checks, 0u);
    EXPECT_EQ(p.content_sha256, content_sha256(*r.model));
    EXPECT_EQ(p.semantic_digest, semantic_digest(*r.model));

    ASSERT_TRUE(r.layout.locations.contains("NORMAL"));
    EXPECT_EQ(r.layout.locations.at("NORMAL").position, (Point{300, 0}));
    ASSERT_TRUE(r.layout.locations.at("NORMAL").label.has_value());
    EXPECT_EQ(*r.layout.locations.at("NORMAL").label, (Point{280, -34}));
}

TEST(ImportUppaal, KeepsConstantNamesNotesAndLayout) {
    const ImportResult r = import_any(read("tests/fixtures/authoring/with_constants.xml"), ImportOptions{"oven.xml", false});
    ASSERT_TRUE(r.model.has_value()) << to_json(r.diagnostics).dump(2);
    const Model& m = *r.model;
    EXPECT_EQ(m.name, "Oven");
    EXPECT_EQ(m.note, "Oven controller with named timing constants.");
    ASSERT_EQ(m.constants.size(), 2u);
    EXPECT_EQ(m.constants[0], (ConstantDecl{"WARMUP", 30, ""}));
    EXPECT_EQ(m.constants[1].name, "HOLD_MAX");
    EXPECT_EQ(m.constants[1].value, 120);
    EXPECT_EQ(m.clocks[0].note, "heating timer");
    EXPECT_EQ(m.locations[1].invariant[0].bound, Bound{std::string("WARMUP")});
    EXPECT_EQ(m.locations[1].note, "the element heats up");
    EXPECT_EQ(m.edges[1].guard[0].bound, Bound{std::int64_t{25}});
    EXPECT_EQ(m.edges[1].note, "temperature reached");
    ASSERT_TRUE(r.layout.edges.contains("e3"));
    EXPECT_EQ(r.layout.edges.at("e3").nails, (std::vector<Point>{{400, 140}, {0, 140}}));
    EXPECT_EQ(r.layout.edges.at("e3").label, (Point{180, 100}));
    EXPECT_EQ(r.layout.locations.at("Off").label, (Point{-10, -30}));
    ASSERT_TRUE(r.provenance.has_value());
    EXPECT_TRUE(r.provenance->preserved);
}

TEST(ImportUppaal, ReportsEveryUnsupportedConstructWithItsPosition) {
    const ImportResult r = import_any(read("tests/fixtures/authoring/unsupported_mix.xml"), ImportOptions{"mix.xml", false});
    EXPECT_FALSE(r.model.has_value());
    EXPECT_FALSE(r.provenance.has_value());
    struct Expected {
        const char* code;
        std::uint32_t line;
    };
    for (const Expected& e : {Expected{"TWC014", 7}, Expected{"TWC012", 8}, Expected{"TWC031", 17}, Expected{"TWC051", 25}}) {
        const Diagnostic* d = find_code(r.diagnostics, e.code);
        ASSERT_NE(d, nullptr) << e.code << " missing in " << to_json(r.diagnostics).dump(2);
        EXPECT_EQ(d->severity, "error");
        ASSERT_TRUE(d->range.has_value()) << e.code;
        EXPECT_EQ(d->range->line, e.line) << e.code << ": " << to_json(*d).dump();
        EXPECT_FALSE(d->message.empty());
    }
    const Diagnostic* urgent = find_code(r.diagnostics, "TWC031");
    ASSERT_NE(urgent, nullptr);
    EXPECT_EQ(urgent->element, (ElementRef{"location", "Rush", ""}));
    EXPECT_FALSE(urgent->hint.empty());
}

TEST(ImportUppaal, RejectsMalformedDocuments) {
    const ImportResult r = import_any("<nta><template>", ImportOptions{"broken.xml", false});
    EXPECT_FALSE(r.model.has_value());
    EXPECT_TRUE(has_errors(r.diagnostics));
}

TEST(Importers, AreRegisteredAndDetectTheirFormats) {
    std::set<std::string> ids;
    for (const Importer* i : importers()) ids.insert(i->id());
    EXPECT_EQ(ids, (std::set<std::string>{"uppaal-xml", "twin-ta-json", "twinta-text"}));
    EXPECT_EQ(import_any("automaton M { initial location A; }", ImportOptions{"m.tta", false}).format, "twinta-text");
    EXPECT_EQ(import_any(to_json(test::pump_like_model()).dump(), ImportOptions{"m.json", false}).format,
              "twin-ta-json");
    const ImportResult unknown = import_any("PK\x03\x04 zip bytes", ImportOptions{"bundle.zip", false});
    EXPECT_EQ(unknown.format, "unknown");
    ASSERT_NE(find_code(unknown.diagnostics, "TWI001"), nullptr);
}

TEST(Importers, TwinTaTextAndJsonImportsAreLossless) {
    const ImportResult text = import_any(print_text(test::pump_like_model()), ImportOptions{"pump.tta", false});
    ASSERT_TRUE(text.model.has_value()) << to_json(text.diagnostics).dump();
    EXPECT_EQ(*text.model, test::pump_like_model());
    ASSERT_TRUE(text.provenance.has_value());
    EXPECT_EQ(text.provenance->importer, "twinta-text");
    EXPECT_TRUE(text.provenance->preserved);

    const ImportResult json = import_any(to_json(test::pump_like_model()).dump(2), ImportOptions{"pump.json", false});
    ASSERT_TRUE(json.model.has_value()) << to_json(json.diagnostics).dump();
    EXPECT_EQ(*json.model, test::pump_like_model());
}

TEST(Importers, ResultSerialises) {
    const ImportResult r = import_any(read("tests/fixtures/authoring/with_constants.xml"), ImportOptions{"oven.xml", false});
    const json::Json j = to_json(r);
    EXPECT_EQ(j.at("format"), "uppaal-xml");
    EXPECT_EQ(j.at("model").at("format"), "twin-ta/1");
    EXPECT_EQ(j.at("layout").at("format"), "twin-ta-layout/1");
    EXPECT_EQ(j.at("provenance").at("importer"), "uppaal-xml");
    EXPECT_TRUE(j.at("provenance").at("preserved").get<bool>());
    EXPECT_TRUE(j.at("diagnostics").is_array());
}

TEST(Export, UppaalExportRoundTripsSemanticsAndLayout) {
    Layout l;
    l.locations["STOPPED"] = LocationLayout{Point{0, 0}, std::nullopt};
    l.locations["COOLING"] = LocationLayout{Point{400, 120}, Point{390, 90}};
    l.edges["e2"] = EdgeLayout{{Point{200, 60}}, Point{210, 40}};
    Result<ExportResult> e = export_model(test::pump_like_model(), l, "uppaal");
    ASSERT_TRUE(e) << e.error().to_string();
    EXPECT_TRUE(e.value().round_trip_verified);
    EXPECT_EQ(e.value().media_type, "application/xml");
    const ImportResult back = import_any(e.value().content, ImportOptions{"exported.xml", false});
    ASSERT_TRUE(back.model.has_value()) << to_json(back.diagnostics).dump();
    EXPECT_EQ(semantic_digest(*back.model), semantic_digest(test::pump_like_model()));
    EXPECT_EQ(back.layout.locations.at("COOLING").position, (Point{400, 120}));
    EXPECT_EQ(back.layout.edges.at("e2").nails, (std::vector<Point>{{200, 60}}));
    EXPECT_EQ(back.model->edges[1].note, "start the cooler");
    EXPECT_EQ(back.model->locations[0].note, "at rest");
}

TEST(Export, TextJsonAndToolchainExports) {
    for (const char* target : {"twinta", "json", "uppaal-toolchain"}) {
        Result<ExportResult> e = export_model(test::pump_like_model(), Layout{}, target);
        ASSERT_TRUE(e) << target << ": " << e.error().to_string();
        EXPECT_TRUE(e.value().round_trip_verified) << target;
        EXPECT_FALSE(e.value().content.empty());
    }
    EXPECT_FALSE(export_model(test::pump_like_model(), Layout{}, "scxml"));
    Model broken = test::pump_like_model();
    broken.edges[0].target = "NOWHERE";
    EXPECT_FALSE(export_model(broken, Layout{}, "uppaal"));
}

TEST(Diff, ClassifiesSemanticAndNoteChanges) {
    const Model a = test::pump_like_model();
    Model notes = a;
    notes.locations[0].note = "changed note";
    const json::Json n = diff_models(a, notes);
    EXPECT_FALSE(n.at("semanticChange").get<bool>());
    ASSERT_EQ(n.at("notes").size(), 1u);
    EXPECT_EQ(n.at("notes")[0].at("element").at("name"), "STOPPED");

    Model b = a;
    b.locations[1].invariant[0].bound = Bound{std::int64_t{900}};
    b.edges.push_back(EdgeDecl{"e5", "STOPPED", "STOPPED", std::nullopt, {}, {}, ""});
    b.clocks.push_back(ClockDecl{"w", ""});
    b.edges[0].resets.clear();
    const json::Json d = diff_models(a, b);
    EXPECT_TRUE(d.at("semanticChange").get<bool>());
    EXPECT_EQ(d.at("clocks").at("added"), json::Json::array({"w"}));
    ASSERT_EQ(d.at("locations").at("changed").size(), 1u);
    EXPECT_EQ(d.at("locations").at("changed")[0].at("name"), "DEGRADED");
    EXPECT_EQ(d.at("locations").at("changed")[0].at("invariant").at("to"), "t <= 900");
    EXPECT_EQ(d.at("edges").at("added"), json::Json::array({"e5"}));
    ASSERT_EQ(d.at("edges").at("changed").size(), 1u);
    EXPECT_EQ(d.at("edges").at("changed")[0].at("id"), "e1");
    EXPECT_TRUE(d.at("edges").at("changed")[0].at("fields").contains("resets"));
    EXPECT_TRUE(diff_models(a, a).at("edges").at("changed").empty());
}

}  // namespace
}  // namespace twin::authoring
