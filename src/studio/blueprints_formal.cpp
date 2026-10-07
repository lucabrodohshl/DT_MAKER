/**
 * @file blueprints_formal.cpp
 * @brief BlueprintService: PT/DT views, ontology and interpretations, formal checks (see blueprints.hpp).
 */
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "blueprints_impl.hpp"
#include "twin/authoring/import.hpp"
#include "twin/authoring/layout.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/authoring/validate.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/core/large_stack.hpp"
#include "twin/core/sha256.hpp"
#include "twin/ontology/source.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using json::Json;
using namespace twin::platform;

namespace {

/// The canonical model of a model artefact's content (legacy UPPAAL XML is imported on the fly).
struct LoadedModel {
    std::optional<authoring::Model> model;
    authoring::Layout layout;
    Json diagnostics = Json::array();
    std::string format;
};

LoadedModel load_model(std::string_view content) {
    LoadedModel out;
    out.format = std::string(authoring::content_format(content));
    if (out.format == "twin-ta/1") {
        auto j = json::parse(content);
        if (!j) {
            out.diagnostics.push_back(Json{{"severity", "error"}, {"code", "TWI004"}, {"message", j.error().message}});
            return out;
        }
        auto m = authoring::model_from_json(j.value());
        if (!m) {
            out.diagnostics.push_back(Json{{"severity", "error"}, {"code", "TWI003"}, {"message", m.error().message}});
            return out;
        }
        out.model = std::move(m).value();
        return out;
    }
    authoring::ImportOptions options;
    options.filename = "model.xml";
    const authoring::ImportResult r = run_with_large_stack([&] { return authoring::import_any(content, options); });
    out.model = r.model;
    out.layout = r.layout;
    out.diagnostics = authoring::to_json(r.diagnostics);
    return out;
}

const char* short_of(std::string_view role) { return role == "pt_model" ? "pt" : "dt"; }

/// Observables an interpretation must cover: locations and sent labels (receives cannot be interpreted).
std::vector<std::pair<std::string, std::string>> observables(const authoring::Model& m) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& l : m.locations) out.emplace_back(l.name, "location");
    std::set<std::string> seen;
    for (const auto& e : m.edges) {
        if (!e.sync || e.sync->direction != '!') continue;
        const std::string label = e.sync->channel + "!";
        if (seen.insert(label).second) out.emplace_back(label, "event");
    }
    return out;
}

}  // namespace

// --------------------------------------------------------------------------------- models

Result<Json> BlueprintService::model(std::string_view id, std::int64_t v, std::string_view short_role) {
    auto role = model_role(short_role);
    if (!role) return std::move(role).error();
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    Json out{{"role", role.value()},
             {"editable", ver.value().state == "draft"},
             {"revision", ver.value().revision},
             {"artifact", nullptr},
             {"model", nullptr},
             {"layout", nullptr},
             {"diagnostics", Json::array()},
             {"semanticDigest", nullptr}};
    auto pin = ver.value().pins.find(role.value());
    if (pin == ver.value().pins.end()) return out;
    auto ref = parse_ref(pin->second);
    if (!ref) return std::move(ref).error();
    std::string content;
    {
        auto l = impl_->core.lock();
        auto c = impl_->core.artifacts->content(ref.value());
        if (!c) return std::move(c).error();
        content = std::move(c).value();
        auto summary = impl_->core.version_summary(ref.value());
        if (summary) out["artifact"] = summary.value();
        auto av = impl_->core.artifacts->version(ref.value());
        if (av) {
            auto ev = impl_->core.evidence->latest_for(EvidenceKind::Validation, {{"subject", ref.value(), av.value().content_sha256}});
            out["validation"] = ev && ev.value() ? to_json(*ev.value()) : Json(nullptr);
        }
    }
    LoadedModel lm = load_model(content);
    out["contentFormat"] = lm.format;
    if (!lm.model) {
        out["diagnostics"] = lm.diagnostics;
        return out;
    }
    out["model"] = authoring::to_json(*lm.model);
    const auto diags = authoring::validate(*lm.model);
    out["diagnostics"] = authoring::to_json(diags);
    if (!authoring::has_errors(diags)) out["semanticDigest"] = authoring::semantic_digest(*lm.model);
    const Json& stored = ver.value().document.value("behavior", Json::object()).value(short_of(role.value()), Json::object());
    if (stored.contains("layout") && !stored.at("layout").is_null()) {
        out["layout"] = stored.at("layout");
    } else if (!lm.layout.locations.empty()) {
        out["layout"] = authoring::to_json(lm.layout);
    }
    return out;
}

Result<Json> BlueprintService::save_model(std::string_view id, std::int64_t v, std::string_view short_role, std::int64_t revision,
                                          const Json& model_json, const Json& layout_json, const Actor& actor) {
    auto role = model_role(short_role);
    if (!role) return std::move(role).error();
    auto m = authoring::model_from_json(model_json);
    if (!m) return make_error(ErrorCode::InvalidArgument, "not a twin-ta/1 model: " + m.error().message);
    std::optional<Json> layout;
    if (!layout_json.is_null()) {
        auto l = authoring::layout_from_json(layout_json);
        if (!l) return make_error(ErrorCode::InvalidArgument, "not a twin-ta-layout/1 document: " + l.error().message);
        layout = authoring::to_json(l.value());
    }
    auto ver = impl_->load_draft(id, v, revision);
    if (!ver) return std::move(ver).error();
    BlueprintVersion next = ver.value();
    auto ref = impl_->ensure_editable(next, role.value(), actor);
    if (!ref) return std::move(ref).error();
    auto content = json::canonical_dump(authoring::to_json(m.value()));
    if (!content) return std::move(content).error();
    auto saved = services_.save_draft(ref.value(), content.value(), std::nullopt, std::nullopt, actor);
    if (!saved) return std::move(saved).error();
    if (layout) next.document["behavior"][short_of(role.value())]["layout"] = *layout;
    auto stored = impl_->store(next, actor);
    if (!stored) return std::move(stored).error();
    const auto diags = authoring::validate(m.value());
    Json out{{"revision", stored.value().revision},
             {"artifact", saved.value()},
             {"diagnostics", authoring::to_json(diags)},
             {"semanticDigest", authoring::has_errors(diags) ? Json(nullptr) : Json(authoring::semantic_digest(m.value()))}};
    out["artifact"].erase("content");
    return out;
}

Result<Json> BlueprintService::import_model(std::string_view id, std::int64_t v, std::string_view short_role, std::int64_t revision,
                                            std::string_view filename, std::string_view content, const Actor& actor) {
    auto role = model_role(short_role);
    if (!role) return std::move(role).error();
    if (auto ver = impl_->load_draft(id, v, revision); !ver) return std::move(ver).error();
    authoring::ImportOptions options;
    options.filename = std::string(filename);
    const authoring::ImportResult r = run_with_large_stack([&] { return authoring::import_any(content, options); });
    Json report = authoring::to_json(r);
    report.erase("model");
    if (!r.model) {
        // Nothing is saved: every unsupported construct is listed, none is approximated.
        return Json{{"imported", false}, {"report", report}};
    }
    auto saved = save_model(id, v, short_role, revision, authoring::to_json(*r.model),
                            r.layout.locations.empty() ? Json(nullptr) : authoring::to_json(r.layout), actor);
    if (!saved) return std::move(saved).error();
    {
        auto l = impl_->core.lock();
        impl_->core.record("blueprint.import_model", "success", std::string(id) + "@" + std::to_string(v),
                           {{"role", role.value()}, {"filename", std::string(filename)}, {"format", r.format}}, actor, "blueprint");
    }
    Json out = saved.value();
    out["imported"] = true;
    out["report"] = report;
    return out;
}

// ------------------------------------------------------------------------------ semantics

Result<Json> BlueprintService::semantics(std::string_view id, std::int64_t v, std::string_view role) {
    if (role != "ontology" && role != "pt_interpretation" && role != "dt_interpretation") {
        return make_error(ErrorCode::InvalidArgument, "role is ontology, pt_interpretation or dt_interpretation").with("role", std::string(role));
    }
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    Json out{{"role", std::string(role)}, {"editable", ver.value().state == "draft"}, {"revision", ver.value().revision},
             {"artifact", nullptr}, {"pinned", nullptr}};
    auto pin = ver.value().pins.find(std::string(role));
    if (pin == ver.value().pins.end()) return out;
    auto ref = parse_ref(pin->second);
    if (!ref) return std::move(ref).error();
    auto detail = services_.version(ref.value());
    if (!detail) return std::move(detail).error();
    out["artifact"] = detail.value();
    out["pinned"] = ref.value().str();
    if (role != "ontology") {
        // Coverage: every location and sent label of the corresponding view should be interpreted.
        const std::string model_role_name = role == "pt_interpretation" ? "pt_model" : "dt_model";
        auto mp = ver.value().pins.find(model_role_name);
        Json coverage = Json::array();
        if (mp != ver.value().pins.end()) {
            std::string content;
            {
                auto l = impl_->core.lock();
                auto mref = parse_ref(mp->second);
                if (mref) {
                    auto c = impl_->core.artifacts->content(mref.value());
                    if (c) content = c.value();
                }
            }
            LoadedModel lm = load_model(content);
            std::set<std::string> keys;
            const Json structure = detail.value().value("structure", Json::object());
            for (const Json& e : structure.value("entries", Json::array())) keys.insert(e.value("key", std::string()));
            if (lm.model) {
                for (const auto& [key, kind] : observables(*lm.model)) {
                    coverage.push_back(Json{{"key", key}, {"kind", kind}, {"mapped", keys.count(key) > 0}});
                }
            }
        }
        out["coverage"] = coverage;
        // Is the interpretation validated against the ontology this version pins?
        auto op = ver.value().pins.find("ontology");
        out["ontologyPinned"] = op == ver.value().pins.end() ? Json(nullptr) : Json(op->second);
        out["validatedAgainstPinnedOntology"] =
            op != ver.value().pins.end() && detail.value().value("ontologyRef", std::string()) == op->second;
    }
    return out;
}

Result<Json> BlueprintService::save_semantics(std::string_view id, std::int64_t v, std::string_view role, std::int64_t revision,
                                              const Json& body, const Actor& actor) {
    if (role != "ontology" && role != "pt_interpretation" && role != "dt_interpretation") {
        return make_error(ErrorCode::InvalidArgument, "role is ontology, pt_interpretation or dt_interpretation").with("role", std::string(role));
    }
    auto ver = impl_->load_draft(id, v, revision);
    if (!ver) return std::move(ver).error();
    BlueprintVersion next = ver.value();
    const std::string r(role);
    if (body.contains("ref")) {
        // Pin an existing, validated or published version (e.g. "use the shared plant ontology v3").
        auto ref = parse_ref(body.value("ref", std::string()));
        if (!ref) return std::move(ref).error();
        {
            auto l = impl_->core.lock();
            auto av = impl_->core.artifacts->version(ref.value());
            if (!av) return std::move(av).error();
            if (av.value().kind != kind_of_role(r)) {
                return make_error(ErrorCode::InvalidArgument, "the version is not a " + std::string(to_string(kind_of_role(r))));
            }
        }
        next.pins[r] = ref.value().str();
    } else {
        if (!body.contains("content") || !body.at("content").is_string()) {
            return make_error(ErrorCode::InvalidArgument, "give 'content' (text) or 'ref' (an artefact version to pin)");
        }
        if (r != "ontology" && !next.pins.count("ontology")) {
            return make_error(ErrorCode::StateError, "define or pin the ontology first: an interpretation is written over an ontology");
        }
        auto ref = impl_->ensure_editable(next, r, actor);
        if (!ref) return std::move(ref).error();
        std::optional<Json> refs;
        if (r != "ontology") {
            refs = Json{{"ontology", next.pins.at("ontology")}, {"twinRole", r == "pt_interpretation" ? "pt" : "dt"}};
        }
        auto saved = services_.save_draft(ref.value(), body.at("content").get<std::string>(), refs, std::nullopt, actor);
        if (!saved) return std::move(saved).error();
    }
    if (r == "ontology") {
        // Open interpretation drafts follow the pinned ontology, so their validation is against it.
        for (const char* ir : {"pt_interpretation", "dt_interpretation"}) {
            auto p = next.pins.find(ir);
            if (p == next.pins.end()) continue;
            auto iref = parse_ref(p->second);
            if (!iref) continue;
            std::optional<std::string> content;
            bool open = false;
            {
                auto l = impl_->core.lock();
                auto av = impl_->core.artifacts->version(iref.value());
                open = av && av.value().is_open() && av.value().refs.value("ontology", std::string()) != next.pins.at("ontology");
                if (open) {
                    auto c = impl_->core.artifacts->content(iref.value());
                    if (c) content = c.value();
                }
            }
            if (open && content) {
                (void)services_.save_draft(iref.value(), *content,
                                           Json{{"ontology", next.pins.at("ontology")}, {"twinRole", std::string(ir) == "pt_interpretation" ? "pt" : "dt"}},
                                           std::nullopt, actor);
            }
        }
    }
    auto stored = impl_->store(next, actor);
    if (!stored) return std::move(stored).error();
    Json out{{"revision", stored.value().revision}, {"pinned", next.pins.at(r)}};
    // Immediate feedback: validate the saved version with the real checkers (records evidence).
    if (body.value("validate", true)) {
        auto ref = parse_ref(next.pins.at(r));
        if (ref) {
            auto validated = services_.validate(ref.value(), actor);
            out["validation"] = validated ? validated.value() : Json{{"error", validated.error().message}};
        }
    }
    return out;
}

// -------------------------------------------------------------------------------- compile

Result<std::shared_ptr<const CompiledDt>> BlueprintService::Impl::compile_dt(const BlueprintVersion& v) {
    std::vector<Binding> all = pinned_bindings(v);
    const Binding* dt = find_binding(all, "dt_model");
    const Binding* di = find_binding(all, "dt_interpretation");
    if (dt == nullptr) return make_error(ErrorCode::InvalidArgument, "the Blueprint has no Digital Twin View yet");
    fs::path model;
    std::optional<fs::path> interp;
    {
        auto l = core.lock();
        auto m = core.materialize(*dt);
        if (!m) return std::move(m).error();
        model = m.value();
        if (di != nullptr) {
            auto i = core.materialize(*di);
            if (i) interp = i.value();
        }
    }
    auto bytes = read_text_file(model);
    if (!bytes) return std::move(bytes).error();
    const Json identity = v.document.value("identity", Json::object());
    const std::string model_id = identity.value("modelId", v.blueprint_id);
    const std::int64_t ticks = identity.value("ticksPerUnit", std::int64_t{1000});
    const std::string key = sha256_hex(bytes.value()) + ":" + (di ? di->sha256 : std::string()) + ":" + model_id + ":" + std::to_string(ticks);
    {
        std::lock_guard l(compiled_mu);
        auto it = compiled.find(key);
        if (it != compiled.end()) return it->second;
    }
    compiler::CompileOptions options;
    options.model_id = model_id;
    options.ticks_per_unit = ticks;
    if (interp) options.interpretation = *interp;
    options.legacy_system_declaration = services.config().legacy_system_declaration;
    const auto result = run_with_large_stack([&] { return compiler::compile_file(model, options); });
    if (const auto* failure = std::get_if<compiler::CompileFailure>(&result)) {
        Error e = make_error(ErrorCode::ValidationError, "the Digital Twin View does not compile");
        for (const auto& d : failure->diagnostics) {
            if (d.severity == compiler::Severity::Error) e.with(d.code, d.message + (d.where.empty() ? "" : " (" + d.where + ")"));
        }
        return e;
    }
    const auto& ok = std::get<compiler::CompileResult>(result);
    auto km = kernel::Model::create(ok.model);
    if (!km) return std::move(km).error();
    auto c = std::make_shared<CompiledDt>(CompiledDt{km.value(), ok.manifest.ir_sha256, sha256_hex(bytes.value())});
    std::lock_guard l(compiled_mu);
    if (compiled.size() > 64) compiled.clear();
    compiled[key] = c;
    return std::shared_ptr<const CompiledDt>(c);
}

}  // namespace twin::studio
