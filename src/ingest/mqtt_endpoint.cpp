/**
 * @file mqtt_endpoint.cpp
 * @brief The twin's own receive-only MQTT endpoint (see mqtt_endpoint.hpp).
 */
#include "twin/ingest/mqtt_endpoint.hpp"

#include <array>
#include <set>

#include "twin/core/wall_clock.hpp"

namespace twin::ingest {

using json::Json;

/// @brief One client connection and its thread.
struct MqttEndpoint::Connection {
    net::Socket socket;
    std::string address;
    std::string client_id;          ///< Set once CONNECT was accepted.
    std::string connected_at;
    std::string last_message_at;
    std::uint64_t messages{0};
    bool admitted{false};
    std::atomic<bool> done{false};
    std::thread thread;
};

Result<std::unique_ptr<MqttEndpoint>> MqttEndpoint::start(EndpointOptions options, Authenticate authenticate, Deliver deliver) {
    auto listener = net::listen_tcp(options.host, options.port);
    if (!listener) return Error(std::move(listener).error()).with("listener", "MQTT endpoint");
    std::unique_ptr<MqttEndpoint> e(  // NOLINT(cppcoreguidelines-owning-memory): private constructor
        new MqttEndpoint(std::move(options), std::move(listener).value(), std::move(authenticate), std::move(deliver)));
    e->acceptor_ = std::thread([raw = e.get()] { raw->accept_loop(); });
    return e;
}

MqttEndpoint::MqttEndpoint(EndpointOptions options, net::Socket listener, Authenticate authenticate, Deliver deliver)
    : options_(std::move(options)), listener_(std::move(listener)), authenticate_(std::move(authenticate)), deliver_(std::move(deliver)) {
    port_ = net::local_port(listener_.fd());
}

MqttEndpoint::~MqttEndpoint() { stop(); }

void MqttEndpoint::stop() {
    if (stopping_.exchange(true)) {
        if (acceptor_.joinable()) acceptor_.join();
        return;
    }
    if (acceptor_.joinable()) acceptor_.join();
    {
        std::lock_guard l(mu_);
        for (const auto& c : connections_) net::shutdown(c->socket.fd());
    }
    reap(true);
    listener_.close();
}

void MqttEndpoint::reap(bool all) {
    std::list<std::shared_ptr<Connection>> finished;
    {
        std::lock_guard l(mu_);
        for (auto it = connections_.begin(); it != connections_.end();) {
            if (all || (*it)->done.load()) {
                finished.push_back(*it);
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& c : finished) {
        if (c->thread.joinable()) c->thread.join();
    }
}

void MqttEndpoint::accept_loop() {
    while (!stopping_.load()) {
        reap(false);
        auto accepted = net::accept_for(listener_.fd(), std::chrono::milliseconds(250));
        if (!accepted || !accepted.value().valid()) continue;
        auto c = std::make_shared<Connection>();
        c->socket = std::move(accepted).value();
        c->address = net::peer_address(c->socket.fd());
        {
            std::lock_guard l(mu_);
            if (connections_.size() >= options_.max_clients) {
                ++refused_;
                continue;  // the socket closes when c goes out of scope
            }
            ++connections_total_;
            connections_.push_back(c);
        }
        c->thread = std::thread([this, c] {
            serve(c);
            net::shutdown(c->socket.fd());
            c->done.store(true);
        });
    }
}

/// @brief Per-connection protocol state of serve().
struct MqttEndpoint::Session {
    std::chrono::milliseconds keep_alive{0};
    std::set<std::uint16_t> qos2_pending;  ///< QoS 2 packet ids delivered, awaiting PUBREL (§4.3.3).
};

void MqttEndpoint::serve(const std::shared_ptr<Connection>& c) {
    using clock = std::chrono::steady_clock;
    mqtt::FrameReader reader(options_.max_packet);
    std::array<char, 16384> buf{};
    const auto accepted_at = clock::now();
    auto last_rx = clock::now();
    Session session;
    while (!stopping_.load()) {
        auto r = net::read_for(c->socket.fd(), buf.data(), buf.size(), std::chrono::milliseconds(250));
        if (!r || r.value().kind == net::ReadResult::Kind::Closed) return;
        const auto now = clock::now();
        if (r.value().kind == net::ReadResult::Kind::Timeout) {
            if (!c->admitted && now - accepted_at > options_.connect_timeout) return;
            // §3.1.2.10: one and a half keep-alive periods without a packet close the connection.
            if (session.keep_alive.count() > 0 && now - last_rx > session.keep_alive * 3 / 2) return;
            continue;
        }
        last_rx = now;
        reader.feed(std::string_view(buf.data(), r.value().bytes));
        for (;;) {
            auto next = reader.next();
            if (!next) {
                count_protocol_error();
                return;
            }
            if (!next.value()) break;
            const Next verdict = c->admitted ? dispatch(*c, session, *next.value()) : admit(*c, session, *next.value());
            if (verdict == Next::Close) return;
        }
    }
}

void MqttEndpoint::count_protocol_error() {
    std::lock_guard l(mu_);
    ++protocol_errors_;
}

MqttEndpoint::Next MqttEndpoint::admit(Connection& c, Session& session, const mqtt::Packet& p) {
    const int fd = c.socket.fd();
    if (p.type != mqtt::Type::Connect) {
        count_protocol_error();
        return Next::Close;
    }
    auto conn = mqtt::parse_connect(p);
    if (!conn) {
        count_protocol_error();
        return Next::Close;
    }
    mqtt::Connect& cc = conn.value();
    if (cc.level != 3 && cc.level != 4) {
        (void)net::write_all(fd, mqtt::connack(false, mqtt::ConnectReturn::BadProtocol));
        return Next::Close;
    }
    if (cc.client_id.empty() && !cc.clean_session) {
        (void)net::write_all(fd, mqtt::connack(false, mqtt::ConnectReturn::IdentifierRejected));
        return Next::Close;
    }
    const mqtt::ConnectReturn verdict = authenticate_ ? authenticate_(cc) : mqtt::ConnectReturn::Accepted;
    if (verdict != mqtt::ConnectReturn::Accepted) {
        {
            std::lock_guard l(mu_);
            ++refused_;
        }
        (void)net::write_all(fd, mqtt::connack(false, verdict));
        return Next::Close;
    }
    {
        std::lock_guard l(mu_);
        if (cc.client_id.empty()) cc.client_id = "anonymous-" + std::to_string(++anonymous_ids_);
        // §3.1.4: a second connection with the same client id takes over; the first is closed.
        for (const auto& other : connections_) {
            if (other.get() != &c && other->admitted && other->client_id == cc.client_id) net::shutdown(other->socket.fd());
        }
        c.client_id = cc.client_id;
        c.connected_at = now_utc_millis();
        c.admitted = true;
    }
    session.keep_alive = std::chrono::seconds(cc.keep_alive);
    return net::write_all(fd, mqtt::connack(false, mqtt::ConnectReturn::Accepted)) ? Next::Continue : Next::Close;
}

MqttEndpoint::Next MqttEndpoint::dispatch(Connection& c, Session& session, const mqtt::Packet& p) {
    const int fd = c.socket.fd();
    auto send = [fd](const std::string& bytes) { return net::write_all(fd, bytes) ? Next::Continue : Next::Close; };
    auto bad = [this] {
        count_protocol_error();
        return Next::Close;
    };
    switch (p.type) {
        case mqtt::Type::Publish: {
            auto m = mqtt::parse_publish(p);
            if (!m) return bad();
            const mqtt::Publish& msg = m.value();
            if (msg.qos < 2 || session.qos2_pending.insert(msg.packet_id).second) {  // a QoS 2 resend is delivered once
                {
                    std::lock_guard l(mu_);
                    ++c.messages;
                    c.last_message_at = now_utc_millis();
                }
                if (deliver_) deliver_(Delivery{msg.topic, msg.payload, c.client_id});
            }
            if (msg.qos == 1) return send(mqtt::ack(mqtt::Type::Puback, msg.packet_id));
            if (msg.qos == 2) return send(mqtt::ack(mqtt::Type::Pubrec, msg.packet_id));
            return Next::Continue;
        }
        case mqtt::Type::Pubrel: {
            auto id = mqtt::parse_ack(p);
            if (!id) return bad();
            session.qos2_pending.erase(id.value());
            return send(mqtt::ack(mqtt::Type::Pubcomp, id.value()));
        }
        case mqtt::Type::Subscribe: {
            auto s = mqtt::parse_subscribe(p);
            if (!s) return bad();
            // Receive-only endpoint: nothing is ever forwarded to subscribers.
            return send(mqtt::suback(mqtt::Suback{s.value().packet_id, std::vector<std::uint8_t>(s.value().topics.size(), 0x80)}));
        }
        case mqtt::Type::Unsubscribe: {
            auto id = mqtt::parse_unsubscribe(p);
            if (!id) return bad();
            return send(mqtt::ack(mqtt::Type::Unsuback, id.value()));
        }
        case mqtt::Type::Pingreq:
            return send(mqtt::pingresp());
        case mqtt::Type::Disconnect:
            return Next::Close;
        default:  // a second CONNECT, or packets only a server sends
            return bad();
    }
}

Json MqttEndpoint::status() const {
    std::lock_guard l(mu_);
    Json clients = Json::array();
    for (const auto& c : connections_) {
        if (!c->admitted || c->done.load()) continue;
        clients.push_back(Json{{"clientId", c->client_id},
                               {"address", c->address},
                               {"connectedAt", c->connected_at},
                               {"messages", c->messages},
                               {"lastMessageAt", c->last_message_at.empty() ? Json(nullptr) : Json(c->last_message_at)}});
    }
    return Json{{"listening", !stopping_.load()},
                {"host", options_.host},
                {"port", port_},
                {"clients", clients},
                {"connections", connections_total_},
                {"refused", refused_},
                {"protocolErrors", protocol_errors_}};
}

}  // namespace twin::ingest
