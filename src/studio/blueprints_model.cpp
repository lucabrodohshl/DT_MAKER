/**
 * @file blueprints_model.cpp
 * @brief Model-driven Blueprints: the `model` section (twin-model/1) and everything derived from it.
 *
 * A Blueprint version whose document has a `model` section is *model-driven*: on every save
 * of the model the server compiles it (twin::model) and
 *  - pins the derived formal artefacts — V_D (dt_model), K (ontology), I_D (dt_interpretation)
 *    and, when the model has a physical reference, V_P (pt_model) and I_P (pt_interpretation) —
 *    as ordinary artefact versions, written only when their content changes;
 *  - rewrites the derived sections (structure, data, presentation, assurance).
 * The release gate, alignment, compilation, scenarios, instances and deployment then work on
 * exactly these artefacts, unchanged. Direct edits of derived parts are refused.
 */
#include <algorithm>
#include <map>
#include <sstream>

#include "blueprints_impl.hpp"
#include "large_stack.hpp"
#include "twin/authoring/model.hpp"
#include "twin/model/compile.hpp"
#include "twin/model/lift.hpp"

namespace twin::studio {

using json::Json;
using platform::ArtifactRef;
using platform::BlueprintVersion;

namespace {

constexpr const char* kDerivedSections[] = {"structure", "data", "presentation", "assurance"};

}  // namespace

bool is_model_driven(const Json& document) { return document.contains("model") && document.at("model").is_object(); }

bool is_derived_section(std::string_view section) {
    return std::find(std::begin(kDerivedSections), std::end(kDerivedSections), section) != std::end(kDerivedSections);
}

namespace {

/// Write @p content into @p role unless the pinned version already holds exactly it.
Status write_role(BlueprintService::Impl& impl, Services& services, BlueprintVersion& v, const std::string& role, const std::string& content,
                  const std::optional<Json>& refs, const std::string& note, const Actor& actor) {
    if (const auto pin = v.pins.find(role); pin != v.pins.end()) {
        auto ref = platform::parse_ref(pin->second);
        if (ref) {
            auto l = impl.core.lock();
            auto current = impl.core.artifacts->content(ref.value());
            auto version = impl.core.artifacts->version(ref.value());
            const bool same_refs = !refs || (version && version.value().refs.value("ontology", std::string()) == refs->value("ontology", std::string()));
            if (current && current.value() == content && same_refs) return ok_status();
        }
    }
    auto ref = impl.ensure_editable(v, role, actor);
    if (!ref) return std::move(ref).error();
    auto saved = services.save_draft(ref.value(), content, refs, note, actor);
    if (!saved) return std::move(saved).error();
    return ok_status();
}

}  // namespace

/// Compile the model of @p v and write everything derived into @p v (pins and sections); not stored.
Result<Json> apply_model(BlueprintService::Impl& impl, Services& services, BlueprintVersion& v, const Actor& actor) {
    const Json& doc = v.document.at("model");
    const model::Compiled c = run_with_large_stack([&] { return model::compile(doc); });
    Json out{{"diagnostics", model::to_json(c.diagnostics)}, {"digest", c.semantic_digest}, {"derived", false}};
    if (!c.artifacts) {
        // A draft can keep an invalid working model, but its previous formal pins must never
        // masquerade as products of the new model or pass the release gate.
        for (const char* role : {"ontology", "dt_model", "dt_interpretation", "pt_model", "pt_interpretation"}) v.pins.erase(role);
        v.document["structure"] = Json{{"root", nullptr}, {"assetTypes", Json::array()}, {"assets", Json::array()}, {"relationships", Json::array()}};
        v.document["data"] = Json{{"properties", Json::array()}, {"telemetry", Json::array()}, {"events", Json::array()}, {"commands", Json::array()}};
        Json presentation = v.document.value("presentation", Json::object());
        for (const char* key : {"keyTelemetry", "importantAssets", "importantPropositions", "importantMonitors", "importantPredictions", "charts"})
            presentation[key] = Json::array();
        presentation["states"] = Json::object();
        presentation["events"] = Json::object();
        v.document["presentation"] = std::move(presentation);
        v.document["assurance"] = Json{{"format", "twin-monitors/1"}, {"requirements", Json::array()}, {"monitors", Json::array()}, {"alerts", Json::array()}};
        return out;
    }
    const model::Artifacts& a = *c.artifacts;
    const std::string note = "Generated from the model (" + c.semantic_digest.substr(0, 12) + ")";

    if (auto s = write_role(impl, services, v, "ontology", a.ontology, std::nullopt, note, actor); !s) return s.error();
    if (a.automaton) {
        auto content = json::canonical_dump(authoring::to_json(*a.automaton));
        if (!content) return std::move(content).error();
        if (auto s = write_role(impl, services, v, "dt_model", content.value(), std::nullopt, note, actor); !s) return s.error();
    }
    const Json dt_refs{{"ontology", v.pins.at("ontology")}, {"twinRole", "dt"}};
    if (auto s = write_role(impl, services, v, "dt_interpretation", a.dt_interpretation, dt_refs, note, actor); !s) return s.error();
    if (a.reference) {
        auto content = json::canonical_dump(authoring::to_json(*a.reference));
        if (!content) return std::move(content).error();
        if (auto s = write_role(impl, services, v, "pt_model", content.value(), std::nullopt, note, actor); !s) return s.error();
        const Json pt_refs{{"ontology", v.pins.at("ontology")}, {"twinRole", "pt"}};
        if (auto s = write_role(impl, services, v, "pt_interpretation", a.pt_interpretation, pt_refs, note, actor); !s) return s.error();
    } else {
        // Alignment needs an independent physical reference; without one in the model, none is pinned.
        v.pins.erase("pt_model");
        v.pins.erase("pt_interpretation");
    }

    // Derived sections; presentation keeps the operator's choices of view and plugin.
    const Json previous = v.document.value("presentation", Json::object());
    for (const char* s : kDerivedSections) v.document[s] = a.sections.at(s);
    for (const char* keep : {"plugin", "primaryView", "icon", "charts", "importantAssets"}) {
        if (previous.contains(keep) && !previous.at(keep).is_null() && !(previous.at(keep).is_array() && previous.at(keep).empty())) {
            v.document["presentation"][keep] = previous.at(keep);
        }
    }
    v.document["assurance"]["format"] = "twin-monitors/1";
    // Identity follows the model's time base.
    if (doc.contains("time")) {
        v.document["identity"]["timeUnit"] = doc.at("time").value("unit", std::string("s"));
        v.document["identity"]["ticksPerUnit"] = doc.at("time").value("ticksPerUnit", std::int64_t{1000});
    }
    out["derived"] = true;
    out["summary"] = a.summary;
    return out;
}

Result<Json> BlueprintService::model_document(std::string_view id, std::int64_t v) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    const BlueprintVersion& bv = ver.value();
    Json out{{"editable", bv.state == "draft"}, {"revision", bv.revision}, {"model", nullptr}};
    if (!is_model_driven(bv.document)) return out;
    out["model"] = bv.document.at("model");
    const model::Compiled c = run_with_large_stack([&] { return model::compile(bv.document.at("model")); });
    out["diagnostics"] = model::to_json(c.diagnostics);
    out["digest"] = c.semantic_digest;
    if (c.artifacts) out["summary"] = c.artifacts->summary;
    return out;
}

Result<Json> BlueprintService::save_model_document(std::string_view id, std::int64_t v, std::int64_t revision, const Json& model,
                                                   const Actor& actor) {
    if (!model.is_object()) return make_error(ErrorCode::InvalidArgument, "the model is a twin-model/1 JSON object");
    auto ver = impl_->load_draft(id, v, revision);
    if (!ver) return std::move(ver).error();
    BlueprintVersion next = ver.value();
    next.document["model"] = model;
    auto applied = apply_model(*impl_, services_, next, actor);
    if (!applied) return std::move(applied).error();
    auto stored = impl_->store(next, actor);
    if (!stored) return std::move(stored).error();
    {
        auto l = impl_->core.lock();
        impl_->core.record("blueprint.model", "success", std::string(id) + "@" + std::to_string(v),
                           {{"revision", stored.value().revision}, {"digest", applied.value().value("digest", std::string())},
                            {"derived", applied.value().value("derived", false)}},
                           actor, "blueprint");
    }
    Json out = applied.value();
    out["revision"] = stored.value().revision;
    out["documentSha256"] = stored.value().document_sha256;
    return out;
}

Result<Json> BlueprintService::generated(std::string_view id, std::int64_t v) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    if (!is_model_driven(ver.value().document)) {
        return make_error(ErrorCode::NotFound, "this Blueprint version has no model").with("blueprint", std::string(id));
    }
    const model::Compiled c = run_with_large_stack([&] { return model::compile(ver.value().document.at("model")); });
    Json out{{"diagnostics", model::to_json(c.diagnostics)}, {"digest", c.semantic_digest}, {"artifacts", nullptr}};
    if (c.artifacts) out["artifacts"] = model::to_json(*c.artifacts);
    return out;
}

Result<Json> BlueprintService::lift_model(std::string_view id, std::int64_t v, const Json& body) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    const BlueprintVersion& bv = ver.value();
    const auto content = [&](const char* role) -> std::optional<std::string> {
        const auto pin = bv.pins.find(role);
        if (pin == bv.pins.end()) return std::nullopt;
        auto ref = platform::parse_ref(pin->second);
        if (!ref) return std::nullopt;
        auto l = impl_->core.lock();
        auto c = impl_->core.artifacts->content(ref.value());
        if (!c) return std::nullopt;
        return c.value();
    };
    const auto dt = content("dt_model");
    if (!dt) return make_error(ErrorCode::InvalidArgument, "the Blueprint has no Digital Twin View to lift");
    auto dt_json = json::parse(*dt);
    if (!dt_json) return std::move(dt_json).error();
    auto ta = authoring::model_from_json(dt_json.value());
    if (!ta) return std::move(ta).error();
    model::LiftInput in;
    in.automaton = ta.value();
    in.ontology = content("ontology").value_or("");
    in.interpretation = content("dt_interpretation").value_or("");
    in.name = body.value("name", bv.document.value("identity", Json::object()).value("modelId", std::string(id)));
    for (char& c : in.name) {
        if (c == '-') c = '_';
    }
    in.label = bv.document.value("identity", Json::object()).value("name", std::string());
    const model::Lifted lifted = run_with_large_stack([&] { return model::lift(in); });
    Json out{{"diagnostics", model::to_json(lifted.diagnostics)}, {"model", nullptr}};
    if (!lifted.document) return out;
    model::Document doc = *lifted.document;
    // The physical reference: the pinned PT view, mapped through the data contract's PT/DT label pairs.
    if (const auto pt = content("pt_model"); pt && body.value("reference", true)) {
        auto pj = json::parse(*pt);
        auto pta = pj ? authoring::model_from_json(pj.value()) : Result<authoring::Model>(pj.error());
        if (pta) {
            model::Reference ref = model::reference_suggestion(doc, pta.value());
            for (const Json& e : bv.document.value("data", Json::object()).value("events", Json::array())) {
                const Json formal = e.value("formal", Json::object());
                std::string p = formal.value("pt", std::string());
                std::string d = formal.value("dt", std::string());
                if (p.empty() || d.empty()) continue;
                if (p.back() == '!' || p.back() == '?') p.pop_back();
                if (d.back() == '!' || d.back() == '?') d.pop_back();
                ref.labels[p] = model::ReferenceLabel{d, false};
            }
            // PT locations without a same-named state keep the meaning the old I_P gave them.
            const std::string pt_interp = content("pt_interpretation").value_or("");
            std::map<std::string, std::string> old;
            std::istringstream lines(pt_interp);
            for (std::string line; std::getline(lines, line);) {
                const std::size_t colon = line.find(':');
                if (colon == std::string::npos || line.empty() || line.front() == ';') continue;
                std::string key = line.substr(0, colon);
                key.erase(key.find_last_not_of(" \t") + 1);
                std::string f = line.substr(colon + 1);
                const std::size_t semi = f.find(';');
                if (semi != std::string::npos && std::count(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(semi), '(') ==
                                                     std::count(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(semi), ')')) {
                    f = f.substr(0, semi);
                }
                old[key] = f;
            }
            for (const auto& l : pta.value().locations) {
                if (ref.states.contains(l.name)) continue;
                if (const auto it = old.find(l.name); it != old.end()) ref.states[l.name] = model::ReferenceState{{}, "smt:" + it->second};
            }
            for (const auto& c : pta.value().channels) {
                if (!ref.labels.contains(c.name)) ref.labels[c.name] = model::ReferenceLabel{"", true};
            }
            doc.reference = std::move(ref);
        }
    }
    // The round-trip guarantee, checked on exactly this automaton.
    const model::Compiled c = run_with_large_stack([&] { return model::compile(doc); });
    std::string why;
    const bool round_trip = c.artifacts && c.artifacts->automaton && model::same_automaton(*c.artifacts->automaton, ta.value(), &why);
    out["model"] = model::encode(doc);
    out["roundTrip"] = round_trip;
    if (!round_trip) out["difference"] = c.artifacts ? why : std::string("the lifted model does not compile");
    out["compileDiagnostics"] = model::to_json(c.diagnostics);
    return out;
}

}  // namespace twin::studio
