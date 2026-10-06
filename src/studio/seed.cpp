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

Result<json::Json> seed_example(Services& services, const std::filesystem::path& example_dir, const SeedOptions& options) {
    auto manifest = read_manifest(example_dir);
    if (!manifest) return std::move(manifest).error();
    const json::Json& m = manifest.value();
    const json::Json& tw = m.at("twin");
    const std::string twin_id = tw.at("id").get<std::string>();
    if (services.twin_record(twin_id)) {
        return make_error(ErrorCode::StateError, "example already seeded").with("twin", twin_id);
    }
    const Actor& actor = options.actor;
    services.app_log().write(LogLevel::Info, "studio.seed", "seeding example", {{"example", m.value("id", std::string())}});

    // 1. Assets and relationships (the operational knowledge graph).
    for (const auto& a : m.value("assets", json::Json::array())) {
        Asset asset;
        asset.id = a.at("id").get<std::string>();
        asset.name = a.at("name").get<std::string>();
        asset.type = a.at("type").get<std::string>();
        asset.parent_id = opt_string(a, "parentId");
        asset.description = a.value("description", std::string());
        asset.tags = a.value("tags", json::Json::array());
        asset.properties = a.value("properties", json::Json::object());
        asset.twin_id = opt_string(a, "twinId");
        if (auto st = services.upsert_asset(asset); !st) return st.error();
    }
    for (const auto& r : m.value("relationships", json::Json::array())) {
        if (auto st = services.relate(r.at(0).get<std::string>(), r.at(1).get<std::string>(), r.at(2).get<std::string>()); !st) {
            return st.error();
        }
    }
    Twin twin;
    twin.id = twin_id;
    twin.name = tw.at("name").get<std::string>();
    twin.asset_id = opt_string(tw, "assetId");
    twin.description = tw.value("description", std::string());
    twin.model_id = tw.at("modelId").get<std::string>();
    twin.ticks_per_unit = tw.value("ticksPerUnit", std::int64_t{1000});
    twin.presentation = tw.value("presentation", json::Json::object());
    if (auto st = services.upsert_twin(twin); !st) return st.error();

    // 2. Formal artefacts: import, validate (real checks), publish.
    std::vector<std::pair<std::string, ArtifactRef>> roles;
    std::string ontology_ref;
    json::Json imported = json::Json::array();
    for (const std::string role : {"ontology", "pt_interpretation", "dt_interpretation", "pt_model", "dt_model"}) {
        const json::Json& spec = m.at("artifacts").at(role);
        auto text = read_text(example_dir / spec.at("file").get<std::string>());
        if (!text) return std::move(text).error();
        json::Json refs = json::Json::object();
        if (role_kinds().at(role) == ArtifactKind::Interpretation) {
            refs["ontology"] = ontology_ref;
            refs["twinRole"] = role == "pt_interpretation" ? "pt" : "dt";
        }
        const std::string id = spec.at("id").get<std::string>();
        auto created = services.create_artifact(role_kinds().at(role), id, spec.value("name", id),
                                                spec.value("description", std::string()), text.value(), refs, actor);
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
        roles.emplace_back(role, ref);
        imported.push_back(ref.str());
    }

    // 3. Initial deployment through the real package pipeline (compile + align + verify).
    auto bindings = services.make_bindings(roles);
    if (!bindings) return std::move(bindings).error();
    auto boot = services.bootstrap_twin(twin_id, bindings.value(), actor);
    if (!boot) return std::move(boot).error();

    // 4. Maintenance history (e.g. a rejected ontology change with real refinement evidence).
    json::Json history = json::Json::array();
    for (const auto& h : m.value("maintenanceHistory", json::Json::array())) {
        if (h.value("kind", std::string()) != "rejected-ontology-change") continue;
        auto change = services.create_change(twin_id, h.at("title").get<std::string>(), h.value("description", std::string()), actor);
        if (!change) return std::move(change).error();
        const std::string change_id = change.value()["id"].get<std::string>();
        const std::string ont_id = m.at("artifacts").at("ontology").at("id").get<std::string>();
        auto draft = services.add_to_change(change_id, ont_id, h.at("title").get<std::string>(), actor);
        if (!draft) return std::move(draft).error();
        const ArtifactRef dref{ont_id, draft.value()["version"].get<std::int64_t>()};
        auto text = read_text(example_dir / h.at("file").get<std::string>());
        if (!text) return std::move(text).error();
        if (auto s = services.save_draft(dref, text.value(), std::nullopt, std::nullopt, actor); !s) return std::move(s).error();
        if (auto v = services.validate(dref, actor); !v) return std::move(v).error();
        auto refinement = services.run_stage(change_id, "refinement", actor);
        if (!refinement) return std::move(refinement).error();
        auto abandoned = services.abandon_change(change_id, h.value("description", std::string("rejected")), actor);
        if (!abandoned) return std::move(abandoned).error();
        history.push_back({{"changeId", change_id}, {"refinementEvidence", refinement.value()["id"]},
                           {"verdict", refinement.value()["verdict"]}});
    }

    // 5. Telemetry channels and synthetic history.
    std::int64_t samples_written = 0;
    const json::Json hist = m.value("history", json::Json::object());
    const std::int64_t now = services.clock().now_ms();
    const std::int64_t days = hist.value("days", std::int64_t{7});
    const std::int64_t period = hist.value("periodSeconds", std::int64_t{30}) * 1000;
    const auto seed = hist.value("seed", std::uint32_t{1});
    std::uint32_t channel_index = 0;
    for (const auto& c : m.value("telemetry", json::Json::array())) {
        TelemetryChannel ch;
        ch.id = c.at("id").get<std::string>();
        ch.asset_id = c.at("assetId").get<std::string>();
        ch.name = c.at("name").get<std::string>();
        ch.value_type = c.at("valueType").get<std::string>();
        ch.unit = c.value("unit", std::string());
        ch.ontology_symbol = opt_string(c, "ontologySymbol");
        ch.source = c.value("source", std::string());
        ch.expected_period_ms = c.value("expectedPeriodMs", std::int64_t{1000});
        ch.presentation = c.value("presentation", json::Json::object());
        if (auto st = services.upsert_channel(ch); !st) return st.error();
        // Live-only channels (no profile, e.g. fed by a runtime) get no synthetic history.
        if (!options.telemetry_history || !c.contains("profile")) continue;
        ProfileGenerator gen(c.value("profile", json::Json::object()), ch.value_type, now, seed + channel_index++);
        std::vector<TelemetrySample> batch;
        // Align sample times to the period grid so all channels share timestamps.
        const std::int64_t start = ((now - days * 86400000) / period) * period;
        for (std::int64_t t = start; t <= now; t += period) {
            if (auto s = gen.sample(t)) batch.push_back(*s);
            if (batch.size() >= 5000) {
                auto n = services.ingest_samples(ch.id, batch);
                if (!n) return std::move(n).error();
                samples_written += n.value();
                batch.clear();
            }
        }
        auto n = services.ingest_samples(ch.id, batch);
        if (!n) return std::move(n).error();
        samples_written += n.value();
    }
    services.app_log().write(LogLevel::Info, "studio.seed", "example seeded",
                             {{"twin", twin_id}, {"telemetrySamples", samples_written}});
    return json::Json{{"example", m.value("id", std::string())},
                      {"twin", twin_id},
                      {"artifacts", imported},
                      {"bootstrap", boot.value()},
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
