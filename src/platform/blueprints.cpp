/**
 * @file blueprints.cpp
 * @brief Twin Blueprints and their versions and bundles (see blueprints.hpp).
 */
#include "twin/platform/blueprints.hpp"

#include <algorithm>

namespace twin::platform {

namespace {

constexpr std::string_view kBlueprintColumns =
    "id, name, description, domain, icon, template_id, cloned_from, created_at, created_by";
constexpr std::string_view kVersionColumns =
    "blueprint_id, version, state, revision, parent_version, document, document_sha256, pins, package_id, bundle_id, "
    "note, created_at, created_by, updated_at, updated_by, published_at";
constexpr std::string_view kBundleColumns = "id, blueprint_id, version, package_id, directory, bundle_hash, created_at, created_by";

Result<Blueprint> read_blueprint(const Statement& s) {
    return Blueprint{s.text(0), s.text(1), s.text(2), s.text(3), s.text(4), s.opt_text(5), s.opt_text(6), s.text(7), s.text(8)};
}

Result<BlueprintVersion> read_version(const Statement& s) {
    BlueprintVersion v;
    v.blueprint_id = s.text(0);
    v.version = s.integer(1);
    v.state = s.text(2);
    v.revision = s.integer(3);
    if (!s.is_null(4)) v.parent = s.integer(4);
    auto doc = json::parse(s.text(5));
    if (!doc) return std::move(doc).error();
    v.document = std::move(doc).value();
    v.document_sha256 = s.text(6);
    auto pins = json::parse(s.text(7));
    if (!pins) return std::move(pins).error();
    for (const auto& [role, ref] : pins.value().items()) {
        if (ref.is_string()) v.pins[role] = ref.get<std::string>();
    }
    v.package_id = s.opt_text(8);
    v.bundle_id = s.opt_text(9);
    v.note = s.text(10);
    v.created_at = s.text(11);
    v.created_by = s.text(12);
    v.updated_at = s.text(13);
    v.updated_by = s.text(14);
    v.published_at = s.opt_text(15);
    return v;
}

Result<BundleRecord> read_bundle(const Statement& s) {
    return BundleRecord{s.text(0), s.text(1), s.integer(2), s.text(3), s.text(4), s.text(5), s.text(6), s.text(7)};
}

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

json::Json pins_json(const std::map<std::string, std::string>& pins) {
    json::Json j = json::Json::object();
    for (const auto& [role, ref] : pins) j[role] = ref;
    return j;
}

Status check_pins(const std::map<std::string, std::string>& pins) {
    for (const auto& [role, ref] : pins) {
        if (std::find(std::begin(kBlueprintRoles), std::end(kBlueprintRoles), role) == std::end(kBlueprintRoles)) {
            return make_error(ErrorCode::InvalidArgument, "unknown formal role").with("role", role);
        }
        if (ref.find('@') == std::string::npos) {
            return make_error(ErrorCode::InvalidArgument, "a pin is an artefact version 'id@n'").with("role", role);
        }
    }
    return {};
}

Result<std::string> document_hash(const json::Json& document) {
    if (!document.is_object() || document.value("format", std::string()) != kBlueprintFormat) {
        return make_error(ErrorCode::ValidationError, "a Blueprint document needs format \"" + std::string(kBlueprintFormat) + "\"");
    }
    auto h = json::canonical_sha256(document);
    if (!h) {
        return make_error(ErrorCode::ValidationError,
                          "Blueprint documents use integers or decimal strings, never floating-point numbers: " +
                              h.error().message);
    }
    return h;
}

}  // namespace

bool valid_blueprint_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64 || id.front() == '-') return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

Result<BlueprintVersion> BlueprintRepository::create(const Blueprint& b, const json::Json& document,
                                                     const std::map<std::string, std::string>& pins, std::string_view actor) {
    if (!valid_blueprint_id(b.id)) {
        return make_error(ErrorCode::InvalidArgument, "a Blueprint id uses lowercase letters, digits and '-' (at most 64)")
            .with("id", b.id);
    }
    if (b.name.empty()) return make_error(ErrorCode::InvalidArgument, "a Blueprint needs a name");
    if (auto st = check_pins(pins); !st) return st.error();
    auto hash = document_hash(document);
    if (!hash) return std::move(hash).error();
    if (blueprint(b.id)) return make_error(ErrorCode::StateError, "a Blueprint with this id already exists").with("id", b.id);
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    const std::string now = iso8601_utc(clock_.now_ms());
    auto q = db_.prepare("INSERT INTO blueprints(" + std::string(kBlueprintColumns) + ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, b.id)
        .bind(2, b.name)
        .bind(3, b.description)
        .bind(4, b.domain)
        .bind(5, b.icon)
        .bind(6, b.template_id)
        .bind(7, b.cloned_from)
        .bind(8, now)
        .bind(9, actor);
    if (auto st = q.value().run(); !st) return st.error();
    auto v = db_.prepare("INSERT INTO blueprint_versions(" + std::string(kVersionColumns) +
                         ") VALUES(?1, 1, 'draft', 1, NULL, ?2, ?3, ?4, NULL, NULL, ?5, ?6, ?7, ?6, ?7, NULL)");
    if (!v) return std::move(v).error();
    v.value()
        .bind(1, b.id)
        .bind(2, document.dump())
        .bind(3, hash.value())
        .bind(4, pins_json(pins).dump())
        .bind(5, std::string_view("Initial version"))
        .bind(6, now)
        .bind(7, actor);
    if (auto st = v.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return version(b.id, 1);
}

Result<Blueprint> BlueprintRepository::blueprint(std::string_view id) const {
    auto q = db_.prepare("SELECT " + std::string(kBlueprintColumns) + " FROM blueprints WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto all = collect<Blueprint>(q.value(), read_blueprint);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return make_error(ErrorCode::NotFound, "no such Blueprint").with("id", std::string(id));
    return std::move(all.value().front());
}

Result<std::vector<Blueprint>> BlueprintRepository::blueprints() const {
    auto q = db_.prepare("SELECT " + std::string(kBlueprintColumns) + " FROM blueprints ORDER BY name, id");
    if (!q) return std::move(q).error();
    return collect<Blueprint>(q.value(), read_blueprint);
}

Result<Blueprint> BlueprintRepository::update_meta(const Blueprint& b) {
    if (b.name.empty()) return make_error(ErrorCode::InvalidArgument, "a Blueprint needs a name");
    auto q = db_.prepare("UPDATE blueprints SET name = ?2, description = ?3, domain = ?4, icon = ?5 WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, b.id).bind(2, b.name).bind(3, b.description).bind(4, b.domain).bind(5, b.icon);
    if (auto st = q.value().run(); !st) return st.error();
    if (db_.changes() == 0) return make_error(ErrorCode::NotFound, "no such Blueprint").with("id", b.id);
    return blueprint(b.id);
}

Result<std::vector<BlueprintVersion>> BlueprintRepository::query(std::string_view where, std::string_view id,
                                                                 std::optional<std::int64_t> v) const {
    auto q = db_.prepare("SELECT " + std::string(kVersionColumns) + " FROM blueprint_versions " + std::string(where) +
                         " ORDER BY version DESC");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    if (v) q.value().bind(2, *v);
    return collect<BlueprintVersion>(q.value(), read_version);
}

Result<BlueprintVersion> BlueprintRepository::version(std::string_view id, std::int64_t v) const {
    auto all = query("WHERE blueprint_id = ?1 AND version = ?2", id, v);
    if (!all) return std::move(all).error();
    if (all.value().empty()) {
        return make_error(ErrorCode::NotFound, "no such Blueprint version").with("id", std::string(id)).with("version", std::to_string(v));
    }
    return std::move(all.value().front());
}

Result<std::vector<BlueprintVersion>> BlueprintRepository::versions(std::string_view id) const {
    return query("WHERE blueprint_id = ?1", id, std::nullopt);
}

Result<std::optional<BlueprintVersion>> BlueprintRepository::draft(std::string_view id) const {
    auto all = query("WHERE blueprint_id = ?1 AND state = 'draft'", id, std::nullopt);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return std::optional<BlueprintVersion>{};
    return std::optional<BlueprintVersion>(std::move(all.value().front()));
}

Result<std::optional<BlueprintVersion>> BlueprintRepository::latest_published(std::string_view id) const {
    auto all = query("WHERE blueprint_id = ?1 AND state = 'published'", id, std::nullopt);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return std::optional<BlueprintVersion>{};
    return std::optional<BlueprintVersion>(std::move(all.value().front()));
}

Result<BlueprintVersion> BlueprintRepository::create_draft(std::string_view id, std::int64_t from, std::string_view note,
                                                           std::string_view actor) {
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto base = version(id, from);
    if (!base) return std::move(base).error();
    auto open = draft(id);
    if (!open) return std::move(open).error();
    if (open.value()) {
        return make_error(ErrorCode::StateError, "this Blueprint already has a draft; finish or publish it first")
            .with("draft", std::to_string(open.value()->version));
    }
    auto all = versions(id);
    if (!all) return std::move(all).error();
    const std::int64_t next = all.value().empty() ? 1 : all.value().front().version + 1;
    const std::string now = iso8601_utc(clock_.now_ms());
    auto q = db_.prepare("INSERT INTO blueprint_versions(" + std::string(kVersionColumns) +
                         ") VALUES(?1, ?2, 'draft', 1, ?3, ?4, ?5, ?6, NULL, NULL, ?7, ?8, ?9, ?8, ?9, NULL)");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, id)
        .bind(2, next)
        .bind(3, from)
        .bind(4, base.value().document.dump())
        .bind(5, base.value().document_sha256)
        .bind(6, pins_json(base.value().pins).dump())
        .bind(7, note.empty() ? std::string("Draft from v" + std::to_string(from)) : std::string(note))
        .bind(8, now)
        .bind(9, actor);
    if (auto st = q.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return version(id, next);
}

Result<BlueprintVersion> BlueprintRepository::save(std::string_view id, std::int64_t v, std::int64_t expected_revision,
                                                   const json::Json& document,
                                                   const std::map<std::string, std::string>& pins, std::string_view actor) {
    if (auto st = check_pins(pins); !st) return st.error();
    auto hash = document_hash(document);
    if (!hash) return std::move(hash).error();
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto current = version(id, v);
    if (!current) return std::move(current).error();
    if (current.value().state != "draft") {
        return make_error(ErrorCode::StateError, "published Blueprint versions are immutable; create a draft from this version")
            .with("version", std::to_string(v));
    }
    if (current.value().revision != expected_revision) {
        return make_error(ErrorCode::StateError, "this draft was changed by someone else since you loaded it")
            .with("expectedRevision", std::to_string(expected_revision))
            .with("currentRevision", std::to_string(current.value().revision))
            .with("updatedBy", current.value().updated_by);
    }
    auto q = db_.prepare("UPDATE blueprint_versions SET document = ?3, document_sha256 = ?4, pins = ?5, revision = revision + 1, "
                         "updated_at = ?6, updated_by = ?7 WHERE blueprint_id = ?1 AND version = ?2");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, id)
        .bind(2, v)
        .bind(3, document.dump())
        .bind(4, hash.value())
        .bind(5, pins_json(pins).dump())
        .bind(6, iso8601_utc(clock_.now_ms()))
        .bind(7, actor);
    if (auto st = q.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return version(id, v);
}

Status BlueprintRepository::set_build(std::string_view id, std::int64_t v, const std::optional<std::string>& package_id,
                                      const std::optional<std::string>& bundle_id) {
    auto current = version(id, v);
    if (!current) return current.error();
    if (current.value().state != "draft" && current.value().package_id && current.value().package_id != package_id) {
        return make_error(ErrorCode::StateError, "a published version's package is fixed");
    }
    auto q = db_.prepare("UPDATE blueprint_versions SET package_id = ?3, bundle_id = ?4 WHERE blueprint_id = ?1 AND version = ?2");
    if (!q) return q.error();
    q.value().bind(1, id).bind(2, v).bind(3, package_id).bind(4, bundle_id);
    return q.value().run();
}

Result<BlueprintVersion> BlueprintRepository::publish(std::string_view id, std::int64_t v, std::string_view actor) {
    auto current = version(id, v);
    if (!current) return std::move(current).error();
    if (current.value().state != "draft") return make_error(ErrorCode::StateError, "only a draft can be published");
    if (!current.value().package_id || !current.value().bundle_id) {
        return make_error(ErrorCode::StateError, "build the verified package and the deployment bundle before publishing");
    }
    auto q = db_.prepare("UPDATE blueprint_versions SET state = 'published', published_at = ?3, updated_by = ?4 "
                         "WHERE blueprint_id = ?1 AND version = ?2 AND state = 'draft'");
    if (!q) return std::move(q).error();
    q.value().bind(1, id).bind(2, v).bind(3, iso8601_utc(clock_.now_ms())).bind(4, actor);
    if (auto st = q.value().run(); !st) return st.error();
    return version(id, v);
}

Result<BlueprintVersion> BlueprintRepository::deprecate(std::string_view id, std::int64_t v) {
    auto q = db_.prepare("UPDATE blueprint_versions SET state = 'deprecated' WHERE blueprint_id = ?1 AND version = ?2 AND state = 'published'");
    if (!q) return std::move(q).error();
    q.value().bind(1, id).bind(2, v);
    if (auto st = q.value().run(); !st) return st.error();
    if (db_.changes() == 0) return make_error(ErrorCode::StateError, "only a published version can be deprecated");
    return version(id, v);
}

Result<BundleRecord> BlueprintRepository::add_bundle(BundleRecord r) {
    auto id = db_.next_id("bundle", "BND");
    if (!id) return std::move(id).error();
    r.id = id.value();
    r.created_at = iso8601_utc(clock_.now_ms());
    auto q = db_.prepare("INSERT INTO bundles(" + std::string(kBundleColumns) + ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, r.id)
        .bind(2, r.blueprint_id)
        .bind(3, r.version)
        .bind(4, r.package_id)
        .bind(5, r.directory)
        .bind(6, r.bundle_hash)
        .bind(7, r.created_at)
        .bind(8, r.created_by);
    if (auto st = q.value().run(); !st) return st.error();
    return bundle(r.id);
}

Result<BundleRecord> BlueprintRepository::bundle(std::string_view id) const {
    auto q = db_.prepare("SELECT " + std::string(kBundleColumns) + " FROM bundles WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto all = collect<BundleRecord>(q.value(), read_bundle);
    if (!all) return std::move(all).error();
    if (all.value().empty()) return make_error(ErrorCode::NotFound, "no such bundle").with("id", std::string(id));
    return std::move(all.value().front());
}

Status BlueprintRepository::set_bundle_directory(std::string_view id, std::string_view directory) {
    auto q = db_.prepare("UPDATE bundles SET directory = ?2 WHERE id = ?1");
    if (!q) return q.error();
    q.value().bind(1, id).bind(2, directory);
    return q.value().run();
}

json::Json to_json(const Blueprint& b) {
    return {{"id", b.id},
            {"name", b.name},
            {"description", b.description},
            {"domain", b.domain},
            {"icon", b.icon},
            {"templateId", b.template_id ? json::Json(*b.template_id) : json::Json(nullptr)},
            {"clonedFrom", b.cloned_from ? json::Json(*b.cloned_from) : json::Json(nullptr)},
            {"createdAt", b.created_at},
            {"createdBy", b.created_by}};
}

json::Json version_summary(const BlueprintVersion& v) {
    return {{"blueprintId", v.blueprint_id},
            {"version", v.version},
            {"label", "v" + std::to_string(v.version) + (v.state == "draft" ? "-draft" : "")},
            {"state", v.state},
            {"revision", v.revision},
            {"parent", v.parent ? json::Json(*v.parent) : json::Json(nullptr)},
            {"documentSha256", v.document_sha256},
            {"pins", pins_json(v.pins)},
            {"packageId", v.package_id ? json::Json(*v.package_id) : json::Json(nullptr)},
            {"bundleId", v.bundle_id ? json::Json(*v.bundle_id) : json::Json(nullptr)},
            {"note", v.note},
            {"createdAt", v.created_at},
            {"createdBy", v.created_by},
            {"updatedAt", v.updated_at},
            {"updatedBy", v.updated_by},
            {"publishedAt", v.published_at ? json::Json(*v.published_at) : json::Json(nullptr)}};
}

json::Json to_json(const BundleRecord& b) {
    return {{"id", b.id},
            {"blueprintId", b.blueprint_id},
            {"version", b.version},
            {"packageId", b.package_id},
            {"bundleHash", b.bundle_hash},
            {"createdAt", b.created_at},
            {"createdBy", b.created_by}};
}

}  // namespace twin::platform
