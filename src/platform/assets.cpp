/**
 * @file assets.cpp
 * @brief Asset registry and graph queries (see assets.hpp).
 */
#include "twin/platform/assets.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace twin::platform {

namespace {

constexpr std::string_view kColumns = "id, name, type, parent_id, description, tags, properties, twin_id";
constexpr std::string_view kContains = "contains";

Result<Asset> read(const Statement& s) {
    Asset a;
    a.id = s.text(0);
    a.name = s.text(1);
    a.type = s.text(2);
    a.parent_id = s.opt_text(3);
    a.description = s.text(4);
    auto tags = json::parse(s.text(5));
    if (!tags) return std::move(tags).error();
    a.tags = std::move(tags).value();
    auto props = json::parse(s.text(6));
    if (!props) return std::move(props).error();
    a.properties = std::move(props).value();
    a.twin_id = s.opt_text(7);
    return a;
}

struct Where {
    std::string sql;
    std::vector<std::string> args;
};

Where where_for(const AssetFilter& f) {
    Where w;
    std::vector<std::string> c;
    if (f.type) {
        w.args.push_back(*f.type);
        c.push_back("type = ?" + std::to_string(w.args.size()));
    }
    if (f.parent_id) {
        if (f.parent_id->empty()) {
            c.emplace_back("parent_id IS NULL");
        } else {
            w.args.push_back(*f.parent_id);
            c.push_back("parent_id = ?" + std::to_string(w.args.size()));
        }
    }
    if (f.text && !f.text->empty()) {
        w.args.push_back("%" + *f.text + "%");
        const auto n = std::to_string(w.args.size());
        c.push_back("(id LIKE ?" + n + " OR name LIKE ?" + n + " OR type LIKE ?" + n + " OR description LIKE ?" + n +
                    ")");
    }
    for (std::size_t i = 0; i < c.size(); ++i) w.sql += (i == 0 ? " WHERE " : " AND ") + c[i];
    return w;
}

Result<std::vector<Asset>> collect(Statement& q) {
    std::vector<Asset> out;
    for (;;) {
        auto row = q.step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        auto a = read(q);
        if (!a) return std::move(a).error();
        out.push_back(std::move(a).value());
    }
    return out;
}

Result<std::vector<Relationship>> collect_rel(Statement& q) {
    std::vector<Relationship> out;
    for (;;) {
        auto row = q.step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        Relationship r;
        r.id = q.integer(0);
        r.source_id = q.text(1);
        r.type = q.text(2);
        r.target_id = q.text(3);
        auto p = json::parse(q.text(4));
        if (!p) return std::move(p).error();
        r.properties = std::move(p).value();
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace

Status AssetRepository::upsert(const Asset& a) {
    if (a.id.empty() || a.name.empty() || a.type.empty()) {
        return make_error(ErrorCode::InvalidArgument, "assets need an id, a name and a type");
    }
    if (a.parent_id && *a.parent_id == a.id) {
        return make_error(ErrorCode::InvalidArgument, "an asset cannot contain itself").with("id", a.id);
    }
    auto q = db_.prepare("INSERT INTO assets(" + std::string(kColumns) +
                         ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) ON CONFLICT(id) DO UPDATE SET name = excluded.name, "
                         "type = excluded.type, parent_id = excluded.parent_id, description = excluded.description, "
                         "tags = excluded.tags, properties = excluded.properties, twin_id = excluded.twin_id");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, a.id)
        .bind(2, a.name)
        .bind(3, a.type)
        .bind(4, a.parent_id)
        .bind(5, a.description)
        .bind(6, a.tags.dump())
        .bind(7, a.properties.dump())
        .bind(8, a.twin_id);
    return q.value().run();
}

Result<Asset> AssetRepository::get(std::string_view id) const {
    auto q = db_.prepare("SELECT " + std::string(kColumns) + " FROM assets WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto all = collect(q.value());
    if (!all) return std::move(all).error();
    if (all.value().empty()) return make_error(ErrorCode::NotFound, "no such asset").with("id", std::string(id));
    return std::move(all.value().front());
}

Result<std::vector<Asset>> AssetRepository::list(const AssetFilter& f) const {
    const Where w = where_for(f);
    auto q = db_.prepare("SELECT " + std::string(kColumns) + " FROM assets" + w.sql +
                         " ORDER BY name COLLATE NOCASE LIMIT ?101 OFFSET ?102");
    if (!q) return std::move(q).error();
    for (std::size_t i = 0; i < w.args.size(); ++i) q.value().bind(static_cast<int>(i + 1), w.args[i]);
    q.value().bind(101, f.limit).bind(102, f.offset);
    return collect(q.value());
}

Result<std::int64_t> AssetRepository::count(const AssetFilter& f) const {
    const Where w = where_for(f);
    auto q = db_.prepare("SELECT count(*) FROM assets" + w.sql);
    if (!q) return std::move(q).error();
    for (std::size_t i = 0; i < w.args.size(); ++i) q.value().bind(static_cast<int>(i + 1), w.args[i]);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    return q.value().integer(0);
}

Result<std::vector<Asset>> AssetRepository::ancestors(std::string_view id) const {
    std::vector<Asset> chain;
    auto current = get(id);
    if (!current) return std::move(current).error();
    std::set<std::string> seen{current.value().id};
    std::optional<std::string> parent = current.value().parent_id;
    while (parent) {
        if (!seen.insert(*parent).second) break;  // defensive: cycles in corrupted data
        auto p = get(*parent);
        if (!p) return std::move(p).error();
        parent = p.value().parent_id;
        chain.push_back(std::move(p).value());
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

Result<std::int64_t> AssetRepository::child_count(std::string_view id) const {
    auto q = db_.prepare("SELECT count(*) FROM assets WHERE parent_id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    return q.value().integer(0);
}

Result<Relationship> AssetRepository::relate(std::string_view source, std::string_view type, std::string_view target,
                                             const json::Json& properties) {
    if (type.empty() || type == kContains) {
        return make_error(ErrorCode::InvalidArgument, "relationship type must be non-empty and not 'contains' "
                                                      "(containment is the parent hierarchy)");
    }
    auto q = db_.prepare("INSERT INTO relationships(source_id, type, target_id, properties) VALUES(?1, ?2, ?3, ?4) "
                         "ON CONFLICT(source_id, type, target_id) DO UPDATE SET properties = excluded.properties");
    if (!q) return std::move(q).error();
    q.value().bind(1, source).bind(2, type).bind(3, target).bind(4, properties.dump());
    if (auto st = q.value().run(); !st) {
        return make_error(ErrorCode::NotFound, "both assets of a relationship must exist")
            .with("source", std::string(source))
            .with("target", std::string(target));
    }
    auto r = db_.prepare("SELECT id, source_id, type, target_id, properties FROM relationships WHERE source_id = ?1 AND "
                         "type = ?2 AND target_id = ?3");
    if (!r) return std::move(r).error();
    r.value().bind(1, source).bind(2, type).bind(3, target);
    auto rel = collect_rel(r.value());
    if (!rel) return std::move(rel).error();
    return std::move(rel.value().front());
}

Result<std::vector<Relationship>> AssetRepository::relationships_of(std::string_view id) const {
    auto q = db_.prepare("SELECT id, source_id, type, target_id, properties FROM relationships "
                         "WHERE source_id = ?1 OR target_id = ?1 ORDER BY type, source_id, target_id");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    return collect_rel(q.value());
}

Result<std::vector<std::pair<std::string, std::int64_t>>> AssetRepository::relationship_types() const {
    auto q = db_.prepare("SELECT type, count(*) FROM relationships GROUP BY type ORDER BY type");
    if (!q) return std::move(q).error();
    std::vector<std::pair<std::string, std::int64_t>> out;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        out.emplace_back(q.value().text(0), q.value().integer(1));
    }
    return out;
}

Result<std::vector<std::pair<std::string, std::int64_t>>> AssetRepository::asset_types() const {
    auto q = db_.prepare("SELECT type, count(*) FROM assets GROUP BY type ORDER BY type");
    if (!q) return std::move(q).error();
    std::vector<std::pair<std::string, std::int64_t>> out;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        out.emplace_back(q.value().text(0), q.value().integer(1));
    }
    return out;
}

Result<Neighborhood> AssetRepository::neighborhood(std::string_view focus, int depth,
                                                   const std::vector<std::string>& types, std::size_t max_nodes) const {
    auto root = get(focus);
    if (!root) return std::move(root).error();
    const bool all_types = types.empty();
    auto allowed = [&](std::string_view t) { return all_types || std::find(types.begin(), types.end(), t) != types.end(); };

    Neighborhood n;
    std::map<std::string, int> dist{{root.value().id, 0}};
    std::map<std::string, Asset> nodes{{root.value().id, root.value()}};
    std::vector<std::string> order{root.value().id};
    std::set<std::int64_t> edge_ids;
    std::set<std::pair<std::string, std::string>> hierarchy_edges;
    std::set<std::string> frontier;
    std::deque<std::string> queue{root.value().id};

    while (!queue.empty()) {
        const std::string id = queue.front();
        queue.pop_front();
        const int d = dist[id];
        // Neighbours: explicit relationships + hierarchy (parent and children).
        std::vector<std::pair<std::string, Relationship>> next;
        auto rels = relationships_of(id);
        if (!rels) return std::move(rels).error();
        for (auto& r : rels.value()) {
            if (!allowed(r.type)) continue;
            const std::string other = r.source_id == id ? r.target_id : r.source_id;
            next.emplace_back(other, std::move(r));
        }
        if (allowed(kContains)) {
            const Asset& self = nodes[id];
            if (self.parent_id) next.emplace_back(*self.parent_id, Relationship{0, *self.parent_id, std::string(kContains), id, {}});
            AssetFilter cf;
            cf.parent_id = id;
            cf.limit = static_cast<std::int64_t>(max_nodes) + 1;
            auto kids = list(cf);
            if (!kids) return std::move(kids).error();
            for (const auto& k : kids.value()) next.emplace_back(k.id, Relationship{0, id, std::string(kContains), k.id, {}});
        }
        for (auto& [other, rel] : next) {
            const bool known = nodes.count(other) != 0;
            if (!known) {
                if (d >= depth) {
                    frontier.insert(id);
                    continue;
                }
                if (nodes.size() >= max_nodes) {
                    n.truncated = true;
                    frontier.insert(id);
                    continue;
                }
                auto a = get(other);
                if (!a) return std::move(a).error();
                nodes.emplace(other, std::move(a).value());
                dist[other] = d + 1;
                order.push_back(other);
                queue.push_back(other);
            }
            if (rel.id == 0) {
                if (hierarchy_edges.insert({rel.source_id, rel.target_id}).second) n.edges.push_back(rel);
            } else if (edge_ids.insert(rel.id).second) {
                n.edges.push_back(rel);
            }
        }
    }
    for (const auto& id : order) n.nodes.push_back(nodes[id]);
    n.frontier.assign(frontier.begin(), frontier.end());
    return n;
}

json::Json to_json(const Asset& a) {
    return {{"id", a.id},
            {"name", a.name},
            {"type", a.type},
            {"parentId", a.parent_id ? json::Json(*a.parent_id) : json::Json(nullptr)},
            {"description", a.description},
            {"tags", a.tags},
            {"properties", a.properties},
            {"twinId", a.twin_id ? json::Json(*a.twin_id) : json::Json(nullptr)}};
}

json::Json to_json(const Relationship& r) {
    return {{"id", r.id}, {"sourceId", r.source_id}, {"type", r.type}, {"targetId", r.target_id},
            {"properties", r.properties}};
}

}  // namespace twin::platform
