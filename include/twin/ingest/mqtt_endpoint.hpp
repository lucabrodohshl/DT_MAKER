/**
 * @file mqtt_endpoint.hpp
 * @brief The twin's own MQTT endpoint: plant devices and gateways connect to it and publish.
 * @ingroup ingest
 *
 * A receive-only MQTT 3.1.1 server: it accepts CONNECT (authenticated by a callback, normally
 * the instance's access token as password), PUBLISH with QoS 0, 1 or 2, and keep-alive. It is
 * not a general broker: it forwards nothing to other clients, so SUBSCRIBE is answered with
 * the failure return code 0x80 for every filter. Every application message is handed to the
 * delivery callback on the connection's own thread, in the order received.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "twin/ingest/mqtt.hpp"
#include "twin/ingest/net.hpp"
#include "twin/json/canonical.hpp"

namespace twin::ingest {

/// @brief Listening options of the endpoint.
struct EndpointOptions {
    std::string host{"0.0.0.0"};                         ///< IPv4 listen address.
    int port{0};                                         ///< TCP port (0 = any free port).
    std::size_t max_packet{mqtt::kDefaultMaxPacket};     ///< Largest accepted packet.
    std::size_t max_clients{64};                         ///< Concurrent connections.
    std::chrono::milliseconds connect_timeout{10000};    ///< Time allowed between TCP accept and CONNECT.
};

/// @brief One received application message.
struct Delivery {
    std::string topic;      ///< Topic it was published to.
    std::string payload;    ///< Payload bytes.
    std::string client_id;  ///< Publishing client.
};

/// @brief See file documentation.
class MqttEndpoint {
public:
    /// @brief Decides whether a CONNECT is admitted (Accepted) or refused (any other code).
    using Authenticate = std::function<mqtt::ConnectReturn(const mqtt::Connect&)>;
    /// @brief Receives each application message.
    using Deliver = std::function<void(const Delivery&)>;

    /// @brief Bind and start accepting; Unavailable if the port cannot be bound.
    [[nodiscard]] static Result<std::unique_ptr<MqttEndpoint>> start(EndpointOptions options, Authenticate authenticate,
                                                                     Deliver deliver);
    ~MqttEndpoint();
    MqttEndpoint(const MqttEndpoint&) = delete;
    MqttEndpoint& operator=(const MqttEndpoint&) = delete;
    MqttEndpoint(MqttEndpoint&&) = delete;
    MqttEndpoint& operator=(MqttEndpoint&&) = delete;

    /// @brief Stop accepting, disconnect every client and join all threads (idempotent).
    void stop();
    /// @brief The port it listens on.
    [[nodiscard]] int port() const noexcept { return port_; }
    /// @brief {listening, host, port, clients[{clientId, address, connectedAt, messages, lastMessageAt}],
    ///        connections, refused, protocolErrors}.
    [[nodiscard]] json::Json status() const;

private:
    struct Connection;
    struct Session;
    /// Outcome of handling one packet: keep the connection, or close it.
    enum class Next { Continue, Close };

    MqttEndpoint(EndpointOptions options, net::Socket listener, Authenticate authenticate, Deliver deliver);
    void accept_loop();
    /// Read loop of one connection (its own thread): CONNECT deadline, keep-alive, framing.
    void serve(const std::shared_ptr<Connection>& c);
    /// The first packet: must be an admissible CONNECT.
    Next admit(Connection& c, Session& session, const mqtt::Packet& p);
    /// Every later packet.
    Next dispatch(Connection& c, Session& session, const mqtt::Packet& p);
    void count_protocol_error();
    void reap(bool all);

    EndpointOptions options_;
    net::Socket listener_;
    int port_{0};
    Authenticate authenticate_;
    Deliver deliver_;
    std::atomic<bool> stopping_{false};
    std::thread acceptor_;
    mutable std::mutex mu_;
    std::list<std::shared_ptr<Connection>> connections_;
    std::uint64_t connections_total_{0};
    std::uint64_t refused_{0};
    std::uint64_t protocol_errors_{0};
    std::uint64_t anonymous_ids_{0};
};

}  // namespace twin::ingest
