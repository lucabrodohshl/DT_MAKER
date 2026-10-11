/**
 * @file mqtt.cpp
 * @brief MQTT 3.1.1 packet codec (see mqtt.hpp).
 */
#include "twin/ingest/mqtt.hpp"

namespace twin::ingest::mqtt {

namespace {

constexpr std::size_t kMaxRemaining = 268'435'455;  // 4-byte variable length integer (§2.2.3)

Error malformed(std::string what) { return make_error(ErrorCode::ParseError, "malformed MQTT packet: " + std::move(what)); }

/// @brief Bounds-checked reader of a packet body.
class Cursor {
public:
    explicit Cursor(std::string_view bytes) : bytes_(bytes) {}

    [[nodiscard]] bool done() const noexcept { return pos_ == bytes_.size(); }
    [[nodiscard]] std::size_t left() const noexcept { return bytes_.size() - pos_; }

    Result<std::uint8_t> byte() {
        if (left() < 1) return malformed("truncated");
        return static_cast<std::uint8_t>(bytes_[pos_++]);
    }
    Result<std::uint16_t> u16() {
        if (left() < 2) return malformed("truncated");
        const auto hi = static_cast<std::uint8_t>(bytes_[pos_]);
        const auto lo = static_cast<std::uint8_t>(bytes_[pos_ + 1]);
        pos_ += 2;
        return static_cast<std::uint16_t>((hi << 8) | lo);
    }
    /// Length-prefixed string or binary data (§1.5.3).
    Result<std::string> string() {
        auto n = u16();
        if (!n) return std::move(n).error();
        if (left() < n.value()) return malformed("string longer than the packet");
        std::string s(bytes_.substr(pos_, n.value()));
        pos_ += n.value();
        return s;
    }
    std::string rest() {
        std::string s(bytes_.substr(pos_));
        pos_ = bytes_.size();
        return s;
    }

private:
    std::string_view bytes_;
    std::size_t pos_{0};
};

void put_u16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v & 0xFFU));
}

void put_string(std::string& out, std::string_view s) {
    put_u16(out, static_cast<std::uint16_t>(s.size()));
    out.append(s);
}

/// Flags a packet type must carry (§2.2.2); std::nullopt for PUBLISH (checked when decoded).
std::optional<std::uint8_t> required_flags(Type t) {
    switch (t) {
        case Type::Publish:
            return std::nullopt;
        case Type::Pubrel:
        case Type::Subscribe:
        case Type::Unsubscribe:
            return std::uint8_t{2};
        default:
            return std::uint8_t{0};
    }
}

}  // namespace

// ------------------------------------------------------------------------------ framing

Result<std::optional<Packet>> FrameReader::next() {
    if (buffer_.size() < 2) return std::optional<Packet>{};
    const auto first = static_cast<std::uint8_t>(buffer_[0]);
    const auto type_code = static_cast<std::uint8_t>(first >> 4);
    if (type_code == 0 || type_code == 15) return malformed("reserved packet type " + std::to_string(type_code));
    std::size_t remaining = 0;
    std::size_t multiplier = 1;
    std::size_t i = 1;
    for (;; ++i) {
        if (i > 4) return malformed("remaining length longer than four bytes");
        if (i >= buffer_.size()) return std::optional<Packet>{};  // length not complete yet
        const auto b = static_cast<std::uint8_t>(buffer_[i]);
        remaining += static_cast<std::size_t>(b & 0x7FU) * multiplier;
        multiplier *= 128;
        if ((b & 0x80U) == 0) break;
    }
    if (remaining > kMaxRemaining || remaining > max_packet_) {
        return make_error(ErrorCode::InvalidArgument, "MQTT packet of " + std::to_string(remaining) + " bytes exceeds the limit of " +
                                                          std::to_string(max_packet_) + " bytes");
    }
    const std::size_t header = i + 1;
    if (buffer_.size() < header + remaining) return std::optional<Packet>{};
    Packet p;
    p.type = static_cast<Type>(type_code);
    p.flags = static_cast<std::uint8_t>(first & 0x0FU);
    if (const auto want = required_flags(p.type); want && p.flags != *want) {
        return malformed("reserved flags " + std::to_string(p.flags) + " on packet type " + std::to_string(type_code));
    }
    p.body = buffer_.substr(header, remaining);
    buffer_.erase(0, header + remaining);
    return std::optional<Packet>{std::move(p)};
}

std::string encode(Type type, std::uint8_t flags, std::string_view body) {
    std::string out;
    out.reserve(body.size() + 5);
    const unsigned first = (static_cast<unsigned>(type) << 4U) | (flags & 0x0FU);
    out.push_back(static_cast<char>(first));
    std::size_t x = body.size();
    do {
        auto b = static_cast<std::uint8_t>(x % 128);
        x /= 128;
        if (x > 0) b = static_cast<std::uint8_t>(b | 0x80U);
        out.push_back(static_cast<char>(b));
    } while (x > 0);
    out.append(body);
    return out;
}

// ------------------------------------------------------------------------------ CONNECT

Result<Connect> parse_connect(const Packet& p) {
    if (p.type != Type::Connect) return malformed("not a CONNECT");
    Cursor c(p.body);
    auto name = c.string();
    if (!name) return std::move(name).error();
    auto level = c.byte();
    if (!level) return std::move(level).error();
    if (name.value() != "MQTT" && name.value() != "MQIsdp") return malformed("unknown protocol name '" + name.value() + "'");
    auto flags = c.byte();
    if (!flags) return std::move(flags).error();
    const std::uint8_t f = flags.value();
    if ((f & 0x01U) != 0) return malformed("reserved CONNECT flag set");
    auto keep = c.u16();
    if (!keep) return std::move(keep).error();
    Connect out;
    out.level = level.value();
    out.clean_session = (f & 0x02U) != 0;
    out.will = (f & 0x04U) != 0;
    out.keep_alive = keep.value();
    const bool has_user = (f & 0x80U) != 0;
    const bool has_pass = (f & 0x40U) != 0;
    if (out.level == 4 && has_pass && !has_user) return malformed("password without user name");
    auto id = c.string();
    if (!id) return std::move(id).error();
    out.client_id = std::move(id).value();
    if (out.will) {
        auto topic = c.string();
        if (!topic) return std::move(topic).error();
        auto message = c.string();
        if (!message) return std::move(message).error();
    }
    if (has_user) {
        auto u = c.string();
        if (!u) return std::move(u).error();
        out.username = std::move(u).value();
    }
    if (has_pass) {
        auto pw = c.string();
        if (!pw) return std::move(pw).error();
        out.password = std::move(pw).value();
    }
    return out;
}

std::string connect(const Connect& c) {
    std::string body;
    put_string(body, "MQTT");
    body.push_back(4);
    std::uint8_t flags = 0;
    if (c.clean_session) flags = static_cast<std::uint8_t>(flags | 0x02U);
    if (c.username) flags = static_cast<std::uint8_t>(flags | 0x80U);
    if (c.password) flags = static_cast<std::uint8_t>(flags | 0x40U);
    body.push_back(static_cast<char>(flags));
    put_u16(body, c.keep_alive);
    put_string(body, c.client_id);
    if (c.username) put_string(body, *c.username);
    if (c.password) put_string(body, *c.password);
    return encode(Type::Connect, 0, body);
}

// ------------------------------------------------------------------------------ CONNACK

std::string_view to_string(ConnectReturn rc) noexcept {
    switch (rc) {
        case ConnectReturn::Accepted: return "connection accepted";
        case ConnectReturn::BadProtocol: return "unacceptable protocol version";
        case ConnectReturn::IdentifierRejected: return "client identifier rejected";
        case ConnectReturn::ServerUnavailable: return "server unavailable";
        case ConnectReturn::BadCredentials: return "bad user name or password";
        case ConnectReturn::NotAuthorized: return "not authorized";
    }
    return "unknown return code";
}

std::string connack(bool session_present, ConnectReturn rc) {
    std::string body;
    body.push_back(session_present ? 1 : 0);
    body.push_back(static_cast<char>(rc));
    return encode(Type::Connack, 0, body);
}

Result<ConnectReturn> parse_connack(const Packet& p) {
    if (p.type != Type::Connack || p.body.size() != 2) return malformed("bad CONNACK");
    const auto rc = static_cast<std::uint8_t>(p.body[1]);
    if (rc > 5) return malformed("unknown CONNACK return code " + std::to_string(rc));
    return static_cast<ConnectReturn>(rc);
}

// ------------------------------------------------------------------------------ PUBLISH

Result<Publish> parse_publish(const Packet& p) {
    if (p.type != Type::Publish) return malformed("not a PUBLISH");
    Publish m;
    m.qos = static_cast<std::uint8_t>((p.flags >> 1) & 0x03U);
    if (m.qos == 3) return malformed("PUBLISH with QoS 3");
    m.dup = (p.flags & 0x08U) != 0;
    m.retain = (p.flags & 0x01U) != 0;
    Cursor c(p.body);
    auto topic = c.string();
    if (!topic) return std::move(topic).error();
    m.topic = std::move(topic).value();
    if (!valid_topic_name(m.topic)) return malformed("invalid topic name '" + m.topic + "'");
    if (m.qos > 0) {
        auto id = c.u16();
        if (!id) return std::move(id).error();
        if (id.value() == 0) return malformed("packet identifier 0");
        m.packet_id = id.value();
    }
    m.payload = c.rest();
    return m;
}

std::string publish(const Publish& m) {
    std::string body;
    put_string(body, m.topic);
    if (m.qos > 0) put_u16(body, m.packet_id);
    body.append(m.payload);
    auto flags = static_cast<std::uint8_t>((m.qos & 0x03U) << 1);
    if (m.dup) flags = static_cast<std::uint8_t>(flags | 0x08U);
    if (m.retain) flags = static_cast<std::uint8_t>(flags | 0x01U);
    return encode(Type::Publish, flags, body);
}

// ------------------------------------------------------------------------------ acknowledgements

std::string ack(Type type, std::uint16_t packet_id) {
    std::string body;
    put_u16(body, packet_id);
    return encode(type, type == Type::Pubrel ? std::uint8_t{2} : std::uint8_t{0}, body);
}

Result<std::uint16_t> parse_ack(const Packet& p) {
    if (p.body.size() != 2) return malformed("acknowledgement of " + std::to_string(p.body.size()) + " bytes");
    Cursor c(p.body);
    return c.u16();
}

// ------------------------------------------------------------------------------ SUBSCRIBE

Result<Subscribe> parse_subscribe(const Packet& p) {
    if (p.type != Type::Subscribe) return malformed("not a SUBSCRIBE");
    Cursor c(p.body);
    auto id = c.u16();
    if (!id) return std::move(id).error();
    Subscribe s;
    s.packet_id = id.value();
    s.topics.clear();
    while (!c.done()) {
        auto filter = c.string();
        if (!filter) return std::move(filter).error();
        auto qos = c.byte();
        if (!qos) return std::move(qos).error();
        if (qos.value() > 2) return malformed("requested QoS " + std::to_string(qos.value()));
        s.topics.push_back(Subscription{std::move(filter).value(), qos.value()});
    }
    if (s.topics.empty()) return malformed("SUBSCRIBE without topic filters");
    return s;
}

std::string subscribe(const Subscribe& s) {
    std::string body;
    put_u16(body, s.packet_id);
    for (const Subscription& t : s.topics) {
        put_string(body, t.filter);
        body.push_back(static_cast<char>(t.qos & 0x03U));
    }
    return encode(Type::Subscribe, 2, body);
}

std::string suback(const Suback& s) {
    std::string body;
    put_u16(body, s.packet_id);
    for (std::uint8_t code : s.codes) body.push_back(static_cast<char>(code));
    return encode(Type::Suback, 0, body);
}

Result<Suback> parse_suback(const Packet& p) {
    if (p.type != Type::Suback) return malformed("not a SUBACK");
    Cursor c(p.body);
    auto id = c.u16();
    if (!id) return std::move(id).error();
    Suback s;
    s.packet_id = id.value();
    while (!c.done()) {
        auto code = c.byte();
        if (!code) return std::move(code).error();
        s.codes.push_back(code.value());
    }
    return s;
}

Result<std::uint16_t> parse_unsubscribe(const Packet& p) {
    if (p.type != Type::Unsubscribe) return malformed("not an UNSUBSCRIBE");
    Cursor c(p.body);
    auto id = c.u16();
    if (!id) return std::move(id).error();
    if (c.done()) return malformed("UNSUBSCRIBE without topic filters");
    while (!c.done()) {
        auto filter = c.string();
        if (!filter) return std::move(filter).error();
    }
    return id.value();
}

std::string pingreq() { return encode(Type::Pingreq, 0, {}); }
std::string pingresp() { return encode(Type::Pingresp, 0, {}); }
std::string disconnect() { return encode(Type::Disconnect, 0, {}); }

// ------------------------------------------------------------------------------ topics

bool valid_topic_name(std::string_view topic) noexcept {
    if (topic.empty() || topic.size() > 65535) return false;
    for (char ch : topic) {
        if (ch == '+' || ch == '#' || ch == '\0') return false;
    }
    return true;
}

bool valid_topic_filter(std::string_view filter) noexcept {
    if (filter.empty() || filter.size() > 65535) return false;
    for (std::size_t i = 0; i < filter.size(); ++i) {
        const char ch = filter[i];
        if (ch == '\0') return false;
        const bool level_start = i == 0 || filter[i - 1] == '/';
        const bool level_end = i + 1 == filter.size() || filter[i + 1] == '/';
        if (ch == '+' && !(level_start && level_end)) return false;
        if (ch == '#' && !(level_start && i + 1 == filter.size())) return false;
    }
    return true;
}

bool topic_matches(std::string_view filter, std::string_view topic) noexcept {
    if (!topic.empty() && topic.front() == '$' && !filter.empty() && (filter.front() == '+' || filter.front() == '#')) return false;
    std::size_t f = 0;
    std::size_t t = 0;
    for (;;) {
        const std::size_t f_end = filter.find('/', f);
        const std::string_view level = filter.substr(f, f_end == std::string_view::npos ? std::string_view::npos : f_end - f);
        if (level == "#") return true;  // also matches the parent level ("a/#" matches "a")
        const std::size_t t_end = topic.find('/', t);
        const std::string_view tlevel = topic.substr(t, t_end == std::string_view::npos ? std::string_view::npos : t_end - t);
        if (level != "+" && level != tlevel) return false;
        const bool filter_last = f_end == std::string_view::npos;
        const bool topic_last = t_end == std::string_view::npos;
        if (filter_last || topic_last) {
            if (filter_last && topic_last) return true;
            // "a/#" against "a": the filter continues with exactly "#".
            return topic_last && !filter_last && filter.substr(f_end + 1) == "#";
        }
        f = f_end + 1;
        t = t_end + 1;
    }
}

}  // namespace twin::ingest::mqtt
