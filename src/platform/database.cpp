/**
 * @file database.cpp
 * @brief SQLite wrapper and the Studio schema (see database.hpp).
 */
#include "twin/platform/database.hpp"

#include <array>
#include <cstdio>
#include <utility>

#include <sqlite3.h>

namespace twin::platform {

namespace {

Error sqlite_error(sqlite3* db, std::string_view what) {
    return make_error(ErrorCode::Unavailable, std::string(what) + ": " + (db != nullptr ? sqlite3_errmsg(db) : "?"));
}

/// @brief One schema migration.
struct Migration {
    std::string_view sql;        ///< Statements, run in one transaction.
    bool rebuilds_tables{false}; ///< Rebuilds a referenced table: foreign keys are off during the
                                 ///< transaction and fully re-checked before it commits.
};

/**
 * Schema migrations, applied in order; `meta.schema_version` records progress.
 * Never edit a released migration: append a new one.
 */
constexpr std::array<Migration, 3> kMigrations = {{
    {R"SQL(
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE sequences (name TEXT PRIMARY KEY, next INTEGER NOT NULL);

-- Versioned formal artefacts (ontology, interpretation, pt_model, dt_model).
CREATE TABLE artifacts (
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL,
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL
);
CREATE TABLE artifact_versions (
  artifact_id TEXT NOT NULL REFERENCES artifacts(id),
  version INTEGER NOT NULL,
  parent_version INTEGER,
  state TEXT NOT NULL,
  content_sha256 TEXT NOT NULL,
  refs TEXT NOT NULL DEFAULT '{}',
  change_description TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  published_at TEXT,
  published_by TEXT,
  PRIMARY KEY (artifact_id, version)
);

-- Formal evidence (validation, refinement, alignment, compilation, package). Immutable rows.
CREATE TABLE evidence (
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL,
  outcome TEXT NOT NULL,
  verdict TEXT NOT NULL,
  summary TEXT NOT NULL,
  checker TEXT NOT NULL,
  document_sha256 TEXT NOT NULL,
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL,
  change_id TEXT
);
CREATE TABLE evidence_inputs (
  evidence_id TEXT NOT NULL REFERENCES evidence(id),
  role TEXT NOT NULL,
  artifact_id TEXT NOT NULL,
  version INTEGER NOT NULL,
  sha256 TEXT NOT NULL,
  PRIMARY KEY (evidence_id, role)
);
CREATE INDEX evidence_inputs_by_artifact ON evidence_inputs(artifact_id, version);

-- Twins, packages, deployments, change workspaces.
CREATE TABLE twins (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  asset_id TEXT,
  description TEXT NOT NULL DEFAULT '',
  model_id TEXT NOT NULL,
  ticks_per_unit INTEGER NOT NULL DEFAULT 1000,
  runtime_url TEXT,
  presentation TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL
);
CREATE TABLE packages (
  id TEXT PRIMARY KEY,
  twin_id TEXT NOT NULL REFERENCES twins(id),
  directory TEXT NOT NULL,
  package_hash TEXT NOT NULL,
  ir_sha256 TEXT NOT NULL,
  model_version TEXT NOT NULL,
  bindings TEXT NOT NULL,
  evidence_id TEXT,
  state TEXT NOT NULL,
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL,
  released_at TEXT,
  change_id TEXT
);
CREATE TABLE deployments (
  seq INTEGER PRIMARY KEY AUTOINCREMENT,
  id TEXT NOT NULL UNIQUE,
  twin_id TEXT NOT NULL REFERENCES twins(id),
  package_id TEXT NOT NULL REFERENCES packages(id),
  previous_package_id TEXT,
  kind TEXT NOT NULL,
  reason TEXT NOT NULL DEFAULT '',
  deployed_at TEXT NOT NULL,
  deployed_by TEXT NOT NULL
);
CREATE TABLE changes (
  id TEXT PRIMARY KEY,
  twin_id TEXT NOT NULL REFERENCES twins(id),
  title TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  state TEXT NOT NULL,
  artifacts TEXT NOT NULL DEFAULT '[]',
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL,
  closed_at TEXT
);

-- Engineering audit trail: append-only, hash-chained (tamper-evident).
CREATE TABLE audit (
  seq INTEGER PRIMARY KEY,
  at TEXT NOT NULL,
  actor TEXT NOT NULL,
  operation TEXT NOT NULL,
  outcome TEXT NOT NULL,
  subject TEXT NOT NULL,
  details TEXT NOT NULL,
  prev_hash TEXT NOT NULL,
  hash TEXT NOT NULL
);

-- Asset registry and knowledge graph (instances; NOT the formal ontology).
CREATE TABLE assets (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  type TEXT NOT NULL,
  parent_id TEXT REFERENCES assets(id),
  description TEXT NOT NULL DEFAULT '',
  tags TEXT NOT NULL DEFAULT '[]',
  properties TEXT NOT NULL DEFAULT '{}',
  twin_id TEXT
);
CREATE INDEX assets_by_parent ON assets(parent_id);
CREATE TABLE relationships (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_id TEXT NOT NULL REFERENCES assets(id),
  type TEXT NOT NULL,
  target_id TEXT NOT NULL REFERENCES assets(id),
  properties TEXT NOT NULL DEFAULT '{}',
  UNIQUE (source_id, type, target_id)
);
CREATE INDEX relationships_by_target ON relationships(target_id);

-- Telemetry history (observations; not formal evidence).
CREATE TABLE telemetry_channels (
  id TEXT PRIMARY KEY,
  asset_id TEXT NOT NULL REFERENCES assets(id),
  name TEXT NOT NULL,
  value_type TEXT NOT NULL,
  unit TEXT NOT NULL DEFAULT '',
  ontology_symbol TEXT,
  source TEXT NOT NULL DEFAULT '',
  expected_period_ms INTEGER NOT NULL DEFAULT 1000,
  presentation TEXT NOT NULL DEFAULT '{}'
);
CREATE TABLE telemetry_samples (
  channel_id TEXT NOT NULL REFERENCES telemetry_channels(id),
  observed_ms INTEGER NOT NULL,
  ingested_ms INTEGER NOT NULL,
  value_num REAL,
  value_text TEXT,
  quality TEXT NOT NULL
);
CREATE INDEX telemetry_by_channel_time ON telemetry_samples(channel_id, observed_ms);
)SQL"},
    // v2: logical model time of a sample when the source provides one (runtime-fed channels).
    {R"SQL(
ALTER TABLE telemetry_samples ADD COLUMN logical_ticks INTEGER;
)SQL"},
    // v3: Twin Blueprints (versioned engineering definitions), their deployment bundles, and
    // twins as Blueprint instances. Packages may now be owned by a Blueprint version
    // ("blueprint:<id>") as well as by a twin, so their foreign key to twins is dropped.
    {R"SQL(
CREATE TABLE blueprints (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  domain TEXT NOT NULL DEFAULT 'generic',
  icon TEXT NOT NULL DEFAULT 'boxes',
  template_id TEXT,
  cloned_from TEXT,
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL
);
CREATE TABLE blueprint_versions (
  blueprint_id TEXT NOT NULL REFERENCES blueprints(id),
  version INTEGER NOT NULL,
  state TEXT NOT NULL,
  revision INTEGER NOT NULL,
  parent_version INTEGER,
  document TEXT NOT NULL,
  document_sha256 TEXT NOT NULL,
  pins TEXT NOT NULL DEFAULT '{}',
  package_id TEXT,
  bundle_id TEXT,
  note TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  updated_by TEXT NOT NULL,
  published_at TEXT,
  PRIMARY KEY (blueprint_id, version)
);
CREATE TABLE bundles (
  id TEXT PRIMARY KEY,
  blueprint_id TEXT NOT NULL REFERENCES blueprints(id),
  version INTEGER NOT NULL,
  package_id TEXT NOT NULL,
  directory TEXT NOT NULL,
  bundle_hash TEXT NOT NULL,
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL
);
ALTER TABLE twins ADD COLUMN blueprint_id TEXT;
ALTER TABLE twins ADD COLUMN blueprint_version INTEGER;
ALTER TABLE twins ADD COLUMN instance_config TEXT NOT NULL DEFAULT '{}';
ALTER TABLE twins ADD COLUMN world_url TEXT;
ALTER TABLE twins ADD COLUMN desired_state TEXT NOT NULL DEFAULT 'stopped';
CREATE TABLE packages_v3 (
  id TEXT PRIMARY KEY,
  twin_id TEXT NOT NULL,
  directory TEXT NOT NULL,
  package_hash TEXT NOT NULL,
  ir_sha256 TEXT NOT NULL,
  model_version TEXT NOT NULL,
  bindings TEXT NOT NULL,
  evidence_id TEXT,
  state TEXT NOT NULL,
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL,
  released_at TEXT,
  change_id TEXT
);
INSERT INTO packages_v3 SELECT id, twin_id, directory, package_hash, ir_sha256, model_version, bindings, evidence_id,
  state, created_at, created_by, released_at, change_id FROM packages;
DROP TABLE packages;
ALTER TABLE packages_v3 RENAME TO packages;
)SQL", true},
}};

}  // namespace

// --- Statement ------------------------------------------------------------------

Statement::~Statement() {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}
Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr)) {}
Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) sqlite3_finalize(stmt_);
        db_ = std::exchange(other.db_, nullptr);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

Statement& Statement::bind(int index, std::string_view text) {
    sqlite3_bind_text(stmt_, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
    return *this;
}
Statement& Statement::bind(int index, std::int64_t value) {
    sqlite3_bind_int64(stmt_, index, value);
    return *this;
}
Statement& Statement::bind(int index, double value) {
    sqlite3_bind_double(stmt_, index, value);
    return *this;
}
Statement& Statement::bind_null(int index) {
    sqlite3_bind_null(stmt_, index);
    return *this;
}
Statement& Statement::bind(int index, const std::optional<std::string>& text) {
    return text ? bind(index, std::string_view(*text)) : bind_null(index);
}
Statement& Statement::bind(int index, const std::optional<std::int64_t>& value) {
    return value ? bind(index, *value) : bind_null(index);
}

Result<bool> Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    if (rc == SQLITE_CONSTRAINT) {
        return make_error(ErrorCode::StateError, std::string("constraint violated: ") + sqlite3_errmsg(db_));
    }
    return sqlite_error(db_, "database step failed");
}

Status Statement::run() {
    for (;;) {
        auto r = step();
        if (!r) return std::move(r).error();
        if (!r.value()) return {};
    }
}

void Statement::reset() { sqlite3_reset(stmt_); }

std::string Statement::text(int column) const {
    const auto* p = sqlite3_column_text(stmt_, column);
    const int n = sqlite3_column_bytes(stmt_, column);
    return p == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n));
}
std::int64_t Statement::integer(int column) const { return sqlite3_column_int64(stmt_, column); }
double Statement::real(int column) const { return sqlite3_column_double(stmt_, column); }
bool Statement::is_null(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }
std::optional<std::string> Statement::opt_text(int column) const {
    return is_null(column) ? std::nullopt : std::optional<std::string>(text(column));
}
std::optional<std::int64_t> Statement::opt_integer(int column) const {
    return is_null(column) ? std::nullopt : std::optional<std::int64_t>(integer(column));
}
std::optional<double> Statement::opt_real(int column) const {
    return is_null(column) ? std::nullopt : std::optional<double>(real(column));
}

// --- Database -------------------------------------------------------------------

Result<std::unique_ptr<Database>> Database::open(const std::string& path) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        Error e = sqlite_error(raw, "cannot open database " + path);
        sqlite3_close(raw);
        return e;
    }
    std::unique_ptr<Database> db(new Database(raw));
    sqlite3_busy_timeout(raw, 5000);
    if (auto st = db->exec("PRAGMA foreign_keys = ON;"); !st) return std::move(st).error();
    if (path != ":memory:") {
        if (auto st = db->exec("PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;"); !st) {
            return std::move(st).error();
        }
    }
    if (auto st = db->migrate(); !st) return std::move(st).error();
    return db;
}

Database::~Database() {
    if (db_ != nullptr) sqlite3_close(db_);
}

Result<Statement> Database::prepare(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) return sqlite_error(db_, "cannot prepare statement");
    return Statement(db_, stmt);
}

Status Database::exec(std::string_view sql) {
    char* message = nullptr;
    const std::string copy(sql);
    const int rc = sqlite3_exec(db_, copy.c_str(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        std::string text = message != nullptr ? message : "unknown error";
        sqlite3_free(message);
        return make_error(ErrorCode::Unavailable, "database error: " + text);
    }
    return {};
}

std::int64_t Database::last_insert_id() const { return sqlite3_last_insert_rowid(db_); }
bool Database::in_transaction() const { return sqlite3_get_autocommit(db_) == 0; }
std::int64_t Database::changes() const { return sqlite3_changes64(db_); }

Status Database::migrate() {
    // Does the meta table exist yet? (Statements are finalised before any migration runs: an open
    // read of the schema would make DROP TABLE fail with SQLITE_LOCKED.)
    int version = 0;
    {
        bool has_meta = false;
        {
            auto probe = prepare("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='meta'");
            if (!probe) return std::move(probe).error();
            auto row = probe.value().step();
            if (!row) return std::move(row).error();
            has_meta = probe.value().integer(0) > 0;
        }
        if (has_meta) {
            auto q = prepare("SELECT value FROM meta WHERE key='schema_version'");
            if (!q) return std::move(q).error();
            auto r = q.value().step();
            if (!r) return std::move(r).error();
            if (r.value()) version = std::stoi(q.value().text(0));
        }
    }
    if (version > static_cast<int>(kMigrations.size())) {
        return make_error(ErrorCode::StateError,
                          "database schema version " + std::to_string(version) + " is newer than this twin-studio");
    }
    for (std::size_t i = static_cast<std::size_t>(version); i < kMigrations.size(); ++i) {
        const Migration& m = kMigrations[i];
        // SQLite's procedure for rebuilding a referenced table: foreign keys off outside the
        // transaction, a full foreign_key_check inside it before committing, then back on.
        if (m.rebuilds_tables) {
            if (auto st = exec("PRAGMA foreign_keys = OFF;"); !st) return st;
        }
        {
            Transaction tx(*this);
            if (!tx.begun()) return tx.begun().error();
            if (auto st = exec(m.sql); !st) return st;
            if (m.rebuilds_tables) {
                auto check = prepare("PRAGMA foreign_key_check");
                if (!check) return std::move(check).error();
                auto violation = check.value().step();
                if (!violation) return std::move(violation).error();
                if (violation.value()) {
                    return make_error(ErrorCode::IntegrityError, "schema migration would break a foreign key")
                        .with("table", check.value().text(0))
                        .with("migration", std::to_string(i + 1));
                }
            }
            {
                auto up = prepare("INSERT INTO meta(key, value) VALUES('schema_version', ?1) "
                                  "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
                if (!up) return std::move(up).error();
                if (auto st = up.value().bind(1, std::to_string(i + 1)).run(); !st) return st;
            }
            if (auto st = tx.commit(); !st) return st;
        }
        if (m.rebuilds_tables) {
            if (auto st = exec("PRAGMA foreign_keys = ON;"); !st) return st;
        }
    }
    schema_version_ = static_cast<int>(kMigrations.size());
    return {};
}

Result<std::string> Database::next_id(std::string_view sequence, std::string_view prefix) {
    auto up = prepare("INSERT INTO sequences(name, next) VALUES(?1, 2) "
                      "ON CONFLICT(name) DO UPDATE SET next = next + 1 RETURNING next - 1");
    if (!up) return std::move(up).error();
    up.value().bind(1, sequence);
    auto row = up.value().step();
    if (!row) return std::move(row).error();
    if (!row.value()) return make_error(ErrorCode::Internal, "sequence update returned no row");
    const std::int64_t n = up.value().integer(0);
    up.value().reset();
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%04lld", static_cast<long long>(n));
    return std::string(prefix) + "-" + buf.data();
}

// --- Transaction ----------------------------------------------------------------

Transaction::Transaction(Database& db)
    : db_(db), nested_(db.in_transaction()), begun_(nested_ ? Status{} : db.exec("BEGIN IMMEDIATE")) {}

Transaction::~Transaction() {
    if (begun_ && !done_ && !nested_) (void)db_.exec("ROLLBACK");
}

Status Transaction::commit() {
    if (!begun_) return begun_;
    done_ = true;
    return nested_ ? Status{} : db_.exec("COMMIT");
}

}  // namespace twin::platform
