/**
 * @file artifacts.cpp
 * @brief Artefact versions and lifecycle (see artifacts.hpp).
 */
#include "twin/platform/artifacts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace twin::platform {

namespace {

constexpr std::array<std::pair<ArtifactKind, std::string_view>, 4> kKinds = {{
    {ArtifactKind::Ontology, "ontology"},
    {ArtifactKind::Interpretation, "interpretation"},
    {ArtifactKind::PtModel, "pt_model"},
    {ArtifactKind::DtModel, "dt_model"},
}};

constexpr std::array<std::pair<Lifecycle, std::string_view>, 6> kStates = {{
    {Lifecycle::Draft, "draft"},
    {Lifecycle::Validating, "validating"},
    {Lifecycle::Verified, "verified"},
    {Lifecycle::Published, "published"},
    {Lifecycle::Superseded, "superseded"},
    {Lifecycle::Rejected, "rejected"},
}};

bool valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) return false;
    if (!(std::islower(static_cast<unsigned char>(id[0])) || std::isdigit(static_cast<unsigned char>(id[0])))) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) ||
               c == '-' || c == '_' || c == '.';
    });
}

constexpr std::string_view kVersionColumns =
    "v.artifact_id, a.kind, v.version, v.parent_version, v.state, v.content_sha256, v.refs, v.change_description, "
    "v.created_at, v.created_by, v.updated_at, v.published_at, v.published_by";

Result<ArtifactVersion> read_version(const Statement& s) {
    ArtifactVersion v;
    v.artifact_id = s.text(0);
    auto kind = artifact_kind_from_string(s.text(1));
    if (!kind) return std::move(kind).error();
    v.kind = kind.value();
    v.version = s.integer(2);
    v.parent = s.opt_integer(3);
    auto state = lifecycle_from_string(s.text(4));
    if (!state) return std::move(state).error();
    v.state = state.value();
    v.content_sha256 = s.text(5);
    auto refs = json::parse(s.text(6));
    if (!refs) return std::move(refs).error();
    v.refs = std::move(refs).value();
    v.change_description = s.text(7);
    v.created_at = s.text(8);
    v.created_by = s.text(9);
    v.updated_at = s.text(10);
    v.published_at = s.opt_text(11);
    v.published_by = s.opt_text(12);
    return v;
}

Error not_found(const ArtifactRef& ref) {
    return make_error(ErrorCode::NotFound, "no such artifact version").with("ref", ref.str());
}

}  // namespace

std::string_view to_string(ArtifactKind kind) noexcept {
    for (const auto& [k, s] : kKinds) {
        if (k == kind) return s;
    }
    return "ontology";
}

Result<ArtifactKind> artifact_kind_from_string(std::string_view text) {
    for (const auto& [k, s] : kKinds) {
        if (s == text) return k;
    }
    return make_error(ErrorCode::InvalidArgument, "unknown artifact kind").with("kind", std::string(text));
}

std::string_view to_string(Lifecycle state) noexcept {
    for (const auto& [k, s] : kStates) {
        if (k == state) return s;
    }
    return "draft";
}

Result<Lifecycle> lifecycle_from_string(std::string_view text) {
    for (const auto& [k, s] : kStates) {
        if (s == text) return k;
    }
    return make_error(ErrorCode::InvalidArgument, "unknown lifecycle state").with("state", std::string(text));
}

bool transition_allowed(Lifecycle from, Lifecycle to) noexcept {
    switch (from) {
        case Lifecycle::Draft: return to == Lifecycle::Validating || to == Lifecycle::Rejected;
        case Lifecycle::Validating: return to == Lifecycle::Verified || to == Lifecycle::Draft;
        case Lifecycle::Verified:
            return to == Lifecycle::Draft || to == Lifecycle::Validating || to == Lifecycle::Published ||
                   to == Lifecycle::Rejected;
        case Lifecycle::Published: return to == Lifecycle::Superseded;
        case Lifecycle::Superseded:
        case Lifecycle::Rejected: return false;
    }
    return false;
}

bool ArtifactVersion::is_open() const noexcept {
    return state == Lifecycle::Draft || state == Lifecycle::Validating || state == Lifecycle::Verified;
}

Result<ArtifactRef> parse_ref(std::string_view text) {
    const auto at = text.rfind('@');
    if (at == std::string_view::npos || at == 0 || at + 1 >= text.size()) {
        return make_error(ErrorCode::InvalidArgument, "expected '<artifact>@<version>'").with("ref", std::string(text));
    }
    std::int64_t v = 0;
    const auto num = text.substr(at + 1);
    const auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), v);
    if (ec != std::errc{} || ptr != num.data() + num.size() || v < 1) {
        return make_error(ErrorCode::InvalidArgument, "invalid version number").with("ref", std::string(text));
    }
    return ArtifactRef{std::string(text.substr(0, at)), v};
}

Result<ArtifactVersion> ArtifactRepository::create(ArtifactKind kind, std::string_view id, std::string_view name,
                                                   std::string_view description, std::string_view content,
                                                   const json::Json& refs, std::string_view actor,
                                                   std::string_view change_description) {
    if (!valid_id(id)) {
        return make_error(ErrorCode::InvalidArgument,
                          "artifact ids use lowercase letters, digits, '-', '_' and '.' (max 64)")
            .with("id", std::string(id));
    }
    auto hash = store_.put(content);
    if (!hash) return std::move(hash).error();
    const std::string now = iso8601_utc(clock_.now_ms());
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto ins = db_.prepare("INSERT INTO artifacts(id, kind, name, description, created_at, created_by) "
                           "VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
    if (!ins) return std::move(ins).error();
    ins.value().bind(1, id).bind(2, to_string(kind)).bind(3, name).bind(4, description).bind(5, now).bind(6, actor);
    if (auto st = ins.value().run(); !st) {
        if (st.error().code == ErrorCode::StateError) {
            return make_error(ErrorCode::StateError, "an artifact with this id already exists").with("id", std::string(id));
        }
        return st.error();
    }
    auto ver = db_.prepare(
        "INSERT INTO artifact_versions(artifact_id, version, parent_version, state, content_sha256, refs, "
        "change_description, created_at, created_by, updated_at) VALUES(?1, 1, NULL, 'draft', ?2, ?3, ?4, ?5, ?6, ?5)");
    if (!ver) return std::move(ver).error();
    ver.value().bind(1, id).bind(2, hash.value()).bind(3, refs.dump()).bind(4, change_description).bind(5, now).bind(
        6, actor);
    if (auto st = ver.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return version({std::string(id), 1});
}

Result<Artifact> ArtifactRepository::get(std::string_view id) const {
    auto q = db_.prepare("SELECT id, kind, name, description, created_at, created_by FROM artifacts WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    if (!row.value()) return make_error(ErrorCode::NotFound, "no such artifact").with("id", std::string(id));
    auto kind = artifact_kind_from_string(q.value().text(1));
    if (!kind) return std::move(kind).error();
    return Artifact{q.value().text(0), kind.value(),        q.value().text(2),
                    q.value().text(3), q.value().text(4), q.value().text(5)};
}

Result<std::vector<Artifact>> ArtifactRepository::list(std::optional<ArtifactKind> kind) const {
    auto q = db_.prepare(kind ? "SELECT id FROM artifacts WHERE kind = ?1 ORDER BY id" : "SELECT id FROM artifacts ORDER BY id");
    if (!q) return std::move(q).error();
    if (kind) q.value().bind(1, to_string(*kind));
    std::vector<std::string> ids;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        ids.push_back(q.value().text(0));
    }
    std::vector<Artifact> out;
    for (const auto& id : ids) {
        auto a = get(id);
        if (!a) return std::move(a).error();
        out.push_back(std::move(a).value());
    }
    return out;
}

Result<std::vector<ArtifactVersion>> ArtifactRepository::query_versions(std::string_view where, std::string_view a,
                                                                         std::optional<std::int64_t> b) const {
    const std::string sql = "SELECT " + std::string(kVersionColumns) +
                            " FROM artifact_versions v JOIN artifacts a ON a.id = v.artifact_id WHERE " +
                            std::string(where) + " ORDER BY v.version DESC";
    auto q = db_.prepare(sql);
    if (!q) return std::move(q).error();
    q.value().bind(1, a);
    if (b) q.value().bind(2, *b);
    std::vector<ArtifactVersion> out;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        auto v = read_version(q.value());
        if (!v) return std::move(v).error();
        out.push_back(std::move(v).value());
    }
    return out;
}

Result<std::vector<ArtifactVersion>> ArtifactRepository::versions(std::string_view id) const {
    if (auto a = get(id); !a) return std::move(a).error();
    return query_versions("v.artifact_id = ?1", id, std::nullopt);
}

Result<ArtifactVersion> ArtifactRepository::version(const ArtifactRef& ref) const {
    auto v = query_versions("v.artifact_id = ?1 AND v.version = ?2", ref.artifact_id, ref.version);
    if (!v) return std::move(v).error();
    if (v.value().empty()) return not_found(ref);
    return std::move(v.value().front());
}

Result<std::optional<ArtifactVersion>> ArtifactRepository::published(std::string_view id) const {
    auto v = query_versions("v.artifact_id = ?1 AND v.state = 'published'", id, std::nullopt);
    if (!v) return std::move(v).error();
    if (v.value().empty()) return std::optional<ArtifactVersion>{};
    return std::optional<ArtifactVersion>(std::move(v.value().front()));
}

Result<std::optional<ArtifactVersion>> ArtifactRepository::open_version(std::string_view id) const {
    auto v = query_versions("v.artifact_id = ?1 AND v.state IN ('draft', 'validating', 'verified')", id, std::nullopt);
    if (!v) return std::move(v).error();
    if (v.value().empty()) return std::optional<ArtifactVersion>{};
    return std::optional<ArtifactVersion>(std::move(v.value().front()));
}

Result<std::string> ArtifactRepository::content(const ArtifactRef& ref) const {
    auto v = version(ref);
    if (!v) return std::move(v).error();
    return store_.get(v.value().content_sha256);
}

Result<ArtifactVersion> ArtifactRepository::create_draft(const ArtifactRef& from, std::string_view actor,
                                                         std::string_view change_description) {
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto base = version(from);
    if (!base) return std::move(base).error();
    auto open = open_version(from.artifact_id);
    if (!open) return std::move(open).error();
    if (open.value()) {
        return make_error(ErrorCode::StateError, "this artifact already has an open draft; continue editing it")
            .with("draft", open.value()->ref().str());
    }
    auto all = versions(from.artifact_id);
    if (!all) return std::move(all).error();
    const std::int64_t next = all.value().front().version + 1;
    const std::string now = iso8601_utc(clock_.now_ms());
    auto ins = db_.prepare(
        "INSERT INTO artifact_versions(artifact_id, version, parent_version, state, content_sha256, refs, "
        "change_description, created_at, created_by, updated_at) VALUES(?1, ?2, ?3, 'draft', ?4, ?5, ?6, ?7, ?8, ?7)");
    if (!ins) return std::move(ins).error();
    ins.value()
        .bind(1, from.artifact_id)
        .bind(2, next)
        .bind(3, from.version)
        .bind(4, base.value().content_sha256)
        .bind(5, base.value().refs.dump())
        .bind(6, change_description)
        .bind(7, now)
        .bind(8, actor);
    if (auto st = ins.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return version({from.artifact_id, next});
}

Result<ArtifactVersion> ArtifactRepository::save(const ArtifactRef& ref, std::string_view content,
                                                 const std::optional<json::Json>& refs,
                                                 const std::optional<std::string>& change_description,
                                                 std::string_view actor) {
    (void)actor;
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto current = version(ref);
    if (!current) return std::move(current).error();
    const Lifecycle state = current.value().state;
    if (state != Lifecycle::Draft && state != Lifecycle::Verified) {
        return make_error(ErrorCode::StateError,
                          "only draft versions can be edited; " + std::string(to_string(state)) +
                              " versions are immutable — create a new draft instead")
            .with("ref", ref.str())
            .with("state", std::string(to_string(state)));
    }
    auto hash = store_.put(content);
    if (!hash) return std::move(hash).error();
    const json::Json new_refs = refs ? *refs : current.value().refs;
    const bool changed = hash.value() != current.value().content_sha256 || new_refs != current.value().refs;
    // Any change of content or references voids a previous successful validation.
    const Lifecycle new_state = changed ? Lifecycle::Draft : state;
    auto up = db_.prepare(
        "UPDATE artifact_versions SET content_sha256 = ?3, refs = ?4, change_description = ?5, state = ?6, "
        "updated_at = ?7 WHERE artifact_id = ?1 AND version = ?2");
    if (!up) return std::move(up).error();
    up.value()
        .bind(1, ref.artifact_id)
        .bind(2, ref.version)
        .bind(3, hash.value())
        .bind(4, new_refs.dump())
        .bind(5, change_description ? *change_description : current.value().change_description)
        .bind(6, to_string(new_state))
        .bind(7, iso8601_utc(clock_.now_ms()));
    if (auto st = up.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return version(ref);
}

Result<ArtifactVersion> ArtifactRepository::set_state(const ArtifactRef& ref, Lifecycle to, std::string_view actor) {
    (void)actor;
    if (to == Lifecycle::Published) {
        return make_error(ErrorCode::InvalidArgument, "use publish() to publish a version");
    }
    auto current = version(ref);
    if (!current) return std::move(current).error();
    if (!transition_allowed(current.value().state, to)) {
        return make_error(ErrorCode::StateError, "lifecycle transition not allowed")
            .with("ref", ref.str())
            .with("from", std::string(to_string(current.value().state)))
            .with("to", std::string(to_string(to)));
    }
    auto up = db_.prepare("UPDATE artifact_versions SET state = ?3, updated_at = ?4 WHERE artifact_id = ?1 AND version = ?2");
    if (!up) return std::move(up).error();
    up.value().bind(1, ref.artifact_id).bind(2, ref.version).bind(3, to_string(to)).bind(4, iso8601_utc(clock_.now_ms()));
    if (auto st = up.value().run(); !st) return st.error();
    return version(ref);
}

Result<ArtifactVersion> ArtifactRepository::publish(const ArtifactRef& ref, std::string_view actor) {
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto current = version(ref);
    if (!current) return std::move(current).error();
    if (current.value().state != Lifecycle::Verified) {
        return make_error(ErrorCode::StateError, "only VERIFIED versions can be published; validate the draft first")
            .with("ref", ref.str())
            .with("state", std::string(to_string(current.value().state)));
    }
    const std::string now = iso8601_utc(clock_.now_ms());
    auto prev = published(ref.artifact_id);
    if (!prev) return std::move(prev).error();
    if (prev.value()) {
        auto sup = db_.prepare("UPDATE artifact_versions SET state = 'superseded', updated_at = ?3 "
                               "WHERE artifact_id = ?1 AND version = ?2");
        if (!sup) return std::move(sup).error();
        sup.value().bind(1, ref.artifact_id).bind(2, prev.value()->version).bind(3, now);
        if (auto st = sup.value().run(); !st) return st.error();
    }
    auto up = db_.prepare("UPDATE artifact_versions SET state = 'published', published_at = ?3, published_by = ?4, "
                          "updated_at = ?3 WHERE artifact_id = ?1 AND version = ?2");
    if (!up) return std::move(up).error();
    up.value().bind(1, ref.artifact_id).bind(2, ref.version).bind(3, now).bind(4, actor);
    if (auto st = up.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return version(ref);
}

json::Json to_json(const Artifact& a) {
    return {{"id", a.id},
            {"kind", std::string(to_string(a.kind))},
            {"name", a.name},
            {"description", a.description},
            {"createdAt", a.created_at},
            {"createdBy", a.created_by}};
}

json::Json to_json(const ArtifactVersion& v) {
    json::Json j = {{"artifactId", v.artifact_id},
                    {"kind", std::string(to_string(v.kind))},
                    {"version", v.version},
                    {"ref", v.ref().str()},
                    {"state", std::string(to_string(v.state))},
                    {"contentSha256", v.content_sha256},
                    {"refs", v.refs},
                    {"changeDescription", v.change_description},
                    {"createdAt", v.created_at},
                    {"createdBy", v.created_by},
                    {"updatedAt", v.updated_at},
                    {"parentVersion", v.parent ? json::Json(*v.parent) : json::Json(nullptr)},
                    {"publishedAt", v.published_at ? json::Json(*v.published_at) : json::Json(nullptr)},
                    {"publishedBy", v.published_by ? json::Json(*v.published_by) : json::Json(nullptr)}};
    return j;
}

}  // namespace twin::platform
