/**
 * @file engine.hpp
 * @brief Studio engine services: the formal authoring operations behind Studio Mode, as a route table.
 * @ingroup studio_engine
 *
 * @defgroup studio_engine Studio engine services
 * @brief Stateless (and sandboxed) engine operations exposed to the Studio UI under /api/v1.
 *
 * Studio's platform owns twin types, instances, versions, jobs and deployments.
 * This module owns the engine operations those workflows need — parsing,
 * validating, formatting, rendering, importing, diffing and compiling canonical
 * models (and, in later sections, alignment explanations, property analysis,
 * sandbox previews and data-binding tests). Every conclusion is computed by the
 * formal tools (twin::authoring, the compiler, the aligner); nothing is
 * stored here except scratch files and sandbox sessions.
 *
 * The routes are a transport-independent table (Route). Studio's HTTP server
 * registers each entry with its own uniform error handling; mount() does the
 * same for a plain httplib server (tests, tools). The contract is
 * api/studio-engine.openapi.yaml.
 */
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::studio_engine {

/// @brief A request as seen by a route handler.
struct Request {
    std::map<std::string, std::string> path;   ///< Path parameters (":id" -> value).
    std::map<std::string, std::string> query;  ///< Query parameters.
    json::Json body = json::Json::object();    ///< Parsed JSON body (empty object when there is none).
    std::string actor{"studio-user"};          ///< Caller for audit records (not authentication).
};

/// @brief A route handler: the API-ready response body, or a structured error.
using Handler = std::function<Result<json::Json>(const Request&)>;

/// @brief One entry of the route table.
struct Route {
    std::string method;   ///< "GET", "POST" or "DELETE".
    std::string pattern;  ///< httplib pattern, e.g. "/api/v1/authoring/models/parse".
    Handler handler;      ///< The operation.
    int ok_status{200};   ///< Status of a successful response.
    std::string summary;  ///< One-line description (matches the OpenAPI summary).
};

/// @brief Configuration of the engine services.
struct EngineConfig {
    std::filesystem::path work_dir;  ///< Scratch space (compilations, sandbox ledgers); created if missing.
};

/// @brief The engine services (see file documentation). Thread-safe.
class Engine {
public:
    /// @brief Engine using @p config.
    explicit Engine(EngineConfig config);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    /// @brief The route table (handlers reference this engine, which must outlive them).
    [[nodiscard]] std::vector<Route> routes();

    /// @brief The configuration.
    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }

private:
    EngineConfig config_;
};

/// @brief HTTP status of an error code (400, 404, 409, 422, 503 or 500), as Studio maps them.
[[nodiscard]] int http_status(ErrorCode code) noexcept;

/// @brief Studio's error envelope: {"error":{"code","message","context":[{"key","value"}]}}.
[[nodiscard]] json::Json error_body(const Error& error);

}  // namespace twin::studio_engine
