/**
 * @file runtime_link.hpp
 * @brief HTTP client of a monitor-mode twin-runtime: the only way the connector reaches the twin.
 * @ingroup ingest
 *
 * The connector never links the kernel, the ledger or the runtime: it submits through the
 * runtime's public API (POST /runtime/pt-event, /runtime/telemetry, /runtime/advance) and reads
 * GET /runtime/state, exactly as any other Physical Twin adapter would. The runtime's kernel
 * accepts or rejects every input and its ledger records the verdict.
 */
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "twin/core/logical_time.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::ingest {

/// @brief The parts of the runtime's state the connector needs.
struct RuntimeState {
    Ticks ticks{0};                   ///< Committed logical time.
    std::optional<Ticks> deadline;    ///< Latest admissible logical time (location invariant), if any.
    bool failed{false};               ///< The session is fail-stopped (ledger unavailable).
};

/// @brief Abstract runtime API (an interface so that tests can substitute a fake).
class RuntimeApi {
public:
    virtual ~RuntimeApi() = default;
    RuntimeApi() = default;
    RuntimeApi(const RuntimeApi&) = delete;
    RuntimeApi& operator=(const RuntimeApi&) = delete;
    RuntimeApi(RuntimeApi&&) = delete;
    RuntimeApi& operator=(RuntimeApi&&) = delete;

    /// @brief GET /runtime/state.
    [[nodiscard]] virtual Result<RuntimeState> state() = 0;
    /// @brief POST /runtime/pt-event: the kernel's verdict view (accepted or rejected), or an
    ///        error (label not translated, runtime unreachable, ...).
    [[nodiscard]] virtual Result<json::Json> pt_event(const std::string& label, Ticks at, const json::Json& detail) = 0;
    /// @brief POST /runtime/telemetry.
    [[nodiscard]] virtual Status telemetry(Ticks at, const json::Json& fields) = 0;
    /// @brief POST /runtime/advance: the verdict view of letting time pass to @p to.
    [[nodiscard]] virtual Result<json::Json> advance(Ticks to) = 0;
};

/// @brief RuntimeApi over HTTP (keep-alive connection, serialised calls).
class HttpRuntime final : public RuntimeApi {
public:
    /// @brief Client of the runtime at @p base_url (e.g. "http://127.0.0.1:18123").
    explicit HttpRuntime(std::string base_url);
    ~HttpRuntime() override;
    HttpRuntime(const HttpRuntime&) = delete;
    HttpRuntime& operator=(const HttpRuntime&) = delete;
    HttpRuntime(HttpRuntime&&) = delete;
    HttpRuntime& operator=(HttpRuntime&&) = delete;

    [[nodiscard]] Result<RuntimeState> state() override;
    [[nodiscard]] Result<json::Json> pt_event(const std::string& label, Ticks at, const json::Json& detail) override;
    [[nodiscard]] Status telemetry(Ticks at, const json::Json& fields) override;
    [[nodiscard]] Result<json::Json> advance(Ticks to) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace twin::ingest
