/**
 * @file host.hpp
 * @brief Runtime hosts: what owns the live twin session behind the API.
 * @ingroup runtime
 *
 * A host owns the current TwinSession of one verified package and the way
 * inputs reach it:
 *  - CoSimDriver (cosim_driver.hpp): the runtime is the co-simulation master
 *    of a Physical Twin simulator reached through a WorldPort (the drone);
 *  - MonitorHost (monitor_host.hpp): the runtime monitors an external
 *    Physical Twin that pushes its events and telemetry (any aligned package,
 *    e.g. the industrial pump).
 * Both submit every semantic input through TwinSession::submit; neither has
 * another write path to semantic state.
 */
#pragma once

#include <memory>
#include <string>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/package/package.hpp"
#include "twin/runtime/event_hub.hpp"
#include "twin/runtime/session.hpp"

namespace twin::runtime {

/// @brief Common interface of runtime hosts (see file documentation).
class RuntimeHost {
public:
    virtual ~RuntimeHost() = default;
    RuntimeHost() = default;
    RuntimeHost(const RuntimeHost&) = delete;
    RuntimeHost& operator=(const RuntimeHost&) = delete;
    RuntimeHost(RuntimeHost&&) = delete;
    RuntimeHost& operator=(RuntimeHost&&) = delete;

    /// @brief Current session (shared: stays valid for callers across resets).
    [[nodiscard]] virtual std::shared_ptr<TwinSession> session() const = 0;
    /// @brief The verified package being executed.
    [[nodiscard]] virtual const package::LoadedPackage& package() const noexcept = 0;
    /// @brief Lifecycle status for the API.
    [[nodiscard]] virtual json::Json status() const = 0;
    /// @brief Start a fresh session (new ledger); earlier executions are kept.
    [[nodiscard]] virtual Status reset() = 0;
    /// @brief "cosimulation" or "monitor".
    [[nodiscard]] virtual std::string mode() const = 0;
};

/// @brief Compact description of a committed ledger record for live observers.
[[nodiscard]] json::Json ledger_event_summary(const SessionEvent& event);

/// @brief Publish every record committed by @p session on @p hub as a "ledger" event.
void stream_ledger(TwinSession& session, EventHub& hub);

}  // namespace twin::runtime
