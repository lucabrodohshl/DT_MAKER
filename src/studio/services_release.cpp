/**
 * @file services_release.cpp
 * @brief Services: twins and their trust summary, impact analysis, change workspaces,
 *        the release pipeline, release, deployment and rollback.
 *
 * Nothing in this file decides a formal question. It *locates* the evidence
 * produced by the formal tools for the exact artefact hashes involved and
 * reports its state; absence of evidence is reported as "not_checked", never as pass.
 */
#include <algorithm>
#include <map>

#include "ontology_json.hpp"
#include "services_impl.hpp"
#include "twin/core/version.hpp"
#include "twin/ontology/diff.hpp"
#include "twin/ontology/source.hpp"
#include "twin/package/package.hpp"

namespace twin::studio {

using namespace twin::platform;
namespace onto = twin::ontology;

namespace {

std::string trust_of(Outcome o) {
    switch (o) {
        case Outcome::Pass: return std::string(trust::kPass);
        case Outcome::Fail: return std::string(trust::kFail);
        case Outcome::Unknown: return std::string(trust::kUnknown);
        case Outcome::Error: return std::string(trust::kError);
    }
    return std::string(trust::kUnknown);
}

std::vector<EvidenceInput> as_inputs(const std::vector<Binding>& bindings, std::initializer_list<std::string_view> roles,
                                     std::string_view prefix = "") {
    std::vector<EvidenceInput> out;
    for (const auto& b : bindings) {
        if (roles.size() == 0 || std::find(roles.begin(), roles.end(), b.role) != roles.end()) {
            out.push_back({std::string(prefix) + b.role, b.ref, b.sha256});
        }
    }
    return out;
}

/// @brief The package manifest recorded in package-build evidence, parsed with the package library itself.
std::optional<package::Manifest> manifest_in(const Result<json::Json>& doc) {
    if (!doc || !doc.value().contains("manifest")) return std::nullopt;
    auto m = package::manifest_from_json(doc.value()["manifest"]);
    if (!m) return std::nullopt;
    return std::move(m).value();
}

bool same_binding(const std::vector<Binding>& a, const std::vector<Binding>& b, std::string_view role) {
    const Binding* x = find_binding(a, role);
    const Binding* y = find_binding(b, role);
    return x != nullptr && y != nullptr && x->sha256 == y->sha256;
}

json::Json stage(std::string id, std::string title, std::string_view state, bool mandatory, std::string detail,
                 json::Json evidence = json::Json::array(), json::Json extra = json::Json::object()) {
    json::Json s = {{"id", std::move(id)},     {"title", std::move(title)}, {"state", std::string(state)},
                    {"mandatory", mandatory},  {"detail", std::move(detail)}, {"evidence", std::move(evidence)}};
    for (auto it = extra.begin(); it != extra.end(); ++it) s[it.key()] = it.value();
    return s;
}

}  // namespace

// --- twins ---------------------------------------------------------------------------------

Result<json::Json> Services::twins() {
    auto l = impl_->lock();
    auto list = impl_->twins->twins();
    if (!list) return std::move(list).error();
    json::Json out = json::Json::array();
    for (const auto& t : list.value()) {
        json::Json j = to_json(t);
        auto dep = impl_->twins->current_deployment(t.id);
        if (!dep) return std::move(dep).error();
        j["deployment"] = dep.value() ? to_json(*dep.value()) : json::Json(nullptr);
        out.push_back(j);
    }
    return out;
}

Result<json::Json> Services::twin(std::string_view id) {
    auto l = impl_->lock();
    auto t = impl_->twins->twin(id);
    if (!t) return std::move(t).error();
    json::Json j = to_json(t.value());
    auto dep = impl_->twins->current_deployment(id);
    if (!dep) return std::move(dep).error();
    j["deployment"] = dep.value() ? to_json(*dep.value()) : json::Json(nullptr);
    json::Json trust_summary = json::Json::object();
    j["bindings"] = json::Json::array();
    if (!dep.value()) {
        for (const char* k : {"alignment", "ontologyRefinement", "packageIntegrity", "runtimeCompatibility", "compilation"}) {
            trust_summary[k] = {{"state", std::string(trust::kNotChecked)}, {"detail", "The twin has never been deployed."}};
        }
        j["trust"] = trust_summary;
        j["package"] = nullptr;
        return j;
    }
    auto pkg = impl_->twins->package(dep.value()->package_id);
    if (!pkg) return std::move(pkg).error();
    j["package"] = to_json(pkg.value());
    for (const auto& b : pkg.value().bindings) {
        auto s = impl_->version_summary(b.ref);
        json::Json bj = to_json(b);
        bj["version"] = s ? s.value() : json::Json(nullptr);
        j["bindings"].push_back(bj);
    }
    const auto& bindings = pkg.value().bindings;

    // Alignment: exact alignment evidence, else the alignment recorded in the package build.
    auto align = impl_->evidence->latest_for(EvidenceKind::Alignment, as_inputs(bindings, {}));
    if (!align) return std::move(align).error();
    if (align.value()) {
        trust_summary["alignment"] = {{"state", trust_of(align.value()->outcome)},
                                      {"verdict", align.value()->verdict},
                                      {"detail", align.value()->summary},
                                      {"evidenceId", align.value()->id},
                                      {"at", align.value()->created_at}};
    } else if (pkg.value().evidence_id) {
        auto pe = impl_->evidence->get(*pkg.value().evidence_id);
        auto doc = pe ? impl_->evidence->document(pe.value()) : Result<json::Json>(make_error(ErrorCode::NotFound, ""));
        const auto manifest = manifest_in(doc);
        const bool aligned = manifest && manifest->aligned && manifest->lint_clean;
        trust_summary["alignment"] = {{"state", aligned ? std::string(trust::kPass) : std::string(trust::kFail)},
                                      {"verdict", aligned ? "aligned" : "not_aligned"},
                                      {"detail", "Recorded by the aligner during the build of " + pkg.value().id + "."},
                                      {"evidenceId", *pkg.value().evidence_id},
                                      {"at", pkg.value().created_at}};
    } else {
        trust_summary["alignment"] = {{"state", std::string(trust::kNotChecked)}, {"detail", "No alignment evidence."}};
    }

    // Ontology lineage: refinement of the deployed ontology version against its parent.
    const Binding* ont = find_binding(bindings, "ontology");
    trust_summary["ontologyRefinement"] = {{"state", std::string(trust::kNotApplicable)},
                                           {"detail", "First version of the ontology (no parent)."}};
    if (ont != nullptr) {
        auto ov = impl_->artifacts->version(ont->ref);
        if (ov && ov.value().parent) {
            auto parent = impl_->artifacts->version({ont->ref.artifact_id, *ov.value().parent});
            EvidenceFilter f;
            f.kind = EvidenceKind::Refinement;
            f.artifact_id = ont->ref.artifact_id;
            f.version = ont->ref.version;
            auto list = impl_->evidence->list(f);
            std::optional<EvidenceRecord> found;
            if (list && parent) {
                for (const auto& e : list.value()) {
                    const auto* bi = e.input("base_ontology");
                    const auto* ci = e.input("candidate_ontology");
                    if (bi && ci && bi->sha256 == parent.value().content_sha256 && ci->sha256 == ont->sha256) {
                        found = e;
                        break;
                    }
                }
            }
            if (found) {
                trust_summary["ontologyRefinement"] = {
                    {"state", trust_of(found->outcome)},
                    {"verdict", found->verdict},
                    {"detail", ont->ref.artifact_id + "@" + std::to_string(*ov.value().parent) + " → " + ont->ref.str() +
                                   ": " + found->summary},
                    {"evidenceId", found->id},
                    {"at", found->created_at}};
            } else {
                trust_summary["ontologyRefinement"] = {
                    {"state", std::string(trust::kNotChecked)},
                    {"detail", "Refinement of " + ont->ref.str() + " against its parent version has not been checked."}};
            }
        }
    }

    // Package integrity (live) and runtime compatibility.
    auto verify = verify_package(pkg.value().id);
    if (verify) {
        trust_summary["packageIntegrity"] = {{"state", verify.value()["integrity"]},
                                             {"detail", verify.value()["integrity"] == "pass"
                                                            ? "All " + std::to_string(verify.value()["checks"].size()) +
                                                                  " integrity checks passed."
                                                            : verify.value()["firstFailure"].get<std::string>()},
                                             {"at", verify.value()["verifiedAt"]}};
    } else {
        trust_summary["packageIntegrity"] = {{"state", std::string(trust::kError)}, {"detail", verify.error().message}};
    }
    if (pkg.value().evidence_id) {
        auto pe = impl_->evidence->get(*pkg.value().evidence_id);
        auto doc = pe ? impl_->evidence->document(pe.value()) : Result<json::Json>(make_error(ErrorCode::NotFound, ""));
        if (const auto manifest = manifest_in(doc)) {
            const auto compat = manifest->kernel_compat;
            const bool ok = compat == std::string(twin::version::kKernelCompat);
            trust_summary["runtimeCompatibility"] = {
                {"state", ok ? std::string(trust::kPass) : std::string(trust::kFail)},
                {"detail", "Package requires " + compat + "; this toolchain targets " +
                               std::string(twin::version::kKernelCompat) + "."}};
            const bool tv = manifest->translation_validated;
            trust_summary["compilation"] = {{"state", tv ? std::string(trust::kPass) : std::string(trust::kFail)},
                                            {"detail", "IR " + pkg.value().ir_sha256.substr(0, 16) +
                                                           "… translation validation " + (tv ? "passed." : "FAILED.")},
                                            {"evidenceId", *pkg.value().evidence_id}};
        }
    }
    if (!trust_summary.contains("runtimeCompatibility")) {
        trust_summary["runtimeCompatibility"] = {{"state", std::string(trust::kUnknown)}, {"detail", "No package manifest evidence."}};
    }
    if (!trust_summary.contains("compilation")) {
        trust_summary["compilation"] = {{"state", std::string(trust::kUnknown)}, {"detail", "No package evidence."}};
    }
    j["trust"] = trust_summary;
    return j;
}

// --- impact --------------------------------------------------------------------------------

Result<json::Json> Services::impact(const ArtifactRef& ref) {
    auto l = impl_->lock();
    auto subject = impl_->artifacts->version(ref);
    if (!subject) return std::move(subject).error();
    auto subject_text = impl_->artifacts->content(ref);
    if (!subject_text) return std::move(subject_text).error();
    const ArtifactKind kind = subject.value().kind;

    json::Json nodes = json::Json::array();
    json::Json edges = json::Json::array();
    json::Json actions = json::Json::array();
    std::set<std::string> action_keys;
    auto add_action = [&](const std::string& stage_id, const std::string& text) {
        if (action_keys.insert(stage_id + text).second) actions.push_back({{"stage", stage_id}, {"action", text}});
    };
    auto node = [&](std::string id, std::string type, std::string label, std::string cls, std::string reason,
                    json::Json extra = json::Json::object()) {
        json::Json n = {{"id", std::move(id)}, {"type", std::move(type)}, {"label", std::move(label)},
                        {"classification", std::move(cls)}, {"reason", std::move(reason)}};
        for (auto it = extra.begin(); it != extra.end(); ++it) n[it.key()] = it.value();
        nodes.push_back(n);
    };
    const std::string root = "artifact:" + ref.str();
    node(root, std::string(to_string(kind)), ref.str(), "changed", "The version under analysis.",
         {{"ref", ref.str()}, {"sha256", subject.value().content_sha256}});

    json::Json twins_json = json::Json::array();
    auto twins = impl_->twins->twins();
    if (!twins) return std::move(twins).error();
    bool deployed_anywhere = false;
    for (const auto& t : twins.value()) {
        auto bound = deployed_bindings(t.id);
        if (!bound) return std::move(bound).error();
        const auto& B = bound.value();
        const auto it = std::find_if(B.begin(), B.end(), [&](const Binding& b) { return b.ref.artifact_id == ref.artifact_id; });
        if (it == B.end()) continue;
        deployed_anywhere = true;
        const Binding old = *it;
        std::vector<Binding> C = B;
        for (auto& b : C) {
            if (b.ref.artifact_id == ref.artifact_id) {
                b.ref = ref;
                b.sha256 = subject.value().content_sha256;
            }
        }
        const bool identical = old.sha256 == subject.value().content_sha256;
        auto dep = impl_->twins->current_deployment(t.id);
        const std::string dep_id = dep && dep.value() ? dep.value()->id : "";
        const std::string pkg_id = dep && dep.value() ? dep.value()->package_id : "";
        const std::string twin_node = "twin:" + t.id;

        // Structural diff against the deployed version (ontology / interpretation).
        std::set<std::string> affected_symbols;
        json::Json structural = nullptr;
        if (!identical && (kind == ArtifactKind::Ontology || kind == ArtifactKind::Interpretation)) {
            auto old_text = impl_->artifacts->content(old.ref);
            if (old_text) {
                if (kind == ArtifactKind::Ontology) {
                    const auto d = onto::diff_ontologies(onto::parse_ontology(old_text.value()).source,
                                                         onto::parse_ontology(subject_text.value()).source);
                    affected_symbols = d.affected_symbols;
                    structural = to_json(d);
                } else {
                    structural = to_json(onto::diff_interpretations(onto::parse_interpretation(old_text.value()).source,
                                                                    onto::parse_interpretation(subject_text.value()).source));
                }
            }
        }

        // Interpretations bound next to a changed ontology.
        if (kind == ArtifactKind::Ontology) {
            for (const char* role : {"pt_interpretation", "dt_interpretation"}) {
                const Binding* ib = find_binding(B, role);
                if (ib == nullptr) continue;
                auto itext = impl_->artifacts->content(ib->ref);
                std::vector<std::string> entries;
                if (itext) entries = onto::entries_using(onto::parse_interpretation(itext.value()).source, affected_symbols);
                const std::string id = "artifact:" + ib->ref.str() + ":" + t.id;
                node(id, "interpretation", ib->ref.str() + " (" + role + ")",
                     entries.empty() ? "potentially_affected" : "requires_verification",
                     entries.empty()
                         ? "No entry mentions a symbol whose declaration or axioms changed; its meaning can still change "
                           "through Δ, which the refinement check (condition c) decides."
                         : std::to_string(entries.size()) + " entr" + (entries.size() == 1 ? "y uses" : "ies use") +
                               " symbols whose axioms or declaration changed; condition (c) of Def. 4 must hold for them.",
                     {{"ref", ib->ref.str()}, {"entries", entries}, {"role", role}});
                edges.push_back({{"from", root}, {"to", id}, {"label", "interprets over"}});
            }
        }

        // Alignment evidence of the deployment and its fate under the change.
        const bool models_changed = kind == ArtifactKind::PtModel || kind == ArtifactKind::DtModel;
        const bool phi_changed = kind == ArtifactKind::Ontology || kind == ArtifactKind::Interpretation;
        auto align = impl_->evidence->latest_for(EvidenceKind::Alignment, as_inputs(B, {}));
        if (!align) return std::move(align).error();
        std::optional<std::string> align_id = align.value() ? std::optional(align.value()->id) : std::nullopt;
        if (!align_id) {
            auto pk = impl_->twins->package(pkg_id);
            if (pk && pk.value().evidence_id) align_id = *pk.value().evidence_id;
        }
        const std::string align_node = "alignment:" + t.id;
        if (identical) {
            node(align_node, "alignment", "Alignment of " + t.name, "unaffected", "The content is identical to the deployed version.",
                 {{"evidenceId", align_id ? json::Json(*align_id) : json::Json(nullptr)}});
        } else if (models_changed) {
            node(align_node, "alignment", "Alignment of " + t.name, "definitely_stale",
                 "A behavioural view changed; Theorem 3 only covers domain-knowledge refinement. Alignment must be re-run.",
                 {{"evidenceId", align_id ? json::Json(*align_id) : json::Json(nullptr)}});
            add_action("alignment", "Re-run semantic alignment for " + t.name + ".");
        } else if (phi_changed) {
            // Preservation route (Theorem 3): a valid Def. 4 check of exactly (Φ_deployed → Φ_candidate).
            std::vector<EvidenceInput> rin = as_inputs(B, {"ontology", "pt_interpretation", "dt_interpretation"}, "base_");
            auto cin = as_inputs(C, {"ontology", "pt_interpretation", "dt_interpretation"}, "candidate_");
            rin.insert(rin.end(), cin.begin(), cin.end());
            auto ref_ev = impl_->evidence->latest_for(EvidenceKind::Refinement, rin);
            if (!ref_ev) return std::move(ref_ev).error();
            if (ref_ev.value() && ref_ev.value()->outcome == Outcome::Pass) {
                node(align_node, "alignment", "Alignment of " + t.name, "preserved",
                     "Theorem 3: the candidate domain knowledge is a valid refinement (" + ref_ev.value()->id +
                         ") of the deployed one, under which the views are aligned.",
                     {{"evidenceId", align_id ? json::Json(*align_id) : json::Json(nullptr)},
                      {"refinementEvidenceId", ref_ev.value()->id}});
            } else if (ref_ev.value()) {
                node(align_node, "alignment", "Alignment of " + t.name, "requires_verification",
                     "The refinement check (" + ref_ev.value()->id + ") returned " + ref_ev.value()->verdict +
                         ", so Theorem 3 does not apply: re-run alignment.",
                     {{"evidenceId", align_id ? json::Json(*align_id) : json::Json(nullptr)},
                      {"refinementEvidenceId", ref_ev.value()->id}});
                add_action("alignment", "Re-run semantic alignment for " + t.name + " (refinement does not hold).");
            } else {
                node(align_node, "alignment", "Alignment of " + t.name, "requires_verification",
                     "Ontology/interpretation dependency changed. Run the refinement check against the deployed "
                     "domain knowledge: if it is a valid refinement, alignment is preserved (Theorem 3); otherwise "
                     "re-run alignment.",
                     {{"evidenceId", align_id ? json::Json(*align_id) : json::Json(nullptr)}});
                add_action("refinement", "Check refinement of the candidate against the deployed domain knowledge of " +
                                             t.name + ".");
            }
        }
        edges.push_back({{"from", root}, {"to", align_node}, {"label", "input of"}});

        // Compilation depends on V_D and I_D only.
        const bool compile_affected = !identical && (kind == ArtifactKind::DtModel ||
                                                     (kind == ArtifactKind::Interpretation && old.role == "dt_interpretation"));
        const std::string compile_node = "compile:" + t.id;
        node(compile_node, "compilation", "Twin IR of " + t.name, compile_affected ? "definitely_stale" : "unaffected",
             compile_affected ? "The IR is compiled from the DT view and its interpretation; it must be recompiled."
                              : "The IR depends only on the DT view and the DT interpretation, which are unchanged.");
        edges.push_back({{"from", root}, {"to", compile_node}, {"label", compile_affected ? "input of" : "not used by"}});
        if (compile_affected) add_action("compile", "Recompile the DT view of " + t.name + ".");

        // Package and deployment.
        const std::string pkg_node = "package:" + pkg_id;
        node(pkg_node, "package", pkg_id, identical ? "unaffected" : "definitely_stale",
             identical ? "Same content." : "Packages are immutable; a new package must be built from the new versions.",
             {{"packageId", pkg_id}});
        edges.push_back({{"from", align_node}, {"to", pkg_node}, {"label", "evidence in"}});
        edges.push_back({{"from", compile_node}, {"to", pkg_node}, {"label", "IR in"}});
        if (!identical) add_action("package", "Build and verify a new package for " + t.name + ".");
        node("deployment:" + dep_id, "deployment", dep_id + " (" + t.name + ")", identical ? "unaffected" : "potentially_affected",
             identical ? "Same content."
                       : "Keeps running " + old.ref.str() + " until a new package is released and deployed; its "
                         "current evidence remains valid for what it runs.",
             {{"deploymentId", dep_id}, {"twinId", t.id}});
        edges.push_back({{"from", pkg_node}, {"to", "deployment:" + dep_id}, {"label", "deployed as"}});
        node(twin_node, "twin", t.name, identical ? "unaffected" : "potentially_affected",
             "Operational twin bound to asset " + t.asset_id.value_or("-") + ".", {{"twinId", t.id}});
        edges.push_back({{"from", "deployment:" + dep_id}, {"to", twin_node}, {"label", "runs"}});
        twins_json.push_back({{"twinId", t.id}, {"name", t.name}, {"deployedRef", old.ref.str()}, {"role", old.role},
                              {"structural", structural}, {"identicalToDeployed", identical}});
    }
    if (!deployed_anywhere) {
        node("none", "note", "Not deployed", "unaffected",
             "No deployed twin uses " + ref.artifact_id + "; no operational artefact is affected.");
    }
    return json::Json{{"subject", impl_->version_summary(ref).value()},
                      {"twins", twins_json},
                      {"nodes", nodes},
                      {"edges", edges},
                      {"requiredActions", actions},
                      {"analyzedAt", iso8601_utc(clock_->now_ms())},
                      {"legend",
                       {{"changed", "the artefact version being analysed"},
                        {"definitely_stale", "evidence/artefact no longer applies and must be regenerated"},
                        {"requires_verification", "a formal check decides whether existing evidence carries over"},
                        {"preserved", "existing evidence carries over by a recorded theorem application"},
                        {"potentially_affected", "keeps working as is; picks up the change only through a new deployment"},
                        {"unaffected", "does not depend on the changed content"}}}};
}

// --- changes -------------------------------------------------------------------------------

Result<json::Json> Services::create_change(std::string_view twin_id, std::string_view title, std::string_view description,
                                           const Actor& actor) {
    auto l = impl_->lock();
    auto c = impl_->twins->create_change(twin_id, title, description, actor.name);
    if (!c) return std::move(c).error();
    impl_->record("change.create", "success", c.value().id, {{"twinId", std::string(twin_id)}, {"title", std::string(title)}},
                  actor, "change");
    return to_json(c.value());
}

Result<json::Json> Services::changes(const std::optional<std::string>& state) {
    auto l = impl_->lock();
    auto list = impl_->twins->changes(state);
    if (!list) return std::move(list).error();
    json::Json out = json::Json::array();
    for (const auto& c : list.value()) out.push_back(to_json(c));
    return out;
}

Result<json::Json> Services::change(std::string_view id) {
    auto l = impl_->lock();
    auto c = impl_->twins->change(id);
    if (!c) return std::move(c).error();
    json::Json j = to_json(c.value());
    json::Json arts = json::Json::array();
    auto base = deployed_bindings(c.value().twin_id);
    for (const auto& ref : c.value().artifacts) {
        auto s = impl_->version_summary(ref);
        json::Json a = s ? s.value() : json::Json{{"ref", ref.str()}};
        a["deployedRef"] = nullptr;
        if (base) {
            for (const auto& b : base.value()) {
                if (b.ref.artifact_id == ref.artifact_id) {
                    a["deployedRef"] = b.ref.str();
                    a["role"] = b.role;
                }
            }
        }
        arts.push_back(a);
    }
    j["artifactVersions"] = arts;
    auto tw = impl_->twins->twin(c.value().twin_id);
    j["twin"] = tw ? to_json(tw.value()) : json::Json(nullptr);
    return j;
}

Result<json::Json> Services::add_to_change(std::string_view change_id, std::string_view artifact_id,
                                           std::string_view description, const Actor& actor) {
    auto l = impl_->lock();
    auto c = impl_->twins->change(change_id);
    if (!c) return std::move(c).error();
    if (c.value().state != "open") return make_error(ErrorCode::StateError, "the change is closed");
    for (const auto& r : c.value().artifacts) {
        if (r.artifact_id == artifact_id) {
            return make_error(ErrorCode::StateError, "this artefact is already part of the change").with("ref", r.str());
        }
    }
    auto bound = deployed_bindings(c.value().twin_id);
    if (!bound) return std::move(bound).error();
    std::optional<ArtifactRef> from;
    for (const auto& b : bound.value()) {
        if (b.ref.artifact_id == artifact_id) from = b.ref;
    }
    if (!from) {
        return make_error(ErrorCode::InvalidArgument, "the twin's deployment does not use this artefact")
            .with("artifact", std::string(artifact_id));
    }
    return create_draft(*from, description, std::string(change_id), actor);
}

Result<json::Json> Services::abandon_change(std::string_view change_id, std::string_view reason, const Actor& actor) {
    if (reason.empty()) return make_error(ErrorCode::InvalidArgument, "abandoning a change requires a reason");
    auto l = impl_->lock();
    Transaction tx(*impl_->db);
    if (!tx.begun()) return tx.begun().error();
    auto c = impl_->twins->change(change_id);
    if (!c) return std::move(c).error();
    for (const auto& r : c.value().artifacts) {
        auto v = impl_->artifacts->version(r);
        if (v && v.value().is_open() && v.value().state != Lifecycle::Validating) {
            if (auto s = impl_->artifacts->set_state(r, Lifecycle::Rejected, actor.name); !s) return std::move(s).error();
        }
    }
    auto closed = impl_->twins->close_change(change_id, "abandoned");
    if (!closed) return std::move(closed).error();
    if (auto st = tx.commit(); !st) return st.error();
    impl_->record("change.abandon", "success", std::string(change_id), {{"reason", std::string(reason)}}, actor, "change");
    return to_json(closed.value());
}

// --- pipeline ------------------------------------------------------------------------------

Result<json::Json> Services::pipeline(std::string_view change_id) {
    auto l = impl_->lock();
    auto c = impl_->twins->change(change_id);
    if (!c) return std::move(c).error();
    const Change& ch = c.value();
    auto base = deployed_bindings(ch.twin_id);
    if (!base) return std::move(base).error();
    const auto& B = base.value();
    json::Json stages = json::Json::array();
    auto running = [&](const std::string& k) { return impl_->is_running(k + ":" + ch.id); };

    if (B.empty()) {
        stages.push_back(stage("edit", "Edit", trust::kBlocked, true,
                               "The twin has no deployment to change; deploy an initial package first."));
        return json::Json{{"changeId", ch.id}, {"stages", stages}, {"releaseReady", false},
                          {"computedAt", iso8601_utc(clock_->now_ms())}};
    }
    auto cand = candidate_bindings(change_id);
    if (!cand) return std::move(cand).error();
    const auto& C = cand.value();
    bool any_ontology_or_interp = false;
    bool models_changed = false;
    for (const auto& r : ch.artifacts) {
        auto v = impl_->artifacts->version(r);
        if (!v) return std::move(v).error();
        if (v.value().kind == ArtifactKind::Ontology || v.value().kind == ArtifactKind::Interpretation) any_ontology_or_interp = true;
        if (v.value().kind == ArtifactKind::PtModel || v.value().kind == ArtifactKind::DtModel) models_changed = true;
    }

    // 1-2. Edit & save.
    stages.push_back(stage("edit", "Edit & save drafts", ch.artifacts.empty() ? trust::kNotChecked : trust::kPass, true,
                           ch.artifacts.empty() ? "Add an artefact to the change to create a draft."
                                                : std::to_string(ch.artifacts.size()) + " draft version(s) in this change."));
    // 3. Validate.
    {
        std::string state(trust::kPass);
        json::Json ev = json::Json::array();
        std::string detail;
        for (const auto& r : ch.artifacts) {
            auto v = impl_->artifacts->version(r).value();
            std::vector<EvidenceInput> in{{"subject", r, v.content_sha256}};
            if (v.kind == ArtifactKind::Interpretation) {
                if (auto it = v.refs.find("ontology"); it != v.refs.end() && it->is_string()) {
                    if (auto o = parse_ref(it->get<std::string>()); o) {
                        if (auto ov = impl_->artifacts->version(o.value()); ov) {
                            in.push_back({"ontology", o.value(), ov.value().content_sha256});
                        }
                    }
                }
            }
            auto e = impl_->evidence->latest_for(EvidenceKind::Validation, in);
            if (!e) return std::move(e).error();
            std::string s = e.value() ? trust_of(e.value()->outcome) : std::string(trust::kNotChecked);
            if (impl_->is_running("validate:" + r.str())) s = trust::kRunning;
            if (e.value()) ev.push_back(e.value()->id);
            detail += r.str() + ": " + s + ". ";
            if (s != trust::kPass && state == trust::kPass) state = s;
            if (s == trust::kFail) state = trust::kFail;
        }
        if (ch.artifacts.empty()) state = trust::kNotChecked;
        stages.push_back(stage("validate", "Validate", state, true, detail, ev));
    }
    // 4. Refinement (domain knowledge only).
    std::optional<EvidenceRecord> refinement;
    if (any_ontology_or_interp) {
        std::vector<EvidenceInput> rin = as_inputs(B, {"ontology", "pt_interpretation", "dt_interpretation"}, "base_");
        auto cin = as_inputs(C, {"ontology", "pt_interpretation", "dt_interpretation"}, "candidate_");
        rin.insert(rin.end(), cin.begin(), cin.end());
        auto e = impl_->evidence->latest_for(EvidenceKind::Refinement, rin);
        if (!e) return std::move(e).error();
        refinement = e.value();
        std::string s = e.value() ? trust_of(e.value()->outcome) : std::string(trust::kNotChecked);
        if (running("refinement")) s = trust::kRunning;
        stages.push_back(stage("refinement", "Check refinement (Def. 4)", s, false,
                               e.value() ? e.value()->summary
                                         : "Not checked for the current drafts. A valid refinement lets existing "
                                           "alignment carry over (Theorem 3).",
                               e.value() ? json::Json::array({e.value()->id}) : json::Json::array()));
    } else {
        stages.push_back(stage("refinement", "Check refinement (Def. 4)", trust::kNotApplicable, false,
                               "The change does not modify the ontology or an interpretation."));
    }
    // 5. Impact (always computable).
    stages.push_back(stage("impact", "Analyze impact", ch.artifacts.empty() ? trust::kNotChecked : trust::kPass, false,
                           "Computed from the dependency graph on demand; see the impact view."));
    // 6. Alignment.
    {
        auto exact = impl_->evidence->latest_for(EvidenceKind::Alignment, as_inputs(C, {}));
        if (!exact) return std::move(exact).error();
        std::string s(trust::kNotChecked);
        std::string detail;
        json::Json ev = json::Json::array();
        json::Json extra = json::Json::object();
        if (exact.value()) {
            s = trust_of(exact.value()->outcome);
            detail = exact.value()->summary;
            ev.push_back(exact.value()->id);
            extra["route"] = "alignment";
        } else if (refinement && refinement->outcome == Outcome::Pass && !models_changed) {
            // Theorem 3 premise 2: the deployed views are aligned under the deployed Φ.
            auto base_align = impl_->evidence->latest_for(EvidenceKind::Alignment, as_inputs(B, {}));
            std::optional<std::string> base_ev;
            bool base_ok = false;
            if (base_align && base_align.value()) {
                base_ok = base_align.value()->outcome == Outcome::Pass;
                base_ev = base_align.value()->id;
            } else if (auto dep = impl_->twins->current_deployment(ch.twin_id); dep && dep.value()) {
                auto pk = impl_->twins->package(dep.value()->package_id);
                if (pk && pk.value().evidence_id) {
                    auto pe = impl_->evidence->get(*pk.value().evidence_id);
                    if (pe) {
                        auto doc = impl_->evidence->document(pe.value());
                        const auto manifest = manifest_in(doc);
                        base_ok = pe.value().outcome == Outcome::Pass && manifest && manifest->aligned && manifest->lint_clean;
                        base_ev = pe.value().id;
                    }
                }
            }
            if (base_ok) {
                s = trust::kPass;
                detail = "Preserved by Theorem 3: valid refinement " + refinement->id + " of the deployed domain "
                         "knowledge, under which the views are aligned (" + *base_ev + ").";
                ev = {refinement->id, *base_ev};
                extra["route"] = "theorem3";
            } else {
                detail = "Refinement holds, but no passing alignment evidence exists for the deployed artefacts; "
                         "re-run alignment.";
            }
        } else if (refinement && refinement->outcome != Outcome::Pass) {
            detail = "Refinement does not hold (" + refinement->verdict + "): alignment must be re-run on the new "
                     "domain knowledge.";
        } else if (models_changed) {
            detail = "A behavioural view changed: alignment must be re-run.";
        } else {
            detail = "Not established for the current drafts.";
        }
        if (running("alignment")) s = trust::kRunning;
        stages.push_back(stage("alignment", "Check / re-check alignment", s, true, detail, ev, extra));
    }
    // 7. Compile.
    {
        auto e = impl_->evidence->latest_for(EvidenceKind::Compilation, as_inputs(C, {"dt_model", "dt_interpretation"}));
        if (!e) return std::move(e).error();
        std::string s = e.value() ? trust_of(e.value()->outcome) : std::string(trust::kNotChecked);
        if (running("compile")) s = trust::kRunning;
        stages.push_back(stage("compile", "Compile DT view", s, true,
                               e.value() ? e.value()->summary : "The DT view has not been compiled with these artefacts.",
                               e.value() ? json::Json::array({e.value()->id}) : json::Json::array()));
    }
    // 8-9. Package build and verification.
    std::optional<PackageRecord> package;
    {
        auto e = impl_->evidence->latest_for(EvidenceKind::Package, as_inputs(C, {}));
        if (!e) return std::move(e).error();
        std::string s = e.value() ? trust_of(e.value()->outcome) : std::string(trust::kNotChecked);
        std::string detail = e.value() ? e.value()->summary : "No package has been built from these exact artefacts.";
        if (e.value() && e.value()->outcome == Outcome::Pass) {
            auto pkgs = impl_->twins->packages(ch.twin_id);
            if (pkgs) {
                for (const auto& p : pkgs.value()) {
                    if (p.evidence_id && *p.evidence_id == e.value()->id) package = p;
                }
            }
        }
        if (running("package")) s = trust::kRunning;
        stages.push_back(stage("package", "Build verified package", s, true, detail,
                               e.value() ? json::Json::array({e.value()->id}) : json::Json::array(),
                               {{"packageId", package ? json::Json(package->id) : json::Json(nullptr)}}));
        if (package) {
            auto v = verify_package(package->id);
            const std::string vs = v ? v.value()["integrity"].get<std::string>() : std::string(trust::kError);
            stages.push_back(stage("verify", "Verify package", vs, true,
                                   v ? (vs == "pass" ? "All " + std::to_string(v.value()["checks"].size()) +
                                                           " integrity checks pass (verified just now)."
                                                     : v.value()["firstFailure"].get<std::string>())
                                     : v.error().message,
                                   json::Json::array(), {{"packageId", package->id}}));
        } else {
            stages.push_back(stage("verify", "Verify package", trust::kNotChecked, true, "Build a package first."));
        }
    }
    // 10. Release readiness and release.
    bool ready = true;
    json::Json blocking = json::Json::array();
    for (const auto& s : stages) {
        if (s["mandatory"].get<bool>() && s["state"] != trust::kPass) {
            ready = false;
            blocking.push_back(s["id"]);
        }
    }
    const bool released = ch.state == "released";
    stages.push_back(stage("release", "Release", released ? trust::kPass : (ready ? trust::kNotChecked : trust::kBlocked), true,
                           released ? "Released: versions published, package " + (package ? package->id : std::string("?")) +
                                          " released."
                                    : (ready ? "Ready to release." : "Blocked until every mandatory stage passes.")));
    // 11. Deploy.
    {
        auto dep = impl_->twins->current_deployment(ch.twin_id);
        const bool deployed = package && dep && dep.value() && dep.value()->package_id == package->id;
        stages.push_back(stage("deploy", "Deploy", deployed ? trust::kPass : (released ? trust::kNotChecked : trust::kBlocked),
                               false,
                               deployed ? "Running as " + dep.value()->id + "."
                                        : (released ? "Released package is ready to deploy." : "Release first.")));
    }
    return json::Json{{"changeId", ch.id},
                      {"changeState", ch.state},
                      {"stages", stages},
                      {"releaseReady", ready && !released},
                      {"blocking", blocking},
                      {"candidateBindings", to_json(C)},
                      {"deployedBindings", to_json(B)},
                      {"packageId", package ? json::Json(package->id) : json::Json(nullptr)},
                      {"computedAt", iso8601_utc(clock_->now_ms())}};
}

Result<json::Json> Services::run_stage(std::string_view change_id, std::string_view stage_id, const Actor& actor) {
    std::string twin_id;
    std::vector<Binding> B;
    std::vector<Binding> C;
    std::vector<ArtifactRef> refs;
    {
        auto l = impl_->lock();
        auto c = impl_->twins->change(change_id);
        if (!c) return std::move(c).error();
        if (c.value().state != "open") return make_error(ErrorCode::StateError, "the change is closed");
        twin_id = c.value().twin_id;
        refs = c.value().artifacts;
        auto b = deployed_bindings(twin_id);
        if (!b) return std::move(b).error();
        B = b.value();
        auto cb = candidate_bindings(change_id);
        if (!cb) return std::move(cb).error();
        C = cb.value();
    }
    const std::optional<std::string> cid = std::string(change_id);
    if (stage_id == "validate") {
        json::Json results = json::Json::array();
        for (const auto& r : refs) {
            auto v = validate(r, actor);
            if (!v) return std::move(v).error();
            results.push_back({{"ref", r.str()}, {"evidenceId", v.value()["evidenceId"]}, {"state", v.value()["state"]}});
        }
        return json::Json{{"stage", "validate"}, {"results", results}};
    }
    auto ref_of = [&](const std::vector<Binding>& bs, std::string_view role) -> std::optional<ArtifactRef> {
        const Binding* b = find_binding(bs, role);
        return b != nullptr ? std::optional<ArtifactRef>(b->ref) : std::nullopt;
    };
    if (stage_id == "refinement") {
        const PhiRefs base{ref_of(B, "ontology").value(), ref_of(B, "pt_interpretation"), ref_of(B, "dt_interpretation")};
        const PhiRefs cand{ref_of(C, "ontology").value(), ref_of(C, "pt_interpretation"), ref_of(C, "dt_interpretation")};
        return run_refinement(base, cand, cid, actor);
    }
    if (stage_id == "alignment") return run_alignment(C, cid, actor);
    if (stage_id == "compile") return run_compile(twin_id, C, cid, actor);
    if (stage_id == "package") return build_package(twin_id, C, cid, actor);
    if (stage_id == "verify") {
        auto p = pipeline(change_id);
        if (!p) return std::move(p).error();
        if (p.value()["packageId"].is_null()) return make_error(ErrorCode::StateError, "build a package first");
        auto v = verify_package(p.value()["packageId"].get<std::string>());
        if (!v) return std::move(v).error();
        impl_->record("package.verify", v.value()["integrity"].get<std::string>(), p.value()["packageId"].get<std::string>(),
                      {{"changeId", std::string(change_id)}}, actor, "package");
        return v;
    }
    return make_error(ErrorCode::InvalidArgument, "unknown or non-runnable stage").with("stage", std::string(stage_id));
}

Result<json::Json> Services::release(std::string_view change_id, const Actor& actor) {
    auto l = impl_->lock();
    auto p = pipeline(change_id);
    if (!p) return std::move(p).error();
    if (!p.value()["releaseReady"].get<bool>()) {
        Error e = make_error(ErrorCode::StateError, "release blocked: mandatory evidence is missing or failed");
        for (const auto& b : p.value()["blocking"]) e.with("blocking", b.get<std::string>());
        return e;
    }
    auto c = impl_->twins->change(change_id);
    if (!c) return std::move(c).error();
    const std::string package_id = p.value()["packageId"].get<std::string>();
    Transaction tx(*impl_->db);
    if (!tx.begun()) return tx.begun().error();
    json::Json published = json::Json::array();
    for (const auto& r : c.value().artifacts) {
        auto v = impl_->artifacts->version(r);
        if (!v) return std::move(v).error();
        if (v.value().state != Lifecycle::Verified) {
            return make_error(ErrorCode::StateError, "every version in the change must be VERIFIED before release")
                .with("ref", r.str())
                .with("state", std::string(to_string(v.value().state)));
        }
        auto pub = impl_->artifacts->publish(r, actor.name);
        if (!pub) return std::move(pub).error();
        published.push_back(r.str());
    }
    auto pkg = impl_->twins->mark_released(package_id);
    if (!pkg) return std::move(pkg).error();
    auto closed = impl_->twins->close_change(change_id, "released");
    if (!closed) return std::move(closed).error();
    if (auto st = tx.commit(); !st) return st.error();
    impl_->record("change.release", "success", std::string(change_id),
                  {{"packageId", package_id}, {"published", published}, {"packageHash", pkg.value().package_hash}}, actor,
                  "change");
    return json::Json{{"change", to_json(closed.value())}, {"package", to_json(pkg.value())}, {"published", published}};
}

// --- deployments ---------------------------------------------------------------------------

Result<json::Json> Services::deploy(std::string_view twin_id, std::string_view package_id, std::string_view reason,
                                    const Actor& actor) {
    auto v = verify_package(package_id);
    if (!v) return std::move(v).error();
    if (v.value()["integrity"] != "pass") {
        return make_error(ErrorCode::IntegrityError, "the package failed integrity verification and cannot be deployed")
            .with("firstFailure", v.value()["firstFailure"].get<std::string>());
    }
    auto l = impl_->lock();
    auto d = impl_->twins->deploy(twin_id, package_id, "deploy", reason, actor.name);
    if (!d) return std::move(d).error();
    impl_->record("deployment.deploy", "success", std::string(twin_id),
                  {{"deploymentId", d.value().id}, {"packageId", std::string(package_id)},
                   {"previousPackageId", d.value().previous_package_id ? json::Json(*d.value().previous_package_id) : json::Json(nullptr)}},
                  actor, "deployment");
    return to_json(d.value());
}

Result<json::Json> Services::rollback_preview(std::string_view twin_id, std::string_view package_id) {
    auto l = impl_->lock();
    auto dep = impl_->twins->current_deployment(twin_id);
    if (!dep) return std::move(dep).error();
    if (!dep.value()) return make_error(ErrorCode::StateError, "the twin has no deployment to roll back");
    auto current = impl_->twins->package(dep.value()->package_id);
    auto target = impl_->twins->package(package_id);
    if (!current) return std::move(current).error();
    if (!target) return std::move(target).error();
    if (target.value().twin_id != twin_id) return make_error(ErrorCode::InvalidArgument, "package belongs to another twin");
    json::Json roles = json::Json::array();
    for (const auto role : kBindingRoles) {
        const Binding* a = current.value().binding(role);
        const Binding* b = target.value().binding(role);
        roles.push_back({{"role", std::string(role)},
                         {"current", a ? json::Json(a->ref.str()) : json::Json(nullptr)},
                         {"target", b ? json::Json(b->ref.str()) : json::Json(nullptr)},
                         {"same", same_binding(current.value().bindings, target.value().bindings, role)}});
    }
    auto verify = verify_package(package_id);
    json::Json target_evidence = nullptr;
    if (target.value().evidence_id) {
        auto e = impl_->evidence->get(*target.value().evidence_id);
        if (e) target_evidence = to_json(e.value());
    }
    return json::Json{{"current", to_json(current.value())},
                      {"target", to_json(target.value())},
                      {"roles", roles},
                      {"targetIntegrity", verify ? verify.value() : json::Json(nullptr)},
                      {"targetEvidence", target_evidence},
                      {"allowed", target.value().state == "released" && verify && verify.value()["integrity"] == "pass" &&
                                      target.value().id != current.value().id}};
}

Result<json::Json> Services::rollback(std::string_view twin_id, std::string_view package_id, std::string_view reason,
                                      const Actor& actor) {
    if (reason.empty()) return make_error(ErrorCode::InvalidArgument, "a rollback requires a reason");
    auto v = verify_package(package_id);
    if (!v) return std::move(v).error();
    if (v.value()["integrity"] != "pass") {
        return make_error(ErrorCode::IntegrityError, "the rollback target failed integrity verification");
    }
    auto l = impl_->lock();
    auto d = impl_->twins->deploy(twin_id, package_id, "rollback", reason, actor.name);
    if (!d) return std::move(d).error();
    impl_->record("deployment.rollback", "success", std::string(twin_id),
                  {{"deploymentId", d.value().id}, {"packageId", std::string(package_id)},
                   {"previousPackageId", d.value().previous_package_id ? json::Json(*d.value().previous_package_id) : json::Json(nullptr)},
                   {"reason", std::string(reason)}},
                  actor, "deployment");
    return to_json(d.value());
}

Result<json::Json> Services::bootstrap_twin(std::string_view twin_id, const std::vector<Binding>& bindings,
                                            const Actor& actor) {
    {
        auto l = impl_->lock();
        auto dep = impl_->twins->current_deployment(twin_id);
        if (!dep) return std::move(dep).error();
        if (dep.value()) {
            return make_error(ErrorCode::StateError, "the twin is already deployed; evolve it through a change workspace");
        }
        for (const auto& b : bindings) {
            auto v = impl_->artifacts->version(b.ref);
            if (!v) return std::move(v).error();
            if (v.value().state != Lifecycle::Published) {
                return make_error(ErrorCode::StateError, "initial deployment requires published versions").with("ref", b.ref.str());
            }
        }
    }
    auto built = build_package(twin_id, bindings, std::nullopt, actor);
    if (!built) return std::move(built).error();
    if (built.value()["outcome"] != "pass" || built.value()["package"].is_null()) {
        return make_error(ErrorCode::ValidationError, "the initial package could not be built: " +
                                                          built.value()["summary"].get<std::string>())
            .with("evidenceId", built.value()["id"].get<std::string>());
    }
    const std::string package_id = built.value()["package"]["id"].get<std::string>();
    {
        auto l = impl_->lock();
        auto rel = impl_->twins->mark_released(package_id);
        if (!rel) return std::move(rel).error();
        impl_->record("package.release", "success", package_id, {{"twinId", std::string(twin_id)}, {"bootstrap", true}},
                      actor, "package");
    }
    auto d = deploy(twin_id, package_id, "initial deployment", actor);
    if (!d) return std::move(d).error();
    return json::Json{{"package", built.value()["package"]}, {"evidence", built.value()}, {"deployment", d.value()}};
}

Result<json::Json> Services::packages(std::string_view twin_id) {
    auto l = impl_->lock();
    auto list = impl_->twins->packages(twin_id);
    if (!list) return std::move(list).error();
    json::Json out = json::Json::array();
    for (const auto& p : list.value()) out.push_back(to_json(p));
    return out;
}

Result<json::Json> Services::package(std::string_view id) {
    auto l = impl_->lock();
    auto p = impl_->twins->package(id);
    if (!p) return std::move(p).error();
    json::Json j = to_json(p.value());
    json::Json bindings = json::Json::array();
    for (const auto& b : p.value().bindings) {
        json::Json bj = to_json(b);
        auto s = impl_->version_summary(b.ref);
        bj["version"] = s ? s.value() : json::Json(nullptr);
        bindings.push_back(bj);
    }
    j["bindings"] = bindings;
    if (p.value().evidence_id) {
        auto e = evidence(*p.value().evidence_id);
        j["buildEvidence"] = e ? e.value() : json::Json(nullptr);
    }
    auto v = verify_package(id);
    j["integrity"] = v ? v.value() : json::Json(nullptr);
    auto deps = impl_->twins->deployments(p.value().twin_id);
    json::Json used = json::Json::array();
    if (deps) {
        for (const auto& d : deps.value()) {
            if (d.package_id == id) used.push_back(to_json(d));
        }
    }
    j["deployments"] = used;
    return j;
}

Result<json::Json> Services::deployments(std::string_view twin_id) {
    auto l = impl_->lock();
    auto list = impl_->twins->deployments(twin_id);
    if (!list) return std::move(list).error();
    auto current = twin_id.empty() ? Result<std::optional<Deployment>>(std::optional<Deployment>{})
                                   : impl_->twins->current_deployment(twin_id);
    json::Json out = json::Json::array();
    std::map<std::string, bool> latest_seen;
    for (const auto& d : list.value()) {
        json::Json j = to_json(d);
        j["current"] = !latest_seen[d.twin_id];
        latest_seen[d.twin_id] = true;
        auto p = impl_->twins->package(d.package_id);
        if (p) {
            json::Json refs = json::Json::object();
            for (const auto& b : p.value().bindings) refs[b.role] = b.ref.str();
            j["artifacts"] = refs;
            j["packageHash"] = p.value().package_hash;
            j["irSha256"] = p.value().ir_sha256;
        }
        out.push_back(j);
    }
    return out;
}

}  // namespace twin::studio
