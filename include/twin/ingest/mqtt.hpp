/**
 * @file mqtt.hpp
 * @brief MQTT 3.1.1 packet codec: framing, the packets the plant connector uses, and topic
 *        matching (OASIS MQTT Version 3.1.1, 2014).
 * @ingroup ingest
 *
 * @defgroup ingest Plant connectivity (twin-ingest)
 * @brief Built-in REST and MQTT inputs for deployed twins: decode what the plant sends, map it
 *        to the twin's data contract and forward it to the twin runtime, whose kernel decides.
 *
 * Only the subset the connector needs is implemented: CONNECT/CONNACK, PUBLISH with QoS 0, 1
 * and 2 (PUBACK, PUBREC, PUBREL, PUBCOMP), SUBSCRIBE/SUBACK, UNSUBSCRIBE/UNSUBACK,
 * PINGREQ/PINGRESP and DISCONNECT. Protocol levels 3 (MQTT 3.1) and 4 (MQTT 3.1.1) are
 * understood; anything else is answered with return code 1, as §3.1.2.2 of the standard asks.
 * Decoding never trusts a length: every field is bounds-checked, and a malformed packet is an
 * error after which the connection must be closed (§4.8).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"

namespace twin::ingest::mqtt {

/// @brief Control packet types (fixed header, high nibble of byte 1).
enum class Type : std::uint8_t {
    Connect = 1,
    Connack = 2,
    Publish = 3,
    Puback = 4,
    Pubrec = 5,
    Pubrel = 6,
    Pubcomp = 7,
    Subscribe = 8,
    Suback = 9,
    Unsubscribe = 10,
    Unsuback = 11,
    Pingreq = 12,
    Pingresp = 13,
    Disconnect = 14,
};

/// @brief One framed control packet: its type, the fixed-header flags and the rest of the packet.
struct Packet {
    Type type{Type::Disconnect};  ///< Packet type.
    std::uint8_t flags{0};        ///< Low nibble of the fixed header.
    std::string body;             ///< Variable header and payload.
};

/// @brief Largest packet accepted by default (1 MiB); the standard allows up to 256 MiB.
inline constexpr std::size_t kDefaultMaxPacket = std::size_t{1} << 20;

/**
 * @brief Incremental decoder of the packet stream of one connection.
 *
 * Bytes are fed as they arrive; next() returns each complete packet once. Reserved flag
 * values (§2.2.2) and oversized packets are reported as errors.
 */
class FrameReader {
public:
    /// @brief Reader that refuses packets larger than @p max_packet bytes.
    explicit FrameReader(std::size_t max_packet = kDefaultMaxPacket) : max_packet_(max_packet) {}

    /// @brief Append received bytes.
    void feed(std::string_view bytes) { buffer_.append(bytes); }

    /// @brief The next complete packet, std::nullopt if more bytes are needed, or an error if the
    ///        stream is malformed (close the connection).
    [[nodiscard]] Result<std::optional<Packet>> next();

private:
    std::string buffer_;
    std::size_t max_packet_;
};

/// @brief A whole packet: fixed header (type, flags, remaining length) followed by @p body.
[[nodiscard]] std::string encode(Type type, std::uint8_t flags, std::string_view body);

/// @brief CONNECT (§3.1).
struct Connect {
    std::uint8_t level{4};                ///< Protocol level: 3 (MQTT 3.1) or 4 (MQTT 3.1.1).
    bool clean_session{true};             ///< Clean session flag.
    std::uint16_t keep_alive{30};         ///< Keep-alive interval in seconds (0 = none).
    std::string client_id;                ///< Client identifier (may be empty with a clean session).
    std::optional<std::string> username;  ///< User name, if sent.
    std::optional<std::string> password;  ///< Password, if sent.
    bool will{false};                     ///< Whether the client registered a will message (ignored).
};

/// @brief Decode a CONNECT; an unknown protocol name or level is reported in Connect::level
///        (callers answer with ConnectReturn::BadProtocol), malformed bytes are an error.
[[nodiscard]] Result<Connect> parse_connect(const Packet& p);
/// @brief Encode a CONNECT (clean session, optional user name and password, no will).
[[nodiscard]] std::string connect(const Connect& c);

/// @brief CONNACK return codes (§3.2.2.3).
enum class ConnectReturn : std::uint8_t {
    Accepted = 0,
    BadProtocol = 1,
    IdentifierRejected = 2,
    ServerUnavailable = 3,
    BadCredentials = 4,
    NotAuthorized = 5,
};

/// @brief Readable text of a CONNACK return code ("bad user name or password", ...).
[[nodiscard]] std::string_view to_string(ConnectReturn rc) noexcept;
/// @brief Encode a CONNACK.
[[nodiscard]] std::string connack(bool session_present, ConnectReturn rc);
/// @brief Decode a CONNACK's return code.
[[nodiscard]] Result<ConnectReturn> parse_connack(const Packet& p);

/// @brief PUBLISH (§3.3).
struct Publish {
    std::string topic;            ///< Topic name (never contains wildcards).
    std::string payload;          ///< Application message (opaque bytes).
    std::uint8_t qos{0};          ///< 0, 1 or 2.
    bool retain{false};           ///< Retain flag.
    bool dup{false};              ///< Duplicate delivery flag.
    std::uint16_t packet_id{0};   ///< Packet identifier (QoS 1 and 2 only).
};

/// @brief Decode a PUBLISH (QoS 3 and wildcard topics are errors).
[[nodiscard]] Result<Publish> parse_publish(const Packet& p);
/// @brief Encode a PUBLISH.
[[nodiscard]] std::string publish(const Publish& m);

/// @brief Encode a two-byte acknowledgement: PUBACK, PUBREC, PUBREL, PUBCOMP or UNSUBACK.
[[nodiscard]] std::string ack(Type type, std::uint16_t packet_id);
/// @brief Packet identifier of a two-byte acknowledgement.
[[nodiscard]] Result<std::uint16_t> parse_ack(const Packet& p);

/// @brief One topic filter of a SUBSCRIBE, with its requested QoS.
struct Subscription {
    std::string filter;    ///< Topic filter (may contain + and #).
    std::uint8_t qos{0};   ///< Requested maximum QoS.
};

/// @brief SUBSCRIBE (§3.8).
struct Subscribe {
    std::uint16_t packet_id{1};          ///< Packet identifier.
    std::vector<Subscription> topics;    ///< At least one filter.
};

/// @brief Decode a SUBSCRIBE.
[[nodiscard]] Result<Subscribe> parse_subscribe(const Packet& p);
/// @brief Encode a SUBSCRIBE.
[[nodiscard]] std::string subscribe(const Subscribe& s);

/// @brief SUBACK (§3.9): one return code per filter (0, 1, 2 = granted QoS; 0x80 = failure).
struct Suback {
    std::uint16_t packet_id{0};         ///< Packet identifier of the SUBSCRIBE.
    std::vector<std::uint8_t> codes;    ///< Return codes, in filter order.
};

/// @brief Encode a SUBACK.
[[nodiscard]] std::string suback(const Suback& s);
/// @brief Decode a SUBACK.
[[nodiscard]] Result<Suback> parse_suback(const Packet& p);
/// @brief Packet identifier of an UNSUBSCRIBE (its filters are not needed by the connector).
[[nodiscard]] Result<std::uint16_t> parse_unsubscribe(const Packet& p);

/// @brief Encode a PINGREQ.
[[nodiscard]] std::string pingreq();
/// @brief Encode a PINGRESP.
[[nodiscard]] std::string pingresp();
/// @brief Encode a DISCONNECT.
[[nodiscard]] std::string disconnect();

/// @brief A topic name a client may publish to: non-empty, at most 65535 bytes, no wildcards, no NUL.
[[nodiscard]] bool valid_topic_name(std::string_view topic) noexcept;
/// @brief A topic filter: '+' fills a whole level, '#' is the whole last level (§4.7.1).
[[nodiscard]] bool valid_topic_filter(std::string_view filter) noexcept;
/// @brief Whether @p topic matches @p filter (§4.7); topics starting with '$' never match a leading wildcard.
[[nodiscard]] bool topic_matches(std::string_view filter, std::string_view topic) noexcept;

}  // namespace twin::ingest::mqtt
