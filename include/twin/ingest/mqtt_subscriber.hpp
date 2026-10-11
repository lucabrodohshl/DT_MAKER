/**
 * @file mqtt_subscriber.hpp
 * @brief MQTT 3.1.1 client that subscribes to the plant's existing broker.
 * @ingroup ingest
 *
 * Connects with a clean session, subscribes to the configured topic filters and hands every
 * application message to the delivery callback (QoS 1 and 2 are acknowledged after delivery).
 * Keep-alive pings are sent at half the keep-alive interval; a lost connection is retried with
 * exponential back-off (1 s doubling to 30 s). TLS is not supported in this version.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "twin/ingest/mqtt_endpoint.hpp"

namespace twin::ingest {

/// @brief Broker connection settings.
struct SubscriberOptions {
    std::string host;                       ///< Broker host name or address.
    int port{1883};                         ///< Broker port.
    std::string client_id;                  ///< Client identifier (unique on the broker).
    std::optional<std::string> username;    ///< User name, if the broker needs one.
    std::optional<std::string> password;    ///< Password, if the broker needs one.
    std::vector<std::string> topics;        ///< Topic filters to subscribe to.
    std::uint8_t qos{1};                    ///< Requested QoS (0, 1 or 2).
    std::uint16_t keep_alive{30};           ///< Keep-alive interval in seconds.
    std::size_t max_packet{mqtt::kDefaultMaxPacket};  ///< Largest accepted packet.
};

/// @brief See file documentation.
class MqttSubscriber {
public:
    /// @brief Subscriber delivering to @p deliver; call start() to connect.
    MqttSubscriber(SubscriberOptions options, MqttEndpoint::Deliver deliver);
    ~MqttSubscriber();
    MqttSubscriber(const MqttSubscriber&) = delete;
    MqttSubscriber& operator=(const MqttSubscriber&) = delete;
    MqttSubscriber(MqttSubscriber&&) = delete;
    MqttSubscriber& operator=(MqttSubscriber&&) = delete;

    /// @brief Start the connection thread (idempotent).
    void start();
    /// @brief Disconnect and join (idempotent).
    void stop();
    /// @brief {state: connecting|connected|retrying|stopped, broker, topics, connectedAt, lastError,
    ///         messages, lastMessageAt, reconnects}.
    [[nodiscard]] json::Json status() const;

private:
    void run();
    /// One connection: returns when it is lost or stop() is called; the error says why.
    [[nodiscard]] Status session();
    void set_state(std::string state, std::string error = {});

    SubscriberOptions options_;
    MqttEndpoint::Deliver deliver_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
    mutable std::mutex mu_;
    std::condition_variable wake_;
    std::atomic<int> fd_{-1};
    std::string state_text_{"stopped"};
    std::string last_error_;
    std::string connected_at_;
    std::string last_message_at_;
    std::uint64_t messages_{0};
    std::uint64_t reconnects_{0};
};

}  // namespace twin::ingest
