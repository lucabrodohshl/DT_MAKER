/**
 * @file config.hpp
 * @brief Configuration of one twin-ingest process (format "twin-ingest/1"), written by Studio
 *        when it deploys an instance with a plant connection.
 * @ingroup ingest
 *
 * @code{.json}
 * {
 *   "format": "twin-ingest/1",
 *   "instance": "pump-p102-dt",
 *   "token": "<access token>",
 *   "time": {"ticksPerUnit": 1000, "unit": "s"},
 *   "rest":   {"enabled": true, "host": "0.0.0.0", "port": 19000},
 *   "mqtt":   {"enabled": true, "host": "0.0.0.0", "port": 19001},
 *   "broker": {"enabled": false, "host": "plc-gateway.plant-a.local", "port": 1883,
 *              "topics": ["plant/line4/pump/#"], "qos": 1, "clientId": "twin-pump-p102-dt",
 *              "username": null, "password": null, "keepAliveSeconds": 30},
 *   "mapping": {"telemetry": [...], "events": [...], "eventField": "event",
 *               "timestamp": {"field": "ts", "unit": "ms"}},
 *   "clock": {"syncSeconds": 0, "graceSeconds": 0.5}
 * }
 * @endcode
 * The runtime's URL and the local status port are given on the command line (they are chosen
 * by the deployment supervisor at start).
 */
#pragma once

#include <filesystem>
#include <string>

#include "twin/ingest/mapping.hpp"
#include "twin/ingest/mqtt_endpoint.hpp"
#include "twin/ingest/mqtt_subscriber.hpp"
#include "twin/ingest/service.hpp"

namespace twin::ingest {

/// @brief Format identifier of the configuration document.
inline constexpr const char* kConfigFormat = "twin-ingest/1";

/// @brief The twin's REST endpoint.
struct RestOptions {
    bool enabled{false};          ///< Listen for plant messages over HTTP.
    std::string host{"0.0.0.0"};  ///< IPv4 listen address.
    int port{0};                  ///< TCP port.
};

/// @brief Everything one twin-ingest process needs.
struct IngestConfig {
    std::string instance;          ///< Twin (instance) id.
    std::string token;             ///< Access token: REST bearer token and MQTT password.
    ServiceOptions service;        ///< Time base and clock policy.
    Mapping mapping;               ///< Plant message -> data contract.
    RestOptions rest;              ///< REST endpoint.
    bool mqtt_enabled{false};      ///< Run the twin's MQTT endpoint.
    EndpointOptions mqtt;          ///< Its listening options.
    bool broker_enabled{false};    ///< Subscribe to the plant's broker.
    SubscriberOptions broker;      ///< Its connection options.
};

/// @brief Parse and check a configuration document (at least one input must be enabled).
[[nodiscard]] Result<IngestConfig> config_from_json(const json::Json& doc);
/// @brief Read and parse a configuration file.
[[nodiscard]] Result<IngestConfig> load_config(const std::filesystem::path& file);

}  // namespace twin::ingest
