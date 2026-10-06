/**
 * @file main.cpp
 * @brief twin-runtime: the production Digital Twin runtime (K_prod).
 *
 * Usage:
 * @verbatim
 *   twin-runtime --package <dir.twinpkg> [--monitor | --world http://127.0.0.1:8091]
 *                [--host 127.0.0.1] [--port 8090] [--static <dir>]
 *                [--ledger-dir var/ledgers] [--package-store <dir>]... [--speed 2]
 *                [--paused] [--deterministic] [--allow-unaligned]
 * @endverbatim
 *
 * Modes:
 *  - co-simulation (default): the runtime is the co-simulation master of the
 *    Physical Twin simulator at --world (the indoor drone). --paused starts with
 *    the co-simulation paused and the mission unstarted (an operator starts it
 *    with POST /simulation/start or POST /mission/start).
 *  - --monitor: the runtime monitors an external Physical Twin that pushes PT
 *    events (POST /runtime/pt-event) and telemetry (POST /runtime/telemetry),
 *    e.g. twin-pt-feed. Works for any aligned package.
 *
 * Start-up refuses any package whose hashes, bindings or alignment evidence do
 * not verify (twin::package::load_and_verify). --package-store names
 * directories of further packages, used only to verify and replay past
 * executions with the exact package they recorded. The API is documented in
 * docs/runtime-api.md.
 */
#include <httplib.h>

#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "twin/package/package.hpp"
#include "twin/runtime/api_server.hpp"
#include "twin/runtime/cosim_driver.hpp"
#include "twin/runtime/executions.hpp"
#include "twin/runtime/monitor_host.hpp"

namespace {

httplib::Server* g_server = nullptr;

void on_signal(int /*sig*/) {
    if (g_server != nullptr) g_server->stop();
}

constexpr const char* kUsage =
    "usage: twin-runtime --package <dir> [--monitor | --world <url>] [--host <h>] [--port <p>]\n"
    "                    [--static <dir>] [--ledger-dir <dir>] [--package-store <dir>]... [--speed <x>]\n"
    "                    [--paused] [--deterministic] [--allow-unaligned]\n";

struct Options {
    std::string package_dir;
    std::string world_url = "http://127.0.0.1:8091";
    std::string host = "127.0.0.1";
    int port = 8090;
    std::string static_dir;
    std::vector<std::filesystem::path> package_store;
    twin::runtime::DriverConfig config;
    bool monitor = false;
    bool paused = false;
    bool allow_unaligned = false;
};

std::optional<Options> parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--monitor" || a == "--paused" || a == "--deterministic" || a == "--allow-unaligned") {
            o.monitor = o.monitor || a == "--monitor";
            o.paused = o.paused || a == "--paused";
            o.config.deterministic = o.config.deterministic || a == "--deterministic";
            o.allow_unaligned = o.allow_unaligned || a == "--allow-unaligned";
            continue;
        }
        if (i + 1 >= argc) return std::nullopt;
        const std::string v = argv[++i];
        if (a == "--package") o.package_dir = v;
        else if (a == "--world") o.world_url = v;
        else if (a == "--host") o.host = v;
        else if (a == "--port") o.port = std::stoi(v);
        else if (a == "--static") o.static_dir = v;
        else if (a == "--ledger-dir") o.config.ledger_dir = v;
        else if (a == "--package-store") o.package_store.emplace_back(v);
        else if (a == "--speed") o.config.speed = std::stod(v);
        else return std::nullopt;
    }
    if (o.package_dir.empty()) return std::nullopt;
    return o;
}

}  // namespace

/// @brief Entry point of `twin-runtime` (see the file documentation for options and modes).
int main(int argc, char** argv) {
    std::optional<Options> parsed = parse(argc, argv);
    if (!parsed) {
        std::cerr << kUsage;
        return 2;
    }
    Options& o = *parsed;

    // Only a verified package is ever executed.
    twin::Result<twin::package::LoadedPackage> pkg =
        twin::package::load_and_verify(o.package_dir, twin::package::VerifyOptions{o.allow_unaligned});
    if (!pkg) {
        std::cerr << "twin-runtime: REFUSING to start: " << pkg.error().to_string() << "\n";
        return 3;
    }
    std::cout << "twin-runtime: package " << pkg.value().package_hash.substr(0, 16) << "… verified ("
              << pkg.value().checks.size() << " checks), model " << pkg.value().manifest.model_id << " "
              << pkg.value().manifest.model_version << "\n";

    std::error_code ec;
    std::filesystem::create_directories(o.config.ledger_dir, ec);
    twin::runtime::EventHub hub;
    twin::runtime::ExecutionStore executions(o.config.ledger_dir);
    twin::runtime::PackageRegistry packages(pkg.value(), o.package_store);
    twin::runtime::ApiContext context{nullptr, nullptr, nullptr, &hub, &executions, &packages};

    std::unique_ptr<twin::runtime::CoSimDriver> driver;
    std::unique_ptr<twin::runtime::MonitorHost> monitor;
    if (o.monitor) {
        twin::Result<std::unique_ptr<twin::runtime::MonitorHost>> m = twin::runtime::MonitorHost::create(
            std::move(pkg).value(), hub, twin::runtime::MonitorConfig{o.config.ledger_dir, o.config.deterministic});
        if (!m) {
            std::cerr << "twin-runtime: cannot start the monitoring session: " << m.error().to_string() << "\n";
            return 4;
        }
        monitor = std::move(m).value();
        context.host = monitor.get();
        context.monitor = monitor.get();
    } else {
        o.config.autostart = !o.paused;
        driver = std::make_unique<twin::runtime::CoSimDriver>(
            std::move(pkg).value(), twin::runtime::make_http_world_port(o.world_url), hub, o.config);
        if (twin::Status s = driver->reset(); !s) {
            std::cerr << "twin-runtime: cannot start the session: " << s.error().to_string() << "\n"
                      << "  (is twin-world running at " << o.world_url << "?)\n";
            return 4;
        }
        driver->launch();
        if (!o.paused) driver->play();
        context.host = driver.get();
        context.cosim = driver.get();
    }

    httplib::Server srv;
    twin::runtime::register_routes(srv, context, o.static_dir);
    g_server = &srv;
    (void)std::signal(SIGINT, on_signal);
    (void)std::signal(SIGTERM, on_signal);
    std::cout << "twin-runtime: " << context.host->mode() << " mode, API on http://" << o.host << ":" << o.port
              << (o.paused && !o.monitor ? "  (paused: start with POST /simulation/start)" : "") << '\n'
              << std::flush;
    const bool ok = srv.listen(o.host, o.port);
    hub.close();
    if (!ok) {
        std::cerr << "twin-runtime: cannot listen on " << o.host << ":" << o.port << "\n";
        return 5;
    }
    return 0;
}
