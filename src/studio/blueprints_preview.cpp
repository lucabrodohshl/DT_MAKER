/**
 * @file blueprints_preview.cpp
 * @brief BlueprintService: isolated Studio previews of a Blueprint version (see blueprints.hpp).
 *
 * A preview runs the real runtime, simulator and feed on the version's verified core, exactly as
 * a deployment would, but in a sandbox: the core is the version's own package when it has one,
 * otherwise a package built from the pinned artefacts into a private directory (same builder and
 * checks, no package record, no evidence); the processes get their own work directory (ledgers,
 * logs); there is no twin record, no deployment record and no telemetry is stored. The preview
 * id contains '~', which no twin id can, so a preview is never mistaken for an instance.
 */
#include <fstream>

#include "blueprints_impl.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/core/sha256.hpp"
#include "twin/monitoring/monitors.hpp"
#include "twin/scene/robot_sim.hpp"
#include "twin/scene/world.hpp"
#include "twin/studio/supervisor.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using json::Json;
using namespace twin::platform;

namespace {

Status write_text(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (!authoring::write_file_atomically(p, bytes)) return make_error(ErrorCode::IoError, "cannot write " + p.string());
    return {};
}

}  // namespace

std::string BlueprintService::preview_id(std::string_view id, std::int64_t version) {
    return "preview~" + std::string(id) + "~v" + std::to_string(version);
}

Result<Json> BlueprintService::start_preview(std::string_view id, std::int64_t v, const Json& body, const Actor& actor) {
    auto ver_r = impl_->load(id, v);
    if (!ver_r) return std::move(ver_r).error();
    const BlueprintVersion ver = ver_r.value();
    if (supervisor_ == nullptr || !supervisor_->available()) {
        return make_error(ErrorCode::Unavailable, "the runtime binaries are not available to this Studio (see Administration → Runtime)");
    }
    const Json identity = ver.document.value("identity", Json::object());
    const std::string pid = preview_id(id, v);
    const fs::path root = services_.config().data_dir / "previews";

    // 1. The verified core.
    fs::path package_dir;
    Json core;
    if (ver.package_id) {
        auto l = impl_->core.lock();
        auto p = impl_->core.twins->package(*ver.package_id);
        if (!p) return std::move(p).error();
        package_dir = p.value().directory;
        core = Json{{"source", "version package"}, {"packageId", p.value().id}, {"packageHash", p.value().package_hash},
                    {"irSha256", p.value().ir_sha256}};
    } else {
        auto bindings = impl_->bindings(ver);
        if (!bindings) {
            return make_error(ErrorCode::StateError, "a preview needs the five formal artefacts: " + bindings.error().message);
        }
        Json key_doc{{"modelId", identity.value("modelId", ver.blueprint_id)},
                     {"ticksPerUnit", identity.value("ticksPerUnit", std::int64_t{1000})},
                     {"assurance", ver.document.value("assurance", Json::object())}};
        for (const Binding& b : bindings.value()) key_doc["bindings"][b.role] = b.ref.str() + "#" + b.sha256;
        auto key = json::canonical_sha256(key_doc);
        if (!key) return std::move(key).error();
        const fs::path cache = root / "cache" / key.value().substr(0, 24);
        package_dir = cache / "package";
        std::error_code ec;
        if (!fs::is_regular_file(package_dir / "manifest.json", ec)) {
            Json monitors = ver.document.value("assurance", Json::object());
            monitors["format"] = std::string(monitoring::kMonitorsFormat);
            auto bytes = json::canonical_dump(monitors);
            if (!bytes) return std::move(bytes).error();
            if (auto w = write_text(cache / "monitors.json", bytes.value()); !w) return w.error();
            Services::BuildTarget target;
            target.owner = "preview:" + ver.blueprint_id;
            target.model_id = identity.value("modelId", ver.blueprint_id);
            target.ticks_per_unit = identity.value("ticksPerUnit", std::int64_t{1000});
            target.model_version = std::to_string(ver.version) + ".0.0-preview";
            target.monitors = cache / "monitors.json";
            target.source_models = true;
            auto built = services_.build_package_into(target, bindings.value(), package_dir);
            if (!built) {
                return Error(built.error()).with("hint", "The preview builds the verified core with the release builder; fix the "
                                                         "reported problem (Assurance → Alignment / Verification).");
            }
            core = built.value();
            // Summarise the builder's integrity checks (all must pass for a package to exist).
            Json failed = Json::array();
            for (const Json& c : core.value("checks", Json::array())) {
                if (!c.value("passed", false)) failed.push_back(c);
            }
            core["checksPassed"] = static_cast<std::int64_t>(core.value("checks", Json::array()).size() - failed.size());
            core["checksFailed"] = failed;
            core.erase("checks");
        } else {
            core = Json::object();
        }
        core["source"] = "sandbox build of the pinned artefacts (not recorded)";
        core["cacheKey"] = key.value().substr(0, 24);
    }

    // 2. Simulator inputs generated from this version's document (as in its deployment bundle).
    const fs::path sandbox = root / pid;
    const Json sim = ver.document.value("simulation", Json::object());
    LaunchPlan plan;
    plan.instance_id = pid;
    plan.kind = "preview";
    plan.bridge = false;
    plan.mode = identity.value("runtimeMode", std::string("monitor"));
    plan.package_dir = package_dir;
    plan.work_dir = sandbox;
    plan.package_store = {package_dir.parent_path()};
    plan.paused = body.value("paused", true);
    {
        const Json s = body.value("speed", Json("1"));
        plan.speed = s.is_number() ? s.get<double>() : std::strtod(s.get<std::string>().c_str(), nullptr);
        if (plan.speed <= 0) plan.speed = 1.0;
    }
    std::string simulator = "none";
    if (plan.mode == "cosimulation") {
        if (sim.value("kind", std::string()) != "mobile-robot") {
            return make_error(ErrorCode::StateError, "co-simulation needs the mobile-robot simulator (Build → World & Layout, simulation)");
        }
        auto world = scene::world_from_json(ver.document.value("world", Json::object()));
        if (!world) return std::move(world).error();
        auto scenario = scene::simulator_scenario(world.value(), sim, identity.value("name", ver.blueprint_id), "Studio preview");
        if (!scenario) return std::move(scenario).error();
        if (auto w = write_text(sandbox / "simulation" / "world-scenario.json", scenario.value().dump(1)); !w) return w.error();
        plan.world_scenario = sandbox / "simulation" / "world-scenario.json";
        simulator = "mobile-robot";
    } else if (sim.value("kind", std::string()) == "event-script" && body.value("simulator", true)) {
        // {"simulator": false}: no event script — the engineer drives the preview (events, scenarios).
        if (auto w = write_text(sandbox / "simulation" / "event-script.json", sim.value("script", Json::object()).dump(1)); !w) return w.error();
        plan.feed = sandbox / "simulation" / "event-script.json";
        simulator = "event-script";
    }

    // 3. Launch in isolation.
    auto started = supervisor_->start(plan);
    {
        auto l = impl_->core.lock();
        impl_->core.record("blueprint.preview.start", started ? "success" : "fail", ver.blueprint_id + "@" + std::to_string(v),
                           {{"preview", pid}, {"error", started ? Json(nullptr) : Json(started.error().message)}}, actor, "blueprint");
    }
    if (!started) return std::move(started).error();
    return Json{{"previewId", pid},
                {"banner", "STUDIO PREVIEW"},
                {"mode", plan.mode},
                {"simulator", simulator},
                {"core", core},
                {"runtime", started.value()},
                {"isolation", "No twin record, no deployment, no stored telemetry; ledgers and logs live in the preview's own directory."}};
}

Result<Json> BlueprintService::preview(std::string_view id, std::int64_t v) {
    if (auto ver = impl_->load(id, v); !ver) return std::move(ver).error();
    const std::string pid = preview_id(id, v);
    Json status = supervisor_ != nullptr ? supervisor_->status(pid) : Json{{"state", "not_started"}};
    return Json{{"previewId", pid}, {"banner", "STUDIO PREVIEW"}, {"runtime", status}};
}

Result<Json> BlueprintService::stop_preview(std::string_view id, std::int64_t v, const Actor& actor) {
    if (auto ver = impl_->load(id, v); !ver) return std::move(ver).error();
    const std::string pid = preview_id(id, v);
    if (supervisor_ != nullptr) supervisor_->stop(pid, "preview stopped by " + actor.name);
    return preview(id, v);
}

}  // namespace twin::studio
