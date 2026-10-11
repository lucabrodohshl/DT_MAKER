/**
 * @file mqtt_subscriber.cpp
 * @brief MQTT client subscribed to the plant's broker (see mqtt_subscriber.hpp).
 */
#include "twin/ingest/mqtt_subscriber.hpp"

#include <array>
#include <set>

#include "twin/core/wall_clock.hpp"

namespace twin::ingest {

using json::Json;

MqttSubscriber::MqttSubscriber(SubscriberOptions options, MqttEndpoint::Deliver deliver)
    : options_(std::move(options)), deliver_(std::move(deliver)) {}

MqttSubscriber::~MqttSubscriber() { stop(); }

void MqttSubscriber::start() {
    if (thread_.joinable()) return;
    stopping_.store(false);
    set_state("connecting");
    thread_ = std::thread([this] { run(); });
}

void MqttSubscriber::stop() {
    stopping_.store(true);
    wake_.notify_all();
    if (const int fd = fd_.load(); fd >= 0) net::shutdown(fd);
    if (thread_.joinable()) thread_.join();
    set_state("stopped");
}

void MqttSubscriber::set_state(std::string state, std::string error) {
    std::lock_guard l(mu_);
    state_text_ = std::move(state);
    if (!error.empty()) last_error_ = std::move(error);
}

void MqttSubscriber::run() {
    std::chrono::milliseconds backoff{1000};
    while (!stopping_.load()) {
        set_state("connecting");
        const Status s = session();
        if (stopping_.load()) break;
        bool was_connected = false;
        {
            std::lock_guard l(mu_);
            ++reconnects_;
            was_connected = !connected_at_.empty();
            connected_at_.clear();
        }
        if (was_connected) backoff = std::chrono::milliseconds(1000);
        set_state("retrying", s ? std::string("the broker closed the connection") : s.error().message);
        std::unique_lock l(mu_);
        wake_.wait_for(l, backoff, [this] { return stopping_.load(); });
        backoff = std::min(backoff * 2, std::chrono::milliseconds(30000));
    }
}

Status MqttSubscriber::session() {
    using clock = std::chrono::steady_clock;
    auto sock = net::connect_tcp(options_.host, options_.port, std::chrono::milliseconds(5000));
    if (!sock) return std::move(sock).error();
    net::Socket socket = std::move(sock).value();
    const int fd = socket.fd();
    fd_.store(fd);
    struct Release {
        std::atomic<int>& fd;
        ~Release() { fd.store(-1); }
    } release{fd_};

    mqtt::Connect hello;
    hello.client_id = options_.client_id;
    hello.username = options_.username;
    hello.password = options_.password;
    hello.keep_alive = options_.keep_alive;
    if (auto w = net::write_all(fd, mqtt::connect(hello)); !w) return w;

    mqtt::FrameReader reader(options_.max_packet);
    std::array<char, 16384> buf{};
    const std::chrono::milliseconds keep_alive = std::chrono::seconds(options_.keep_alive);
    const auto opened = clock::now();
    auto last_tx = clock::now();
    std::optional<clock::time_point> ping_sent;
    bool connacked = false;
    bool subscribed = false;
    std::set<std::uint16_t> qos2_pending;
    auto send = [&](const std::string& bytes) {
        last_tx = clock::now();
        return net::write_all(fd, bytes);
    };

    while (!stopping_.load()) {
        const auto now = clock::now();
        if (!connacked && now - opened > std::chrono::seconds(10)) return make_error(ErrorCode::Unavailable, "no CONNACK from the broker within 10 s");
        if (connacked && !subscribed && now - opened > std::chrono::seconds(20)) return make_error(ErrorCode::Unavailable, "no SUBACK from the broker within 10 s");
        if (keep_alive.count() > 0) {
            if (ping_sent && now - *ping_sent > keep_alive) return make_error(ErrorCode::Unavailable, "the broker did not answer the keep-alive ping");
            if (!ping_sent && now - last_tx >= keep_alive / 2) {
                if (auto w = send(mqtt::pingreq()); !w) return w;
                ping_sent = clock::now();
            }
        }
        auto r = net::read_for(fd, buf.data(), buf.size(), std::chrono::milliseconds(250));
        if (!r) return std::move(r).error();
        if (r.value().kind == net::ReadResult::Kind::Closed) return make_error(ErrorCode::Unavailable, "the broker closed the connection");
        if (r.value().kind == net::ReadResult::Kind::Timeout) continue;
        reader.feed(std::string_view(buf.data(), r.value().bytes));
        for (;;) {
            auto next = reader.next();
            if (!next) return std::move(next).error();
            if (!next.value()) break;
            const mqtt::Packet& p = *next.value();
            switch (p.type) {
                case mqtt::Type::Connack: {
                    auto rc = mqtt::parse_connack(p);
                    if (!rc) return std::move(rc).error();
                    if (rc.value() != mqtt::ConnectReturn::Accepted) {
                        return make_error(ErrorCode::Unavailable, "the broker refused the connection: " + std::string(mqtt::to_string(rc.value())));
                    }
                    connacked = true;
                    mqtt::Subscribe sub;
                    sub.packet_id = 1;
                    for (const std::string& t : options_.topics) sub.topics.push_back(mqtt::Subscription{t, options_.qos});
                    if (auto w = send(mqtt::subscribe(sub)); !w) return w;
                    break;
                }
                case mqtt::Type::Suback: {
                    auto s = mqtt::parse_suback(p);
                    if (!s) return std::move(s).error();
                    for (std::size_t i = 0; i < s.value().codes.size(); ++i) {
                        if (s.value().codes[i] == 0x80) {
                            return make_error(ErrorCode::Unavailable, "the broker refused the subscription to '" +
                                                                          (i < options_.topics.size() ? options_.topics[i] : std::string("?")) + "'");
                        }
                    }
                    subscribed = true;
                    {
                        std::lock_guard l(mu_);
                        connected_at_ = now_utc_millis();
                        state_text_ = "connected";
                    }
                    break;
                }
                case mqtt::Type::Publish: {
                    auto m = mqtt::parse_publish(p);
                    if (!m) return std::move(m).error();
                    const mqtt::Publish& msg = m.value();
                    if (msg.qos < 2 || qos2_pending.insert(msg.packet_id).second) {
                        {
                            std::lock_guard l(mu_);
                            ++messages_;
                            last_message_at_ = now_utc_millis();
                        }
                        if (deliver_) deliver_(Delivery{msg.topic, msg.payload, options_.client_id});
                    }
                    if (msg.qos == 1) {
                        if (auto w = send(mqtt::ack(mqtt::Type::Puback, msg.packet_id)); !w) return w;
                    } else if (msg.qos == 2) {
                        if (auto w = send(mqtt::ack(mqtt::Type::Pubrec, msg.packet_id)); !w) return w;
                    }
                    break;
                }
                case mqtt::Type::Pubrel: {
                    auto id = mqtt::parse_ack(p);
                    if (!id) return std::move(id).error();
                    qos2_pending.erase(id.value());
                    if (auto w = send(mqtt::ack(mqtt::Type::Pubcomp, id.value())); !w) return w;
                    break;
                }
                case mqtt::Type::Pingresp:
                    ping_sent.reset();
                    break;
                default:
                    return make_error(ErrorCode::ParseError, "unexpected packet type " + std::to_string(static_cast<int>(p.type)) + " from the broker");
            }
        }
    }
    (void)net::write_all(fd, mqtt::disconnect());
    return ok_status();
}

Json MqttSubscriber::status() const {
    std::lock_guard l(mu_);
    return Json{{"state", state_text_},
                {"broker", options_.host + ":" + std::to_string(options_.port)},
                {"clientId", options_.client_id},
                {"topics", options_.topics},
                {"qos", options_.qos},
                {"connectedAt", connected_at_.empty() ? Json(nullptr) : Json(connected_at_)},
                {"lastError", last_error_.empty() ? Json(nullptr) : Json(last_error_)},
                {"messages", messages_},
                {"lastMessageAt", last_message_at_.empty() ? Json(nullptr) : Json(last_message_at_)},
                {"reconnects", reconnects_}};
}

}  // namespace twin::ingest
