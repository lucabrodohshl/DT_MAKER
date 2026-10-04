/**
 * @file evidence.cpp
 * @brief Evidence records (see evidence.hpp).
 */
#include "twin/platform/evidence.hpp"

#include <algorithm>
#include <array>

namespace twin::platform {

namespace {

constexpr std::array<std::pair<EvidenceKind, std::string_view>, 5> kKinds = {{
    {EvidenceKind::Validation, "validation"},
    {EvidenceKind::Refinement, "refinement"},
    {EvidenceKind::Alignment, "alignment"},
    {EvidenceKind::Compilation, "compilation"},
    {EvidenceKind::Package, "package"},
}};

constexpr std::array<std::pair<Outcome, std::string_view>, 4> kOutcomes = {{
    {Outcome::Pass, "pass"}, {Outcome::Fail, "fail"}, {Outcome::Unknown, "unknown"}, {Outcome::Error, "error"}}};

constexpr std::string_view kColumns =
    "id, kind, outcome, verdict, summary, checker, document_sha256, created_at, created_by, change_id";

/// @brief WHERE clause and its bindings for a filter.
struct Where {
    std::string sql;
    std::vector<std::string> text;
    std::optional<std::int64_t> version;
};

Where where_for(const EvidenceFilter& f) {
    Where w;
    std::vector<std::string> clauses;
    if (f.kind) {
        w.text.emplace_back(to_string(*f.kind));
        clauses.push_back("e.kind = ?" + std::to_string(w.text.size()));
    }
    if (f.change_id) {
        w.text.push_back(*f.change_id);
        clauses.push_back("e.change_id = ?" + std::to_string(w.text.size()));
    }
    if (f.artifact_id) {
        w.text.push_back(*f.artifact_id);
        std::string c = "EXISTS (SELECT 1 FROM evidence_inputs i WHERE i.evidence_id = e.id AND i.artifact_id = ?" +
                        std::to_string(w.text.size());
        if (f.version) c += " AND i.version = ?100";
        clauses.push_back(c + ")");
        w.version = f.version;
    }
    for (std::size_t i = 0; i < clauses.size(); ++i) w.sql += (i == 0 ? " WHERE " : " AND ") + clauses[i];
    return w;
}

void bind_where(Statement& s, const Where& w) {
    for (std::size_t i = 0; i < w.text.size(); ++i) s.bind(static_cast<int>(i + 1), w.text[i]);
    if (w.version) s.bind(100, *w.version);
}

}  // namespace

std::string_view to_string(EvidenceKind kind) noexcept {
    for (const auto& [k, s] : kKinds) {
        if (k == kind) return s;
    }
    return "validation";
}
Result<EvidenceKind> evidence_kind_from_string(std::string_view text) {
    for (const auto& [k, s] : kKinds) {
        if (s == text) return k;
    }
    return make_error(ErrorCode::InvalidArgument, "unknown evidence kind").with("kind", std::string(text));
}
std::string_view to_string(Outcome outcome) noexcept {
    for (const auto& [k, s] : kOutcomes) {
        if (k == outcome) return s;
    }
    return "unknown";
}
Result<Outcome> outcome_from_string(std::string_view text) {
    for (const auto& [k, s] : kOutcomes) {
        if (s == text) return k;
    }
    return make_error(ErrorCode::InvalidArgument, "unknown outcome").with("outcome", std::string(text));
}

const EvidenceInput* EvidenceRecord::input(std::string_view role) const noexcept {
    for (const auto& i : inputs) {
        if (i.role == role) return &i;
    }
    return nullptr;
}

Result<EvidenceRecord> EvidenceRepository::record(EvidenceKind kind, Outcome outcome, std::string_view verdict,
                                                  std::string_view summary, std::string_view checker,
                                                  const json::Json& document, std::vector<EvidenceInput> inputs,
                                                  std::string_view actor, const std::optional<std::string>& change_id) {
    auto canonical = json::canonical_dump(document);
    if (!canonical) return std::move(canonical).error();
    auto hash = store_.put(canonical.value());
    if (!hash) return std::move(hash).error();
    std::sort(inputs.begin(), inputs.end(), [](const auto& a, const auto& b) { return a.role < b.role; });

    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto id = db_.next_id("evidence", "EV");
    if (!id) return std::move(id).error();
    auto ins = db_.prepare("INSERT INTO evidence(" + std::string(kColumns) +
                           ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)");
    if (!ins) return std::move(ins).error();
    ins.value()
        .bind(1, id.value())
        .bind(2, to_string(kind))
        .bind(3, to_string(outcome))
        .bind(4, verdict)
        .bind(5, summary)
        .bind(6, checker)
        .bind(7, hash.value())
        .bind(8, iso8601_utc(clock_.now_ms()))
        .bind(9, actor)
        .bind(10, change_id);
    if (auto st = ins.value().run(); !st) return st.error();
    for (const auto& in : inputs) {
        auto i = db_.prepare("INSERT INTO evidence_inputs(evidence_id, role, artifact_id, version, sha256) "
                             "VALUES(?1, ?2, ?3, ?4, ?5)");
        if (!i) return std::move(i).error();
        i.value().bind(1, id.value()).bind(2, in.role).bind(3, in.ref.artifact_id).bind(4, in.ref.version).bind(5, in.sha256);
        if (auto st = i.value().run(); !st) return st.error();
    }
    if (auto st = tx.commit(); !st) return st.error();
    return get(id.value());
}

Result<std::vector<EvidenceInput>> EvidenceRepository::inputs_of(std::string_view id) const {
    auto q = db_.prepare("SELECT role, artifact_id, version, sha256 FROM evidence_inputs WHERE evidence_id = ?1 "
                         "ORDER BY role");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    std::vector<EvidenceInput> out;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        out.push_back({q.value().text(0), {q.value().text(1), q.value().integer(2)}, q.value().text(3)});
    }
    return out;
}

Result<std::vector<EvidenceRecord>> EvidenceRepository::list(const EvidenceFilter& filter) const {
    const Where w = where_for(filter);
    auto q = db_.prepare("SELECT " + std::string(kColumns) + " FROM evidence e" + w.sql +
                         " ORDER BY e.created_at DESC, e.id DESC LIMIT ?101 OFFSET ?102");
    if (!q) return std::move(q).error();
    bind_where(q.value(), w);
    q.value().bind(101, filter.limit).bind(102, filter.offset);
    std::vector<EvidenceRecord> out;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        const Statement& s = q.value();
        EvidenceRecord r;
        r.id = s.text(0);
        auto kind = evidence_kind_from_string(s.text(1));
        if (!kind) return std::move(kind).error();
        r.kind = kind.value();
        auto outcome = outcome_from_string(s.text(2));
        if (!outcome) return std::move(outcome).error();
        r.outcome = outcome.value();
        r.verdict = s.text(3);
        r.summary = s.text(4);
        r.checker = s.text(5);
        r.document_sha256 = s.text(6);
        r.created_at = s.text(7);
        r.created_by = s.text(8);
        r.change_id = s.opt_text(9);
        out.push_back(std::move(r));
    }
    for (auto& r : out) {
        auto in = inputs_of(r.id);
        if (!in) return std::move(in).error();
        r.inputs = std::move(in).value();
    }
    return out;
}

Result<std::int64_t> EvidenceRepository::count(const EvidenceFilter& filter) const {
    const Where w = where_for(filter);
    auto q = db_.prepare("SELECT count(*) FROM evidence e" + w.sql);
    if (!q) return std::move(q).error();
    bind_where(q.value(), w);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    return q.value().integer(0);
}

Result<EvidenceRecord> EvidenceRepository::get(std::string_view id) const {
    auto q = db_.prepare("SELECT " + std::string(kColumns) + " FROM evidence WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    if (!row.value()) return make_error(ErrorCode::NotFound, "no such evidence").with("id", std::string(id));
    const Statement& s = q.value();
    EvidenceRecord r;
    r.id = s.text(0);
    auto kind = evidence_kind_from_string(s.text(1));
    if (!kind) return std::move(kind).error();
    r.kind = kind.value();
    auto outcome = outcome_from_string(s.text(2));
    if (!outcome) return std::move(outcome).error();
    r.outcome = outcome.value();
    r.verdict = s.text(3);
    r.summary = s.text(4);
    r.checker = s.text(5);
    r.document_sha256 = s.text(6);
    r.created_at = s.text(7);
    r.created_by = s.text(8);
    r.change_id = s.opt_text(9);
    auto in = inputs_of(r.id);
    if (!in) return std::move(in).error();
    r.inputs = std::move(in).value();
    return r;
}

Result<json::Json> EvidenceRepository::document(const EvidenceRecord& record) const {
    auto bytes = store_.get(record.document_sha256);
    if (!bytes) return std::move(bytes).error();
    return json::parse_canonical(bytes.value());
}

Result<std::optional<EvidenceRecord>> EvidenceRepository::latest_for(EvidenceKind kind,
                                                                     const std::vector<EvidenceInput>& inputs) const {
    if (inputs.empty()) return std::optional<EvidenceRecord>{};
    // Candidates: same kind, having the first input exactly; then compare the full input sets.
    auto q = db_.prepare(
        "SELECT e.id FROM evidence e JOIN evidence_inputs i ON i.evidence_id = e.id "
        "WHERE e.kind = ?1 AND i.role = ?2 AND i.sha256 = ?3 AND i.artifact_id = ?4 "
        "ORDER BY e.created_at DESC, e.id DESC");
    if (!q) return std::move(q).error();
    q.value().bind(1, to_string(kind)).bind(2, inputs.front().role).bind(3, inputs.front().sha256).bind(
        4, inputs.front().ref.artifact_id);
    std::vector<std::string> ids;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        ids.push_back(q.value().text(0));
    }
    std::vector<EvidenceInput> wanted = inputs;
    std::sort(wanted.begin(), wanted.end(), [](const auto& a, const auto& b) { return a.role < b.role; });
    for (const auto& id : ids) {
        auto in = inputs_of(id);
        if (!in) return std::move(in).error();
        const auto& have = in.value();
        const bool same = have.size() == wanted.size() &&
                          std::equal(have.begin(), have.end(), wanted.begin(), [](const auto& a, const auto& b) {
                              return a.role == b.role && a.sha256 == b.sha256 && a.ref.artifact_id == b.ref.artifact_id;
                          });
        if (same) {
            auto r = get(id);
            if (!r) return std::move(r).error();
            return std::optional<EvidenceRecord>(std::move(r).value());
        }
    }
    return std::optional<EvidenceRecord>{};
}

json::Json to_json(const EvidenceRecord& r) {
    json::Json inputs = json::Json::array();
    for (const auto& i : r.inputs) {
        inputs.push_back({{"role", i.role}, {"ref", i.ref.str()}, {"artifactId", i.ref.artifact_id},
                          {"version", i.ref.version}, {"sha256", i.sha256}});
    }
    return {{"id", r.id},
            {"kind", std::string(to_string(r.kind))},
            {"outcome", std::string(to_string(r.outcome))},
            {"verdict", r.verdict},
            {"summary", r.summary},
            {"checker", r.checker},
            {"evidenceSha256", r.document_sha256},
            {"createdAt", r.created_at},
            {"createdBy", r.created_by},
            {"changeId", r.change_id ? json::Json(*r.change_id) : json::Json(nullptr)},
            {"inputs", inputs}};
}

}  // namespace twin::platform
