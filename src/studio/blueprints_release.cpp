/**
 * @file blueprints_release.cpp
 * @brief BlueprintService: checks, Verified Core Package + Twin Deployment Bundle, publish, export,
 *        instances and deployment (see blueprints.hpp).
 *
 * Proof boundary: the Verified Core Package (twin-package/1) carries everything the correctness
 * argument covers — V_D, IR, K, I_P/I_D, V_P, alignment and compilation evidence, monitors. The
 * Twin Deployment Bundle (twin-bundle/1) adds the rest of the Blueprint (assets, world, data
 * contract, connectivity, presentation, scenarios, simulator inputs) with integrity hashes only;
 * its manifest states the verification scope explicitly so nothing is presented as more verified
 * than it is.
 */
#include <algorithm>
#include <fstream>
#include <set>

#include "blueprints_impl.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/core/sha256.hpp"
#include "twin/core/version.hpp"
#include "twin/monitoring/monitors.hpp"
#include "twin/package/package.hpp"
#include "twin/scene/robot_sim.hpp"
#include "twin/scene/world.hpp"
#include "twin/studio/runtime_bridge.hpp"
#include "twin/studio/supervisor.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using json::Json;
using namespace twin::platform;

namespace {

Status write_bytes(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (!authoring::write_file_atomically(p, bytes)) return make_error(ErrorCode::IoError, "cannot write " + p.string());
    return {};
}

std::string value_type_of(const std::string& t) {
    if (t == "real" || t == "integer") return "number";
    if (t == "boolean") return "boolean";
    return "category";
}

const std::vector<std::string>& formally_verified_scope() {
    static const std::vector<std::string> kScope = {
        "Digital Twin View V_D (model/dt_view.xml, canonical model)", "Twin IR (ir/model.ir.json, translation-validated)",
        "Ontology K", "Interpretations I_P and I_D", "Physical System View V_P (alignment input)",
        "Semantic alignment evidence", "Compilation manifest", "Monitor definitions (versioned with the core)"};
    return kScope;
}

const std::vector<std::string>& integrity_only_scope() {
    static const std::vector<std::string> kScope = {
        "Asset / structural model", "World and layout (floor plan geometry, images)", "Data contract and connector configuration",
        "Operational presentation", "Scenario metadata", "Simulator inputs (generated world scenario, event script)"};
    return kScope;
}

}  // namespace

// ------------------------------------------------------------------------------ checks

Result<Json> BlueprintService::run_check(std::string_view id, std::int64_t v, std::string_view check, const Json& body, const Actor& actor) {
    auto ver_r = impl_->load(id, v);
    if (!ver_r) return std::move(ver_r).error();
    const BlueprintVersion ver = ver_r.value();
    const std::string ctx = Impl::context(ver);
    const Json identity = ver.document.value("identity", Json::object());
    if (check == "formal") {
        Json results = Json::array();
        for (const char* role : {"ontology", "pt_interpretation", "dt_interpretation", "pt_model", "dt_model"}) {
            auto p = ver.pins.find(role);
            if (p == ver.pins.end()) {
                results.push_back(Json{{"role", role}, {"state", "missing"}});
                continue;
            }
            auto ref = parse_ref(p->second);
            if (!ref) continue;
            bool open = false;
            {
                auto l = impl_->core.lock();
                auto av = impl_->core.artifacts->version(ref.value());
                open = av && av.value().is_open();
            }
            auto r = services_.validate(ref.value(), actor);
            if (!r) {
                results.push_back(Json{{"role", role}, {"ref", ref.value().str()}, {"state", "error"}, {"error", r.error().message}});
            } else {
                Json x = r.value();
                x.erase("content");
                results.push_back(Json{{"role", role}, {"ref", ref.value().str()}, {"open", open}, {"result", x}});
            }
        }
        return Json{{"check", "formal"}, {"results", results}};
    }
    if (check == "alignment") {
        auto b = impl_->bindings(ver);
        if (!b) return std::move(b).error();
        return services_.run_alignment(b.value(), ctx, actor);
    }
    if (check == "compile") {
        Services::BuildTarget target;
        target.owner = blueprint_owner(ver.blueprint_id);
        target.model_id = identity.value("modelId", ver.blueprint_id);
        target.ticks_per_unit = identity.value("ticksPerUnit", std::int64_t{1000});
        return services_.run_compile_for(target, impl_->pinned_bindings(ver), ctx, actor);
    }
    if (check == "refinement") {
        std::optional<BlueprintVersion> base;
        {
            auto l = impl_->core.lock();
            auto all = impl_->repo->versions(id);
            if (!all) return std::move(all).error();
            for (const BlueprintVersion& x : all.value()) {
                if (x.state == "published" && x.version != v) {
                    base = x;
                    break;
                }
            }
        }
        if (!base) return make_error(ErrorCode::StateError, "a refinement check compares with a published version; this Blueprint has none yet");
        auto phi = [](const BlueprintVersion& x) -> Result<Services::PhiRefs> {
            auto o = x.pins.find("ontology");
            if (o == x.pins.end()) return make_error(ErrorCode::InvalidArgument, "no ontology pinned");
            auto oref = parse_ref(o->second);
            if (!oref) return std::move(oref).error();
            Services::PhiRefs p{oref.value(), std::nullopt, std::nullopt};
            if (auto i = x.pins.find("pt_interpretation"); i != x.pins.end()) {
                if (auto r = parse_ref(i->second)) p.pt_interpretation = r.value();
            }
            if (auto i = x.pins.find("dt_interpretation"); i != x.pins.end()) {
                if (auto r = parse_ref(i->second)) p.dt_interpretation = r.value();
            }
            return p;
        };
        auto b = phi(*base);
        auto c = phi(ver);
        if (!b) return std::move(b).error();
        if (!c) return std::move(c).error();
        return services_.run_refinement(b.value(), c.value(), ctx, actor);
    }
    if (check == "scenarios") return run_scenarios(id, v, std::nullopt, actor);
    if (check != "package") {
        return make_error(ErrorCode::InvalidArgument, "check is formal, alignment, compile, refinement, scenarios or package").with("check", std::string(check));
    }

    // -------------------------------------------------------------- package + bundle
    auto st = status(id, v);
    if (!st) return std::move(st).error();
    if (!st.value()["readiness"].value("readyToPackage", false) && !body.value("force", false)) {
        Error e = make_error(ErrorCode::StateError, "the release gate does not pass yet; fix the blockers before packaging");
        for (const Json& b : st.value()["readiness"]["blockers"]) e.with("blocker", b.get<std::string>());
        return e;
    }
    auto bindings = impl_->bindings(ver);
    if (!bindings) return std::move(bindings).error();
    const fs::path work = services_.config().data_dir / "work" / ("bp-" + ver.blueprint_id + "-" + std::to_string(ver.version));
    Json monitors = ver.document.value("assurance", Json::object());
    monitors["format"] = std::string(monitoring::kMonitorsFormat);
    auto monitors_bytes = json::canonical_dump(monitors);
    if (!monitors_bytes) return std::move(monitors_bytes).error();
    if (auto w = write_bytes(work / "monitors.json", monitors_bytes.value()); !w) return w.error();
    Services::BuildTarget target;
    target.owner = blueprint_owner(ver.blueprint_id);
    target.model_id = identity.value("modelId", ver.blueprint_id);
    target.ticks_per_unit = identity.value("ticksPerUnit", std::int64_t{1000});
    target.model_version = std::to_string(ver.version) + ".0.0";
    target.monitors = work / "monitors.json";
    target.source_models = true;
    target.type_metadata = Json{{"format", "twin-type-metadata/1"},
                                {"blueprintId", ver.blueprint_id},
                                {"blueprintVersion", ver.version},
                                {"name", identity.value("name", ver.blueprint_id)},
                                {"runtimeMode", identity.value("runtimeMode", std::string("monitor"))},
                                {"documentSha256", ver.document_sha256}};
    auto built = services_.build_package_for(target, bindings.value(), ctx, actor);
    if (!built) return std::move(built).error();
    if (built.value()["package"].is_null() || built.value().value("outcome", std::string()) != "pass") {
        return Json{{"check", "package"}, {"built", false}, {"evidence", built.value()}};
    }
    const Json pkg = built.value()["package"];
    const std::string package_id = pkg.value("id", std::string());

    // Deployment bundle: integrity-protected, not formally verified.
    std::vector<std::pair<std::string, std::string>> files;  // path, role
    const fs::path staging = services_.config().data_dir / "bundles" / ("staging-" + package_id);
    std::error_code ec;
    fs::remove_all(staging, ec);
    auto doc_bytes = json::canonical_dump(ver.document);
    if (!doc_bytes) return std::move(doc_bytes).error();
    if (auto w = write_bytes(staging / "blueprint.json", doc_bytes.value()); !w) return w.error();
    files.emplace_back("blueprint.json", "blueprint_document");
    const Json sim = ver.document.value("simulation", Json::object());
    if (sim.value("kind", std::string()) == "mobile-robot") {
        auto world = scene::world_from_json(ver.document.value("world", Json::object()));
        if (!world) return std::move(world).error();
        auto scenario = scene::simulator_scenario(world.value(), sim, sim.value("name", identity.value("name", std::string())),
                                                  sim.value("description", std::string()));
        if (!scenario) return std::move(scenario).error();
        if (auto w = write_bytes(staging / "simulation" / "world-scenario.json", scenario.value().dump(1)); !w) return w.error();
        files.emplace_back("simulation/world-scenario.json", "simulator_world");
    } else if (sim.value("kind", std::string()) == "event-script") {
        if (auto w = write_bytes(staging / "simulation" / "event-script.json", sim.value("script", Json::object()).dump(1)); !w) return w.error();
        files.emplace_back("simulation/event-script.json", "simulator_script");
    }
    Json listed = Json::array();
    for (const auto& [path, role] : files) {
        auto bytes = read_text_file(staging / path);
        if (!bytes) return std::move(bytes).error();
        listed.push_back(Json{{"path", path}, {"role", role}, {"sha256", sha256_hex(bytes.value())},
                              {"size", static_cast<std::int64_t>(bytes.value().size())}, {"verification", "integrity"}});
    }
    Json manifest{{"format", "twin-bundle/1"},
                  {"blueprint", {{"id", ver.blueprint_id}, {"version", ver.version}, {"name", identity.value("name", ver.blueprint_id)},
                                 {"documentSha256", ver.document_sha256}}},
                  {"verifiedCore", {{"format", std::string(twin::version::kPackageFormat)}, {"packageId", package_id},
                                    {"packageHash", pkg.value("packageHash", std::string())}, {"irSha256", pkg.value("irSha256", std::string())}}},
                  {"files", listed},
                  {"verificationScope", {{"formallyVerified", formally_verified_scope()}, {"integrityOnly", integrity_only_scope()},
                                         {"note", "Only the Verified Core Package is covered by the alignment and compilation evidence. "
                                                  "Bundle files are versioned and hash-protected, not formally verified."}}}};
    auto manifest_bytes = json::canonical_dump(manifest);
    if (!manifest_bytes) return std::move(manifest_bytes).error();
    if (auto w = write_bytes(staging / "bundle.json", manifest_bytes.value()); !w) return w.error();
    BundleRecord rec;
    rec.blueprint_id = ver.blueprint_id;
    rec.version = ver.version;
    rec.package_id = package_id;
    rec.directory = staging.string();
    rec.bundle_hash = sha256_hex(manifest_bytes.value());
    rec.created_by = actor.name;
    Result<BundleRecord> stored = make_error(ErrorCode::Internal, "unset");
    {
        auto l = impl_->core.lock();
        stored = impl_->repo->add_bundle(rec);
    }
    if (!stored) return std::move(stored).error();
    const fs::path final_dir = services_.config().data_dir / "bundles" / stored.value().id;
    fs::rename(staging, final_dir, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot finalise the bundle directory: " + ec.message());
    {
        auto l = impl_->core.lock();
        if (auto s2 = impl_->repo->set_bundle_directory(stored.value().id, fs::absolute(final_dir).string()); !s2) return s2.error();
        if (auto s2 = impl_->repo->set_build(ver.blueprint_id, ver.version, package_id, stored.value().id); !s2) return s2.error();
        impl_->core.record("blueprint.package", "success", ver.blueprint_id + "@" + std::to_string(ver.version),
                           {{"packageId", package_id}, {"bundleId", stored.value().id}, {"bundleHash", rec.bundle_hash}}, actor, "package");
    }
    Json b = to_json(stored.value());
    b["manifest"] = manifest;
    return Json{{"check", "package"}, {"built", true}, {"evidence", built.value()}, {"package", pkg}, {"bundle", b}};
}

// ----------------------------------------------------------------------------- package

Result<Json> BlueprintService::package(std::string_view id, std::int64_t v) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    Json out{{"version", version_summary(ver.value())}, {"package", nullptr}, {"bundle", nullptr}};
    // What would be packaged (also before a build): the pinned formal inputs and the bundle sections.
    Json core = Json::array();
    for (const auto role : kBlueprintRoles) {
        auto p = ver.value().pins.find(std::string(role));
        core.push_back(Json{{"role", std::string(role)}, {"ref", p == ver.value().pins.end() ? Json(nullptr) : Json(p->second)}});
    }
    out["verifiedCoreInputs"] = core;
    Json content = Json::array();
    for (const auto section : {"structure", "world", "data", "connectivity", "presentation", "scenarios", "simulation"}) {
        auto h = json::canonical_sha256(ver.value().document.value(section, Json()));
        content.push_back(Json{{"section", section}, {"sha256", h ? Json(h.value()) : Json(nullptr)}});
    }
    out["deploymentContent"] = content;
    out["verificationScope"] = Json{{"formallyVerified", formally_verified_scope()}, {"integrityOnly", integrity_only_scope()}};
    if (ver.value().package_id) {
        auto p = services_.package(*ver.value().package_id);
        if (p) out["package"] = p.value();
        auto integrity = services_.verify_package(*ver.value().package_id);
        if (integrity) out["integrity"] = integrity.value();
    }
    if (ver.value().bundle_id) {
        auto l = impl_->core.lock();
        auto b = impl_->repo->bundle(*ver.value().bundle_id);
        if (b) {
            Json bj = to_json(b.value());
            auto manifest = read_text_file(fs::path(b.value().directory) / "bundle.json");
            if (manifest) {
                bj["intact"] = sha256_hex(manifest.value()) == b.value().bundle_hash;
                auto m = json::parse(manifest.value());
                if (m) {
                    bj["manifest"] = m.value();
                    bool files_ok = true;
                    for (const Json& f : m.value().value("files", Json::array())) {
                        auto bytes = read_text_file(fs::path(b.value().directory) / f.value("path", std::string()));
                        files_ok = files_ok && bytes && sha256_hex(bytes.value()) == f.value("sha256", std::string());
                    }
                    bj["filesIntact"] = files_ok;
                }
            } else {
                bj["intact"] = false;
            }
            out["bundle"] = bj;
        }
    }
    return out;
}

Result<Json> BlueprintService::publish(std::string_view id, std::int64_t v, const Actor& actor) {
    auto ver = impl_->load_draft(id, v, std::nullopt);
    if (!ver) return std::move(ver).error();
    auto st = status(id, v);
    if (!st) return std::move(st).error();
    if (st.value()["readiness"].value("verdict", std::string()) != "ready") {
        Error e = make_error(ErrorCode::StateError, "RELEASE BLOCKED: the release gate does not pass");
        for (const Json& b : st.value()["readiness"]["blockers"]) e.with("blocker", b.get<std::string>());
        return e;
    }
    // Publish the formal drafts this version pins (they are validated: the gate checked it).
    for (const auto& [role, ref_text] : ver.value().pins) {
        auto ref = parse_ref(ref_text);
        if (!ref) continue;
        Lifecycle state = Lifecycle::Published;
        {
            auto l = impl_->core.lock();
            auto av = impl_->core.artifacts->version(ref.value());
            if (av) state = av.value().state;
        }
        if (state == Lifecycle::Verified) {
            auto p = services_.publish(ref.value(), actor);
            if (!p) return std::move(p).error().with("role", role);
        }
    }
    auto l = impl_->core.lock();
    auto pkg = impl_->core.twins->package(*ver.value().package_id);
    if (pkg && pkg.value().state == "built") {
        auto released = impl_->core.twins->mark_released(*ver.value().package_id);
        if (!released) return std::move(released).error();
    }
    auto published = impl_->repo->publish(id, v, actor.name);
    if (!published) return std::move(published).error();
    impl_->core.record("blueprint.publish", "success", std::string(id) + "@" + std::to_string(v),
                       {{"packageId", *ver.value().package_id}, {"bundleId", *ver.value().bundle_id}}, actor, "blueprint");
    return version_summary(published.value());
}

Result<Json> BlueprintService::export_bundle(std::string_view id, std::int64_t v) {
    auto ver = impl_->load(id, v);
    if (!ver) return std::move(ver).error();
    Json formal = Json::object();
    {
        auto l = impl_->core.lock();
        for (const auto& [role, ref_text] : ver.value().pins) {
            auto ref = parse_ref(ref_text);
            if (!ref) continue;
            auto c = impl_->core.artifacts->content(ref.value());
            if (!c) return std::move(c).error();
            const std::string_view fmt = authoring::content_format(c.value());
            formal[role] = Json{{"filename", role + (fmt == "twin-ta/1" ? std::string(".tta.json") : fmt == "uppaal-xml" ? std::string(".xml") : std::string(".txt"))},
                                {"content", c.value()},
                                {"ref", ref_text}};
        }
    }
    Json meta;
    {
        auto l = impl_->core.lock();
        auto b = impl_->repo->blueprint(id);
        if (!b) return std::move(b).error();
        meta = to_json(b.value());
    }
    return Json{{"format", "twin-blueprint-bundle/1"},
                {"blueprint", meta},
                {"version", ver.value().version},
                {"document", ver.value().document},
                {"formal", formal},
                {"exportedAt", iso8601_utc(services_.clock().now_ms())},
                {"note", "Import with Studio → Import. Evidence is not exported: it is re-established by running the checks."}};
}

// ---------------------------------------------------------------------------- instances

Result<Json> BlueprintService::instances(const std::optional<std::string>& blueprint) {
    std::vector<Twin> twins;
    {
        auto l = impl_->core.lock();
        auto t = impl_->core.twins->twins();
        if (!t) return std::move(t).error();
        twins = t.value();
    }
    Json out = Json::array();
    for (const Twin& t : twins) {
        if (!t.blueprint_id) continue;
        if (blueprint && *t.blueprint_id != *blueprint) continue;
        auto one = instance(t.id);
        if (one) out.push_back(one.value());
    }
    return out;
}

Result<Json> BlueprintService::instance(std::string_view id) {
    Twin t;
    std::optional<Deployment> dep;
    std::optional<BlueprintVersion> latest;
    {
        auto l = impl_->core.lock();
        auto tw = impl_->core.twins->twin(id);
        if (!tw) return std::move(tw).error();
        t = tw.value();
        auto d = impl_->core.twins->current_deployment(id);
        if (d) dep = d.value();
        if (t.blueprint_id) {
            auto lp = impl_->repo->latest_published(*t.blueprint_id);
            if (lp) latest = lp.value();
        }
    }
    Json j = to_json(t);
    j["deployment"] = dep ? to_json(*dep) : Json(nullptr);
    j["runtime"] = supervisor_ != nullptr ? supervisor_->status(t.id) : Json{{"state", "not_started"}};
    j["latestPublishedVersion"] = latest ? Json(latest->version) : Json(nullptr);
    j["upgradeAvailable"] = latest && t.blueprint_version && latest->version > *t.blueprint_version;
    return j;
}

Result<Json> BlueprintService::create_instance(const Json& body, const Actor& actor) {
    const std::string bp = body.value("blueprintId", std::string());
    const std::int64_t version = body.value("version", std::int64_t{0});
    const std::string id = body.value("id", std::string());
    const std::string name = body.value("name", id);
    if (!valid_blueprint_id(id)) return make_error(ErrorCode::InvalidArgument, "an instance id uses lowercase letters, digits and '-'").with("id", id);
    auto ver_r = impl_->load(bp, version);
    if (!ver_r) return std::move(ver_r).error();
    const BlueprintVersion ver = ver_r.value();
    if (ver.state != "published" && !body.value("allowDraft", false)) {
        return make_error(ErrorCode::StateError, "instances are created from published versions; publish v" + std::to_string(version) + " first");
    }
    if (services_.twin_record(id)) return make_error(ErrorCode::StateError, "a twin with this id already exists").with("id", id);
    const Json& doc = ver.document;
    const Json structure = doc.value("structure", Json::object());
    const Json placement = body.value("placement", Json::object());
    const std::string parent_override = placement.value("parentAssetId", std::string());
    const Json asset_ids = body.value("assetIds", Json::object());  // optional explicit ids per Blueprint asset

    // Asset map: instance-scoped assets get instance ids, context assets are shared.
    std::map<std::string, std::string> map;
    std::map<std::string, Json> defs;
    for (const Json& a : structure.value("assets", Json::array())) {
        const std::string aid = a.value("id", std::string());
        defs[aid] = a;
        if (a.value("scope", std::string("instance")) == "context") map[aid] = aid;
        else map[aid] = asset_ids.value(aid, aid == structure.value("root", std::string()) ? id : id + "-" + aid);
    }
    const std::string root = structure.value("root", std::string());
    if (root.empty() || !map.count(root)) return make_error(ErrorCode::InvalidArgument, "the Blueprint has no root asset");
    std::map<std::string, Json> type_defaults;
    for (const Json& t : structure.value("assetTypes", Json::array())) {
        Json props = Json::object();
        for (const Json& p : t.value("properties", Json::array())) {
            if (p.contains("default")) props[p.value("key", std::string())] = p.at("default");
        }
        type_defaults[t.value("id", std::string())] = props;
    }
    // Create assets parents first.
    std::vector<std::string> order;
    std::set<std::string> placed;
    for (std::size_t guard = 0; order.size() < defs.size() && guard <= defs.size(); ++guard) {
        for (const auto& [aid, a] : defs) {
            if (placed.count(aid)) continue;
            const std::string parent = a.value("parent", std::string());
            if (parent.empty() || placed.count(parent) || !defs.count(parent)) {
                order.push_back(aid);
                placed.insert(aid);
            }
        }
    }
    const Json overrides = body.value("properties", Json::object());
    for (const std::string& aid : order) {
        const Json& a = defs.at(aid);
        Asset asset;
        asset.id = map.at(aid);
        const bool context = a.value("scope", std::string("instance")) == "context";
        if (context) {
            auto l = impl_->core.lock();
            if (impl_->core.assets->get(asset.id)) continue;  // shared context asset already exists
        }
        asset.name = aid == root ? name : a.value("name", aid);
        asset.type = a.value("type", std::string("Asset"));
        const std::string parent = a.value("parent", std::string());
        if (!parent.empty() && map.count(parent)) asset.parent_id = map.at(parent);
        else if (aid == root && !parent_override.empty()) asset.parent_id = parent_override;
        if (aid != root && !parent.empty() && !map.count(parent)) asset.parent_id = parent;
        asset.description = a.value("description", std::string());
        asset.tags = a.value("tags", Json::array());
        Json props = type_defaults.count(asset.type) ? type_defaults.at(asset.type) : Json::object();
        for (const Json items_k_val = a.value("properties", Json::object()); const auto& [k, val] : items_k_val.items()) props[k] = val;
        for (const Json items_k_val = overrides.value(aid, Json::object()); const auto& [k, val] : items_k_val.items()) props[k] = val;
        for (auto& [k, val] : props.items()) {
            if (!val.is_string()) val = val.dump();
        }
        asset.properties = props;
        if (aid == root) asset.twin_id = id;
        if (auto st = services_.upsert_asset(asset); !st) return Error(st.error()).with("asset", asset.id);
    }
    for (const Json& r : structure.value("relationships", Json::array())) {
        const std::string s = r.value("source", std::string());
        const std::string d = r.value("target", std::string());
        if (!map.count(s) || !map.count(d)) continue;
        (void)services_.relate(map.at(s), r.value("type", std::string("relatedTo")), map.at(d));
    }

    // Telemetry channels from the data contract; values reach them through the runtime and the bridge.
    const Json conn = body.contains("connectivity") ? body.at("connectivity") : doc.value("connectivity", Json::object());
    std::map<std::string, Json> binding_of;
    for (const Json& b : conn.value("bindings", Json::array())) {
        if (b.value("target", Json::object()).value("kind", std::string()) == "telemetry") binding_of[b["target"].value("id", std::string())] = b;
    }
    Json channels = Json::array();
    for (const Json& t : doc.value("data", Json::object()).value("telemetry", Json::array())) {
        const std::string tid = t.value("id", std::string());
        TelemetryChannel ch;
        ch.id = id + "." + tid;
        const std::string owner = t.value("asset", std::string());
        ch.asset_id = map.count(owner) ? map.at(owner) : map.at(root);
        ch.name = tid;
        ch.value_type = value_type_of(t.value("type", std::string("real")));
        ch.unit = t.value("unit", std::string());
        if (t.contains("ontologySymbol") && t.at("ontologySymbol").is_string() && !t.at("ontologySymbol").get<std::string>().empty()) {
            ch.ontology_symbol = t.at("ontologySymbol").get<std::string>();
        }
        std::string field = tid;
        Json transform = nullptr;
        if (binding_of.count(tid)) {
            const Json& b = binding_of.at(tid);
            const Json sel = b.value("select", Json::object());
            field = sel.value("field", sel.value("path", tid));
            if (field.rfind("$.", 0) == 0) field = field.substr(2);
            const Json unit = b.value("unit", Json::object());
            if (unit.contains("scale") || unit.contains("offset")) {
                transform = Json{{"scale", unit.value("scale", Json("1"))}, {"offset", unit.value("offset", Json("0"))}};
            }
        }
        ch.source = runtime_source_prefix(id) + field;
        ch.expected_period_ms = t.value("expectedPeriodMs", std::int64_t{1000});
        const Json pres = t.value("presentation", Json::object());
        ch.presentation = Json{{"label", t.value("label", tid)},
                               {"category", pres.value("category", std::string())},
                               {"precision", pres.value("precision", std::int64_t{2})},
                               {"preferredView", pres.value("chart", std::string(ch.value_type == "number" ? "line" : "state"))},
                               {"transform", transform},
                               {"range", t.value("range", Json::object())}};
        if (auto st = services_.upsert_channel(ch); !st) return Error(st.error()).with("channel", ch.id);
        channels.push_back(ch.id);
    }

    // Twin record: an instance of the Blueprint version.
    const Json identity = doc.value("identity", Json::object());
    const Json pres = doc.value("presentation", Json::object());
    Json key = Json::array();
    for (const Json& k : pres.value("keyTelemetry", Json::array())) {
        if (k.is_string()) key.push_back(id + "." + k.get<std::string>());
    }
    Twin t;
    t.id = id;
    t.name = name;
    t.asset_id = map.at(root);
    t.description = body.value("description", identity.value("description", std::string()));
    t.model_id = identity.value("modelId", bp);
    t.ticks_per_unit = identity.value("ticksPerUnit", std::int64_t{1000});
    t.presentation = Json{{"plugin", pres.value("plugin", Json(nullptr))},
                          {"timeUnit", identity.value("timeUnit", std::string("s"))},
                          {"states", pres.value("states", Json::object())},
                          {"events", pres.value("events", Json::object())},
                          {"keyTelemetry", key},
                          {"primaryView", pres.value("primaryView", std::string("status"))},
                          {"importantAssets", pres.value("importantAssets", Json::array())},
                          {"importantMonitors", pres.value("importantMonitors", Json::array())},
                          {"icon", pres.value("icon", identity.value("icon", std::string("boxes")))}};
    t.blueprint_id = bp;
    t.blueprint_version = version;
    Json asset_map = Json::object();
    for (const auto& [k, val] : map) asset_map[k] = val;
    t.instance_config = Json{{"identity", body.value("identity", Json::object())},
                             {"placement", placement},
                             {"connectivity", conn},
                             {"target", body.value("target", Json{{"kind", "local"}})},
                             {"assetMap", asset_map},
                             {"channels", channels}};
    t.desired_state = "stopped";
    if (auto st = services_.upsert_twin(t); !st) return st.error();
    {
        auto l = impl_->core.lock();
        impl_->core.record("instance.create", "success", id, {{"blueprint", bp}, {"version", version}, {"assets", map.size()}}, actor, "twin");
    }
    return instance(id);
}

Result<Json> BlueprintService::deploy_instance(std::string_view id, const Json& body, const Actor& actor) {
    auto tw = services_.twin_record(id);
    if (!tw) return std::move(tw).error();
    Twin t = tw.value();
    if (!t.blueprint_id) return make_error(ErrorCode::StateError, "this twin is not a Blueprint instance");
    const std::int64_t version = body.value("version", t.blueprint_version.value_or(0));
    auto ver_r = impl_->load(*t.blueprint_id, version);
    if (!ver_r) return std::move(ver_r).error();
    const BlueprintVersion ver = ver_r.value();
    if (ver.state == "draft" || !ver.package_id || !ver.bundle_id) {
        return make_error(ErrorCode::StateError, "only published Blueprint versions (with package and bundle) can be deployed")
            .with("version", std::to_string(version));
    }
    // Record the deployment (verifies the package's integrity first). Re-deploying the running
    // package just restarts the processes.
    Json deployment = nullptr;
    auto d = services_.deploy(id, *ver.package_id, body.value("reason", std::string(version != t.blueprint_version.value_or(version) ? "upgrade" : "deploy")), actor);
    if (d) {
        deployment = d.value();
    } else if (d.error().message.find("already deployed") == std::string::npos) {
        return std::move(d).error();
    }
    if (t.blueprint_version != version) {
        t.blueprint_version = version;
        if (auto st = services_.upsert_twin(t); !st) return st.error();
    }
    if (supervisor_ == nullptr) return make_error(ErrorCode::Unavailable, "no deployment supervisor");
    PackageRecord pkg;
    BundleRecord bundle;
    {
        auto l = impl_->core.lock();
        auto p = impl_->core.twins->package(*ver.package_id);
        if (!p) return std::move(p).error();
        pkg = p.value();
        auto b = impl_->repo->bundle(*ver.bundle_id);
        if (!b) return std::move(b).error();
        bundle = b.value();
    }
    const Json identity = ver.document.value("identity", Json::object());
    const Json target = t.instance_config.value("target", Json::object());
    LaunchPlan plan;
    plan.instance_id = t.id;
    plan.mode = identity.value("runtimeMode", std::string("monitor"));
    plan.package_dir = pkg.directory;
    plan.work_dir = services_.config().data_dir / "instances" / t.id;
    plan.package_store = {services_.config().data_dir / "packages"};
    plan.paused = target.value("paused", true);
    {
        const Json s = target.value("speed", Json("1.5"));
        plan.speed = s.is_number() ? s.get<double>() : std::strtod(s.get<std::string>().c_str(), nullptr);
        if (plan.speed <= 0) plan.speed = 1.0;
    }
    const fs::path bdir = bundle.directory;
    std::error_code ec;
    if (plan.mode == "cosimulation") {
        if (!fs::is_regular_file(bdir / "simulation" / "world-scenario.json", ec)) {
            return make_error(ErrorCode::StateError, "the bundle has no simulator world (Blueprint simulation kind must be mobile-robot)");
        }
        plan.world_scenario = bdir / "simulation" / "world-scenario.json";
    } else {
        // Event-script simulator as the data source (when the instance binds a simulator source).
        bool simulated = false;
        for (const Json& s : t.instance_config.value("connectivity", Json::object()).value("sources", Json::array())) {
            simulated = simulated || s.value("kind", std::string()) == "simulator";
        }
        if (simulated && fs::is_regular_file(bdir / "simulation" / "event-script.json", ec)) {
            plan.feed = bdir / "simulation" / "event-script.json";
            if (!target.contains("speed")) plan.speed = 1.0;
        }
    }
    auto started = supervisor_->start(plan);
    {
        auto l = impl_->core.lock();
        impl_->core.record("instance.deploy", started ? "success" : "fail", t.id,
                           {{"version", version}, {"packageId", *ver.package_id}, {"bundleId", *ver.bundle_id},
                            {"error", started ? Json(nullptr) : Json(started.error().message)}},
                           actor, "deployment");
    }
    if (!started) return std::move(started).error();
    return Json{{"instance", t.id}, {"version", version}, {"deployment", deployment}, {"runtime", started.value()},
                {"operate", "/twins/" + t.id}};
}

Result<Json> BlueprintService::control_instance(std::string_view id, std::string_view action, const Actor& actor) {
    auto tw = services_.twin_record(id);
    if (!tw) return std::move(tw).error();
    if (action == "stop") {
        if (supervisor_ != nullptr) supervisor_->stop(std::string(id), "stopped by " + actor.name);
        Twin t = tw.value();
        t.desired_state = "stopped";
        t.runtime_url.reset();
        t.world_url.reset();
        if (auto st = services_.upsert_twin(t); !st) return st.error();
        auto l = impl_->core.lock();
        impl_->core.record("instance.stop", "success", std::string(id), Json::object(), actor, "deployment");
        return instance(id);
    }
    if (action == "start") return deploy_instance(id, Json::object(), actor);
    return make_error(ErrorCode::InvalidArgument, "action is start or stop");
}

}  // namespace twin::studio
