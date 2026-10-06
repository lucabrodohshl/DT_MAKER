/**
 * @file engine.cpp
 * @brief Studio engine route table: authoring operations on canonical models.
 */
#include "twin/studio_engine/engine.hpp"

#include <algorithm>
#include <fstream>
#include <variant>

#include "twin/alignment/diagnostics.hpp"
#include "twin/authoring/diff.hpp"
#include "twin/authoring/import.hpp"
#include "twin/authoring/properties.hpp"
#include "twin/authoring/text.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/authoring/validate.hpp"
#include "twin/compiler/reader.hpp"
#include "twin/core/large_stack.hpp"
#include "twin/core/sha256.hpp"
#include "twin/ir/codec.hpp"

namespace twin::studio_engine {
namespace {

namespace fs = std::filesystem;
using authoring::Diagnostic;
using authoring::Model;

Error bad_request(std::string message) { return make_error(ErrorCode::InvalidArgument, std::move(message)); }

Result<std::string> string_arg(const json::Json& body, const char* key) {
    if (!body.is_object() || !body.contains(key) || !body.at(key).is_string()) {
        return bad_request(std::string("member '") + key + "' (string) is required");
    }
    return body.at(key).get<std::string>();
}

Result<Model> model_arg(const json::Json& body, const char* key) {
    if (!body.is_object() || !body.contains(key)) return bad_request(std::string("member '") + key + "' (twin-ta/1 model) is required");
    Result<Model> m = authoring::model_from_json(body.at(key));
    if (!m) {
        Error e = bad_request("'" + std::string(key) + "' is not a valid twin-ta/1 model: " + m.error().message);
        for (const auto& [k, v] : m.error().context) e.with(k, v);
        return e;
    }
    return m;
}

Result<authoring::Layout> layout_arg(const json::Json& body) {
    if (!body.contains("layout") || body.at("layout").is_null()) return authoring::Layout{};
    Result<authoring::Layout> l = authoring::layout_from_json(body.at("layout"));
    if (!l) return bad_request("'layout' is not a valid twin-ta-layout/1 document: " + l.error().message);
    return l;
}

json::Json compiler_diagnostics(const std::vector<compiler::Diagnostic>& ds) {
    json::Json out = json::Json::array();
    for (const compiler::Diagnostic& d : ds) {
        out.push_back({{"severity", compiler::to_string(d.severity)},
                       {"code", d.code},
                       {"message", d.message},
                       {"hint", d.hint},
                       {"element", {{"kind", "document"}, {"name", d.where}, {"part", ""}}}});
    }
    return out;
}

// ------------------------------------------------------------------- handlers
Result<json::Json> parse(const Request& req) {
    Result<std::string> source = string_arg(req.body, "source");
    if (!source) return std::move(source).error();
    const authoring::ParseResult p = authoring::parse_text(source.value());
    json::Json out{{"valid", p.model.has_value() && !authoring::has_errors(p.diagnostics)},
                   {"diagnostics", authoring::to_json(p.diagnostics)},
                   {"symbols", authoring::to_json(p.symbols)}};
    if (p.model) {
        out["model"] = authoring::to_json(*p.model);
        out["contentSha256"] = authoring::content_sha256(*p.model);
        if (!authoring::has_errors(p.diagnostics)) out["semanticDigest"] = authoring::semantic_digest(*p.model);
    }
    return out;
}

Result<json::Json> validate(const Request& req) {
    Result<Model> m = model_arg(req.body, "model");
    if (!m) return std::move(m).error();
    const std::vector<Diagnostic> ds = authoring::validate(m.value());
    json::Json out{{"valid", !authoring::has_errors(ds)},
                   {"diagnostics", authoring::to_json(ds)},
                   {"contentSha256", authoring::content_sha256(m.value())},
                   {"text", authoring::print_text(m.value())}};
    if (!authoring::has_errors(ds)) out["semanticDigest"] = authoring::semantic_digest(m.value());
    return out;
}

Result<json::Json> format(const Request& req) {
    Result<std::string> source = string_arg(req.body, "source");
    if (!source) return std::move(source).error();
    const authoring::ParseResult p = authoring::parse_text(source.value());
    json::Json out{{"diagnostics", authoring::to_json(p.diagnostics)}};
    out["source"] = p.model ? json::Json(authoring::print_text(*p.model)) : json::Json(nullptr);
    return out;
}

Result<json::Json> render(const Request& req) {
    Result<Model> m = model_arg(req.body, "model");
    if (!m) return std::move(m).error();
    Result<authoring::Layout> l = layout_arg(req.body);
    if (!l) return std::move(l).error();
    Result<std::string> target = string_arg(req.body, "target");
    if (!target) return std::move(target).error();
    Result<authoring::ExportResult> e = authoring::export_model(m.value(), l.value(), target.value());
    if (!e) return std::move(e).error();
    return json::Json{{"content", e.value().content},
                      {"mediaType", e.value().media_type},
                      {"roundTripVerified", e.value().round_trip_verified}};
}

Result<json::Json> diff(const Request& req) {
    Result<Model> from = model_arg(req.body, "from");
    if (!from) return std::move(from).error();
    Result<Model> to = model_arg(req.body, "to");
    if (!to) return std::move(to).error();
    return authoring::diff_models(from.value(), to.value());
}

Result<json::Json> constraint(const Request& req) {
    Result<std::string> text = string_arg(req.body, "text");
    if (!text) return std::move(text).error();
    const std::string kind = req.body.value("kind", std::string("guard"));
    if (kind != "guard" && kind != "invariant") return bad_request("'kind' must be \"guard\" or \"invariant\"");
    Result<authoring::Constraint> c = authoring::parse_constraint(text.value());
    if (!c) {
        const Error& e = c.error();
        json::Json d{{"severity", "error"},
                     {"code", std::string(e.context_value("code").empty() ? "TWT001" : e.context_value("code"))},
                     {"message", e.message},
                     {"hint", ""},
                     {"element", {{"kind", "document"}, {"name", ""}, {"part", kind}}}};
        if (!e.context_value("column").empty()) {
            const auto col = std::stoul(std::string(e.context_value("column")));
            d["range"] = {{"line", 1}, {"column", col}, {"endLine", 1}, {"endColumn", col}};
        }
        return json::Json{{"diagnostics", json::Json::array({d})}};
    }
    json::Json diagnostics = json::Json::array();
    if (kind == "guard") {
        for (const authoring::Atom& a : c.value()) {
            if (a.minus) {
                diagnostics.push_back({{"severity", "error"},
                                       {"code", "TWM009"},
                                       {"message", "diagonal constraint '" + a.clock + " - " + *a.minus + "' in a guard"},
                                       {"hint", "the aligner's guard parser drops diagonal guards; use them in invariants only"},
                                       {"element", {{"kind", "document"}, {"name", ""}, {"part", "guard"}}}});
            }
        }
    }
    return json::Json{{"atoms", authoring::constraint_to_json(c.value())},
                      {"text", authoring::constraint_text(c.value())},
                      {"diagnostics", diagnostics}};
}

Result<json::Json> import(const Request& req) {
    Result<std::string> content = string_arg(req.body, "content");
    if (!content) return std::move(content).error();
    authoring::ImportOptions o;
    o.filename = req.body.value("filename", std::string());
    if (req.body.contains("options") && req.body.at("options").is_object()) {
        o.legacy_system_declaration = req.body.at("options").value("legacySystemDeclaration", false);
    }
    return authoring::to_json(authoring::import_any(content.value(), o));
}

Result<json::Json> importers(const Request&) {
    json::Json out = json::Json::array();
    for (const authoring::Importer* i : authoring::importers()) {
        out.push_back({{"id", i->id()}, {"version", i->version()}, {"description", i->description()}});
    }
    return out;
}

Result<json::Json> compile(const Request& req, const fs::path& work_dir) {
    Result<Model> m = model_arg(req.body, "model");
    if (!m) return std::move(m).error();
    const std::vector<Diagnostic> structural = authoring::validate(m.value());
    if (authoring::has_errors(structural)) {
        return json::Json{{"compiled", false}, {"diagnostics", authoring::to_json(structural)}};
    }
    compiler::CompileOptions o;
    o.model_id = req.body.value("modelId", std::string("draft"));
    o.model_version = req.body.value("modelVersion", std::string("0.0.0-draft"));
    o.ticks_per_unit = req.body.value("ticksPerUnit", std::int64_t{1000});
    const fs::path dir = work_dir / "compile";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (req.body.contains("interpretation") && req.body.at("interpretation").is_string()) {
        const std::string text = req.body.at("interpretation").get<std::string>();
        const fs::path interp = dir / (sha256_hex(text) + ".interp");
        std::ofstream(interp, std::ios::binary | std::ios::trunc) << text;
        o.interpretation = interp;
    }
    auto outcome = authoring::compile_model(m.value(), o, dir);
    if (auto* failure = std::get_if<compiler::CompileFailure>(&outcome)) {
        return json::Json{{"compiled", false}, {"diagnostics", compiler_diagnostics(failure->diagnostics)}};
    }
    const authoring::CompiledModel& c = std::get<authoring::CompiledModel>(outcome);
    return json::Json{{"compiled", true},
                      {"ir", ir::to_json(c.result.model)},
                      {"irSha256", c.result.manifest.ir_sha256},
                      {"semanticDigest", c.semantic_digest},
                      {"sourceMap", c.source_map},
                      {"manifest", compiler::to_json(c.result.manifest)},
                      {"diagnostics", compiler_diagnostics(c.result.manifest.diagnostics)}};
}

/// A view's toolchain bytes from {<key>Model: twin-ta/1} or {<key>Source: twin-ta/1 JSON text or UPPAAL XML}.
Result<std::string> view_source(const json::Json& body, const std::string& key) {
    if (body.contains(key + "Model")) {
        Result<Model> m = model_arg(body, (key + "Model").c_str());
        if (!m) return std::move(m).error();
        return authoring::render_toolchain_xml(m.value());
    }
    Result<std::string> source = string_arg(body, (key + "Source").c_str());
    if (!source) return bad_request("one of '" + key + "Model' or '" + key + "Source' is required");
    Result<std::string> bytes = authoring::toolchain_source(source.value());
    if (!bytes) return bad_request("'" + key + "Source' is not a usable model: " + bytes.error().message);
    return bytes;
}

std::size_t internal_transitions(const std::string& xml) {
    const compiler::ReadResult r = compiler::read_uppaal(xml, compiler::ReaderOptions{true});
    if (!r.model) return 0;
    return static_cast<std::size_t>(std::count_if(r.model->edges.begin(), r.model->edges.end(), [](const auto& e) {
        return e.action.kind == ir::ActionKind::Internal;
    }));
}

Result<json::Json> alignment_diagnose(const Request& req) {
    if (!req.body.contains("evidence") || !req.body.at("evidence").is_object()) {
        return bad_request("member 'evidence' (twin-alignment-evidence/1) is required");
    }
    const json::Json& evidence = req.body.at("evidence");
    Result<std::string> pt = view_source(req.body, "pt");
    if (!pt) return std::move(pt).error();
    Result<std::string> dt = view_source(req.body, "dt");
    if (!dt) return std::move(dt).error();
    const bool aligned = evidence.contains("verdict") && evidence.at("verdict").value("aligned", false);
    json::Json modes = evidence.contains("modes")
                           ? evidence.at("modes")
                           : alignment::alignment_modes(aligned, internal_transitions(pt.value()), internal_transitions(dt.value()));
    return json::Json{{"verdict", evidence.value("verdict", json::Json::object())},
                      {"modes", modes},
                      {"diagnostics", alignment::to_json(alignment::diagnose(evidence, pt.value(), dt.value()))},
                      {"correspondences",
                       {{"labels", evidence.value("label_equivalence", json::Json::array())},
                        {"locations", evidence.value("location_consistency", json::Json::array())}}}};
}

/// Write @p text to a content-addressed scratch file.
fs::path scratch(const fs::path& dir, const std::string& text, const char* extension) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path p = dir / (sha256_hex(text) + extension);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
    return p;
}

Result<json::Json> alignment_explain(const Request& req, const fs::path& work_dir) {
    Result<std::string> ontology = string_arg(req.body, "ontology");
    if (!ontology) return std::move(ontology).error();
    Result<std::string> ip = string_arg(req.body, "ptInterpretation");
    if (!ip) return std::move(ip).error();
    Result<std::string> id = string_arg(req.body, "dtInterpretation");
    if (!id) return std::move(id).error();
    Result<std::string> pt = string_arg(req.body, "pt");
    if (!pt) return std::move(pt).error();
    Result<std::string> dt = string_arg(req.body, "dt");
    if (!dt) return std::move(dt).error();
    const fs::path dir = work_dir / "explain";
    alignment::AlignmentInputs in;
    in.ontology = scratch(dir, ontology.value(), ".ont");
    in.pt_interpretation = scratch(dir, ip.value(), ".interp");
    in.dt_interpretation = scratch(dir, id.value(), ".interp");
    Result<alignment::PairExplanation> e =
        alignment::explain_pair(in, pt.value(), dt.value(), req.body.value("kind", std::string("event")));
    if (!e) return std::move(e).error();
    return alignment::to_json(e.value());
}

std::optional<std::string> optional_text(const json::Json& body, const char* key) {
    if (body.contains(key) && body.at(key).is_string()) return body.at(key).get<std::string>();
    return std::nullopt;
}

Result<json::Json> properties_analyse(const Request& req) {
    Result<std::string> property = string_arg(req.body, "property");
    if (!property) return std::move(property).error();
    Result<Model> dt = model_arg(req.body, "dtModel");
    if (!dt) return std::move(dt).error();
    return authoring::to_json(authoring::analyse_property(property.value(), dt.value(), optional_text(req.body, "ontology")));
}

Result<json::Json> properties_check(const Request& req) {
    Result<std::string> property = string_arg(req.body, "property");
    if (!property) return std::move(property).error();
    Result<std::string> dt = view_source(req.body, "dt");
    if (!dt) return std::move(dt).error();
    authoring::PropertyCheckInputs in;
    in.property = property.value();
    in.dt_xml = dt.value();
    in.ontology_text = optional_text(req.body, "ontology");
    in.dt_interpretation_text = optional_text(req.body, "dtInterpretation");
    return authoring::check_property(in);
}

Result<json::Json> monitors_validate(const Request& req) {
    if (!req.body.contains("monitors")) return bad_request("member 'monitors' (twin-monitors/1) is required");
    Result<monitoring::MonitorsDocument> doc = monitoring::monitors_from_json(req.body.at("monitors"));
    if (!doc) {
        json::Json d{{"severity", "error"},
                     {"code", "TWN000"},
                     {"message", doc.error().message},
                     {"hint", ""},
                     {"element", {{"kind", "document"}, {"name", std::string(doc.error().context_value("path"))}, {"part", ""}}}};
        return json::Json{{"valid", false}, {"diagnostics", json::Json::array({d})}};
    }
    std::optional<Model> pt;
    std::optional<Model> dt;
    if (req.body.contains("ptModel")) {
        Result<Model> m = model_arg(req.body, "ptModel");
        if (!m) return std::move(m).error();
        pt = std::move(m).value();
    }
    if (req.body.contains("dtModel")) {
        Result<Model> m = model_arg(req.body, "dtModel");
        if (!m) return std::move(m).error();
        dt = std::move(m).value();
    }
    const json::Json telemetry = req.body.value("telemetry", json::Json(nullptr));
    const std::vector<Diagnostic> ds = authoring::validate_monitors(doc.value(), pt ? &*pt : nullptr, dt ? &*dt : nullptr,
                                                                   telemetry, optional_text(req.body, "ontology"));
    return json::Json{{"valid", !authoring::has_errors(ds)}, {"diagnostics", authoring::to_json(ds)}};
}

}  // namespace

Engine::Engine(EngineConfig config) : config_(std::move(config)) {
    std::error_code ec;
    fs::create_directories(config_.work_dir, ec);
}

Engine::~Engine() = default;

std::vector<Route> Engine::routes() {
    const std::string a = "/api/v1/authoring";
    const fs::path work = config_.work_dir;
    std::vector<Route> table = {
        {"POST", a + "/models/parse", parse, 200, "Parse TwinTA text into the canonical model"},
        {"POST", a + "/models/validate", validate, 200, "Validate a canonical model (structure only)"},
        {"POST", a + "/models/format", format, 200, "Format TwinTA text (parse, then print)"},
        {"POST", a + "/models/render", render, 200, "Render a model as TwinTA, JSON or UPPAAL XML"},
        {"POST", a + "/models/diff", diff, 200, "Structural difference of two models"},
        {"POST", a + "/models/compile", [work](const Request& r) { return compile(r, work); }, 200,
         "Compile a draft model to Twin IR (with the IR <-> model source map)"},
        {"POST", a + "/constraints/parse", constraint, 200, "Parse a guard or invariant field"},
        {"POST", a + "/import", import, 200, "Import a model file (UPPAAL XML, twin-ta/1 JSON, TwinTA)"},
        {"GET", a + "/importers", importers, 200, "Registered model importers"},
        {"POST", a + "/properties/analyse", properties_analyse, 200,
         "What the backend can check or monitor for a property (and why)"},
        {"POST", a + "/properties/check", properties_check, 200, "Check a property at design time (evidence document)"},
        {"POST", a + "/monitors/validate", monitors_validate, 200, "Validate a monitor document against the views"},
        {"POST", a + "/alignment/diagnose", alignment_diagnose, 200,
         "Findings, modes and correspondences of an alignment evidence document"},
        {"POST", a + "/alignment/explain", [work](const Request& r) { return alignment_explain(r, work); }, 200,
         "How a PT/DT label (or location) pair relates under the ontology"},
    };
    // HTTP worker threads have small stacks; the formal tools recurse deeply.
    for (Route& r : table) {
        r.handler = [h = std::move(r.handler)](const Request& req) {
            return run_with_large_stack([&] { return h(req); });
        };
    }
    return table;
}

int http_status(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument:
        case ErrorCode::ParseError:
        case ErrorCode::TimeNotRepresentable: return 400;
        case ErrorCode::NotFound: return 404;
        case ErrorCode::StateError:
        case ErrorCode::IntegrityError: return 409;
        case ErrorCode::ValidationError:
        case ErrorCode::UnsupportedConstruct:
        case ErrorCode::IncompatibleObservation:
        case ErrorCode::InvariantViolation:
        case ErrorCode::TransitionNotEnabled:
        case ErrorCode::TimeRegression:
        case ErrorCode::ArithmeticOverflow: return 422;
        case ErrorCode::Unavailable: return 503;
        case ErrorCode::IoError:
        case ErrorCode::Internal: return 500;
    }
    return 500;
}

json::Json error_body(const Error& e) {
    json::Json context = json::Json::array();
    for (const auto& [k, v] : e.context) context.push_back({{"key", k}, {"value", v}});
    return {{"error", {{"code", std::string(to_string(e.code))}, {"message", e.message}, {"context", context}}}};
}

}  // namespace twin::studio_engine
