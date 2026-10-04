/**
 * @file main.cpp
 * @brief twin-world: the Physical Twin simulator and the building-information service.
 *
 * Usage: twin-world --scenario <scenario.json> [--host 127.0.0.1] [--port 8091]
 *
 * This executable is only an HTTP transport: every route, its validation and
 * its JSON shape are defined once in twin::world::WorldService (see
 * include/twin/world/service.hpp for the route table). In-process tests use
 * the same WorldService, so the HTTP API and the tested API are identical.
 *
 * The Digital Twin runtime uses only /env/... and /pt/...; /observer/... is
 * the ground truth for the visualisation.
 */
#include <httplib.h>

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

#include "twin/world/service.hpp"

namespace {

httplib::Server* g_server = nullptr;

void on_signal(int /*sig*/) {
    if (g_server != nullptr) g_server->stop();
}

constexpr const char* kUsage = "usage: twin-world --scenario <file.json> [--host H] [--port P]\n";

/// Forward one HTTP request to the service and copy its reply back.
void forward(twin::world::WorldService& service, const httplib::Request& req, httplib::Response& res) {
    twin::world::ApiRequest request{req.method, req.path, {}, req.body};
    for (const auto& [key, value] : req.params) request.query.emplace(key, value);
    const twin::world::ApiReply reply = service.handle(request);
    res.status = reply.status;
    res.set_content(reply.body.dump(), "application/json");
}

}  // namespace

int main(int argc, char** argv) {
    std::string scenario;
    std::string host = "127.0.0.1";
    int port = 8091;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) {
            std::cerr << kUsage;
            return 2;
        }
        if (a == "--scenario") scenario = argv[++i];
        else if (a == "--host") host = argv[++i];
        else if (a == "--port") port = std::stoi(argv[++i]);
        else {
            std::cerr << kUsage;
            return 2;
        }
    }
    if (scenario.empty()) {
        std::cerr << kUsage;
        return 2;
    }
    twin::Result<std::unique_ptr<twin::world::WorldService>> service = twin::world::WorldService::open(scenario);
    if (!service) {
        std::cerr << "twin-world: " << service.error().to_string() << "\n";
        return 3;
    }
    twin::world::WorldService& world = *service.value();

    httplib::Server srv;
    srv.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Headers", "Content-Type"},
                             {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}});
    srv.Options(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });
    srv.Get(".*", [&world](const httplib::Request& req, httplib::Response& res) { forward(world, req, res); });
    srv.Post(".*", [&world](const httplib::Request& req, httplib::Response& res) { forward(world, req, res); });

    g_server = &srv;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::cout << "twin-world: scenario " << scenario << " on http://" << host << ":" << port << std::endl;
    if (!srv.listen(host, port)) {
        std::cerr << "twin-world: cannot listen on " << host << ":" << port << "\n";
        return 4;
    }
    return 0;
}
