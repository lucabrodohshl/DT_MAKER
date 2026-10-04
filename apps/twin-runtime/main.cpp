/**
 * @file main.cpp
 * @brief twin-runtime: the production Digital Twin runtime (K_prod).
 *
 * Usage:
 *   twin-runtime --package <dir.twinpkg> [--world http://127.0.0.1:8091]
 *                [--host 127.0.0.1] [--port 8090] [--ui <web/drone-console/dist>]
 *                [--ledger-dir var/ledgers] [--speed 2] [--paused] [--deterministic]
 *                [--allow-unaligned]
 *
 * Start-up refuses any package whose hashes, bindings or alignment evidence do
 * not verify (see twin::package::load_and_verify). The runtime then serves the
 * API documented in docs/runtime-api.md and drives the co-simulation.
 */
#include <httplib.h>

#include <csignal>
#include <iostream>
#include <string>

#include "twin/package/package.hpp"
#include "twin/runtime/api_server.hpp"
#include "twin/runtime/cosim_driver.hpp"

namespace {

httplib::Server* g_server = nullptr;

void on_signal(int /*sig*/) {
    if (g_server != nullptr) g_server->stop();
}

constexpr const char* kUsage =
    "usage: twin-runtime --package <dir> [--world <url>] [--host <h>] [--port <p>] [--ui <dir>]\n"
    "                    [--ledger-dir <dir>] [--speed <x>] [--paused] [--deterministic] [--allow-unaligned]\n";

}  // namespace

int main(int argc, char** argv) {
    std::string package_dir;
    std::string world_url = "http://127.0.0.1:8091";
    std::string host = "127.0.0.1";
    int port = 8090;
    std::string ui_dir;
    twin::runtime::DriverConfig config;
    bool paused = false;
    bool allow_unaligned = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << kUsage;
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--package") package_dir = next();
        else if (a == "--world") world_url = next();
        else if (a == "--host") host = next();
        else if (a == "--port") port = std::stoi(next());
        else if (a == "--ui") ui_dir = next();
        else if (a == "--ledger-dir") config.ledger_dir = next();
        else if (a == "--speed") config.speed = std::stod(next());
        else if (a == "--paused") paused = true;
        else if (a == "--deterministic") config.deterministic = true;
        else if (a == "--allow-unaligned") allow_unaligned = true;
        else {
            std::cerr << kUsage;
            return 2;
        }
    }
    if (package_dir.empty()) {
        std::cerr << kUsage;
        return 2;
    }

    // Only a verified package is ever executed.
    twin::Result<twin::package::LoadedPackage> pkg =
        twin::package::load_and_verify(package_dir, twin::package::VerifyOptions{allow_unaligned});
    if (!pkg) {
        std::cerr << "twin-runtime: REFUSING to start: " << pkg.error().to_string() << "\n";
        return 3;
    }
    std::cout << "twin-runtime: package " << pkg.value().package_hash.substr(0, 16) << "… verified ("
              << pkg.value().checks.size() << " checks), model " << pkg.value().manifest.model_id << " "
              << pkg.value().manifest.model_version << "\n";

    twin::runtime::EventHub hub;
    config.autostart = !paused;
    twin::runtime::CoSimDriver driver(std::move(pkg).value(), twin::runtime::make_http_world_port(world_url), hub,
                                      config);
    if (twin::Status s = driver.reset(); !s) {
        std::cerr << "twin-runtime: cannot start the session: " << s.error().to_string() << "\n"
                  << "  (is twin-world running at " << world_url << "?)\n";
        return 4;
    }
    driver.launch();
    if (!paused) driver.play();

    httplib::Server srv;
    twin::runtime::register_routes(srv, driver, hub, ui_dir);
    g_server = &srv;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::cout << "twin-runtime: API on http://" << host << ":" << port
              << (ui_dir.empty() ? "" : "  (UI: http://" + host + ":" + std::to_string(port) + "/)") << std::endl;
    const bool ok = srv.listen(host, port);
    hub.close();
    if (!ok) {
        std::cerr << "twin-runtime: cannot listen on " << host << ":" << port << "\n";
        return 5;
    }
    return 0;
}
