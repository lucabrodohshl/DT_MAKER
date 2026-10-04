/**
 * @file audit.cpp
 * @brief Hash-chained engineering audit (see audit.hpp).
 */
#include "twin/platform/audit.hpp"

#include "twin/core/sha256.hpp"

namespace twin::platform {

namespace {

const std::string kGenesis(64, '0');

struct Where {
    std::string sql;
    std::vector<std::string> args;
};

Where where_for(const AuditFilter& f) {
    Where w;
    std::vector<std::string> clauses;
    if (f.operation_prefix) {
        w.args.push_back(*f.operation_prefix + "%");
        clauses.push_back("operation LIKE ?" + std::to_string(w.args.size()));
    }
    if (f.subject) {
        w.args.push_back(*f.subject);
        const auto n = std::to_string(w.args.size());
        w.args.push_back(*f.subject + "@%");
        clauses.push_back("(subject = ?" + n + " OR subject LIKE ?" + std::to_string(w.args.size()) + ")");
    }
    if (f.actor) {
        w.args.push_back(*f.actor);
        clauses.push_back("actor = ?" + std::to_string(w.args.size()));
    }
    for (std::size_t i = 0; i < clauses.size(); ++i) w.sql += (i == 0 ? " WHERE " : " AND ") + clauses[i];
    return w;
}

Result<AuditRecord> read(const Statement& s) {
    AuditRecord r;
    r.seq = s.integer(0);
    r.at = s.text(1);
    r.actor = s.text(2);
    r.operation = s.text(3);
    r.outcome = s.text(4);
    r.subject = s.text(5);
    auto d = json::parse(s.text(6));
    if (!d) return std::move(d).error();
    r.details = std::move(d).value();
    r.prev_hash = s.text(7);
    r.hash = s.text(8);
    return r;
}

constexpr std::string_view kColumns = "seq, at, actor, operation, outcome, subject, details, prev_hash, hash";

}  // namespace

Result<std::string> AuditLog::canonical_payload(const AuditRecord& r) {
    const json::Json body = {{"seq", r.seq},           {"at", r.at},           {"actor", r.actor},
                             {"operation", r.operation}, {"outcome", r.outcome}, {"subject", r.subject},
                             {"details", r.details},   {"prev_hash", r.prev_hash}};
    return json::canonical_dump(body);
}

Result<AuditRecord> AuditLog::append(std::string_view operation, std::string_view outcome, std::string_view subject,
                                     const json::Json& details, std::string_view actor) {
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto head = db_.prepare("SELECT seq, hash FROM audit ORDER BY seq DESC LIMIT 1");
    if (!head) return std::move(head).error();
    auto row = head.value().step();
    if (!row) return std::move(row).error();
    AuditRecord r;
    r.seq = row.value() ? head.value().integer(0) + 1 : 1;
    r.prev_hash = row.value() ? head.value().text(1) : kGenesis;
    r.at = iso8601_utc(clock_.now_ms());
    r.actor = actor;
    r.operation = operation;
    r.outcome = outcome;
    r.subject = subject;
    r.details = details.is_null() ? json::Json::object() : details;
    auto payload = canonical_payload(r);
    if (!payload) return std::move(payload).error();
    r.hash = sha256_hex(r.prev_hash + "\n" + payload.value());
    auto ins = db_.prepare("INSERT INTO audit(" + std::string(kColumns) + ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
    if (!ins) return std::move(ins).error();
    auto details_text = json::canonical_dump(r.details);
    if (!details_text) return std::move(details_text).error();
    ins.value()
        .bind(1, r.seq)
        .bind(2, r.at)
        .bind(3, r.actor)
        .bind(4, r.operation)
        .bind(5, r.outcome)
        .bind(6, r.subject)
        .bind(7, details_text.value())
        .bind(8, r.prev_hash)
        .bind(9, r.hash);
    if (auto st = ins.value().run(); !st) return st.error();
    if (auto st = tx.commit(); !st) return st.error();
    return r;
}

Result<std::vector<AuditRecord>> AuditLog::list(const AuditFilter& filter) const {
    const Where w = where_for(filter);
    auto q = db_.prepare("SELECT " + std::string(kColumns) + " FROM audit" + w.sql +
                         " ORDER BY seq DESC LIMIT ?101 OFFSET ?102");
    if (!q) return std::move(q).error();
    for (std::size_t i = 0; i < w.args.size(); ++i) q.value().bind(static_cast<int>(i + 1), w.args[i]);
    q.value().bind(101, filter.limit).bind(102, filter.offset);
    std::vector<AuditRecord> out;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        auto r = read(q.value());
        if (!r) return std::move(r).error();
        out.push_back(std::move(r).value());
    }
    return out;
}

Result<std::int64_t> AuditLog::count(const AuditFilter& filter) const {
    const Where w = where_for(filter);
    auto q = db_.prepare("SELECT count(*) FROM audit" + w.sql);
    if (!q) return std::move(q).error();
    for (std::size_t i = 0; i < w.args.size(); ++i) q.value().bind(static_cast<int>(i + 1), w.args[i]);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    return q.value().integer(0);
}

Result<AuditVerification> AuditLog::verify() const {
    AuditVerification v;
    v.verified_at = iso8601_utc(clock_.now_ms());
    auto q = db_.prepare("SELECT " + std::string(kColumns) + " FROM audit ORDER BY seq ASC");
    if (!q) return std::move(q).error();
    std::string prev = kGenesis;
    std::int64_t expected_seq = 1;
    v.head_hash = kGenesis;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        auto r = read(q.value());
        if (!r) {
            v.first_invalid = q.value().integer(0);
            v.reason = "record is not readable: " + r.error().message;
            return v;
        }
        ++v.records;
        const AuditRecord& rec = r.value();
        if (rec.seq != expected_seq) {
            v.first_invalid = rec.seq;
            v.reason = "sequence gap: expected record " + std::to_string(expected_seq) + " (deleted or reordered)";
            return v;
        }
        if (rec.prev_hash != prev) {
            v.first_invalid = rec.seq;
            v.reason = "broken link: prev_hash does not match the preceding record";
            return v;
        }
        auto payload = canonical_payload(rec);
        if (!payload || sha256_hex(rec.prev_hash + "\n" + payload.value()) != rec.hash) {
            v.first_invalid = rec.seq;
            v.reason = "content modified: the record no longer matches its hash";
            return v;
        }
        prev = rec.hash;
        ++expected_seq;
    }
    v.valid = true;
    v.head_hash = prev;
    return v;
}

json::Json to_json(const AuditRecord& r) {
    return {{"seq", r.seq},           {"at", r.at},           {"actor", r.actor},
            {"operation", r.operation}, {"outcome", r.outcome}, {"subject", r.subject},
            {"details", r.details},   {"prevHash", r.prev_hash}, {"hash", r.hash}};
}

json::Json to_json(const AuditVerification& v) {
    return {{"valid", v.valid},
            {"records", v.records},
            {"firstInvalidSeq", v.first_invalid ? json::Json(*v.first_invalid) : json::Json(nullptr)},
            {"reason", v.reason},
            {"headHash", v.head_hash},
            {"verifiedAt", v.verified_at}};
}

}  // namespace twin::platform
