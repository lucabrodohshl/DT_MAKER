/**
 * @file http_world_port.cpp
 * @brief HTTP implementation of WorldPort (only /env and /pt routes).
 */
#include <httplib.h>

#include <mutex>

#include "twin/runtime/world_port.hpp"

namespace twin::runtime {
namespace {

class HttpWorldPort final : public WorldPort {
public:
    explicit HttpWorldPort(const std::string& base_url) : client_(base_url) {
        client_.set_connection_timeout(2, 0);
        client_.set_read_timeout(5, 0);
        client_.set_keep_alive(true);
    }

    Result<json::Json> known_map() override { return get("/env/map/known"); }
    Result<json::Json> updates_since(std::uint64_t since) override {
        return get("/env/map/updates?since=" + std::to_string(since));
    }
    Result<json::Json> mission() override { return get("/env/mission"); }
    Result<json::Json> step(Ticks dt) override { return post("/pt/step", json::Json{{"dt", dt}}); }
    Status command(const json::Json& c) override {
        Result<json::Json> r = post("/pt/command", c);
        if (!r) return r.error();
        return ok_status();
    }
    Status reset() override {
        Result<json::Json> r = post("/admin/reset", json::Json::object());
        if (!r) return r.error();
        return ok_status();
    }

private:
    static Result<json::Json> decode(const httplib::Result& res, const std::string& path) {
        if (!res) {
            return make_error(ErrorCode::Unavailable, "physical-twin service unreachable")
                .with("route", path)
                .with("error", httplib::to_string(res.error()));
        }
        Result<json::Json> body = json::parse(res->body);
        if (!body) return std::move(body).error().with("route", path);
        return world_reply(res->status, std::move(body).value(), path);
    }
    Result<json::Json> get(const std::string& path) {
        std::lock_guard lock(mutex_);
        return decode(client_.Get(path), path);
    }
    Result<json::Json> post(const std::string& path, const json::Json& body) {
        std::lock_guard lock(mutex_);
        return decode(client_.Post(path, body.dump(), "application/json"), path);
    }

    std::mutex mutex_;
    httplib::Client client_;
};

}  // namespace

std::unique_ptr<WorldPort> make_http_world_port(const std::string& base_url) {
    return std::make_unique<HttpWorldPort>(base_url);
}

}  // namespace twin::runtime
