/**
 * @file blueprints_core.cpp
 * @brief BlueprintService: catalogue, creation, versions, drafts and section saves (see blueprints.hpp).
 */
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>

#include "blueprints_impl.hpp"
#include "twin/authoring/import.hpp"
#include "twin/authoring/model.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/core/large_stack.hpp"
#include "twin/monitoring/monitors.hpp"
#include "twin/scene/world.hpp"
#include "twin/studio/supervisor.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using json::Json;
using namespace twin::platform;

// ------------------------------------------------------------------------------- helpers

Result<Json> resolve_includes(Json document, const fs::path& base) {
    if (!document.is_object()) return document;
    for (auto& [key, value] : document.items()) {
        if (value.is_object() && value.size() == 1 && value.contains("$file") && value.at("$file").is_string()) {
            const std::string rel = value.at("$file").get<std::string>();
            if (rel.find("..") != std::string::npos || fs::path(rel).is_absolute()) {
                return make_error(ErrorCode::InvalidArgument, "an include must name a file next to the document").with("file", rel);
            }
            auto text = read_text_file(base / rel);
            if (!text) return std::move(text).error();
            auto j = json::parse(text.value());
            if (!j) return std::move(j).error().with("file", rel);
            value = std::move(j).value();
        }
    }
    return document;
}

Result<std::string> read_text_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return make_error(ErrorCode::IoError, "cannot read " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ArtifactKind kind_of_role(std::string_view role) {
    if (role == "ontology") return ArtifactKind::Ontology;
    if (role == "pt_model") return ArtifactKind::PtModel;
    if (role == "dt_model") return ArtifactKind::DtModel;
    return ArtifactKind::Interpretation;
}

Result<std::string> model_role(std::string_view short_role) {
    if (short_role == "pt" || short_role == "pt_model") return std::string("pt_model");
    if (short_role == "dt" || short_role == "dt_model") return std::string("dt_model");
    return make_error(ErrorCode::InvalidArgument, "the role is 'pt' (Physical System View) or 'dt' (Digital Twin View)")
        .with("role", std::string(short_role));
}

json::Json to_json(const std::vector<SectionFinding>& findings) {
    Json out = Json::array();
    for (const SectionFinding& f : findings) {
        out.push_back(Json{{"section", f.section}, {"severity", f.severity}, {"code", f.code}, {"message", f.message},
                           {"target", f.target}, {"path", f.path}});
    }
    return out;
}

Json blank_document(std::string_view id, std::string_view name, std::string_view domain) {
    return Json{
        {"format", std::string(kBlueprintFormat)},
        {"identity",
         {{"name", std::string(name)},
          {"description", ""},
          {"domain", std::string(domain)},
          {"icon", "boxes"},
          {"tags", Json::array()},
          {"modelId", std::string(id)},
          {"timeUnit", "s"},
          {"ticksPerUnit", 1000},
          {"runtimeMode", "monitor"},
          {"plugin", nullptr}}},
        {"structure", {{"root", nullptr}, {"assetTypes", Json::array()}, {"assets", Json::array()}, {"relationships", Json::array()}}},
        {"world", scene::empty_world(20000, 15000)},
        {"data", {{"properties", Json::array()}, {"telemetry", Json::array()}, {"events", Json::array()}, {"commands", Json::array()}}},
        {"connectivity", {{"sources", Json::array()}, {"bindings", Json::array()}}},
        {"presentation",
         {{"displayName", std::string(name)},
          {"icon", "boxes"},
          {"primaryView", "status"},
          {"plugin", nullptr},
          {"keyTelemetry", Json::array()},
          {"importantAssets", Json::array()},
          {"importantPropositions", Json::array()},
          {"importantMonitors", Json::array()},
          {"importantPredictions", Json::array()},
          {"states", Json::object()},
          {"events", Json::object()},
          {"charts", Json::array()}}},
        {"behavior", {{"pt", {{"layout", nullptr}}}, {"dt", {{"layout", nullptr}}}}},
        {"assurance", {{"format", std::string(monitoring::kMonitorsFormat)}, {"requirements", Json::array()},
                       {"monitors", Json::array()}, {"alerts", Json::array()}}},
        {"simulation", {{"kind", "none"}}},
        {"scenarios", Json::array()}};
}

namespace {

std::string slugify(std::string_view text) {
    std::string out;
    bool dash = false;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0) {
            out += static_cast<char>(std::tolower(c));
            dash = false;
        } else if (!out.empty() && !dash) {
            out += '-';
            dash = true;
        }
        if (out.size() >= 48) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("blueprint") : out;
}

std::string role_suffix(std::string_view role) {
    if (role == "pt_model") return "pt-view";
    if (role == "dt_model") return "dt-view";
    if (role == "ontology") return "ontology";
    if (role == "pt_interpretation") return "pt-interpretation";
    return "dt-interpretation";
}

std::string role_title(std::string_view role) {
    if (role == "pt_model") return "Physical System View (V_P)";
    if (role == "dt_model") return "Digital Twin View (V_D)";
    if (role == "ontology") return "Ontology";
    if (role == "pt_interpretation") return "PT interpretation (I_P)";
    return "DT interpretation (I_D)";
}

/// Initial content of a new artefact for a role (an empty but well-typed document).
std::string initial_content(std::string_view role, std::string_view name) {
    if (role == "pt_model" || role == "dt_model") {
        std::string automaton;
        for (const char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) != 0) automaton += c;
        }
        if (automaton.empty() || std::isdigit(static_cast<unsigned char>(automaton.front())) != 0) automaton = "Twin" + automaton;
        automaton += role == "pt_model" ? "PT" : "DT";
        authoring::Model m;
        m.name = automaton;
        return json::canonical_dump(authoring::to_json(m)).value();
    }
    if (role == "ontology") return "; Ontology of " + std::string(name) + " (sorts, functions, relations, axioms)\n";
    return "; Interpretation of " + std::string(name) + " (one line per location and event)\n";
}

/// A free artefact id derived from @p base.
std::string free_artifact_id(ArtifactRepository& artifacts, const std::string& base) {
    std::string id = base;
    for (int n = 2; artifacts.get(id); ++n) id = base + "-" + std::to_string(n);
    return id;
}

/// Ordered list of the roles in the order they must be created (ontology before interpretations).
const std::vector<std::string>& creation_order() {
    static const std::vector<std::string> kOrder = {"ontology", "pt_model", "dt_model", "pt_interpretation", "dt_interpretation"};
    return kOrder;
}

}  // namespace

// ----------------------------------------------------------------------------------- Impl

BlueprintService::Impl::Impl(BlueprintService& s, Services& svc, fs::path templates)
    : self(s), services(svc), core(svc.internals()), templates_dir(std::move(templates)),
      repo(std::make_unique<BlueprintRepository>(*core.db, svc.clock())) {}

Result<BlueprintVersion> BlueprintService::Impl::load(std::string_view id, std::int64_t version) {
    auto l = core.lock();
    return repo->version(id, version);
}

Result<BlueprintVersion> BlueprintService::Impl::load_draft(std::string_view id, std::int64_t version,
                                                            std::optional<std::int64_t> revision) {
    auto v = load(id, version);
    if (!v) return std::move(v).error();
    if (v.value().state != "draft") {
        return make_error(ErrorCode::StateError,
                          "v" + std::to_string(version) + " is " + v.value().state +
                              " and immutable. Create a draft from this version to change it.")
            .with("version", std::to_string(version));
    }
    if (revision && *revision != v.value().revision) {
        return make_error(ErrorCode::StateError, "this draft was changed since you loaded it (revision " +
                                                     std::to_string(v.value().revision) + ", you had " + std::to_string(*revision) +
                                                     "); reload to see the latest version")
            .with("expectedRevision", std::to_string(*revision))
            .with("currentRevision", std::to_string(v.value().revision))
            .with("updatedBy", v.value().updated_by);
    }
    return v;
}

Result<BlueprintVersion> BlueprintService::Impl::store(const BlueprintVersion& v, const Actor& actor) {
    auto l = core.lock();
    return repo->save(v.blueprint_id, v.version, v.revision, v.document, v.pins, actor.name);
}

std::string BlueprintService::Impl::context(const BlueprintVersion& v) {
    return "blueprint:" + v.blueprint_id + "@" + std::to_string(v.version);
}

EvidenceInput BlueprintService::Impl::document_input(const BlueprintVersion& v) {
    return EvidenceInput{"blueprint_document", ArtifactRef{"blueprint:" + v.blueprint_id, v.version}, v.document_sha256};
}

Result<fs::path> BlueprintService::Impl::template_dir(std::string_view template_id) const {
    if (template_id.empty() || template_id.find('/') != std::string_view::npos || template_id.find("..") != std::string_view::npos) {
        return make_error(ErrorCode::InvalidArgument, "invalid template id");
    }
    const fs::path dir = templates_dir / std::string(template_id);
    std::error_code ec;
    if (!fs::is_regular_file(dir / "template.json", ec)) {
        return make_error(ErrorCode::NotFound, "no such template").with("template", std::string(template_id));
    }
    return dir;
}

Result<ArtifactRef> BlueprintService::Impl::ensure_editable(BlueprintVersion& v, std::string_view role, const Actor& actor) {
    const std::string r(role);
    auto it = v.pins.find(r);
    if (it == v.pins.end()) {
        // No artefact yet: create one (version 1, DRAFT) owned by this Blueprint.
        std::string id;
        Json refs = Json::object();
        {
            auto l = core.lock();
            id = free_artifact_id(*core.artifacts, v.blueprint_id + "-" + role_suffix(r));
        }
        if (kind_of_role(r) == ArtifactKind::Interpretation) {
            auto o = v.pins.find("ontology");
            if (o == v.pins.end()) {
                auto ont = ensure_editable(v, "ontology", actor);
                if (!ont) return std::move(ont).error();
                o = v.pins.find("ontology");
            }
            refs["ontology"] = o->second;
            refs["twinRole"] = r == "pt_interpretation" ? "pt" : "dt";
        }
        const std::string name = v.document.value("identity", Json::object()).value("name", v.blueprint_id);
        auto created = services.create_artifact(kind_of_role(r), id, name + " — " + role_title(r),
                                                "Created in Blueprint " + v.blueprint_id, initial_content(r, name), refs, actor);
        if (!created) return std::move(created).error();
        const ArtifactRef ref{id, 1};
        v.pins[r] = ref.str();
        return ref;
    }
    auto ref = parse_ref(it->second);
    if (!ref) return std::move(ref).error();
    ArtifactVersion current;
    {
        auto l = core.lock();
        auto cv = core.artifacts->version(ref.value());
        if (!cv) return std::move(cv).error();
        current = cv.value();
    }
    if (current.is_open()) return ref;
    // Pinned version is immutable: derive a draft from it (its lineage continues).
    std::optional<ArtifactVersion> open;
    {
        auto l = core.lock();
        auto ov = core.artifacts->open_version(ref.value().artifact_id);
        if (!ov) return std::move(ov).error();
        open = ov.value();
    }
    if (open) {
        return make_error(ErrorCode::StateError,
                          "artefact " + ref.value().artifact_id + " already has an open version (" + open->ref().str() +
                              ") used elsewhere; finish or reject it, or pin it in this Blueprint")
            .with("open", open->ref().str());
    }
    auto draft = services.create_draft(ref.value(), "Edited in Blueprint " + v.blueprint_id + " v" + std::to_string(v.version),
                                       std::nullopt, actor);
    if (!draft) return std::move(draft).error();
    const ArtifactRef next{ref.value().artifact_id, draft.value().value("version", std::int64_t{0})};
    v.pins[r] = next.str();
    return next;
}

std::vector<Binding> BlueprintService::Impl::pinned_bindings(const BlueprintVersion& v) {
    std::vector<Binding> out;
    auto l = core.lock();
    for (const auto role : kBlueprintRoles) {
        auto it = v.pins.find(std::string(role));
        if (it == v.pins.end()) continue;
        auto ref = parse_ref(it->second);
        if (!ref) continue;
        auto b = core.bind(std::string(role), ref.value());
        if (b) out.push_back(b.value());
    }
    return out;
}

Result<std::vector<Binding>> BlueprintService::Impl::bindings(const BlueprintVersion& v) {
    std::vector<Binding> out = pinned_bindings(v);
    std::string missing;
    for (const auto role : kBlueprintRoles) {
        if (!std::any_of(out.begin(), out.end(), [&](const Binding& b) { return b.role == role; })) {
            missing += (missing.empty() ? "" : ", ") + std::string(role);
        }
    }
    if (!missing.empty()) {
        return make_error(ErrorCode::InvalidArgument, "the Blueprint version does not define: " + missing).with("missing", missing);
    }
    return out;
}

std::optional<Json> BlueprintService::Impl::alignment_for(const BlueprintVersion& v) {
    auto b = bindings(v);
    if (!b) return std::nullopt;
    std::vector<EvidenceInput> in;
    for (const Binding& x : b.value()) in.push_back(EvidenceInput{x.role, x.ref, x.sha256});
    auto l = core.lock();
    auto e = core.evidence->latest_for(EvidenceKind::Alignment, in);
    if (!e || !e.value()) return std::nullopt;
    auto doc = core.evidence->document(*e.value());
    if (!doc) return std::nullopt;
    Json j = doc.value();
    j["evidenceId"] = e.value()->id;
    j["outcome"] = std::string(to_string(e.value()->outcome));
    return j;
}

// ------------------------------------------------------------------------- the service

BlueprintService::BlueprintService(Services& services, fs::path templates_dir)
    : services_(services), impl_(std::make_unique<Impl>(*this, services, std::move(templates_dir))) {}

BlueprintService::~BlueprintService() = default;

Result<Json> BlueprintService::templates() {
    Json out = Json::array();
    std::error_code ec;
    if (!fs::is_directory(impl_->templates_dir, ec)) return out;
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(impl_->templates_dir, ec)) {
        if (e.is_directory() && fs::is_regular_file(e.path() / "template.json")) dirs.push_back(e.path());
    }
    std::sort(dirs.begin(), dirs.end());
    for (const fs::path& d : dirs) {
        auto text = read_text_file(d / "template.json");
        if (!text) continue;
        auto t = json::parse(text.value());
        if (!t) continue;
        out.push_back(Json{{"id", d.filename().string()},
                           {"name", t.value().value("name", d.filename().string())},
                           {"description", t.value().value("description", std::string())},
                           {"domain", t.value().value("domain", std::string("generic"))},
                           {"icon", t.value().value("icon", std::string("boxes"))},
                           {"order", t.value().value("order", std::int64_t{50})},
                           {"includes", t.value().value("includes", Json::array())}});
    }
    std::stable_sort(out.begin(), out.end(), [](const Json& a, const Json& b) { return a["order"].get<std::int64_t>() < b["order"].get<std::int64_t>(); });
    return out;
}

Result<Json> BlueprintService::palettes() {
    auto text = read_text_file(impl_->templates_dir / "palettes.json");
    if (!text) return Json{{"format", "twin-palettes/1"}, {"palettes", Json::object()}};
    return json::parse(text.value());
}

Result<Json> BlueprintService::list() {
    Json out = Json::array();
    std::vector<Blueprint> all;
    {
        auto l = impl_->core.lock();
        auto bs = impl_->repo->blueprints();
        if (!bs) return std::move(bs).error();
        all = std::move(bs).value();
    }
    auto twins = [&]() -> std::vector<Twin> {
        auto l = impl_->core.lock();
        auto t = impl_->core.twins->twins();
        return t ? t.value() : std::vector<Twin>{};
    }();
    for (const Blueprint& b : all) {
        std::vector<BlueprintVersion> versions;
        {
            auto l = impl_->core.lock();
            auto vs = impl_->repo->versions(b.id);
            if (!vs) return std::move(vs).error();
            versions = std::move(vs).value();
        }
        Json j = to_json(b);
        Json vs = Json::array();
        std::optional<BlueprintVersion> draft;
        std::optional<BlueprintVersion> published;
        for (const BlueprintVersion& v : versions) {
            vs.push_back(version_summary(v));
            if (v.state == "draft" && !draft) draft = v;
            if (v.state == "published" && !published) published = v;
        }
        j["versions"] = vs;
        j["draft"] = draft ? version_summary(*draft) : Json(nullptr);
        j["published"] = published ? version_summary(*published) : Json(nullptr);
        j["latest"] = versions.empty() ? Json(nullptr) : version_summary(versions.front());
        j["updatedAt"] = versions.empty() ? b.created_at : versions.front().updated_at;
        std::int64_t instances = 0;
        for (const Twin& t : twins) instances += (t.blueprint_id && *t.blueprint_id == b.id) ? 1 : 0;
        j["instanceCount"] = instances;
        // Cheap readiness hint for the library (the full gate is GET .../status).
        if (draft) {
            const auto findings = impl_->validate_sections(*draft);
            j["draftErrors"] = std::count_if(findings.begin(), findings.end(), [](const SectionFinding& f) { return f.severity == "error"; });
            j["draftWarnings"] = std::count_if(findings.begin(), findings.end(), [](const SectionFinding& f) { return f.severity == "warning"; });
        }
        out.push_back(j);
    }
    return out;
}

Result<Json> BlueprintService::get(std::string_view id) {
    Blueprint b;
    std::vector<BlueprintVersion> versions;
    {
        auto l = impl_->core.lock();
        auto bp = impl_->repo->blueprint(id);
        if (!bp) return std::move(bp).error();
        b = bp.value();
        auto vs = impl_->repo->versions(id);
        if (!vs) return std::move(vs).error();
        versions = std::move(vs).value();
    }
    Json j = to_json(b);
    Json vs = Json::array();
    for (const BlueprintVersion& v : versions) vs.push_back(version_summary(v));
    j["versions"] = vs;
    auto inst = instances(std::string(id));
    j["instances"] = inst ? inst.value() : Json::array();
    return j;
}

Result<Json> BlueprintService::update_meta(std::string_view id, const Json& body, const Actor& actor) {
    auto l = impl_->core.lock();
    auto b = impl_->repo->blueprint(id);
    if (!b) return std::move(b).error();
    Blueprint next = b.value();
    next.name = body.value("name", next.name);
    next.description = body.value("description", next.description);
    next.domain = body.value("domain", next.domain);
    next.icon = body.value("icon", next.icon);
    auto saved = impl_->repo->update_meta(next);
    if (!saved) return std::move(saved).error();
    impl_->core.record("blueprint.meta", "success", std::string(id), {{"name", next.name}}, actor, "blueprint");
    return to_json(saved.value());
}

Result<Json> BlueprintService::version(std::string_view id, std::int64_t v) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    Json j = version_summary(ver.value());
    j["document"] = ver.value().document;
    j["editable"] = ver.value().state == "draft";
    Json pinned = Json::object();
    for (const auto& [role, ref_text] : ver.value().pins) {
        auto ref = parse_ref(ref_text);
        if (!ref) continue;
        auto l = impl_->core.lock();
        auto summary = impl_->core.version_summary(ref.value());
        if (summary) pinned[role] = summary.value();
    }
    j["artifacts"] = pinned;
    {
        auto l = impl_->core.lock();
        auto b = impl_->repo->blueprint(id);
        if (b) j["blueprint"] = to_json(b.value());
    }
    return j;
}

Result<Json> BlueprintService::create_draft(std::string_view id, std::int64_t from, std::string_view note, const Actor& actor) {
    auto l = impl_->core.lock();
    auto d = impl_->repo->create_draft(id, from, note, actor.name);
    if (!d) return std::move(d).error();
    impl_->core.record("blueprint.draft", "success", std::string(id) + "@" + std::to_string(d.value().version),
                       {{"from", from}, {"note", std::string(note)}}, actor, "blueprint");
    return version_summary(d.value());
}

Result<Json> BlueprintService::save_section(std::string_view id, std::int64_t v, std::string_view section,
                                            std::int64_t revision, const Json& content, const Actor& actor) {
    if (std::find(std::begin(kBlueprintSections), std::end(kBlueprintSections), section) == std::end(kBlueprintSections)) {
        return make_error(ErrorCode::InvalidArgument, "unknown Blueprint section").with("section", std::string(section));
    }
    // Structural decoding: content that cannot be stored canonically or read by its consumers is refused.
    if (section == "scenarios" ? !content.is_array() : !content.is_object()) {
        return make_error(ErrorCode::InvalidArgument, section == "scenarios" ? "scenarios is an array" : "a section is a JSON object")
            .with("section", std::string(section));
    }
    if (section == "world") {
        auto w = scene::world_from_json(content);
        if (!w) return std::move(w).error().with("section", "world");
    }
    if (section == "assurance") {
        Json doc = content;
        doc["format"] = std::string(monitoring::kMonitorsFormat);
        auto m = monitoring::monitors_from_json(doc);
        if (!m) return std::move(m).error().with("section", "assurance");
    }
    auto ver = impl_->load_draft(id, v, revision);
    if (!ver) return std::move(ver).error();
    BlueprintVersion next = ver.value();
    next.document[std::string(section)] = content;
    if (section == "assurance") next.document["assurance"]["format"] = std::string(monitoring::kMonitorsFormat);
    if (section == "identity") {
        // Keep the Blueprint's display metadata in step with its identity section.
        auto l = impl_->core.lock();
        auto b = impl_->repo->blueprint(id);
        if (b) {
            Blueprint meta = b.value();
            meta.name = content.value("name", meta.name);
            meta.description = content.value("description", meta.description);
            meta.domain = content.value("domain", meta.domain);
            meta.icon = content.value("icon", meta.icon);
            if (!meta.name.empty()) (void)impl_->repo->update_meta(meta);
        }
    }
    auto saved = impl_->store(next, actor);
    if (!saved) return std::move(saved).error();
    std::vector<SectionFinding> mine;
    for (SectionFinding& f : impl_->validate_sections(saved.value())) {
        if (f.section == section) mine.push_back(std::move(f));
    }
    {
        auto l = impl_->core.lock();
        impl_->core.record("blueprint.save", "success", std::string(id) + "@" + std::to_string(v),
                           {{"section", std::string(section)}, {"revision", saved.value().revision}}, actor, "blueprint");
    }
    return Json{{"revision", saved.value().revision},
                {"documentSha256", saved.value().document_sha256},
                {"updatedAt", saved.value().updated_at},
                {"section", std::string(section)},
                {"findings", to_json(mine)}};
}

// ------------------------------------------------------------------------------ create

namespace {

/// Formal file contents keyed by role, with the file name (for importers).
struct FormalFile {
    std::string filename;
    std::string content;
};

}  // namespace

Result<Json> BlueprintService::create(const Json& body, const Actor& actor) {
    const std::string mode = body.value("mode", std::string("blank"));
    std::string name = body.value("name", std::string());
    Json document;
    std::string domain = body.value("domain", std::string("generic"));
    std::string icon = body.value("icon", std::string("boxes"));
    std::string description = body.value("description", std::string());
    std::optional<std::string> template_id;
    std::optional<std::string> cloned_from;
    std::map<std::string, FormalFile> formal;
    std::map<std::string, std::string> reuse_pins;  // pins of existing artefact versions (none by default)

    if (mode == "blank") {
        if (name.empty()) return make_error(ErrorCode::InvalidArgument, "a new Blueprint needs a name");
    } else if (mode == "template") {
        auto dir = impl_->template_dir(body.value("templateId", std::string()));
        if (!dir) return std::move(dir).error();
        auto text = read_text_file(dir.value() / "template.json");
        if (!text) return std::move(text).error();
        auto t = json::parse(text.value());
        if (!t) return std::move(t).error();
        template_id = body.value("templateId", std::string());
        if (name.empty()) name = t.value().value("name", std::string("New Blueprint"));
        domain = t.value().value("domain", domain);
        icon = t.value().value("icon", icon);
        if (description.empty()) description = t.value().value("description", std::string());
        if (t.value().contains("document")) {
            auto d = read_text_file(dir.value() / t.value().at("document").get<std::string>());
            if (!d) return std::move(d).error();
            auto dj = json::parse(d.value());
            if (!dj) return std::move(dj).error();
            auto resolved = resolve_includes(std::move(dj).value(), dir.value());
            if (!resolved) return std::move(resolved).error();
            document = std::move(resolved).value();
        }
        for (const Json items_role_file = t.value().value("formal", Json::object()); const auto& [role, file] : items_role_file.items()) {
            auto c = read_text_file(dir.value() / file.get<std::string>());
            if (!c) return std::move(c).error();
            formal[role] = FormalFile{file.get<std::string>(), c.value()};
        }
    } else if (mode == "clone") {
        const std::string from = body.value("from", std::string());
        const std::int64_t from_v = body.value("version", std::int64_t{0});
        auto src = impl_->load(from, from_v);
        if (!src) return std::move(src).error();
        {
            auto l = impl_->core.lock();
            auto b = impl_->repo->blueprint(from);
            if (!b) return std::move(b).error();
            domain = b.value().domain;
            icon = b.value().icon;
            if (description.empty()) description = b.value().description;
            if (name.empty()) name = b.value().name + " (copy)";
        }
        document = src.value().document;
        cloned_from = from + "@" + std::to_string(from_v);
        auto l = impl_->core.lock();
        for (const auto& [role, ref_text] : src.value().pins) {
            auto ref = parse_ref(ref_text);
            if (!ref) continue;
            auto c = impl_->core.artifacts->content(ref.value());
            if (!c) return std::move(c).error();
            const std::string_view fmt = authoring::content_format(c.value());
            formal[role] = FormalFile{role_suffix(role) + (fmt == "twin-ta/1" ? ".json" : fmt == "uppaal-xml" ? ".xml" : ".txt"), c.value()};
        }
    } else if (mode == "import") {
        const Json bundle = body.value("bundle", Json::object());
        if (bundle.value("format", std::string()) != "twin-blueprint-bundle/1") {
            return make_error(ErrorCode::InvalidArgument, "an import bundle has format \"twin-blueprint-bundle/1\"");
        }
        document = bundle.value("document", Json::object());
        const Json meta = bundle.value("blueprint", Json::object());
        if (name.empty()) name = meta.value("name", std::string("Imported Blueprint"));
        domain = meta.value("domain", domain);
        icon = meta.value("icon", icon);
        if (description.empty()) description = meta.value("description", std::string());
        for (const Json items_role_f = bundle.value("formal", Json::object()); const auto& [role, f] : items_role_f.items()) {
            formal[role] = FormalFile{f.value("filename", role), f.value("content", std::string())};
        }
    } else if (mode == "document") {
        // A complete document over existing artefact versions (seeding, programmatic import).
        if (name.empty()) return make_error(ErrorCode::InvalidArgument, "a new Blueprint needs a name");
        document = body.value("document", Json::object());
        for (const Json items_role_ref = body.value("pins", Json::object()); const auto& [role, ref] : items_role_ref.items()) {
            if (!ref.is_string()) return make_error(ErrorCode::InvalidArgument, "pins map roles to 'artifact@version'").with("role", role);
            auto parsed = parse_ref(ref.get<std::string>());
            if (!parsed) return std::move(parsed).error();
            auto l = impl_->core.lock();
            auto av = impl_->core.artifacts->version(parsed.value());
            if (!av) return std::move(av).error();
            if (av.value().kind != kind_of_role(role)) {
                return make_error(ErrorCode::InvalidArgument, "pinned version has the wrong kind").with("role", role);
            }
            reuse_pins[role] = parsed.value().str();
        }
    } else if (mode == "formal") {
        if (name.empty()) return make_error(ErrorCode::InvalidArgument, "a new Blueprint needs a name");
        const std::pair<const char*, const char*> keys[] = {{"ptModel", "pt_model"}, {"dtModel", "dt_model"}, {"ontology", "ontology"},
                                                            {"ptInterpretation", "pt_interpretation"}, {"dtInterpretation", "dt_interpretation"}};
        for (const auto& [key, role] : keys) {
            if (body.contains(key) && body.at(key).is_object()) {
                formal[role] = FormalFile{body.at(key).value("filename", std::string(role)), body.at(key).value("content", std::string())};
            }
        }
    } else {
        return make_error(ErrorCode::InvalidArgument, "mode is blank, template, clone, import, formal or document").with("mode", mode);
    }

    std::string id = body.value("id", std::string());
    if (id.empty()) {
        id = slugify(name);
        auto l = impl_->core.lock();
        for (int n = 2; impl_->repo->blueprint(id); ++n) id = slugify(name) + "-" + std::to_string(n);
    }
    if (!valid_blueprint_id(id)) {
        return make_error(ErrorCode::InvalidArgument, "a Blueprint id uses lowercase letters, digits and '-'").with("id", id);
    }
    {
        auto l = impl_->core.lock();
        if (impl_->repo->blueprint(id)) return make_error(ErrorCode::StateError, "a Blueprint with this id already exists").with("id", id);
    }

    // Document: start from a blank one and overlay the provided sections, so every section exists.
    Json doc = blank_document(id, name, domain);
    if (document.is_object()) {
        for (const auto section : kBlueprintSections) {
            if (document.contains(std::string(section))) doc[std::string(section)] = document.at(std::string(section));
        }
    }
    doc["identity"]["name"] = name;
    doc["identity"]["domain"] = domain;
    doc["identity"]["icon"] = doc["identity"].value("icon", icon);
    if (!description.empty()) doc["identity"]["description"] = description;
    if (mode != "clone" && mode != "import" && mode != "document") doc["identity"]["modelId"] = id;
    if (!doc["presentation"].contains("displayName") || doc["presentation"]["displayName"].get<std::string>().empty()) {
        doc["presentation"]["displayName"] = name;
    }

    Blueprint bp{id, name, description, domain, icon, template_id, cloned_from, "", ""};
    Result<BlueprintVersion> created = make_error(ErrorCode::Internal, "unset");
    {
        auto l = impl_->core.lock();
        created = impl_->repo->create(bp, doc, reuse_pins, actor.name);
    }
    if (!created) return std::move(created).error();
    BlueprintVersion v = created.value();

    // Formal artefacts: models are imported into the canonical form (unsupported constructs are
    // reported and leave the role empty); ontology and interpretations are stored as text.
    Json imports = Json::array();
    for (const std::string& role : creation_order()) {
        auto f = formal.find(role);
        if (f == formal.end()) continue;
        const bool model = role == "pt_model" || role == "dt_model";
        if (model) {
            authoring::ImportOptions options;
            options.filename = f->second.filename;
            const authoring::ImportResult r = run_with_large_stack([&] { return authoring::import_any(f->second.content, options); });
            Json report{{"role", role}, {"filename", f->second.filename}, {"imported", r.model.has_value()},
                        {"format", r.format}, {"diagnostics", authoring::to_json(r.diagnostics)}};
            if (r.provenance) report["provenance"] = authoring::to_json(*r.provenance);
            imports.push_back(report);
            if (!r.model) continue;
            auto ref = impl_->ensure_editable(v, role, actor);
            if (!ref) return std::move(ref).error();
            auto saved = services_.save_draft(ref.value(), json::canonical_dump(authoring::to_json(*r.model)).value(), std::nullopt,
                                              std::string("Imported from ") + f->second.filename, actor);
            if (!saved) return std::move(saved).error();
            // A layout found in the file (UPPAAL coordinates) replaces the document's; otherwise keep it.
            if (!r.layout.locations.empty()) {
                v.document["behavior"][role == "pt_model" ? "pt" : "dt"]["layout"] = authoring::to_json(r.layout);
            }
        } else {
            if (kind_of_role(role) == ArtifactKind::Interpretation && !v.pins.count("ontology")) {
                imports.push_back(Json{{"role", role}, {"filename", f->second.filename}, {"imported", false},
                                       {"reason", "an interpretation needs an ontology; import or define the ontology first"}});
                continue;
            }
            auto ref = impl_->ensure_editable(v, role, actor);
            if (!ref) return std::move(ref).error();
            std::optional<Json> refs;
            if (kind_of_role(role) == ArtifactKind::Interpretation) {
                refs = Json{{"ontology", v.pins.at("ontology")}, {"twinRole", role == "pt_interpretation" ? "pt" : "dt"}};
            }
            auto saved = services_.save_draft(ref.value(), f->second.content, refs, std::string("Imported ") + f->second.filename, actor);
            if (!saved) return std::move(saved).error();
            imports.push_back(Json{{"role", role}, {"filename", f->second.filename}, {"imported", true}});
        }
    }
    auto stored = impl_->store(v, actor);
    if (!stored) return std::move(stored).error();
    {
        auto l = impl_->core.lock();
        impl_->core.record("blueprint.create", "success", id,
                           {{"mode", mode}, {"name", name}, {"template", template_id ? Json(*template_id) : Json(nullptr)},
                            {"clonedFrom", cloned_from ? Json(*cloned_from) : Json(nullptr)}},
                           actor, "blueprint");
    }
    Json out = version_summary(stored.value());
    out["blueprint"] = to_json(bp);
    out["blueprint"]["id"] = id;
    out["imports"] = imports;
    return out;
}

// ------------------------------------------------------------------------------- search

void BlueprintService::search(std::string_view q, std::vector<SearchHit>& hits) {
    std::vector<Blueprint> all;
    {
        auto l = impl_->core.lock();
        auto bs = impl_->repo->blueprints();
        if (!bs) return;
        all = std::move(bs).value();
    }
    auto str = [](const Json& o, const char* key) {
        return o.is_object() && o.contains(key) && o.at(key).is_string() ? o.at(key).get<std::string>() : std::string();
    };
    for (const Blueprint& b : all) {
        std::optional<BlueprintVersion> v;
        {
            auto l = impl_->core.lock();
            auto vs = impl_->repo->versions(b.id);
            if (!vs || vs.value().empty()) continue;
            for (const BlueprintVersion& x : vs.value()) {
                if (x.state == "draft") {
                    v = x;
                    break;
                }
            }
            if (!v) v = vs.value().front();
        }
        const std::string base = "/studio/blueprints/" + b.id + "/v/" + std::to_string(v->version);
        const std::string where = b.name + " v" + std::to_string(v->version) + (v->state == "draft" ? " (draft)" : "");
        auto add = [&](const char* kind, const std::string& id, const std::string& title, const std::string& what,
                       const std::string& route, std::initializer_list<std::string_view> fields) {
            int best = -1;
            for (std::string_view f : fields) {
                const int sc = search_score(f, q);
                if (sc >= 0 && (best < 0 || sc < best)) best = sc;
            }
            if (best >= 0) hits.push_back({kind, id, title.empty() ? id : title, where + " · " + what, route, best});
        };
        int best = -1;
        for (std::string_view f : {std::string_view(b.id), std::string_view(b.name)}) {
            const int sc = search_score(f, q);
            if (sc >= 0 && (best < 0 || sc < best)) best = sc;
        }
        if (best >= 0) hits.push_back({"blueprint", b.id, b.name, "Blueprint · " + b.domain, "/studio/blueprints/" + b.id, best});

        const Json& d = v->document;
        auto each = [&](const Json& parent, const char* section, const char* list, auto&& fn) {
            if (!parent.is_object() || !parent.contains(section) || !parent.at(section).is_object()) return;
            const Json& sec = parent.at(section);
            if (!sec.contains(list) || !sec.at(list).is_array()) return;
            for (const Json& e : sec.at(list)) {
                if (e.is_object()) fn(e);
            }
        };
        each(d, "structure", "assetTypes", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("asset_type", id, str(e, "name"), "asset type", base + "/build/structure?type=" + id, {id, str(e, "name")});
        });
        each(d, "structure", "assets", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("blueprint_asset", id, str(e, "name"), str(e, "scope") == "context" ? "context asset" : "asset",
                base + "/build/structure?asset=" + id, {id, str(e, "name")});
        });
        each(d, "world", "objects", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("world_object", id, str(e, "name"), "world object (" + str(e, "kind") + ")", base + "/build/world?object=" + id,
                {id, str(e, "name"), str(e, "semanticType")});
        });
        each(d, "data", "telemetry", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("telemetry", id, str(e, "label"), "telemetry" + (str(e, "unit").empty() ? std::string() : " · " + str(e, "unit")),
                base + "/build/data?telemetry=" + id, {id, str(e, "label")});
        });
        each(d, "data", "events", [&](const Json& e) {
            const std::string id = str(e, "id");
            const Json formal = e.contains("formal") ? e.at("formal") : Json::object();
            add("event", id, str(e, "label"), "event", base + "/build/data?event=" + id,
                {id, str(e, "label"), str(formal, "pt"), str(formal, "dt")});
        });
        each(d, "data", "commands", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("command", id, str(e, "label"), "command", base + "/build/data?command=" + id, {id, str(e, "label")});
        });
        each(d, "connectivity", "sources", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("data_source", id, str(e, "name"), "data source (" + str(e, "kind") + ")",
                base + "/build/data?tab=connectivity&source=" + id, {id, str(e, "name")});
        });
        each(d, "assurance", "requirements", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("requirement", id, str(e, "title"), "requirement", base + "/assurance/requirements?id=" + id, {id, str(e, "title")});
        });
        each(d, "assurance", "monitors", [&](const Json& e) {
            const std::string id = str(e, "id");
            add("monitor", id, str(e, "name"), "monitor (" + str(e, "kind") + ")", base + "/assurance/monitors?id=" + id,
                {id, str(e, "name")});
        });
        if (d.contains("scenarios") && d.at("scenarios").is_array()) {
            for (const Json& e : d.at("scenarios")) {
                const std::string id = str(e, "id");
                add("scenario", id, str(e, "name"), "scenario", base + "/test/scenarios/" + id, {id, str(e, "name")});
            }
        }
        // States of the PT and DT views (pinned canonical models).
        for (const auto& [role, page, label] : {std::tuple{"pt_model", "pt", "PT view state"}, std::tuple{"dt_model", "dt", "DT view state"}}) {
            auto pin = v->pins.find(role);
            if (pin == v->pins.end()) continue;
            auto ref = parse_ref(pin->second);
            if (!ref) continue;
            std::string text;
            {
                auto l = impl_->core.lock();
                auto c = impl_->core.artifacts->content(ref.value());
                if (!c) continue;
                text = std::move(c).value();
            }
            auto doc = json::parse(text);
            if (!doc) continue;
            auto model = authoring::model_from_json(doc.value());
            if (!model) continue;
            for (const auto& loc : model.value().locations) {
                add("state", loc.name, loc.name, label, base + "/behavior/" + page + "?state=" + loc.name, {loc.name});
            }
        }
    }
}

}  // namespace twin::studio
