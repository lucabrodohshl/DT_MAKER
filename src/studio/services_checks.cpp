/**
 * @file services_checks.cpp
 * @brief Services: refinement, alignment, compilation, package build/verification, evidence queries.
 *
 * Every check follows the same discipline:
 *  1. read the exact inputs (versions + hashes) under the database lock;
 *  2. run the authoritative tool outside the lock (Z3 / aligner / compiler / package builder);
 *  3. persist an immutable evidence record bound to those input hashes.
 * A tool that fails to run is recorded with outcome "error" ("check failed"),
 * never as a negative verdict.
 */
#include <random>

#include "ontology_json.hpp"
#include "large_stack.hpp"
#include "services_impl.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/alignment/aligner_identity.hpp"
#include "twin/alignment/alignment.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/core/version.hpp"
#include "twin/ontology/refinement.hpp"
#include "twin/package/builder.hpp"
#include "twin/package/package.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using namespace twin::platform;
namespace onto = twin::ontology;

namespace {

Outcome outcome_of(onto::RefinementVerdict v) {
    switch (v) {
        case onto::RefinementVerdict::ValidRefinement: return Outcome::Pass;
        case onto::RefinementVerdict::NotARefinement: return Outcome::Fail;
        case onto::RefinementVerdict::Unknown: return Outcome::Unknown;
        case onto::RefinementVerdict::CheckFailed: return Outcome::Error;
    }
    return Outcome::Error;
}

json::Json bindings_doc(const std::vector<Binding>& bindings) { return to_json(bindings); }

std::vector<EvidenceInput> inputs_of(const std::vector<Binding>& bindings, std::initializer_list<std::string_view> roles) {
    std::vector<EvidenceInput> out;
    for (const auto& b : bindings) {
        if (roles.size() == 0 || std::find(roles.begin(), roles.end(), b.role) != roles.end()) {
            out.push_back({b.role, b.ref, b.sha256});
        }
    }
    return out;
}

Status require_roles(const std::vector<Binding>& bindings, std::initializer_list<std::string_view> roles) {
    for (const auto r : roles) {
        if (find_binding(bindings, r) == nullptr) {
            return make_error(ErrorCode::InvalidArgument, "missing binding for role").with("role", std::string(r));
        }
    }
    return {};
}

std::string short_id() {
    std::random_device rd;
    return std::to_string(rd()) + std::to_string(rd());
}

}  // namespace

// --- refinement ----------------------------------------------------------------------

Result<json::Json> Services::run_refinement(const PhiRefs& base, const PhiRefs& candidate,
                                            const std::optional<std::string>& change_id, const Actor& actor) {
    RunningCheck running(*impl_, "refinement:" + (change_id ? *change_id : candidate.ontology.str()));
    if (!running.acquired()) return make_error(ErrorCode::StateError, "this refinement check is already running");

    onto::DomainKnowledgeText b;
    onto::DomainKnowledgeText c;
    std::vector<EvidenceInput> inputs;
    {
        auto l = impl_->lock();
        auto load = [&](const ArtifactRef& ref, const std::string& role, std::string& out) -> Status {
            auto v = impl_->artifacts->version(ref);
            if (!v) return std::move(v).error();
            auto text = impl_->artifacts->content(ref);
            if (!text) return std::move(text).error();
            out = std::move(text).value();
            inputs.push_back({role, ref, v.value().content_sha256});
            return {};
        };
        auto load_opt = [&](const std::optional<ArtifactRef>& ref, const std::string& role,
                            std::optional<std::string>& out) -> Status {
            if (!ref) return {};
            std::string text;
            if (auto st = load(*ref, role, text); !st) return st;
            out = std::move(text);
            return {};
        };
        if (auto st = load(base.ontology, "base_ontology", b.ontology); !st) return st.error();
        if (auto st = load(candidate.ontology, "candidate_ontology", c.ontology); !st) return st.error();
        if (auto st = load_opt(base.pt_interpretation, "base_pt_interpretation", b.pt_interpretation); !st) return st.error();
        if (auto st = load_opt(base.dt_interpretation, "base_dt_interpretation", b.dt_interpretation); !st) return st.error();
        // A candidate without its own interpretation keeps the base one (Def. 4 is then checked
        // for that unchanged text under the candidate axioms); record it in the candidate role.
        if (auto st = load_opt(candidate.pt_interpretation ? candidate.pt_interpretation : base.pt_interpretation,
                               "candidate_pt_interpretation", c.pt_interpretation);
            !st) {
            return st.error();
        }
        if (auto st = load_opt(candidate.dt_interpretation ? candidate.dt_interpretation : base.dt_interpretation,
                               "candidate_dt_interpretation", c.dt_interpretation);
            !st) {
            return st.error();
        }
    }
    events_.publish("evidence", {{"kind", "refinement"}, {"state", "check_running"}, {"subject", candidate.ontology.str()}},
                    iso8601_utc(clock_->now_ms()));

    const auto report = run_with_large_stack([&] { return onto::check_refinement(b, c, onto::SolverConfig{config_.solver_timeout_ms}); });

    auto l = impl_->lock();
    json::Json doc = to_json(report);
    doc["base"] = {{"ontology", base.ontology.str()},
                   {"ptInterpretation", base.pt_interpretation ? json::Json(base.pt_interpretation->str()) : json::Json(nullptr)},
                   {"dtInterpretation", base.dt_interpretation ? json::Json(base.dt_interpretation->str()) : json::Json(nullptr)}};
    doc["candidate"] = {{"ontology", candidate.ontology.str()},
                        {"ptInterpretation", candidate.pt_interpretation ? json::Json(candidate.pt_interpretation->str())
                                                                         : doc["base"]["ptInterpretation"]},
                        {"dtInterpretation", candidate.dt_interpretation ? json::Json(candidate.dt_interpretation->str())
                                                                         : doc["base"]["dtInterpretation"]}};
    auto ev = impl_->evidence->record(EvidenceKind::Refinement, outcome_of(report.verdict), onto::to_string(report.verdict),
                                      report.summary, report.checker, doc, inputs, actor.name, change_id);
    if (!ev) return std::move(ev).error();
    impl_->record("refinement.run", std::string(to_string(outcome_of(report.verdict))), candidate.ontology.str(),
                  {{"evidenceId", ev.value().id},
                   {"base", base.ontology.str()},
                   {"verdict", onto::to_string(report.verdict)},
                   {"changeId", change_id ? json::Json(*change_id) : json::Json(nullptr)}},
                  actor, "evidence");
    json::Json j = to_json(ev.value());
    j["document"] = doc;
    return j;
}

// --- alignment -------------------------------------------------------------------------

Result<json::Json> Services::run_alignment(const std::vector<Binding>& bindings,
                                           const std::optional<std::string>& change_id, const Actor& actor) {
    if (auto st = require_roles(bindings, {"pt_model", "dt_model", "ontology", "pt_interpretation", "dt_interpretation"});
        !st) {
        return st.error();
    }
    const Binding* dt = find_binding(bindings, "dt_model");
    RunningCheck running(*impl_, "alignment:" + (change_id ? *change_id : dt->ref.str()));
    if (!running.acquired()) return make_error(ErrorCode::StateError, "this alignment check is already running");
    alignment::AlignmentInputs in;
    {
        auto l = impl_->lock();
        auto path = [&](std::string_view role) { return impl_->materialize(*find_binding(bindings, role)); };
        auto pt = path("pt_model");
        auto dtp = path("dt_model");
        auto k = path("ontology");
        auto ip = path("pt_interpretation");
        auto id = path("dt_interpretation");
        for (const auto* r : {&pt, &dtp, &k, &ip, &id}) {
            if (!*r) return r->error();
        }
        in = {pt.value(), dtp.value(), k.value(), ip.value(), id.value(), config_.legacy_system_declaration};
    }
    events_.publish("evidence", {{"kind", "alignment"}, {"state", "check_running"}, {"subject", dt->ref.str()}},
                    iso8601_utc(clock_->now_ms()));

    const auto result = run_with_large_stack([&] { return alignment::check_alignment(in); });

    auto l = impl_->lock();
    Outcome outcome = Outcome::Error;
    std::string verdict = "check_failed";
    std::string summary;
    json::Json doc;
    if (result) {
        const auto& e = result.value();
        doc = alignment::to_json(e);
        outcome = e.aligned && e.lint_clean ? Outcome::Pass : Outcome::Fail;
        verdict = e.aligned ? (e.lint_clean ? "aligned" : "aligned_with_lint_errors") : "not_aligned";
        summary = e.aligned ? "V_P and V_D are semantically aligned under Φ (Definition 5)" +
                                  std::string(e.lint_clean ? "." : ", but the lint found constructs the aligner skips.")
                            : "Not aligned: no matching DT behaviour for PT label '" + e.counterexample_pt + "'.";
    } else {
        doc = {{"format", "twin-alignment-evidence/1"}, {"error", result.error().to_string()}};
        summary = "The aligner could not be run: " + result.error().message;
    }
    doc["bindings"] = bindings_doc(bindings);
    auto ev = impl_->evidence->record(EvidenceKind::Alignment, outcome, verdict, summary,
                                      std::string(alignment::kAlignerName) + " " +
                                          std::string(alignment::kAlignerSourceDigest).substr(0, 16),
                                      doc, inputs_of(bindings, {}), actor.name, change_id);
    if (!ev) return std::move(ev).error();
    impl_->record("alignment.run", std::string(to_string(outcome)), dt->ref.str(),
                  {{"evidenceId", ev.value().id}, {"verdict", verdict},
                   {"changeId", change_id ? json::Json(*change_id) : json::Json(nullptr)}},
                  actor, "evidence");
    json::Json j = to_json(ev.value());
    j["document"] = doc;
    return j;
}

// --- compilation -----------------------------------------------------------------------

Result<json::Json> Services::run_compile(std::string_view twin_id, const std::vector<Binding>& bindings,
                                         const std::optional<std::string>& change_id, const Actor& actor) {
    BuildTarget target;
    {
        auto l = impl_->lock();
        auto t = impl_->twins->twin(twin_id);
        if (!t) return std::move(t).error();
        target.owner = t.value().id;
        target.model_id = t.value().model_id;
        target.ticks_per_unit = t.value().ticks_per_unit;
    }
    return run_compile_for(target, bindings, change_id, actor);
}

Result<json::Json> Services::run_compile_for(const BuildTarget& target, const std::vector<Binding>& bindings,
                                             const std::optional<std::string>& change_id, const Actor& actor) {
    if (auto st = require_roles(bindings, {"dt_model", "dt_interpretation"}); !st) return st.error();
    std::filesystem::path model;
    std::filesystem::path interp;
    {
        auto l = impl_->lock();
        auto m = impl_->materialize(*find_binding(bindings, "dt_model"));
        auto i = impl_->materialize(*find_binding(bindings, "dt_interpretation"));
        if (!m) return std::move(m).error();
        if (!i) return std::move(i).error();
        model = m.value();
        interp = i.value();
    }
    compiler::CompileOptions options;
    options.model_id = target.model_id;
    options.ticks_per_unit = target.ticks_per_unit;
    options.interpretation = interp;
    options.legacy_system_declaration = config_.legacy_system_declaration;
    const auto result = run_with_large_stack([&] { return compiler::compile_file(model, options); });

    auto l = impl_->lock();
    json::Json doc;
    Outcome outcome = Outcome::Fail;
    std::string summary;
    json::Json diags = json::Json::array();
    auto add = [&](const std::vector<compiler::Diagnostic>& ds) {
        for (const auto& d : ds) {
            diags.push_back({{"code", d.code}, {"severity", compiler::to_string(d.severity)}, {"message", d.message},
                             {"where", d.where}, {"hint", d.hint}});
        }
    };
    if (const auto* ok = std::get_if<compiler::CompileResult>(&result)) {
        doc = compiler::to_json(ok->manifest);
        doc["source_path"] = find_binding(bindings, "dt_model")->ref.str();  // stable, not a work-dir path
        outcome = ok->manifest.translation_validation.passed ? Outcome::Pass : Outcome::Fail;
        summary = "Compiled to Twin IR " + ok->manifest.ir_sha256.substr(0, 12) + "… (translation validation " +
                  (ok->manifest.translation_validation.passed ? "passed" : "FAILED") + ").";
    } else {
        add(std::get<compiler::CompileFailure>(result).diagnostics);
        doc = {{"format", "twin-compilation-failure/1"}, {"diagnostics", diags}};
        summary = "Compilation failed with " + std::to_string(diags.size()) + " diagnostic(s).";
    }
    doc["bindings"] = bindings_doc(bindings);
    auto ev = impl_->evidence->record(EvidenceKind::Compilation, outcome, outcome == Outcome::Pass ? "compiled" : "failed",
                                      summary, "twin-compiler " + std::string(twin::version::kCompiler), doc,
                                      inputs_of(bindings, {"dt_model", "dt_interpretation"}), actor.name, change_id);
    if (!ev) return std::move(ev).error();
    impl_->record("model.compile", std::string(to_string(outcome)), find_binding(bindings, "dt_model")->ref.str(),
                  {{"evidenceId", ev.value().id}}, actor, "evidence");
    json::Json j = to_json(ev.value());
    j["document"] = doc;
    return j;
}

// --- packages --------------------------------------------------------------------------

Result<json::Json> Services::build_package(std::string_view twin_id, const std::vector<Binding>& bindings,
                                           const std::optional<std::string>& change_id, const Actor& actor) {
    BuildTarget target;
    {
        auto l = impl_->lock();
        auto t = impl_->twins->twin(twin_id);
        if (!t) return std::move(t).error();
        target.owner = t.value().id;
        target.model_id = t.value().model_id;
        target.ticks_per_unit = t.value().ticks_per_unit;
    }
    return build_package_for(target, bindings, change_id, actor);
}

Result<json::Json> Services::build_package_for(const BuildTarget& target, const std::vector<Binding>& bindings,
                                               const std::optional<std::string>& change_id, const Actor& actor) {
    if (auto st = require_roles(bindings, {"pt_model", "dt_model", "ontology", "pt_interpretation", "dt_interpretation"});
        !st) {
        return st.error();
    }
    const std::string twin_id = target.owner;
    RunningCheck running(*impl_, "package:" + (change_id ? *change_id : twin_id));
    if (!running.acquired()) return make_error(ErrorCode::StateError, "a package build is already running");
    package::BuildInputs in;
    std::size_t existing = 0;
    {
        auto l = impl_->lock();
        auto pk = impl_->twins->packages(twin_id);
        if (!pk) return std::move(pk).error();
        existing = pk.value().size();
        auto path = [&](std::string_view role) { return impl_->materialize(*find_binding(bindings, role)); };
        auto pt = path("pt_model");
        auto dt = path("dt_model");
        auto k = path("ontology");
        auto ip = path("pt_interpretation");
        auto id = path("dt_interpretation");
        for (const auto* r : {&pt, &dt, &k, &ip, &id}) {
            if (!*r) return r->error();
        }
        in.pt_model = pt.value();
        in.dt_model = dt.value();
        in.ontology = k.value();
        in.pt_interpretation = ip.value();
        in.dt_interpretation = id.value();
        if (target.source_models) {
            // Canonical models travel with the package; the builder checks each renders to the shipped view.
            for (const char* role : {"pt_model", "dt_model"}) {
                const Binding* b = find_binding(bindings, role);
                auto content = impl_->artifacts->content(b->ref);
                if (!content || authoring::content_format(content.value()) != "twin-ta/1") continue;
                const std::filesystem::path p = config_.data_dir / "work" / (b->sha256 + ".tta.json");
                if (!authoring::write_file_atomically(p, content.value())) {
                    return make_error(ErrorCode::IoError, "cannot write work file").with("file", p.string());
                }
                (std::string_view(role) == "pt_model" ? in.pt_source_model : in.dt_source_model) = p;
            }
        }
    }
    in.model_id = target.model_id;
    in.model_version = target.model_version.value_or("1." + std::to_string(existing) + ".0");
    in.ticks_per_unit = target.ticks_per_unit;
    in.monitors = target.monitors;
    in.type_metadata = target.type_metadata;
    in.legacy_system_declaration = config_.legacy_system_declaration;
    const fs::path staging = config_.data_dir / "packages" / ("staging-" + short_id());

    const auto built = run_with_large_stack([&] { return package::build_package(in, staging); });

    auto l = impl_->lock();
    if (!built) {
        std::error_code ec;
        fs::remove_all(staging, ec);
        const bool refused = built.error().code == ErrorCode::ValidationError ||
                             built.error().code == ErrorCode::UnsupportedConstruct ||
                             built.error().code == ErrorCode::IntegrityError;
        json::Json context = json::Json::object();
        for (const auto& [k, v] : built.error().context) context[k] = v;
        json::Json doc = {{"format", "twin-package-build/1"},
                          {"built", false},
                          {"error", built.error().message},
                          {"errorCode", std::string(twin::to_string(built.error().code))},
                          {"context", context},
                          {"bindings", bindings_doc(bindings)}};
        auto ev = impl_->evidence->record(EvidenceKind::Package, refused ? Outcome::Fail : Outcome::Error,
                                          refused ? "refused" : "check_failed",
                                          "Package not built: " + built.error().message,
                                          "twin-package-builder " + std::string(twin::version::kPackageFormat), doc,
                                          inputs_of(bindings, {}), actor.name, change_id);
        if (!ev) return std::move(ev).error();
        impl_->record("package.build", "fail", std::string(twin_id), {{"evidenceId", ev.value().id}}, actor, "evidence");
        json::Json j = to_json(ev.value());
        j["document"] = doc;
        j["package"] = nullptr;
        return j;
    }
    const auto& loaded = built.value().package;
    json::Json checks = json::Json::array();
    bool all = true;
    for (const auto& c : loaded.checks) {
        checks.push_back({{"name", c.name}, {"passed", c.passed}, {"detail", c.detail}});
        all = all && c.passed;
    }
    PackageRecord rec;
    rec.twin_id = twin_id;
    rec.package_hash = loaded.package_hash;
    rec.ir_sha256 = loaded.ir_sha256;
    rec.model_version = in.model_version;
    rec.bindings = bindings;
    rec.created_by = actor.name;
    rec.change_id = change_id;
    rec.directory = staging.string();
    auto stored = impl_->twins->add_package(rec);
    if (!stored) return std::move(stored).error();
    // Move the package directory to its permanent, id-named location (immutable from now on).
    const fs::path final_dir = config_.data_dir / "packages" / stored.value().id;
    std::error_code ec;
    fs::rename(staging, final_dir, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot finalise package directory: " + ec.message());
    if (auto st = impl_->db->prepare("UPDATE packages SET directory = ?2 WHERE id = ?1"); st) {
        st.value().bind(1, stored.value().id).bind(2, fs::absolute(final_dir).string());
        if (auto r = st.value().run(); !r) return r.error();
    } else {
        return st.error();
    }
    json::Json doc = {{"format", "twin-package-build/1"},
                      {"built", true},
                      {"packageId", stored.value().id},
                      {"packageHash", loaded.package_hash},
                      {"irSha256", loaded.ir_sha256},
                      {"manifest", package::to_json(loaded.manifest)},
                      {"checks", checks},
                      {"bindings", bindings_doc(bindings)}};
    auto ev = impl_->evidence->record(EvidenceKind::Package, all ? Outcome::Pass : Outcome::Fail,
                                      all ? "built_and_verified" : "verification_failed",
                                      "Package " + stored.value().id + " built (" + std::to_string(loaded.checks.size()) +
                                          " integrity checks " + (all ? "passed" : "with failures") + ").",
                                      "twin-package-builder " + std::string(twin::version::kPackageFormat), doc,
                                      inputs_of(bindings, {}), actor.name, change_id);
    if (!ev) return std::move(ev).error();
    if (auto st = impl_->db->prepare("UPDATE packages SET evidence_id = ?2 WHERE id = ?1"); st) {
        st.value().bind(1, stored.value().id).bind(2, ev.value().id);
        if (auto r = st.value().run(); !r) return r.error();
    }
    impl_->record("package.build", "success", stored.value().id,
                  {{"evidenceId", ev.value().id}, {"packageHash", loaded.package_hash}, {"irSha256", loaded.ir_sha256},
                   {"changeId", change_id ? json::Json(*change_id) : json::Json(nullptr)}},
                  actor, "package");
    json::Json j = to_json(ev.value());
    j["document"] = doc;
    auto final_rec = impl_->twins->package(stored.value().id);
    j["package"] = final_rec ? to_json(final_rec.value()) : json::Json(nullptr);
    return j;
}

Result<json::Json> Services::verify_package(std::string_view package_id) {
    PackageRecord rec;
    {
        auto l = impl_->lock();
        auto p = impl_->twins->package(package_id);
        if (!p) return std::move(p).error();
        rec = p.value();
    }
    const auto checks = run_with_large_stack([&] { return package::verify_report(rec.directory); });
    json::Json list = json::Json::array();
    bool all = !checks.empty();
    std::string failed;
    for (const auto& c : checks) {
        list.push_back({{"name", c.name}, {"passed", c.passed}, {"detail", c.detail}});
        if (!c.passed && failed.empty()) failed = c.name + ": " + c.detail;
        all = all && c.passed;
    }
    // The manifest must still be the one recorded when the package was built.
    if (all) {
        auto loaded = run_with_large_stack([&] { return package::load_and_verify(rec.directory); });
        if (!loaded || loaded.value().package_hash != rec.package_hash) {
            all = false;
            failed = "package hash differs from the hash recorded at build time";
        }
    }
    return json::Json{{"packageId", rec.id},
                      {"packageHash", rec.package_hash},
                      {"integrity", all ? "pass" : "fail"},
                      {"firstFailure", failed},
                      {"checks", list},
                      {"verifiedAt", iso8601_utc(clock_->now_ms())},
                      {"verifier", "twin::package::verify_report (" + std::string(twin::version::kPackageFormat) + ")"}};
}

// --- evidence queries ---------------------------------------------------------------------

Result<json::Json> Services::evidence(std::string_view id) {
    auto l = impl_->lock();
    auto r = impl_->evidence->get(id);
    if (!r) return std::move(r).error();
    auto doc = impl_->evidence->document(r.value());
    json::Json j = to_json(r.value());
    if (doc) {
        j["document"] = doc.value();
        j["documentIntegrity"] = "pass";
    } else {
        j["document"] = nullptr;
        j["documentIntegrity"] = doc.error().code == ErrorCode::IntegrityError ? "fail" : "unknown";
        j["documentError"] = doc.error().message;
    }
    return j;
}

Result<json::Json> Services::evidence_list(const EvidenceFilter& filter) {
    auto l = impl_->lock();
    auto list = impl_->evidence->list(filter);
    if (!list) return std::move(list).error();
    auto total = impl_->evidence->count(filter);
    if (!total) return std::move(total).error();
    return json::Json{{"items", impl_->evidence_json(list.value())}, {"total", total.value()},
                      {"limit", filter.limit}, {"offset", filter.offset}};
}

Result<json::Json> Services::evidence_status(std::string_view evidence_id, const std::optional<std::string>& twin_id,
                                             const std::optional<std::string>& change_id) {
    auto l = impl_->lock();
    auto r = impl_->evidence->get(evidence_id);
    if (!r) return std::move(r).error();
    std::vector<Binding> context;
    std::string context_name;
    if (change_id) {
        auto b = candidate_bindings(*change_id);
        if (!b) return std::move(b).error();
        context = std::move(b).value();
        context_name = "change " + *change_id;
    } else if (twin_id) {
        auto b = deployed_bindings(*twin_id);
        if (!b) return std::move(b).error();
        context = std::move(b).value();
        context_name = "deployment of " + *twin_id;
    } else {
        return make_error(ErrorCode::InvalidArgument, "give a twin or a change as context");
    }
    // Refinement evidence is about a *candidate*: compare its candidate roles with the context.
    EvidenceRecord view = r.value();
    if (view.kind == EvidenceKind::Refinement) {
        std::vector<EvidenceInput> mapped;
        for (const auto& in : view.inputs) {
            if (in.role.rfind("candidate_", 0) == 0) mapped.push_back({in.role.substr(10), in.ref, in.sha256});
        }
        view.inputs = mapped;
    }
    auto a = applicability(view, context, *impl_->artifacts);
    if (!a) return std::move(a).error();
    json::Json j = to_json(a.value());
    j["evidenceId"] = r.value().id;
    j["context"] = context_name;
    j["evaluatedAt"] = iso8601_utc(clock_->now_ms());
    return j;
}

}  // namespace twin::studio
