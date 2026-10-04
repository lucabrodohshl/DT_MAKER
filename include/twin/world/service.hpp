/**
 * @file service.hpp
 * @brief The world's JSON API, shared by the HTTP server and in-process test ports.
 * @ingroup world
 *
 * WorldService is the single definition of the Physical Twin's external
 * interface: routing, request validation, JSON shapes and status codes. The
 * twin-world executable is only an HTTP adapter around handle(); tests drive
 * the very same handle() in-process (tests/support/in_process_world_port.hpp).
 * The two transports therefore cannot drift apart.
 *
 * Routes (JSON bodies; times in ticks, 1 tick = 1 ms of logical time):
 *
 * | Method | Path                    | Audience      | Purpose                                         |
 * |--------|-------------------------|---------------|-------------------------------------------------|
 * | GET    | /health                 | anyone        | liveness                                        |
 * | GET    | /scenario               | anyone        | scenario metadata and facility timeline         |
 * | GET    | /env/map/known          | Digital Twin  | published building knowledge (prior + learned)  |
 * | GET    | /env/map/updates?since= | Digital Twin  | update feed (sensor observations, notices)      |
 * | GET    | /env/mission            | Digital Twin  | mission: home, targets, drone parameters        |
 * | GET    | /env/hazards            | Digital Twin  | declared no-fly cells                           |
 * | POST   | /pt/command             | Digital Twin  | command to the flight controller                |
 * | POST   | /pt/step {"dt"}         | co-sim master | advance logical time; telemetry + PT events     |
 * | GET    | /pt/telemetry           | Digital Twin  | latest telemetry                                |
 * | POST   | /admin/reset            | operator      | restart the scenario from its file              |
 * | GET    | /observer/ground-truth  | visualisation | GROUND TRUTH map (never used by the twin)       |
 * | GET    | /observer/state         | visualisation | true drone state                                |
 * | POST   | /observer/inject        | operator      | change the ground truth ("act of god")          |
 *
 * Status codes: 200 success; 400 malformed request; 404 unknown route;
 * 405 wrong method; 409 command refused in the current flight mode;
 * 500 scenario could not be (re)loaded. Error bodies are
 * `{"error": {"code": "<ErrorCode>", "message": "..."}}`.
 */
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/world/world.hpp"

namespace twin::world {

/// @brief A transport-independent request (what an HTTP request carries).
struct ApiRequest {
    std::string method;                         ///< "GET" or "POST".
    std::string path;                           ///< Route path without query, e.g. "/env/map/updates".
    std::map<std::string, std::string> query;   ///< Decoded query parameters.
    std::string body;                           ///< Request body (JSON text, may be empty).
};

/// @brief A transport-independent reply.
struct ApiReply {
    int status{200};                        ///< HTTP status code.
    json::Json body = json::Json::object(); ///< JSON body.
};

/**
 * @brief Owner of the simulated world and the implementation of its API.
 *
 * Thread safety: handle() may be called concurrently; a reset swaps the world
 * atomically with respect to other requests.
 */
class WorldService {
public:
    /// @brief Load the scenario file and create the world.
    [[nodiscard]] static Result<std::unique_ptr<WorldService>> open(const std::filesystem::path& scenario_path);

    /// @brief Dispatch one request (see the route table in the file documentation).
    [[nodiscard]] ApiReply handle(const ApiRequest& request);

    /// @brief Convenience for GET requests.
    [[nodiscard]] ApiReply get(const std::string& path, std::map<std::string, std::string> query = {});
    /// @brief Convenience for POST requests with a JSON body.
    [[nodiscard]] ApiReply post(const std::string& path, const json::Json& body = json::Json::object());

    /// @brief Path of the scenario file this service runs.
    [[nodiscard]] const std::filesystem::path& scenario_path() const noexcept { return scenario_path_; }

private:
    explicit WorldService(std::filesystem::path scenario_path);
    Status reload();

    std::filesystem::path scenario_path_;
    std::mutex mutex_;
    std::unique_ptr<World> world_;
};

}  // namespace twin::world
