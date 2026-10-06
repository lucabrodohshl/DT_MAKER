/**
 * @file twins.cpp
 * @brief Twins, packages, deployments, changes (see twins.hpp).
 */
#include "twin/platform/twins.hpp"

#include <algorithm>

namespace twin::platform {

namespace {

constexpr std::string_view kTwinColumns =
    "id, name, asset_id, description, model_id, ticks_per_unit, runtime_url, presentation, created_at, blueprint_id, "
    "blueprint_version, instance_config, world_url, desired_state";
constexpr std::string_view kPackageColumns =
    "id, twin_id, directory, package_hash, ir_sha256, model_version, bindings, evidence_id, state, created_at, "
    "created_by, released_at, change_id";
constexpr std::string_view kDeploymentColumns =
    "seq, id, twin_id, package_id, previous_package_id, kind, reason, deployed_at, deployed_by";
constexpr std::string_view kChangeColumns =
    "id, twin_id, title, description, state, artifacts, created_at, created_by, closed_at";

template <class T, class F>
Result<std::vector<T>> collect(Statement& q, F&& read) {
    std::vector<T> out;
    for (;;) {
        auto row = q.step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        auto item = read(q);
        if (!item) return std::move(item).error();
        out.push_back(std::move(item).value());
    }
    return out;
}

Result<Twin> read_twin(const Statement& s) {
    Twin t;
    t.id = s.text(0);
    t.name = s.text(1);
    t.asset_id = s.opt_text(2);
    t.description = s.text(3);
    t.model_id = s.text(4);
    t.ticks_per_unit = s.integer(5);
    t.runtime_url = s.opt_text(6);
    auto p = json::parse(s.text(7));
    if (!p) return std::move(p).error();
    t.presentation = std::move(p).value();
    t.created_at = s.text(8);
    t.blueprint_id = s.opt_text(9);
    if (!s.is_null(10)) t.blueprint_version = s.integer(10);
    auto c = json::parse(s.text(11).empty() ? std::string("{}") : s.text(11));
    if (!c) return std::move(c).error();
    t.instance_config = std::move(c).value();
    t.world_url = s.opt_text(12);
    t.desired_state = s.text(13);
    return t;
}

Result<PackageRecord> read_package(const Statement& s) {
    PackageRecord p;
    p.id = s.text(0);
    p.twin_id = s.text(1);
    p.directory = s.text(2);
    p.package_hash = s.text(3);
    p.ir_sha256 = s.text(4);
    p.model_version = s.text(5);
    auto b = json::parse(s.text(6));
    if (!b) return std::move(b).error();
    auto bindings = bindings_from_json(b.value());
    if (!bindings) return std::move(bindings).error();
    p.bindings = std::move(bindings).value();
    p.evidence_id = s.opt_text(7);
    p.state = s.text(8);
    p.created_at = s.text(9);
    p.created_by = s.text(10);
    p.released_at = s.opt_text(11);
    p.change_id = s.opt_text(12);
    return p;
}

Result<Deployment> read_deployment(const Statement& s) {
    return Deployment{s.integer(0), s.text(1), s.text(2), s.text(3), s.opt_text(4),
                      s.text(5),    s.text(6), s.text(7), s.text(8)};
}

Result<Change> read_change(const Statement& s) {
    Change c;
    c.id = s.text(0);
    c.twin_id = s.text(1);
    c.title = s.text(2);
    c.description = s.text(3);
    c.state = s.text(4);
    auto a = json::parse(s.text(5));
    if (!a) return std::move(a).error();
    for (const auto& r : a.value()) {
        if (!r.is_string()) return make_error(ErrorCode::ValidationError, "malformed change artifact list");
        auto ref = parse_ref(r.get<std::string>());
        if (!ref) return std::move(ref).error();
        c.artifacts.push_back(std::move(ref).value());
    }
    c.created_at = s.text(6);
    c.created_by = s.text(7);
    c.closed_at = s.opt_text(8);
    return c;
}

}  // namespace

std::string blueprint_owner(std::string_view blueprint_id) { return "blueprint:" + std::string(blueprint_id); }

const Binding* PackageRecord::binding(std::string_view role) const noexcept {
    for (const auto& b : bindings) {
        if (b.role == role) return &b;
    }
    return nullptr;
}

// --- twins ------------------------------------------------------------------------

Status TwinRepository::upsert_twin(const Twin& t) {
    if (t.id.empty() || t.name.empty() || t.model_id.empty()) {
        return make_error(ErrorCode::InvalidArgument, "twins need an id, a name and a model id");
    }
    if (t.desired_state != "running" && t.desired_state != "stopped") {
        return make_error(ErrorCode::InvalidArgument, "desired state must be 'running' or 'stopped'");
    }
    auto q = db_.prepare("INSERT INTO twins(" + std::string(kTwinColumns) +
                         ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14) ON CONFLICT(id) DO UPDATE "
                         "SET name = excluded.name, asset_id = excluded.asset_id, description = excluded.description, "
                         "model_id = excluded.model_id, ticks_per_unit = excluded.ticks_per_unit, runtime_url = "
                         "excluded.runtime_url, presentation = excluded.presentation, blueprint_id = "
                         "excluded.blueprint_id, blueprint_version = excluded.blueprint_version, instance_config = "
                         "excluded.instance_config, world_url = excluded.world_url, desired_state = excluded.desired_state");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, t.id)
        .bind(2, t.name)
        .bind(3, t.asset_id)
        .bind(4, t.description)
        .bind(5, t.model_id)
        .bind(6, t.ticks_per_unit)
        .bind(7, t.runtime_url)
        .bind(8, t.presentation.dump())
        .bind(9, t.created_at.empty() ? iso8601_utc(clock_.now_ms()) : t.created_at)
        .bind(10, t.blueprint_id)
        .bind(11, t.blueprint_version)
        .bind(12, t.instance_config.dump())
        .bind(13, t.world_url)
        .bind(14, t.desired_state);
    return q.value().run();
}

Result<Twin> TwinRepository::twin(std::string_view id) const {
    auto q = db_.prepare("SELECT " + std::string(kTwinColumns) + " FROM twins WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto all = collect<Twin>(q.value(), read_twin);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return make_error(ErrorCode::NotFound, "no such twin").with("id", std::string(id));
    return std::move(all.value().front());
}

Result<std::vector<Twin>> TwinRepository::twins() const {
    auto q = db_.prepare("SELECT " + std::string(kTwinColumns) + " FROM twins ORDER BY name");
    if (!q) return std::move(q).error();
    return collect<Twin>(q.value(), read_twin);
}

// --- packages ---------------------------------------------------------------------

Result<PackageRecord> TwinRepository::add_package(PackageRecord p) {
    auto id = db_.next_id("package", "PKG");
    if (!id) return std::move(id).error();
    p.id = id.value();
    p.created_at = iso8601_utc(clock_.now_ms());
    p.state = "built";
    auto q = db_.prepare("INSERT INTO packages(" + std::string(kPackageColumns) +
                         ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, NULL, ?12)");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, p.id)
        .bind(2, p.twin_id)
        .bind(3, p.directory)
        .bind(4, p.package_hash)
        .bind(5, p.ir_sha256)
        .bind(6, p.model_version)
        .bind(7, to_json(p.bindings).dump())
        .bind(8, p.evidence_id)
        .bind(9, p.state)
        .bind(10, p.created_at)
        .bind(11, p.created_by)
        .bind(12, p.change_id);
    if (auto st = q.value().run(); !st) return st.error();
    return package(p.id);
}

Result<std::vector<PackageRecord>> TwinRepository::query_packages(std::string_view where, std::string_view arg) const {
    auto q = db_.prepare("SELECT " + std::string(kPackageColumns) + " FROM packages " + std::string(where) +
                         " ORDER BY created_at DESC, id DESC");
    if (!q) return std::move(q).error();
    if (!arg.empty()) q.value().bind(1, arg);
    return collect<PackageRecord>(q.value(), read_package);
}

Result<PackageRecord> TwinRepository::package(std::string_view id) const {
    auto all = query_packages("WHERE id = ?1", id);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return make_error(ErrorCode::NotFound, "no such package").with("id", std::string(id));
    return std::move(all.value().front());
}

Result<std::vector<PackageRecord>> TwinRepository::packages(std::string_view twin_id) const {
    return twin_id.empty() ? query_packages("", "") : query_packages("WHERE twin_id = ?1", twin_id);
}

Result<PackageRecord> TwinRepository::mark_released(std::string_view id) {
    auto q = db_.prepare("UPDATE packages SET state = 'released', released_at = ?2 WHERE id = ?1 AND state = 'built'");
    if (!q) return std::move(q).error();
    q.value().bind(1, id).bind(2, iso8601_utc(clock_.now_ms()));
    if (auto st = q.value().run(); !st) return st.error();
    if (db_.changes() == 0) {
        return make_error(ErrorCode::StateError, "package is not in state 'built'").with("id", std::string(id));
    }
    return package(id);
}

// --- deployments ------------------------------------------------------------------

Result<Deployment> TwinRepository::deploy(std::string_view twin_id, std::string_view package_id, std::string_view kind,
                                          std::string_view reason, std::string_view actor) {
    if (kind != "deploy" && kind != "rollback") {
        return make_error(ErrorCode::InvalidArgument, "deployment kind must be 'deploy' or 'rollback'");
    }
    if (kind == "rollback" && reason.empty()) {
        return make_error(ErrorCode::InvalidArgument, "a rollback requires a reason");
    }
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto pkg = package(package_id);
    if (!pkg) return std::move(pkg).error();
    auto owner = twin(twin_id);
    if (!owner) return std::move(owner).error();
    const bool blueprint_package =
        owner.value().blueprint_id && pkg.value().twin_id == blueprint_owner(*owner.value().blueprint_id);
    if (pkg.value().twin_id != twin_id && !blueprint_package) {
        return make_error(ErrorCode::InvalidArgument, "package belongs to a different twin")
            .with("package", std::string(package_id));
    }
    if (pkg.value().state != "released") {
        return make_error(ErrorCode::StateError, "only released packages can be deployed")
            .with("package", std::string(package_id));
    }
    auto current = current_deployment(twin_id);
    if (!current) return std::move(current).error();
    if (current.value() && current.value()->package_id == package_id) {
        return make_error(ErrorCode::StateError, "this package is already deployed").with("package", std::string(package_id));
    }
    auto id = db_.next_id("deployment", "DEP");
    if (!id) return std::move(id).error();
    auto q = db_.prepare("INSERT INTO deployments(id, twin_id, package_id, previous_package_id, kind, reason, "
                         "deployed_at, deployed_by) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, id.value())
        .bind(2, twin_id)
        .bind(3, package_id)
        .bind(4, current.value() ? std::optional<std::string>(current.value()->package_id) : std::nullopt)
        .bind(5, kind)
        .bind(6, reason)
        .bind(7, iso8601_utc(clock_.now_ms()))
        .bind(8, actor);
    if (auto st = q.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return deployment(id.value());
}

Result<std::vector<Deployment>> TwinRepository::query_deployments(std::string_view where, std::string_view arg) const {
    auto q = db_.prepare("SELECT " + std::string(kDeploymentColumns) + " FROM deployments " + std::string(where) +
                         " ORDER BY seq DESC");
    if (!q) return std::move(q).error();
    if (!arg.empty()) q.value().bind(1, arg);
    return collect<Deployment>(q.value(), read_deployment);
}

Result<std::optional<Deployment>> TwinRepository::current_deployment(std::string_view twin_id) const {
    auto all = query_deployments("WHERE twin_id = ?1", twin_id);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return std::optional<Deployment>{};
    return std::optional<Deployment>(std::move(all.value().front()));
}

Result<std::vector<Deployment>> TwinRepository::deployments(std::string_view twin_id) const {
    return twin_id.empty() ? query_deployments("", "") : query_deployments("WHERE twin_id = ?1", twin_id);
}

Result<Deployment> TwinRepository::deployment(std::string_view id) const {
    auto all = query_deployments("WHERE id = ?1", id);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return make_error(ErrorCode::NotFound, "no such deployment").with("id", std::string(id));
    return std::move(all.value().front());
}

// --- changes ----------------------------------------------------------------------

Result<Change> TwinRepository::create_change(std::string_view twin_id, std::string_view title,
                                             std::string_view description, std::string_view actor) {
    if (title.empty()) return make_error(ErrorCode::InvalidArgument, "a change needs a title");
    if (auto t = twin(twin_id); !t) return std::move(t).error();
    auto id = db_.next_id("change", "CHG");
    if (!id) return std::move(id).error();
    auto q = db_.prepare("INSERT INTO changes(id, twin_id, title, description, state, artifacts, created_at, created_by) "
                         "VALUES(?1, ?2, ?3, ?4, 'open', '[]', ?5, ?6)");
    if (!q) return std::move(q).error();
    q.value().bind(1, id.value()).bind(2, twin_id).bind(3, title).bind(4, description).bind(5, iso8601_utc(clock_.now_ms())).bind(
        6, actor);
    if (auto st = q.value().run(); !st) return st.error();
    return change(id.value());
}

Result<std::vector<Change>> TwinRepository::query_changes(std::string_view where, std::string_view arg) const {
    auto q = db_.prepare("SELECT " + std::string(kChangeColumns) + " FROM changes " + std::string(where) +
                         " ORDER BY created_at DESC, id DESC");
    if (!q) return std::move(q).error();
    if (!arg.empty()) q.value().bind(1, arg);
    return collect<Change>(q.value(), read_change);
}

Result<Change> TwinRepository::change(std::string_view id) const {
    auto all = query_changes("WHERE id = ?1", id);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return make_error(ErrorCode::NotFound, "no such change").with("id", std::string(id));
    return std::move(all.value().front());
}

Result<std::vector<Change>> TwinRepository::changes(std::optional<std::string> state) const {
    return state ? query_changes("WHERE state = ?1", *state) : query_changes("", "");
}

Result<Change> TwinRepository::set_change_artifacts(std::string_view id, const std::vector<ArtifactRef>& artifacts) {
    auto c = change(id);
    if (!c) return std::move(c).error();
    if (c.value().state != "open") return make_error(ErrorCode::StateError, "the change is closed").with("id", std::string(id));
    json::Json list = json::Json::array();
    for (const auto& a : artifacts) list.push_back(a.str());
    auto q = db_.prepare("UPDATE changes SET artifacts = ?2 WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id).bind(2, list.dump());
    if (auto st = q.value().run(); !st) return st.error();
    return change(id);
}

Result<Change> TwinRepository::close_change(std::string_view id, std::string_view state) {
    if (state != "released" && state != "abandoned") {
        return make_error(ErrorCode::InvalidArgument, "a change closes as 'released' or 'abandoned'");
    }
    auto q = db_.prepare("UPDATE changes SET state = ?2, closed_at = ?3 WHERE id = ?1 AND state = 'open'");
    if (!q) return std::move(q).error();
    q.value().bind(1, id).bind(2, state).bind(3, iso8601_utc(clock_.now_ms()));
    if (auto st = q.value().run(); !st) return st.error();
    if (db_.changes() == 0) return make_error(ErrorCode::StateError, "the change is not open").with("id", std::string(id));
    return change(id);
}

// --- JSON -------------------------------------------------------------------------

json::Json to_json(const Binding& b) {
    return {{"role", b.role}, {"ref", b.ref.str()}, {"artifactId", b.ref.artifact_id}, {"version", b.ref.version},
            {"sha256", b.sha256}};
}

json::Json to_json(const std::vector<Binding>& bindings) {
    json::Json a = json::Json::array();
    for (const auto& b : bindings) a.push_back(to_json(b));
    return a;
}

Result<std::vector<Binding>> bindings_from_json(const json::Json& j) {
    if (!j.is_array()) return make_error(ErrorCode::ValidationError, "bindings must be an array");
    std::vector<Binding> out;
    for (const auto& item : j) {
        auto role = json::get_string(item, "role");
        auto ref = json::get_string(item, "ref");
        auto sha = json::get_string(item, "sha256");
        if (!role || !ref || !sha) return make_error(ErrorCode::ValidationError, "malformed binding");
        auto r = parse_ref(ref.value());
        if (!r) return std::move(r).error();
        out.push_back({role.value(), r.value(), sha.value()});
    }
    return out;
}

json::Json to_json(const Twin& t) {
    return {{"id", t.id},
            {"name", t.name},
            {"assetId", t.asset_id ? json::Json(*t.asset_id) : json::Json(nullptr)},
            {"description", t.description},
            {"modelId", t.model_id},
            {"ticksPerUnit", t.ticks_per_unit},
            {"runtimeUrl", t.runtime_url ? json::Json(*t.runtime_url) : json::Json(nullptr)},
            {"presentation", t.presentation},
            {"createdAt", t.created_at},
            {"blueprintId", t.blueprint_id ? json::Json(*t.blueprint_id) : json::Json(nullptr)},
            {"blueprintVersion", t.blueprint_version ? json::Json(*t.blueprint_version) : json::Json(nullptr)},
            {"instanceConfig", t.instance_config},
            {"worldUrl", t.world_url ? json::Json(*t.world_url) : json::Json(nullptr)},
            {"desiredState", t.desired_state}};
}

json::Json to_json(const PackageRecord& p) {
    return {{"id", p.id},
            {"twinId", p.twin_id},
            {"packageHash", p.package_hash},
            {"irSha256", p.ir_sha256},
            {"modelVersion", p.model_version},
            {"bindings", to_json(p.bindings)},
            {"evidenceId", p.evidence_id ? json::Json(*p.evidence_id) : json::Json(nullptr)},
            {"state", p.state},
            {"createdAt", p.created_at},
            {"createdBy", p.created_by},
            {"releasedAt", p.released_at ? json::Json(*p.released_at) : json::Json(nullptr)},
            {"changeId", p.change_id ? json::Json(*p.change_id) : json::Json(nullptr)}};
}

json::Json to_json(const Deployment& d) {
    return {{"seq", d.seq},
            {"id", d.id},
            {"twinId", d.twin_id},
            {"packageId", d.package_id},
            {"previousPackageId", d.previous_package_id ? json::Json(*d.previous_package_id) : json::Json(nullptr)},
            {"kind", d.kind},
            {"reason", d.reason},
            {"deployedAt", d.deployed_at},
            {"deployedBy", d.deployed_by}};
}

json::Json to_json(const Change& c) {
    json::Json artifacts = json::Json::array();
    for (const auto& a : c.artifacts) artifacts.push_back(a.str());
    return {{"id", c.id},
            {"twinId", c.twin_id},
            {"title", c.title},
            {"description", c.description},
            {"state", c.state},
            {"artifacts", artifacts},
            {"createdAt", c.created_at},
            {"createdBy", c.created_by},
            {"closedAt", c.closed_at ? json::Json(*c.closed_at) : json::Json(nullptr)}};
}

}  // namespace twin::platform
