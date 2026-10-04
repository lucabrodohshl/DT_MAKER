/**
 * @file database.hpp
 * @brief Minimal RAII wrapper over SQLite: connection, prepared statements, transactions, migrations.
 * @ingroup platform
 *
 * @defgroup platform Studio platform (persistence and lifecycle)
 * @brief Authoritative engineering and operational records of Verified Twin Studio.
 *
 * All Studio records except immutable content live in one SQLite database
 * (`<data-dir>/studio.db`, WAL mode). Immutable content (ontology texts,
 * models, evidence documents) lives in the content-addressed ObjectStore.
 * Every repository of this module takes a `Database&`; none owns global state.
 *
 * Not thread-safe: the server serialises access to one Database with a mutex
 * (see twin::studio::Services).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "twin/core/result.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace twin::platform {

class Database;

/**
 * @brief A prepared statement. Bind with 1-based indices, then step().
 *
 * Text columns are returned as std::string; NULL columns as std::nullopt via
 * the `opt_*` accessors.
 */
class Statement {
public:
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    /// @name Binding (chainable; indices are 1-based)
    /// @{
    Statement& bind(int index, std::string_view text);
    Statement& bind(int index, const std::string& text) { return bind(index, std::string_view(text)); }
    Statement& bind(int index, const char* text) { return bind(index, std::string_view(text)); }
    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, double value);
    Statement& bind_null(int index);
    Statement& bind(int index, const std::optional<std::string>& text);
    Statement& bind(int index, const std::optional<std::int64_t>& value);
    /// @}

    /// @brief Advance: true if a row is available, false when done.
    [[nodiscard]] Result<bool> step();
    /// @brief Execute to completion (for statements without result rows).
    [[nodiscard]] Status run();
    /// @brief Reset for re-execution (keeps bindings).
    void reset();

    /// @name Column access (0-based)
    /// @{
    [[nodiscard]] std::string text(int column) const;
    [[nodiscard]] std::int64_t integer(int column) const;
    [[nodiscard]] double real(int column) const;
    [[nodiscard]] bool is_null(int column) const;
    [[nodiscard]] std::optional<std::string> opt_text(int column) const;
    [[nodiscard]] std::optional<std::int64_t> opt_integer(int column) const;
    [[nodiscard]] std::optional<double> opt_real(int column) const;
    /// @}

private:
    friend class Database;
    Statement(sqlite3* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}
    sqlite3* db_{nullptr};
    sqlite3_stmt* stmt_{nullptr};
};

/// @brief An open SQLite database with the Studio schema applied.
class Database {
public:
    /**
     * @brief Open (creating if needed) and migrate the database at @p path.
     * Use ":memory:" for tests.
     */
    [[nodiscard]] static Result<std::unique_ptr<Database>> open(const std::string& path);

    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    /// @brief Prepare a statement.
    [[nodiscard]] Result<Statement> prepare(std::string_view sql);
    /// @brief Execute one or more statements without results.
    [[nodiscard]] Status exec(std::string_view sql);
    /// @brief Row id of the last INSERT.
    [[nodiscard]] std::int64_t last_insert_id() const;
    /// @brief True while a transaction is open on this connection.
    [[nodiscard]] bool in_transaction() const;
    /// @brief Rows changed by the last statement.
    [[nodiscard]] std::int64_t changes() const;
    /// @brief Current schema version (after migrations).
    [[nodiscard]] int schema_version() const noexcept { return schema_version_; }

    /// @brief Allocate the next identifier of a sequence, rendered "<prefix>-<n zero-padded to 4>".
    [[nodiscard]] Result<std::string> next_id(std::string_view sequence, std::string_view prefix);

private:
    explicit Database(sqlite3* db) : db_(db) {}
    Status migrate();
    sqlite3* db_{nullptr};
    int schema_version_{0};
};

/**
 * @brief RAII transaction: BEGIN IMMEDIATE on construction, ROLLBACK on
 * destruction unless commit() succeeded.
 *
 * Transactions nest by joining: a Transaction created while another one is
 * open neither begins nor commits; the outermost one decides. An error in an
 * inner scope therefore propagates and makes the outer scope roll back.
 */
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    /// @brief Whether BEGIN succeeded.
    [[nodiscard]] const Status& begun() const noexcept { return begun_; }
    /// @brief COMMIT.
    [[nodiscard]] Status commit();

private:
    Database& db_;
    bool nested_{false};
    Status begun_;
    bool done_{false};
};

}  // namespace twin::platform
