/**
 * @file services_ops.cpp
 * @brief Services: assets and knowledge graph, telemetry, overview, global search, audit and logs.
 */
#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <regex>

#include "services_impl.hpp"
#include "twin/ontology/source.hpp"

namespace twin::studio {

using namespace twin::platform;
namespace onto = twin::ontology;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains_ci(std::string_view hay, const std::string& needle_lower) {
    return lower(hay).find(needle_lower) != std::string::npos;
}

std::string route_for_version(ArtifactKind kind, const ArtifactRef& r) {
    switch (kind) {
        case ArtifactKind::Ontology:
            return "/engineering/ontologies/" + r.artifact_id + "/versions/" + std::to_string(r.version);
        case ArtifactKind::Interpretation:
            return "/engineering/interpretations/" + r.artifact_id + "/versions/" + std::to_string(r.version);
        case ArtifactKind::PtModel:
        case ArtifactKind::DtModel:
            return "/engineering/models/" + r.artifact_id + "/versions/" + std::to_string(r.version);
    }
    return "/engineering";
}

std::string route_for_artifact(ArtifactKind kind, const std::string& id) {
    switch (kind) {
        case ArtifactKind::Ontology: return "/engineering/ontologies/" + id;
        case ArtifactKind::Interpretation: return "/engineering/interpretations/" + id;
        case ArtifactKind::PtModel:
        case ArtifactKind::DtModel: return "/engineering/models/" + id;
    }
    return "/engineering";
}

}  // namespace

// --- assets & graph -------------------------------------------------------------------

Result<json::Json> Services::assets(const AssetFilter& filter) {
    auto l = impl_->lock();
    auto list = impl_->assets->list(filter);
    if (!list) return std::move(list).error();
    auto total = impl_->assets->count(filter);
    if (!total) return std::move(total).error();
    json::Json items = json::Json::array();
    for (const auto& a : list.value()) {
        json::Json j = to_json(a);
        auto n = impl_->assets->child_count(a.id);
        j["childCount"] = n ? n.value() : 0;
        items.push_back(j);
    }
    return json::Json{{"items", items}, {"total", total.value()}, {"limit", filter.limit}, {"offset", filter.offset}};
}

Result<json::Json> Services::create_asset(const json::Json& body, const Actor& actor) {
    Asset a;
    a.id = body.value("id", std::string());
    a.name = body.value("name", std::string());
    a.type = body.value("type", std::string());
    a.description = body.value("description", std::string());
    if (body.contains("tags") && body["tags"].is_array()) a.tags = body["tags"];
    if (body.contains("properties") && body["properties"].is_object()) a.properties = body["properties"];
    if (body.contains("parentId") && body["parentId"].is_string() && !body["parentId"].get<std::string>().empty()) {
        a.parent_id = body["parentId"].get<std::string>();
    }
    static const std::regex id_re("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$");
    if (!std::regex_match(a.id, id_re)) {
        return make_error(ErrorCode::InvalidArgument, "asset id must be 1-64 characters: letters, digits, '.', '_' or '-'")
            .with("id", a.id);
    }
    if (a.name.empty() || a.type.empty()) return make_error(ErrorCode::InvalidArgument, "an asset needs a name and a type");
    {
        auto l = impl_->lock();
        if (impl_->assets->get(a.id)) return make_error(ErrorCode::StateError, "an asset with this id already exists").with("id", a.id);
        if (a.parent_id && !impl_->assets->get(*a.parent_id)) {
            return make_error(ErrorCode::NotFound, "the parent asset does not exist").with("parentId", *a.parent_id);
        }
        if (auto st = impl_->assets->upsert(a); !st) return st.error();
        json::Json details = {{"name", a.name}, {"type", a.type}, {"parentId", a.parent_id ? json::Json(*a.parent_id) : json::Json(nullptr)}};
        if (body.contains("clonedFrom") && body["clonedFrom"].is_string()) details["clonedFrom"] = body["clonedFrom"];
        impl_->record("asset.create", "success", a.id, details, actor, "asset");
    }
    return asset(a.id);
}

Result<json::Json> Services::link_assets(std::string_view source, std::string_view type, std::string_view target,
                                         const Actor& actor) {
    if (type.empty() || type == "contains") {
        return make_error(ErrorCode::InvalidArgument, "choose a relationship type other than 'contains' (hierarchy is set by the parent)");
    }
    if (source == target) return make_error(ErrorCode::InvalidArgument, "an asset cannot be linked to itself");
    {
        auto l = impl_->lock();
        if (!impl_->assets->get(source)) return make_error(ErrorCode::NotFound, "no such asset").with("id", std::string(source));
        if (!impl_->assets->get(target)) return make_error(ErrorCode::NotFound, "no such asset").with("id", std::string(target));
        auto r = impl_->assets->relate(source, type, target);
        if (!r) return std::move(r).error();
        impl_->record("asset.link", "success", std::string(source),
                      {{"type", std::string(type)}, {"target", std::string(target)}}, actor, "asset");
    }
    return asset(source);
}

Result<json::Json> Services::asset(std::string_view id) {
    auto l = impl_->lock();
    auto a = impl_->assets->get(id);
    if (!a) return std::move(a).error();
    json::Json j = to_json(a.value());
    auto crumbs = impl_->assets->ancestors(id);
    if (!crumbs) return std::move(crumbs).error();
    j["ancestors"] = json::Json::array();
    for (const auto& c : crumbs.value()) j["ancestors"].push_back({{"id", c.id}, {"name", c.name}, {"type", c.type}});
    AssetFilter children;
    children.parent_id = std::string(id);
    auto kids = impl_->assets->list(children);
    if (!kids) return std::move(kids).error();
    j["children"] = json::Json::array();
    for (const auto& k : kids.value()) j["children"].push_back({{"id", k.id}, {"name", k.name}, {"type", k.type}, {"twinId", k.twin_id ? json::Json(*k.twin_id) : json::Json(nullptr)}});
    auto rels = impl_->assets->relationships_of(id);
    if (!rels) return std::move(rels).error();
    j["relationships"] = json::Json::array();
    for (const auto& r : rels.value()) {
        json::Json rj = to_json(r);
        const std::string other = r.source_id == id ? r.target_id : r.source_id;
        auto o = impl_->assets->get(other);
        rj["direction"] = r.source_id == id ? "outgoing" : "incoming";
        rj["other"] = o ? json::Json{{"id", o.value().id}, {"name", o.value().name}, {"type", o.value().type}} : json::Json(nullptr);
        j["relationships"].push_back(rj);
    }
    // The twin bound to this asset or, failing that, to the nearest ancestor.
    j["twin"] = nullptr;
    std::optional<std::string> twin_id = a.value().twin_id;
    j["twinInherited"] = false;
    if (!twin_id) {
        for (auto it = crumbs.value().rbegin(); it != crumbs.value().rend(); ++it) {
            if (it->twin_id) {
                twin_id = it->twin_id;
                j["twinInherited"] = true;
                j["twinAssetId"] = it->id;
                break;
            }
        }
    }
    if (twin_id) {
        auto t = twin(*twin_id);
        if (t) j["twin"] = t.value();
    }
    return j;
}

Result<json::Json> Services::neighborhood(std::string_view id, int depth, const std::vector<std::string>& types,
                                          std::size_t max_nodes) {
    auto l = impl_->lock();
    auto n = impl_->assets->neighborhood(id, std::clamp(depth, 0, 4), types, std::clamp<std::size_t>(max_nodes, 1, 500));
    if (!n) return std::move(n).error();
    json::Json nodes = json::Json::array();
    for (const auto& a : n.value().nodes) {
        json::Json j = to_json(a);
        auto c = impl_->assets->child_count(a.id);
        j["childCount"] = c ? c.value() : 0;
        nodes.push_back(j);
    }
    json::Json edges = json::Json::array();
    for (const auto& e : n.value().edges) edges.push_back(to_json(e));
    return json::Json{{"focus", std::string(id)},
                      {"nodes", nodes},
                      {"edges", edges},
                      {"frontier", n.value().frontier},
                      {"truncated", n.value().truncated}};
}

Result<json::Json> Services::graph_facets() {
    auto l = impl_->lock();
    auto rt = impl_->assets->relationship_types();
    auto at = impl_->assets->asset_types();
    if (!rt) return std::move(rt).error();
    if (!at) return std::move(at).error();
    json::Json rel = json::Json::array({{{"type", "contains"}, {"count", nullptr}, {"hierarchy", true}}});
    for (const auto& [t, c] : rt.value()) rel.push_back({{"type", t}, {"count", c}, {"hierarchy", false}});
    json::Json types = json::Json::array();
    for (const auto& [t, c] : at.value()) types.push_back({{"type", t}, {"count", c}});
    AssetFilter roots;
    roots.parent_id = "";
    auto r = impl_->assets->list(roots);
    if (!r) return std::move(r).error();
    json::Json root_ids = json::Json::array();
    for (const auto& a : r.value()) root_ids.push_back({{"id", a.id}, {"name", a.name}, {"type", a.type}});
    return json::Json{{"relationshipTypes", rel}, {"assetTypes", types}, {"roots", root_ids}};
}

// --- telemetry ----------------------------------------------------------------------------

Result<json::Json> Services::telemetry_channels(std::string_view asset_id, bool include_descendants) {
    auto l = impl_->lock();
    if (auto a = impl_->assets->get(asset_id); !a) return std::move(a).error();
    std::vector<std::string> ids{std::string(asset_id)};
    if (include_descendants) {
        std::deque<std::string> queue{std::string(asset_id)};
        while (!queue.empty()) {
            AssetFilter f;
            f.parent_id = queue.front();
            f.limit = 1000;
            queue.pop_front();
            auto kids = impl_->assets->list(f);
            if (!kids) return std::move(kids).error();
            for (const auto& k : kids.value()) {
                ids.push_back(k.id);
                queue.push_back(k.id);
            }
        }
    }
    const std::int64_t now = clock_->now_ms();
    json::Json out = json::Json::array();
    for (const auto& id : ids) {
        auto chans = impl_->telemetry->channels(id);
        if (!chans) return std::move(chans).error();
        for (const auto& c : chans.value()) {
            json::Json j = to_json(c);
            auto latest = impl_->telemetry->latest(c.id);
            if (!latest) return std::move(latest).error();
            j["latest"] = latest.value() ? to_json(*latest.value()) : json::Json(nullptr);
            auto fr = impl_->telemetry->freshness(c, now);
            j["freshness"] = fr ? std::string(to_string(fr.value())) : "missing";
            j["ageMs"] = latest.value() ? json::Json(now - latest.value()->observed_ms) : json::Json(nullptr);
            out.push_back(j);
        }
    }
    return json::Json{{"assetId", std::string(asset_id)}, {"channels", out}, {"now", iso8601_utc(now)}};
}

Result<json::Json> Services::telemetry_series(std::string_view channel_id, std::int64_t from_ms, std::int64_t to_ms,
                                              std::int64_t max_points) {
    auto l = impl_->lock();
    auto ch = impl_->telemetry->channel(channel_id);
    if (!ch) return std::move(ch).error();
    auto s = impl_->telemetry->query(channel_id, from_ms, to_ms, std::clamp<std::int64_t>(max_points, 2, 5000));
    if (!s) return std::move(s).error();
    json::Json j = to_json(s.value());
    j["channel"] = to_json(ch.value());
    return j;
}

Result<json::Json> Services::ingest(const json::Json& samples) {
    if (!samples.is_array()) return make_error(ErrorCode::InvalidArgument, "expected an array of samples");
    std::map<std::string, std::vector<TelemetrySample>> by_channel;
    const std::int64_t now = clock_->now_ms();
    for (const auto& s : samples) {
        auto channel = json::get_string(s, "channel");
        if (!channel) return std::move(channel).error();
        TelemetrySample t;
        t.ingested_ms = now;
        if (s.contains("observedMs") && s["observedMs"].is_number_integer()) {
            t.observed_ms = s["observedMs"].get<std::int64_t>();
        } else {
            auto at = json::get_string(s, "observedAt");
            if (!at) return make_error(ErrorCode::InvalidArgument, "each sample needs observedAt (ISO 8601 UTC) or observedMs");
            auto ms = parse_iso8601_utc(at.value());
            if (!ms) return std::move(ms).error();
            t.observed_ms = ms.value();
        }
        if (!s.contains("value")) return make_error(ErrorCode::InvalidArgument, "each sample needs a value");
        const auto& v = s["value"];
        if (v.is_number()) t.number = v.get<double>();
        else if (v.is_boolean()) t.text = v.get<bool>() ? "true" : "false";
        else if (v.is_string()) t.text = v.get<std::string>();
        else return make_error(ErrorCode::InvalidArgument, "value must be a number, boolean or string");
        t.quality = s.value("quality", std::string("good"));
        by_channel[channel.value()].push_back(std::move(t));
    }
    std::int64_t total = 0;
    for (auto& [channel, list] : by_channel) {
        std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.observed_ms < b.observed_ms; });
        auto n = ingest_samples(channel, list);
        if (!n) return std::move(n).error();
        total += n.value();
    }
    return json::Json{{"ingested", total}, {"channels", by_channel.size()}};
}

Result<std::vector<TelemetryChannel>> Services::telemetry_channels_all() {
    auto l = impl_->lock();
    return impl_->telemetry->channels("");
}

// --- overview ------------------------------------------------------------------------------

Result<json::Json> Services::overview() {
    auto l = impl_->lock();
    json::Json j;
    AssetFilter all;
    all.limit = 100000;
    auto total = impl_->assets->count(all);
    auto types = impl_->assets->asset_types();
    if (!total) return std::move(total).error();
    if (!types) return std::move(types).error();
    json::Json by_type = json::Json::array();
    for (const auto& [t, c] : types.value()) by_type.push_back({{"type", t}, {"count", c}});
    j["assets"] = {{"total", total.value()}, {"byType", by_type}};

    // Telemetry freshness across all channels.
    auto chans = impl_->telemetry->channels("");
    if (!chans) return std::move(chans).error();
    std::map<std::string, int> fresh_counts{{"fresh", 0}, {"stale", 0}, {"missing", 0}, {"invalid", 0}};
    json::Json attention = json::Json::array();
    const std::int64_t now = clock_->now_ms();
    for (const auto& c : chans.value()) {
        auto f = impl_->telemetry->freshness(c, now);
        const std::string s = f ? std::string(to_string(f.value())) : "missing";
        ++fresh_counts[s];
        if (s != "fresh") {
            auto latest = impl_->telemetry->latest(c.id);
            attention.push_back({{"channelId", c.id},
                                 {"assetId", c.asset_id},
                                 {"label", c.presentation.value("label", c.name)},
                                 {"freshness", s},
                                 {"lastObservedAt", latest && latest.value() ? json::Json(iso8601_utc(latest.value()->observed_ms))
                                                                             : json::Json(nullptr)}});
        }
    }
    j["telemetry"] = {{"channels", chans.value().size()}, {"freshness", fresh_counts}, {"attention", attention}};

    // Twins, deployments, trust (all evidence-backed via twin()).
    auto twin_list = impl_->twins->twins();
    if (!twin_list) return std::move(twin_list).error();
    json::Json twins_json = json::Json::array();
    int trust_issues = 0;
    int connected = 0;
    for (const auto& t : twin_list.value()) {
        auto tj = twin(t.id);
        if (!tj) return std::move(tj).error();
        json::Json item = {{"id", t.id}, {"name", t.name}, {"assetId", t.asset_id ? json::Json(*t.asset_id) : json::Json(nullptr)},
                           {"deployment", tj.value()["deployment"]}, {"trust", tj.value()["trust"]},
                           {"runtimeConnected", t.runtime_url.has_value()}};
        if (t.runtime_url) ++connected;
        for (auto it = tj.value()["trust"].begin(); it != tj.value()["trust"].end(); ++it) {
            const auto s = it.value().value("state", std::string());
            if (s == "fail" || s == "error" || s == "stale" || s == "invalidated") ++trust_issues;
        }
        twins_json.push_back(item);
    }
    j["twins"] = twins_json;
    j["runtime"] = {{"connectedTwins", connected},
                    {"totalTwins", twin_list.value().size()},
                    {"note", connected == 0 ? "No twin-runtime is connected: live behavioural state, executions and "
                                              "conformance are unavailable."
                                            : ""}};
    j["verificationIssues"] = trust_issues;

    // Engineering activity.
    auto open_changes = impl_->twins->changes(std::string("open"));
    if (!open_changes) return std::move(open_changes).error();
    json::Json changes = json::Json::array();
    for (const auto& c : open_changes.value()) changes.push_back(to_json(c));
    int drafts = 0;
    auto arts = impl_->artifacts->list();
    if (!arts) return std::move(arts).error();
    for (const auto& a : arts.value()) {
        auto o = impl_->artifacts->open_version(a.id);
        if (o && o.value()) ++drafts;
    }
    j["engineering"] = {{"openChanges", changes}, {"openDrafts", drafts}};
    AuditFilter af;
    af.limit = 8;
    auto recent = impl_->audit->list(af);
    if (!recent) return std::move(recent).error();
    json::Json events = json::Json::array();
    for (const auto& r : recent.value()) events.push_back(to_json(r));
    j["recentEngineeringEvents"] = events;
    j["generatedAt"] = iso8601_utc(now);
    return j;
}

// --- search ---------------------------------------------------------------------------------

Result<json::Json> Services::search(std::string_view query, std::size_t limit) {
    const std::string q = lower(query);
    json::Json hits = json::Json::array();
    if (q.size() < 2) return json::Json{{"query", std::string(query)}, {"hits", hits}};
    auto l = impl_->lock();
    auto add = [&](std::string kind, std::string id, std::string title, std::string subtitle, std::string route) {
        if (hits.size() < limit) {
            hits.push_back({{"kind", std::move(kind)}, {"id", std::move(id)}, {"title", std::move(title)},
                            {"subtitle", std::move(subtitle)}, {"route", std::move(route)}});
        }
    };
    AssetFilter af;
    af.text = std::string(query);
    af.limit = static_cast<std::int64_t>(limit);
    if (auto a = impl_->assets->list(af); a) {
        for (const auto& x : a.value()) add("asset", x.id, x.name, x.type, "/assets/" + x.id);
    }
    if (auto t = impl_->twins->twins(); t) {
        for (const auto& x : t.value()) {
            if (contains_ci(x.id, q) || contains_ci(x.name, q) || contains_ci(x.model_id, q)) {
                add("twin", x.id, x.name, "Digital twin · model " + x.model_id,
                    x.asset_id ? "/assets/" + *x.asset_id : "/engineering/deployments");
            }
        }
    }
    if (auto arts = impl_->artifacts->list(); arts) {
        for (const auto& a : arts.value()) {
            if (contains_ci(a.id, q) || contains_ci(a.name, q)) {
                add(std::string(to_string(a.kind)), a.id, a.name, a.id, route_for_artifact(a.kind, a.id));
            }
            // Versions: "process-pump@2" style queries.
            if (q.find('@') != std::string::npos) {
                if (auto r = parse_ref(query); r && r.value().artifact_id == a.id) {
                    add(std::string(to_string(a.kind)) + "_version", r.value().str(), a.name + " v" + std::to_string(r.value().version),
                        r.value().str(), route_for_version(a.kind, r.value()));
                }
            }
            if (a.kind != ArtifactKind::Ontology) continue;
            // Ontology symbols of the published (or latest) version.
            auto pub = impl_->artifacts->published(a.id);
            std::optional<ArtifactVersion> v = pub && pub.value() ? pub.value() : std::nullopt;
            if (!v) {
                auto vs = impl_->artifacts->versions(a.id);
                if (vs && !vs.value().empty()) v = vs.value().front();
            }
            if (!v) continue;
            auto text = impl_->artifacts->content(v->ref());
            if (!text) continue;
            const auto src = onto::parse_ontology(text.value()).source;
            auto sym = [&](const std::string& name, const std::string& what) {
                if (contains_ci(name, q)) {
                    add("ontology_symbol", name, name, what + " in " + v->ref().str(),
                        route_for_version(a.kind, v->ref()) + "?symbol=" + name);
                }
            };
            for (const auto& s : src.sorts) sym(s.name, "sort");
            for (const auto& f : src.functions) sym(f.name, "function");
            for (const auto& r : src.relations) sym(r.name, "relation");
            for (const auto& ax : src.axioms) {
                if (contains_ci(ax.id, q)) {
                    add("ontology_axiom", ax.id, ax.id, "axiom in " + v->ref().str(),
                        route_for_version(a.kind, v->ref()) + "?axiom=" + ax.id);
                }
            }
        }
    }
    // Interpretation keys (propositions/events), e.g. "bearing_risk", "DEGRADED".
    if (auto interps = impl_->artifacts->list(ArtifactKind::Interpretation); interps) {
        for (const auto& a : interps.value()) {
            auto pub = impl_->artifacts->published(a.id);
            if (!pub || !pub.value()) continue;
            auto text = impl_->artifacts->content(pub.value()->ref());
            if (!text) continue;
            for (const auto& e : onto::parse_interpretation(text.value()).source.entries) {
                if (contains_ci(e.key, q)) {
                    add("interpretation_entry", e.key, e.key, (e.is_event ? "event in " : "location in ") + pub.value()->ref().str(),
                        route_for_version(ArtifactKind::Interpretation, pub.value()->ref()) + "?entry=" + e.key);
                }
            }
        }
    }
    if (auto chans = impl_->telemetry->channels(""); chans) {
        for (const auto& c : chans.value()) {
            const std::string label = c.presentation.value("label", c.name);
            if (contains_ci(c.id, q) || contains_ci(label, q)) {
                add("telemetry_channel", c.id, label, c.id + (c.unit.empty() ? "" : " · " + c.unit),
                    "/assets/" + c.asset_id + "/telemetry?channel=" + c.id);
            }
        }
    }
    // Identifiers of engineering records.
    const std::string upper = [&] {
        std::string u(query);
        std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return u;
    }();
    if (upper.rfind("EV-", 0) == 0) {
        if (auto e = impl_->evidence->get(upper); e) {
            add("evidence", e.value().id, e.value().id + " · " + std::string(to_string(e.value().kind)), e.value().summary,
                e.value().kind == EvidenceKind::Refinement ? "/maintenance/refinement/" + e.value().id
                                                           : "/engineering/verification/" + e.value().id);
        }
    }
    if (upper.rfind("PKG-", 0) == 0) {
        if (auto p = impl_->twins->package(upper); p) add("package", p.value().id, p.value().id, p.value().twin_id, "/engineering/packages/" + p.value().id);
    }
    if (upper.rfind("DEP-", 0) == 0) {
        if (auto d = impl_->twins->deployment(upper); d) {
            add("deployment", d.value().id, d.value().id, d.value().twin_id + " · " + d.value().package_id,
                "/engineering/deployments?twin=" + d.value().twin_id);
        }
    }
    if (auto cs = impl_->twins->changes(); cs) {
        for (const auto& c : cs.value()) {
            if (contains_ci(c.id, q) || contains_ci(c.title, q)) add("change", c.id, c.title, c.id + " · " + c.state, "/maintenance/changes/" + c.id);
        }
    }
    return json::Json{{"query", std::string(query)}, {"hits", hits}};
}

// --- audit & logs ---------------------------------------------------------------------------

Result<json::Json> Services::audit(const AuditFilter& filter) {
    auto l = impl_->lock();
    auto list = impl_->audit->list(filter);
    if (!list) return std::move(list).error();
    auto total = impl_->audit->count(filter);
    if (!total) return std::move(total).error();
    json::Json items = json::Json::array();
    for (const auto& r : list.value()) items.push_back(to_json(r));
    return json::Json{{"items", items}, {"total", total.value()}, {"limit", filter.limit}, {"offset", filter.offset}};
}

Result<json::Json> Services::verify_audit() {
    auto l = impl_->lock();
    auto v = impl_->audit->verify();
    if (!v) return std::move(v).error();
    return to_json(v.value());
}

Result<json::Json> Services::logs(const LogFilter& filter) {
    auto entries = log_->read(filter);
    if (!entries) return std::move(entries).error();
    json::Json items = json::Json::array();
    for (const auto& e : entries.value()) items.push_back(to_json(e));
    return json::Json{{"items", items}};
}

}  // namespace twin::studio
