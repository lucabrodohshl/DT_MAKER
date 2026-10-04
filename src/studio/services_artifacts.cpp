/**
 * @file services_artifacts.cpp
 * @brief Services: artefact browsing, drafts, validation, publication, diff, symbols, evaluation.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "ontology_json.hpp"
#include "large_stack.hpp"
#include "services_impl.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/core/version.hpp"
#include "twin/ontology/diff.hpp"
#include "twin/ontology/evaluate.hpp"
#include "twin/ontology/validation.hpp"

namespace twin::studio {

using namespace twin::platform;
namespace onto = twin::ontology;

namespace {

/// @brief The ontology version an interpretation version refers to.
Result<ArtifactRef> ontology_of(const ArtifactVersion& interp) {
    auto it = interp.refs.find("ontology");
    if (it == interp.refs.end() || !it->is_string()) {
        return make_error(ErrorCode::ValidationError, "interpretation does not reference an ontology version")
            .with("ref", interp.ref().str());
    }
    return parse_ref(it->get<std::string>());
}

/// @brief Exact decimal text of a double (6 decimals, trailing zeros trimmed).
std::string decimal_text(double v) {
    std::array<char, 64> buf{};
    std::snprintf(buf.data(), buf.size(), "%.6f", v);
    std::string s = buf.data();
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

}  // namespace

Result<json::Json> Services::list_artifacts(std::optional<ArtifactKind> kind) {
    auto l = impl_->lock();
    auto all = impl_->artifacts->list(kind);
    if (!all) return std::move(all).error();
    json::Json out = json::Json::array();
    for (const auto& a : all.value()) {
        auto versions = impl_->artifacts->versions(a.id);
        if (!versions) return std::move(versions).error();
        json::Json item = to_json(a);
        item["versionCount"] = versions.value().size();
        item["latest"] = versions.value().empty() ? json::Json(nullptr) : to_json(versions.value().front());
        item["published"] = nullptr;
        item["open"] = nullptr;
        for (const auto& v : versions.value()) {
            if (v.state == Lifecycle::Published) item["published"] = to_json(v);
            if (v.is_open()) item["open"] = to_json(v);
        }
        out.push_back(item);
    }
    return out;
}

Result<json::Json> Services::artifact(std::string_view id) {
    auto l = impl_->lock();
    auto a = impl_->artifacts->get(id);
    if (!a) return std::move(a).error();
    auto versions = impl_->artifacts->versions(id);
    if (!versions) return std::move(versions).error();
    json::Json vs = json::Json::array();
    for (const auto& v : versions.value()) vs.push_back(to_json(v));

    // Dependants: interpretations referring to this ontology, packages/deployments binding it.
    json::Json interpretations = json::Json::array();
    if (a.value().kind == ArtifactKind::Ontology) {
        auto interps = impl_->artifacts->list(ArtifactKind::Interpretation);
        if (!interps) return std::move(interps).error();
        for (const auto& i : interps.value()) {
            auto ivs = impl_->artifacts->versions(i.id);
            if (!ivs) return std::move(ivs).error();
            for (const auto& iv : ivs.value()) {
                auto o = ontology_of(iv);
                if (o && o.value().artifact_id == a.value().id) {
                    interpretations.push_back({{"ref", iv.ref().str()},
                                               {"name", i.name},
                                               {"state", std::string(to_string(iv.state))},
                                               {"ontologyRef", o.value().str()}});
                }
            }
        }
    }
    json::Json used_by = json::Json::array();
    auto twins = impl_->twins->twins();
    if (!twins) return std::move(twins).error();
    for (const auto& t : twins.value()) {
        auto dep = impl_->twins->current_deployment(t.id);
        if (!dep) return std::move(dep).error();
        if (!dep.value()) continue;
        auto pkg = impl_->twins->package(dep.value()->package_id);
        if (!pkg) return std::move(pkg).error();
        for (const auto& b : pkg.value().bindings) {
            if (b.ref.artifact_id == a.value().id) {
                used_by.push_back({{"twinId", t.id},
                                   {"twinName", t.name},
                                   {"role", b.role},
                                   {"ref", b.ref.str()},
                                   {"packageId", pkg.value().id},
                                   {"deploymentId", dep.value()->id}});
            }
        }
    }
    json::Json j = to_json(a.value());
    j["versions"] = vs;
    j["interpretations"] = interpretations;
    j["deployedIn"] = used_by;
    return j;
}

Result<json::Json> Services::version(const ArtifactRef& ref) {
    auto l = impl_->lock();
    auto v = impl_->artifacts->version(ref);
    if (!v) return std::move(v).error();
    auto content = impl_->artifacts->content(ref);
    if (!content) return std::move(content).error();
    auto summary = impl_->version_summary(ref);
    if (!summary) return std::move(summary).error();
    json::Json j = summary.value();
    j["content"] = content.value();

    // Fast structural reading (no solver): structure + strict diagnostics for the editor.
    std::vector<EvidenceInput> validation_inputs{{"subject", ref, v.value().content_sha256}};
    if (v.value().kind == ArtifactKind::Ontology) {
        const auto parsed = onto::parse_ontology(content.value());
        j["structure"] = to_json(parsed.source);
        j["diagnostics"] = to_json(parsed.diagnostics);
    } else if (v.value().kind == ArtifactKind::Interpretation) {
        std::optional<onto::OntologySource> ont;
        if (auto o = ontology_of(v.value()); o) {
            j["ontologyRef"] = o.value().str();
            if (auto ot = impl_->artifacts->content(o.value()); ot) ont = onto::parse_ontology(ot.value()).source;
            if (auto ov = impl_->artifacts->version(o.value()); ov) {
                validation_inputs.push_back({"ontology", o.value(), ov.value().content_sha256});
            }
        }
        const auto parsed = onto::parse_interpretation(content.value(), ont ? &*ont : nullptr);
        j["structure"] = to_json(parsed.source);
        j["diagnostics"] = to_json(parsed.diagnostics);
    } else {
        j["structure"] = nullptr;
        j["diagnostics"] = json::Json::array();
    }
    // Validation evidence for exactly these bytes (and, for interpretations, this ontology).
    auto ev = impl_->evidence->latest_for(EvidenceKind::Validation, validation_inputs);
    if (!ev) return std::move(ev).error();
    if (ev.value()) {
        json::Json e = to_json(*ev.value());
        auto doc = impl_->evidence->document(*ev.value());
        if (doc) e["document"] = doc.value();
        j["validation"] = e;
    } else {
        j["validation"] = nullptr;
    }
    j["validationRunning"] = impl_->is_running("validate:" + ref.str());
    // Parent (lineage).
    if (v.value().parent) {
        auto p = impl_->version_summary({ref.artifact_id, *v.value().parent});
        j["parentSummary"] = p ? p.value() : json::Json(nullptr);
    } else {
        j["parentSummary"] = nullptr;
    }
    return j;
}

Result<json::Json> Services::create_artifact(ArtifactKind kind, std::string_view id, std::string_view name,
                                             std::string_view description, std::string_view content,
                                             const json::Json& refs, const Actor& actor) {
    auto l = impl_->lock();
    if (kind == ArtifactKind::Interpretation) {
        auto it = refs.find("ontology");
        if (it == refs.end() || !it->is_string() || !parse_ref(it->get<std::string>())) {
            return make_error(ErrorCode::InvalidArgument,
                              "an interpretation must reference its ontology version: refs.ontology = \"<id>@<n>\"");
        }
    }
    auto v = impl_->artifacts->create(kind, id, name, description, content, refs.is_null() ? json::Json::object() : refs,
                                      actor.name, "initial import");
    if (!v) return std::move(v).error();
    impl_->record("artifact.create", "success", v.value().ref().str(),
                  {{"kind", std::string(to_string(kind))}, {"sha256", v.value().content_sha256}}, actor, "artifact");
    return to_json(v.value());
}

Result<json::Json> Services::create_draft(const ArtifactRef& from, std::string_view description,
                                          const std::optional<std::string>& change_id, const Actor& actor) {
    auto l = impl_->lock();
    Transaction tx(*impl_->db);
    if (!tx.begun()) return tx.begun().error();
    auto v = impl_->artifacts->create_draft(from, actor.name, description);
    if (!v) return std::move(v).error();
    if (change_id) {
        auto c = impl_->twins->change(*change_id);
        if (!c) return std::move(c).error();
        auto list = c.value().artifacts;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const ArtifactRef& r) { return r.artifact_id == from.artifact_id; }),
                   list.end());
        list.push_back(v.value().ref());
        if (auto st = impl_->twins->set_change_artifacts(*change_id, list); !st) return std::move(st).error();
    }
    if (auto st = tx.commit(); !st) return st.error();
    impl_->record("artifact.draft.create", "success", v.value().ref().str(),
                  {{"from", from.str()},
                   {"description", std::string(description)},
                   {"changeId", change_id ? json::Json(*change_id) : json::Json(nullptr)}},
                  actor, "artifact");
    return to_json(v.value());
}

Result<json::Json> Services::save_draft(const ArtifactRef& ref, std::string_view content,
                                        const std::optional<json::Json>& refs,
                                        const std::optional<std::string>& description, const Actor& actor) {
    auto l = impl_->lock();
    auto before = impl_->artifacts->version(ref);
    if (!before) return std::move(before).error();
    auto v = impl_->artifacts->save(ref, content, refs, description, actor.name);
    if (!v) return std::move(v).error();
    const bool changed = v.value().content_sha256 != before.value().content_sha256;
    impl_->record("artifact.draft.save", "success", ref.str(),
                  {{"sha256", v.value().content_sha256},
                   {"previousSha256", before.value().content_sha256},
                   {"contentChanged", changed}},
                  actor, "artifact");
    json::Json j = to_json(v.value());
    // Return strict diagnostics so the editor can mark problems immediately after saving.
    if (v.value().kind == ArtifactKind::Ontology) {
        j["diagnostics"] = to_json(onto::parse_ontology(content).diagnostics);
    } else if (v.value().kind == ArtifactKind::Interpretation) {
        std::optional<onto::OntologySource> ont;
        if (auto o = ontology_of(v.value()); o) {
            if (auto ot = impl_->artifacts->content(o.value()); ot) ont = onto::parse_ontology(ot.value()).source;
        }
        j["diagnostics"] = to_json(onto::parse_interpretation(content, ont ? &*ont : nullptr).diagnostics);
    }
    return j;
}

Result<json::Json> Services::validate(const ArtifactRef& ref, const Actor& actor) {
    RunningCheck running(*impl_, "validate:" + ref.str());
    if (!running.acquired()) return make_error(ErrorCode::StateError, "validation of this version is already running");

    // 1. Read inputs and mark VALIDATING (under the lock).
    ArtifactVersion version;
    std::string content;
    std::optional<ArtifactVersion> ontology_version;
    std::string ontology_content;
    {
        auto l = impl_->lock();
        auto v = impl_->artifacts->version(ref);
        if (!v) return std::move(v).error();
        version = v.value();
        auto c = impl_->artifacts->content(ref);
        if (!c) return std::move(c).error();
        content = c.value();
        if (version.kind == ArtifactKind::Interpretation) {
            auto o = ontology_of(version);
            if (!o) return std::move(o).error();
            auto ov = impl_->artifacts->version(o.value());
            if (!ov) return std::move(ov).error();
            ontology_version = ov.value();
            auto oc = impl_->artifacts->content(o.value());
            if (!oc) return std::move(oc).error();
            ontology_content = oc.value();
        }
        if (version.state == Lifecycle::Draft || version.state == Lifecycle::Verified) {
            auto s = impl_->artifacts->set_state(ref, Lifecycle::Validating, actor.name);
            if (!s) return std::move(s).error();
        }
    }
    events_.publish("artifact", {{"subject", ref.str()}, {"state", "validating"}}, iso8601_utc(clock_->now_ms()));

    // 2. Run the formal validation outside the lock.
    bool valid = false;
    json::Json document;
    std::string summary;
    const onto::SolverConfig solver{config_.solver_timeout_ms};
    std::string checker = onto::checker_identity();
    switch (version.kind) {
        case ArtifactKind::Ontology: {
            const auto r = run_with_large_stack([&] { return onto::validate_ontology(content, solver); });
            valid = r.valid;
            document = to_json(r);
            summary = valid ? "Ontology is well-formed, read identically by the aligner, and consistent."
                            : "Ontology validation found errors.";
            break;
        }
        case ArtifactKind::Interpretation: {
            const auto r = run_with_large_stack([&] { return onto::validate_interpretation(ontology_content, content, solver); });
            valid = r.valid;
            document = to_json(r);
            document["ontologyRef"] = ontology_version->ref().str();
            summary = valid ? "Interpretation is well-formed over " + ontology_version->ref().str() + "."
                            : "Interpretation validation found errors.";
            break;
        }
        case ArtifactKind::PtModel:
        case ArtifactKind::DtModel: {
            Binding b{version.kind == ArtifactKind::DtModel ? "dt_model" : "pt_model", ref, version.content_sha256};
            Result<std::filesystem::path> path = make_error(ErrorCode::Internal, "unset");
            {
                auto l = impl_->lock();
                path = impl_->materialize(b);
            }
            if (!path) return std::move(path).error();
            compiler::CompileOptions options;
            options.model_id = ref.artifact_id;
            options.legacy_system_declaration = config_.legacy_system_declaration;
            const auto result = run_with_large_stack([&] { return compiler::compile_file(path.value(), options); });
            checker = "twin-compiler " + std::string(twin::version::kCompiler);
            json::Json diags = json::Json::array();
            auto add = [&](const std::vector<compiler::Diagnostic>& ds) {
                for (const auto& d : ds) {
                    diags.push_back({{"code", d.code},
                                     {"severity", compiler::to_string(d.severity)},
                                     {"message", d.message},
                                     {"where", d.where},
                                     {"hint", d.hint}});
                }
            };
            if (const auto* ok = std::get_if<compiler::CompileResult>(&result)) {
                valid = ok->manifest.translation_validation.passed;
                add(ok->manifest.diagnostics);
                document = {{"format", "twin-model-validation/1"},
                            {"valid", valid},
                            {"irSha256", ok->manifest.ir_sha256},
                            {"translationValidation", ok->manifest.translation_validation.passed},
                            {"eventDeterministic", ok->manifest.determinism.event_deterministic},
                            {"diagnostics", diags}};
            } else {
                add(std::get<compiler::CompileFailure>(result).diagnostics);
                document = {{"format", "twin-model-validation/1"}, {"valid", false}, {"diagnostics", diags}};
            }
            summary = valid ? "Model compiles strictly and passes translation validation against the aligner's reading."
                            : "The model is outside the supported fragment or malformed.";
            break;
        }
    }

    // 3. Record evidence and the resulting state (under the lock).
    auto l = impl_->lock();
    std::vector<EvidenceInput> inputs{{"subject", ref, version.content_sha256}};
    if (ontology_version) inputs.push_back({"ontology", ontology_version->ref(), ontology_version->content_sha256});
    auto ev = impl_->evidence->record(EvidenceKind::Validation, valid ? Outcome::Pass : Outcome::Fail,
                                      valid ? "valid" : "invalid", summary, checker, document, inputs, actor.name);
    if (!ev) return std::move(ev).error();
    auto current = impl_->artifacts->version(ref);
    if (!current) return std::move(current).error();
    if (current.value().state == Lifecycle::Validating) {
        // Content cannot change while VALIDATING (save requires DRAFT/VERIFIED), so the verdict applies.
        auto s = impl_->artifacts->set_state(ref, valid ? Lifecycle::Verified : Lifecycle::Draft, actor.name);
        if (!s) return std::move(s).error();
    }
    impl_->record("artifact.validate", valid ? "pass" : "fail", ref.str(),
                  {{"evidenceId", ev.value().id}, {"sha256", version.content_sha256}}, actor, "evidence");
    auto out = this->version(ref);
    if (!out) return std::move(out).error();
    json::Json j = out.value();
    j["evidenceId"] = ev.value().id;
    return j;
}

Result<json::Json> Services::publish(const ArtifactRef& ref, const Actor& actor) {
    auto l = impl_->lock();
    auto v = impl_->artifacts->publish(ref, actor.name);
    if (!v) return std::move(v).error();
    impl_->record("artifact.publish", "success", ref.str(), {{"sha256", v.value().content_sha256}}, actor, "artifact");
    return to_json(v.value());
}

Result<json::Json> Services::reject(const ArtifactRef& ref, std::string_view reason, const Actor& actor) {
    if (reason.empty()) return make_error(ErrorCode::InvalidArgument, "rejecting a version requires a reason");
    auto l = impl_->lock();
    auto v = impl_->artifacts->set_state(ref, Lifecycle::Rejected, actor.name);
    if (!v) return std::move(v).error();
    impl_->record("artifact.reject", "success", ref.str(), {{"reason", std::string(reason)}}, actor, "artifact");
    return to_json(v.value());
}

Result<json::Json> Services::diff(const ArtifactRef& from, const ArtifactRef& to) {
    auto l = impl_->lock();
    auto fv = impl_->artifacts->version(from);
    auto tv = impl_->artifacts->version(to);
    if (!fv) return std::move(fv).error();
    if (!tv) return std::move(tv).error();
    if (fv.value().kind != tv.value().kind) {
        return make_error(ErrorCode::InvalidArgument, "can only compare versions of the same kind");
    }
    auto fc = impl_->artifacts->content(from);
    auto tc = impl_->artifacts->content(to);
    if (!fc) return std::move(fc).error();
    if (!tc) return std::move(tc).error();
    json::Json j;
    j["from"] = impl_->version_summary(from).value();
    j["to"] = impl_->version_summary(to).value();
    j["from"]["content"] = fc.value();
    j["to"]["content"] = tc.value();
    j["kind"] = std::string(to_string(fv.value().kind));
    j["affectedInterpretations"] = json::Json::array();
    if (fv.value().kind == ArtifactKind::Ontology) {
        const auto d = onto::diff_ontologies(onto::parse_ontology(fc.value()).source, onto::parse_ontology(tc.value()).source);
        j["structural"] = to_json(d);
        // Interpretations over this ontology whose entries mention an affected symbol.
        auto interps = impl_->artifacts->list(ArtifactKind::Interpretation);
        if (!interps) return std::move(interps).error();
        for (const auto& i : interps.value()) {
            auto pub = impl_->artifacts->published(i.id);
            auto open = impl_->artifacts->open_version(i.id);
            for (const auto* cand : {pub ? &pub.value() : nullptr, open ? &open.value() : nullptr}) {
                if (cand == nullptr || !cand->has_value()) continue;
                const ArtifactVersion& iv = **cand;
                auto o = ontology_of(iv);
                if (!o || o.value().artifact_id != from.artifact_id) continue;
                auto ic = impl_->artifacts->content(iv.ref());
                if (!ic) continue;
                const auto keys = onto::entries_using(onto::parse_interpretation(ic.value()).source, d.affected_symbols);
                j["affectedInterpretations"].push_back(
                    {{"ref", iv.ref().str()}, {"name", i.name}, {"state", std::string(to_string(iv.state))}, {"entries", keys}});
            }
        }
    } else if (fv.value().kind == ArtifactKind::Interpretation) {
        j["structural"] = to_json(onto::diff_interpretations(onto::parse_interpretation(fc.value()).source,
                                                             onto::parse_interpretation(tc.value()).source));
    } else {
        j["structural"] = nullptr;  // models: source diff only (structure via the compiler's IR)
    }
    return j;
}

Result<json::Json> Services::symbol(const ArtifactRef& ref, std::string_view name) {
    auto l = impl_->lock();
    auto content = impl_->artifacts->content(ref);
    if (!content) return std::move(content).error();
    const auto src = onto::parse_ontology(content.value()).source;
    json::Json decl;
    if (const auto* s = src.find_sort(name)) {
        decl = {{"kind", "sort"}, {"name", s->name}, {"signature", ""}, {"comment", s->comment}, {"span", to_json(s->span)}};
    } else if (const auto* f = src.find_function(name)) {
        decl = {{"kind", "function"}, {"name", f->name}, {"signature", onto::signature(*f)}, {"comment", f->comment},
                {"span", to_json(f->span)}};
    } else if (const auto* r = src.find_relation(name)) {
        decl = {{"kind", "relation"}, {"name", r->name}, {"signature", onto::signature(*r)}, {"comment", r->comment},
                {"span", to_json(r->span)}};
    } else {
        return make_error(ErrorCode::NotFound, "symbol is not declared in this ontology version")
            .with("symbol", std::string(name))
            .with("ref", ref.str());
    }
    json::Json axioms = json::Json::array();
    for (const auto& a : src.axioms) {
        const auto syms = onto::formula_symbols(a.formula).value_or(std::vector<std::string>{});
        if (std::find(syms.begin(), syms.end(), name) != syms.end()) {
            axioms.push_back({{"id", a.id}, {"formula", a.formula}, {"span", to_json(a.span)}});
        }
    }
    json::Json interpretations = json::Json::array();
    auto interps = impl_->artifacts->list(ArtifactKind::Interpretation);
    if (!interps) return std::move(interps).error();
    for (const auto& i : interps.value()) {
        auto versions = impl_->artifacts->versions(i.id);
        if (!versions) return std::move(versions).error();
        for (const auto& iv : versions.value()) {
            if (!(iv.state == Lifecycle::Published || iv.is_open())) continue;
            auto o = ontology_of(iv);
            if (!o || o.value().artifact_id != ref.artifact_id) continue;
            auto ic = impl_->artifacts->content(iv.ref());
            if (!ic) continue;
            const auto parsed = onto::parse_interpretation(ic.value()).source;
            json::Json entries = json::Json::array();
            for (const auto& e : parsed.entries) {
                const auto syms = onto::formula_symbols(e.formula).value_or(std::vector<std::string>{});
                if (std::find(syms.begin(), syms.end(), name) != syms.end()) {
                    entries.push_back({{"key", e.key}, {"isEvent", e.is_event}, {"formula", e.formula}});
                }
            }
            if (!entries.empty()) {
                interpretations.push_back({{"ref", iv.ref().str()}, {"name", i.name},
                                           {"state", std::string(to_string(iv.state))}, {"entries", entries}});
            }
        }
    }
    json::Json channels = json::Json::array();
    auto all_channels = impl_->telemetry->channels("");
    if (!all_channels) return std::move(all_channels).error();
    for (const auto& c : all_channels.value()) {
        if (c.ontology_symbol && *c.ontology_symbol == name) channels.push_back(to_json(c));
    }
    return json::Json{{"ontologyRef", ref.str()},
                      {"declaration", decl},
                      {"axioms", axioms},
                      {"interpretations", interpretations},
                      {"telemetryChannels", channels}};
}

Result<json::Json> Services::evaluate(const ArtifactRef& interpretation,
                                      const std::vector<std::pair<std::string, std::string>>& observations,
                                      const std::optional<std::string>& asset_id, const std::vector<std::string>& keys) {
    std::string ont_text;
    std::string interp_text;
    std::string ontology_ref;
    std::vector<onto::Observation> obs;
    json::Json used = json::Json::array();
    json::Json warnings = json::Json::array();
    {
        auto l = impl_->lock();
        auto v = impl_->artifacts->version(interpretation);
        if (!v) return std::move(v).error();
        if (v.value().kind != ArtifactKind::Interpretation) {
            return make_error(ErrorCode::InvalidArgument, "not an interpretation").with("ref", interpretation.str());
        }
        auto o = ontology_of(v.value());
        if (!o) return std::move(o).error();
        ontology_ref = o.value().str();
        auto oc = impl_->artifacts->content(o.value());
        auto ic = impl_->artifacts->content(interpretation);
        if (!oc) return std::move(oc).error();
        if (!ic) return std::move(ic).error();
        ont_text = oc.value();
        interp_text = ic.value();
        for (const auto& [sym, val] : observations) {
            obs.push_back({sym, val});
            used.push_back({{"symbol", sym}, {"value", val}, {"source", "request"}});
        }
        if (asset_id && observations.empty()) {
            auto channels = telemetry_channels(*asset_id, true);
            if (!channels) return std::move(channels).error();
            for (const auto& c : channels.value()["channels"]) {
                if (c["ontologySymbol"].is_null() || c["latest"].is_null()) continue;
                const std::string sym = c["ontologySymbol"].get<std::string>();
                const json::Json& latest = c["latest"];
                std::string value;
                if (latest["value"].is_string()) value = latest["value"].get<std::string>();
                else if (latest["value"].is_number()) value = decimal_text(latest["value"].get<double>());
                else continue;
                if (c["freshness"] != "fresh") {
                    warnings.push_back("observation of '" + sym + "' (" + c["id"].get<std::string>() + ") is " +
                                       c["freshness"].get<std::string>() + "; last observed " +
                                       latest["observedAt"].get<std::string>());
                }
                if (latest["quality"] == "bad") continue;  // never reason from invalid data
                obs.push_back({sym, value});
                used.push_back({{"symbol", sym},
                                {"value", value},
                                {"unit", c["unit"]},
                                {"channelId", c["id"]},
                                {"assetId", c["assetId"]},
                                {"observedAt", latest["observedAt"]},
                                {"ingestedAt", latest["ingestedAt"]},
                                {"quality", latest["quality"]},
                                {"freshness", c["freshness"]},
                                {"source", c["source"]}});
            }
        }
    }
    auto r = run_with_large_stack([&] { return onto::evaluate_interpretation(ont_text, interp_text, obs, keys, onto::SolverConfig{config_.solver_timeout_ms}); });
    if (!r) return std::move(r).error();
    json::Json j = to_json(r.value());
    j["interpretationRef"] = interpretation.str();
    j["ontologyRef"] = ontology_ref;
    j["observations"] = used;
    j["warnings"] = warnings;
    j["evaluatedAt"] = iso8601_utc(clock_->now_ms());
    return j;
}

}  // namespace twin::studio
