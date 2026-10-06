/**
 * @file seed.cpp
 * @brief Example import and telemetry synthesis (see seed.hpp).
 */
#include "twin/studio/seed.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <numbers>
#include <sstream>
#include <thread>

#include "blueprints_impl.hpp"
#include "twin/authoring/import.hpp"
#include "twin/core/large_stack.hpp"
#include "twin/studio/blueprints.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using namespace twin::platform;

namespace {

Result<std::string> read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return make_error(ErrorCode::IoError, "cannot read " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Result<json::Json> read_manifest(const fs::path& dir) {
    auto text = read_text(dir / "example.json");
    if (!text) return std::move(text).error();
    return json::parse(text.value());
}

std::optional<std::string> opt_string(const json::Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

const std::map<std::string, ArtifactKind>& role_kinds() {
    static const std::map<std::string, ArtifactKind> kRoles = {
        {"ontology", ArtifactKind::Ontology},
        {"pt_interpretation", ArtifactKind::Interpretation},
        {"dt_interpretation", ArtifactKind::Interpretation},
        {"pt_model", ArtifactKind::PtModel},
        {"dt_model", ArtifactKind::DtModel},
    };
    return kRoles;
}

}  // namespace

// --- ProfileGenerator ---------------------------------------------------------------

ProfileGenerator::ProfileGenerator(json::Json profile, std::string value_type, std::int64_t anchor_ms, std::uint32_t seed)
    : profile_(std::move(profile)), type_(std::move(value_type)), anchor_ms_(anchor_ms), rng_(seed) {}

double ProfileGenerator::baseline(std::int64_t t_ms) {
    const double base = profile_.value("base", 0.0);
    const double amp = profile_.value("dailyAmplitude", 0.0);
    const double noise = profile_.value("noise", 0.0);
    const double hour = std::fmod(static_cast<double>(t_ms) / 3.6e6, 24.0);
    return base + amp * std::sin(2.0 * std::numbers::pi * (hour - 9.0) / 24.0) + noise * normal_(rng_);
}

TelemetrySample ProfileGenerator::make(std::int64_t t_ms, double v) {
    TelemetrySample s;
    s.observed_ms = t_ms;
    s.ingested_ms = t_ms + 40 + static_cast<std::int64_t>(uniform_(rng_) * 160.0);  // transport latency
    if (profile_.contains("min")) v = std::max(v, profile_["min"].get<double>());
    if (profile_.contains("max")) v = std::min(v, profile_["max"].get<double>());
    if (type_ == "boolean") {
        s.text = v >= 0.5 ? "true" : "false";
    } else {
        s.number = std::round(v * 1000.0) / 1000.0;
    }
    s.quality = uniform_(rng_) < 0.002 ? "uncertain" : "good";
    return s;
}

std::optional<TelemetrySample> ProfileGenerator::sample(std::int64_t t_ms) {
    double v = baseline(t_ms);
    if (type_ == "boolean") v = profile_.value("base", 0.0);
    for (const auto& ep : profile_.value("episodes", json::Json::array())) {
        const auto start = anchor_ms_ - static_cast<std::int64_t>(ep.value("startHoursAgo", 0.0) * 3.6e6);
        const auto end = start + static_cast<std::int64_t>(ep.value("durationMinutes", 0.0) * 6e4);
        if (t_ms < start || t_ms >= end) continue;
        if (ep.value("gap", false)) return std::nullopt;
        const double ramp_ms = ep.value("rampMinutes", 0.0) * 6e4;
        double w = 1.0;
        if (ramp_ms > 0) {
            w = std::min({1.0, static_cast<double>(t_ms - start) / ramp_ms, static_cast<double>(end - t_ms) / ramp_ms});
        }
        const double value = ep.value("value", 0.0);
        if (ep.value("mode", std::string("offset")) == "set") {
            v = v + (value - v) * w;
        } else {
            v += value * w;
        }
    }
    return make(t_ms, v);
}

TelemetrySample ProfileGenerator::live_sample(std::int64_t t_ms) {
    const double v = type_ == "boolean" ? profile_.value("base", 0.0) : baseline(t_ms);
    return make(t_ms, v);
}

// --- seeding --------------------------------------------------------------------------

namespace {

/// Formal artefacts of an example: model files are imported into the canonical twin-ta/1 form
/// (unsupported constructs abort the seed); the diagram layout found in the file is returned.
Result<std::map<std::string, ArtifactRef>> seed_formal(Services& services, const fs::path& dir, const json::Json& formal,
                                                       const Actor& actor, json::Json& layouts) {
    std::map<std::string, ArtifactRef> pins;
    std::string ontology_ref;
    for (const std::string role : {"ontology", "pt_interpretation", "dt_interpretation", "pt_model", "dt_model"}) {
        if (!formal.contains(role)) continue;
        const json::Json& spec = formal.at(role);
        const std::string file = spec.at("file").get<std::string>();
        auto text = read_text(dir / file);
        if (!text) return std::move(text).error();
        std::string content = text.value();
        json::Json refs = json::Json::object();
        if (role_kinds().at(role) == ArtifactKind::Interpretation) {
            refs["ontology"] = ontology_ref;
            refs["twinRole"] = role == "pt_interpretation" ? "pt" : "dt";
        }
        if (role == "pt_model" || role == "dt_model") {
            authoring::ImportOptions options;
            options.filename = fs::path(file).filename().string();
            const authoring::ImportResult r = run_with_large_stack([&] { return authoring::import_any(content, options); });
            if (!r.model) {
                Error e = make_error(ErrorCode::UnsupportedConstruct, "example model does not import").with("file", file);
                for (const auto& d : r.diagnostics) e.with(d.code, d.message);
                return e;
            }
            content = json::canonical_dump(authoring::to_json(*r.model)).value();
            if (!r.layout.locations.empty()) layouts[role == "pt_model" ? "pt" : "dt"] = authoring::to_json(r.layout);
        }
        const std::string id = spec.at("id").get<std::string>();
        auto created = services.create_artifact(role_kinds().at(role), id, spec.value("name", id), spec.value("description", std::string()),
                                                content, refs, actor);
        if (!created) return std::move(created).error();
        const ArtifactRef ref{id, 1};
        auto validated = services.validate(ref, actor);
        if (!validated) return std::move(validated).error();
        if (validated.value()["state"] != "verified") {
            return make_error(ErrorCode::ValidationError, "example artefact does not validate").with("ref", ref.str());
        }
        auto published = services.publish(ref, actor);
        if (!published) return std::move(published).error();
        if (role == "ontology") ontology_ref = ref.str();
        pins[role] = ref;
    }
    return pins;
}

Status write_history(Services& services, const json::Json& history, const json::Json& profiles, const std::string& instance,
                     std::int64_t& samples_written) {
    const std::int64_t now = services.clock().now_ms();
    const std::int64_t days = history.value("days", std::int64_t{7});
    const std::int64_t period = history.value("periodSeconds", std::int64_t{30}) * 1000;
    const auto seed = history.value("seed", std::uint32_t{1});
    std::uint32_t channel_index = 0;
    auto channels = services.telemetry_channels_all();
    if (!channels) return channels.error();
    for (const auto& [name, profile] : profiles.items()) {
        const std::string channel_id = instance + "." + name;
        auto it = std::find_if(channels.value().begin(), channels.value().end(), [&](const TelemetryChannel& c) { return c.id == channel_id; });
        if (it == channels.value().end()) continue;
        ProfileGenerator gen(profile, it->value_type, now, seed + channel_index++);
        std::vector<TelemetrySample> batch;
        const std::int64_t start = ((now - days * 86400000) / period) * period;
        for (std::int64_t t = start; t <= now; t += period) {
            if (auto smp = gen.sample(t)) batch.push_back(*smp);
            if (batch.size() >= 5000) {
                auto n = services.ingest_samples(channel_id, batch);
                if (!n) return n.error();
                samples_written += n.value();
                batch.clear();
            }
        }
        auto n = services.ingest_samples(channel_id, batch);
        if (!n) return n.error();
        samples_written += n.value();
    }
    return {};
}

}  // namespace

Result<json::Json> seed_example(Services& services, const std::filesystem::path& example_dir, const SeedOptions& options) {
    auto manifest = read_manifest(example_dir);
    if (!manifest) return std::move(manifest).error();
    const json::Json& m = manifest.value();
    if (m.value("format", std::string()) != "twin-example/2") {
        return make_error(ErrorCode::InvalidArgument, "examples use format twin-example/2 (a Blueprint plus instances)")
            .with("example", example_dir.string());
    }
    const json::Json bp = m.at("blueprint");
    const std::string bp_id = bp.at("id").get<std::string>();
    BlueprintService& blueprints = services.blueprints();
    if (blueprints.get(bp_id)) return make_error(ErrorCode::StateError, "example already seeded").with("blueprint", bp_id);
    const Actor& actor = options.actor;
    services.app_log().write(LogLevel::Info, "studio.seed", "seeding example", {{"example", m.value("id", std::string())}});

    // 1. The estate: site assets that exist independently of any Blueprint.
    for (const auto& a : m.value("estate", json::Json::object()).value("assets", json::Json::array())) {
        Asset asset;
        asset.id = a.at("id").get<std::string>();
        asset.name = a.at("name").get<std::string>();
        asset.type = a.at("type").get<std::string>();
        asset.parent_id = opt_string(a, "parentId");
        asset.description = a.value("description", std::string());
        asset.tags = a.value("tags", json::Json::array());
        asset.properties = a.value("properties", json::Json::object());
        if (auto st = services.upsert_asset(asset); !st) return st.error();
    }

    // 2. Formal artefacts (imported, validated and published by the real tools).
    json::Json layouts = json::Json::object();
    auto pins = seed_formal(services, example_dir, bp.at("formal"), actor, layouts);
    if (!pins) return std::move(pins).error();

    // 3. The Blueprint document over those artefacts.
    auto doc_text = read_text(example_dir / bp.at("document").get<std::string>());
    if (!doc_text) return std::move(doc_text).error();
    auto doc = json::parse(doc_text.value());
    if (!doc) return std::move(doc).error();
    auto resolved = resolve_includes(std::move(doc).value(), example_dir);
    if (!resolved) return std::move(resolved).error();
    json::Json document = std::move(resolved).value();
    for (const char* role : {"pt", "dt"}) {
        if (layouts.contains(role) && document["behavior"][role].value("layout", json::Json()).is_null()) {
            document["behavior"][role]["layout"] = layouts[role];
        }
    }
    json::Json pin_json = json::Json::object();
    for (const auto& [role, ref] : pins.value()) pin_json[role] = ref.str();
    auto created = blueprints.create({{"mode", "document"},
                                      {"id", bp_id},
                                      {"name", bp.value("name", bp_id)},
                                      {"description", bp.value("description", std::string())},
                                      {"domain", bp.value("domain", std::string("generic"))},
                                      {"icon", bp.value("icon", std::string("boxes"))},
                                      {"document", document},
                                      {"pins", pin_json}},
                                     actor);
    if (!created) return std::move(created).error();

    // 4. Verification, tests, packaging and release through the ordinary checks.
    json::Json checks = json::Json::object();
    for (const char* check : {"alignment", "compile", "scenarios", "package"}) {
        auto r = blueprints.run_check(bp_id, 1, check, json::Json::object(), actor);
        if (!r) return std::move(r).error().with("check", check);
        checks[check] = r.value().value("outcome", r.value().value("built", false) ? std::string("pass") : std::string("fail"));
        if (std::string(check) == "scenarios" && r.value().value("outcome", std::string()) != "pass") {
            Error e = make_error(ErrorCode::ValidationError, "an example scenario test fails");
            for (const auto& res : r.value()["results"]) {
                for (const auto& st : res["steps"]) {
                    const std::string status = st.value("status", std::string());
                    if (status == "ok" || status == "pass") continue;
                    e.with(res.value("id", std::string()) + "#" + st.value("id", std::string()), st.dump());
                }
            }
            return e;
        }
    }
    auto published = blueprints.publish(bp_id, 1, actor);
    if (!published) return std::move(published).error();

    // 5. Instances: assets, channels, twin record and the deployment record. Processes are started
    //    by the deployment supervisor when Studio serves (desired state "running").
    json::Json instances = json::Json::array();
    std::int64_t samples_written = 0;
    for (const auto& inst : m.value("instances", json::Json::array())) {
        json::Json body = inst;
        body["blueprintId"] = bp_id;
        body["version"] = 1;
        auto made = blueprints.create_instance(body, actor);
        if (!made) return std::move(made).error();
        const std::string id = inst.at("id").get<std::string>();
        for (const auto& r : inst.value("estateRelationships", json::Json::array())) {
            (void)services.relate(r.at(0).get<std::string>(), r.at(1).get<std::string>(), r.at(2).get<std::string>());
        }
        if (inst.value("deploy", false)) {
            auto ver = blueprints.version(bp_id, 1);
            if (!ver) return std::move(ver).error();
            auto d = services.deploy(id, ver.value()["packageId"].get<std::string>(), "initial deployment", actor);
            if (!d) return std::move(d).error();
            auto t = services.twin_record(id);
            if (t) {
                auto rec = t.value();
                rec.desired_state = "running";
                (void)services.upsert_twin(rec);
            }
        }
        if (options.telemetry_history && inst.contains("profiles")) {
            if (auto st = write_history(services, inst.value("history", json::Json::object()), inst.at("profiles"), id, samples_written); !st) {
                return st.error();
            }
        }
        instances.push_back(id);
    }

    // 6. Maintenance history (e.g. a rejected ontology change with real refinement evidence).
    json::Json history = json::Json::array();
    for (const auto& h : m.value("maintenanceHistory", json::Json::array())) {
        if (h.value("kind", std::string()) != "rejected-ontology-change" || instances.empty()) continue;
        const std::string twin_id = instances.front().get<std::string>();
        auto change = services.create_change(twin_id, h.at("title").get<std::string>(), h.value("description", std::string()), actor);
        if (!change) return std::move(change).error();
        const std::string change_id = change.value()["id"].get<std::string>();
        const std::string ont_id = bp.at("formal").at("ontology").at("id").get<std::string>();
        auto draft = services.add_to_change(change_id, ont_id, h.at("title").get<std::string>(), actor);
        if (!draft) return std::move(draft).error();
        const ArtifactRef dref{ont_id, draft.value()["version"].get<std::int64_t>()};
        auto text = read_text(example_dir / h.at("file").get<std::string>());
        if (!text) return std::move(text).error();
        if (auto sv = services.save_draft(dref, text.value(), std::nullopt, std::nullopt, actor); !sv) return std::move(sv).error();
        if (auto v = services.validate(dref, actor); !v) return std::move(v).error();
        auto refinement = services.run_stage(change_id, "refinement", actor);
        if (!refinement) return std::move(refinement).error();
        auto abandoned = services.abandon_change(change_id, h.value("description", std::string("rejected")), actor);
        if (!abandoned) return std::move(abandoned).error();
        // The rejected proposal must not stay open: the Blueprint's next draft derives from the published ontology.
        (void)services.reject(dref, h.value("description", std::string("rejected")), actor);
        history.push_back({{"changeId", change_id}, {"refinementEvidence", refinement.value()["id"]}, {"verdict", refinement.value()["verdict"]}});
    }
    services.app_log().write(LogLevel::Info, "studio.seed", "example seeded",
                             {{"blueprint", bp_id}, {"instances", instances}, {"telemetrySamples", samples_written}});
    return json::Json{{"example", m.value("id", std::string())},
                      {"blueprint", bp_id},
                      {"instances", instances},
                      {"checks", checks},
                      {"maintenanceHistory", history},
                      {"telemetrySamples", samples_written}};
}

void run_demo_feed(Services& services, const std::filesystem::path& example_dir, const std::atomic<bool>& stop) {
    auto manifest = read_manifest(example_dir);
    if (!manifest) {
        services.app_log().write(LogLevel::Error, "studio.demo-feed", "cannot read example manifest",
                                 {{"error", manifest.error().to_string()}});
        return;
    }
    struct Feed {
        std::string channel;
        std::int64_t period;
        std::int64_t next;
        ProfileGenerator gen;
    };
    std::vector<Feed> feeds;
    const std::int64_t now = services.clock().now_ms();
    std::uint32_t i = 0;
    for (const auto& c : manifest.value().value("telemetry", json::Json::array())) {
        // Channels fed by a runtime are never simulated: live data must have exactly one source.
        if (c.value("source", std::string()).rfind("runtime:", 0) == 0 || !c.contains("profile")) continue;
        const std::int64_t period = c.value("expectedPeriodMs", std::int64_t{5000});
        feeds.push_back({c.at("id").get<std::string>(), period, now,
                         ProfileGenerator(c.value("profile", json::Json::object()), c.at("valueType").get<std::string>(), now,
                                          9000 + i++)});
    }
    services.app_log().write(LogLevel::Info, "studio.demo-feed", "simulated telemetry feed started",
                             {{"channels", feeds.size()}});
    while (!stop.load()) {
        const std::int64_t t = services.clock().now_ms();
        for (auto& f : feeds) {
            if (t < f.next) continue;
            f.next = t + f.period;
            auto s = f.gen.live_sample(t);
            s.ingested_ms = t;
            if (auto n = services.ingest_samples(f.channel, {s}); !n) {
                services.app_log().write(LogLevel::Warn, "studio.demo-feed", "ingest failed",
                                         {{"channel", f.channel}, {"error", n.error().message}});
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

}  // namespace twin::studio
