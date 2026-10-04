/**
 * @file main.cpp
 * @brief `twin-studio` — the Verified Twin Studio server.
 *
 * Commands:
 *   twin-studio serve  [--data-dir DIR] [--host H] [--port N] [--web-root DIR]
 *                      [--runtime TWIN=URL]... [--world TWIN=URL]... [--simulate-telemetry EXAMPLE_DIR]...
 *   twin-studio seed   --example DIR [--data-dir DIR] [--no-telemetry]
 *   twin-studio demo   [--data-dir DIR] [--port N] [--web-root DIR] [--runtime TWIN=URL]...
 *                      (seeds examples/industrial-pump into an empty data dir, then serves
 *                       with the simulated pump telemetry feed)
 *   twin-studio version
 *
 * Exit codes: 0 success, 1 runtime failure, 2 usage error.
 */
#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "twin/studio/seed.hpp"
#include "twin/studio/server.hpp"
#include "twin/studio/services.hpp"

namespace fs = std::filesystem;
using namespace twin::studio;

namespace {

std::atomic<StudioServer*> g_server{nullptr};

void on_signal(int) {
    if (auto* s = g_server.load()) s->stop();
}

struct Args {
    std::string command;
    std::map<std::string, std::vector<std::string>> options;
    std::vector<std::string> flags;

    [[nodiscard]] std::optional<std::string> get(const std::string& key) const {
        auto it = options.find(key);
        if (it == options.end() || it->second.empty()) return std::nullopt;
        return it->second.back();
    }
    [[nodiscard]] bool flag(const std::string& f) const {
        return std::find(flags.begin(), flags.end(), f) != flags.end();
    }
};

int usage(const std::string& error = {}) {
    if (!error.empty()) std::cerr << "error: " << error << "\n\n";
    std::cerr << "usage:\n"
                 "  twin-studio serve  [--data-dir DIR] [--host H] [--port N] [--web-root DIR]\n"
                 "                     [--runtime TWIN=URL]... [--world TWIN=URL]... [--simulate-telemetry EXAMPLE_DIR]...\n"
                 "  twin-studio seed   --example DIR [--data-dir DIR] [--no-telemetry]\n"
                 "  twin-studio demo   [--data-dir DIR] [--port N] [--web-root DIR] [--runtime TWIN=URL]...\n"
                 "  twin-studio version\n";
    return 2;
}

std::optional<Args> parse(int argc, char** argv) {
    if (argc < 2) return std::nullopt;
    Args a;
    a.command = argv[1];
    static const std::vector<std::string> kFlags = {"--no-telemetry"};
    for (int i = 2; i < argc; ++i) {
        std::string k = argv[i];
        if (std::find(kFlags.begin(), kFlags.end(), k) != kFlags.end()) {
            a.flags.push_back(k);
            continue;
        }
        if (k.rfind("--", 0) != 0 || i + 1 >= argc) return std::nullopt;
        a.options[k].push_back(argv[++i]);
    }
    return a;
}

std::map<std::string, std::string> pairs(const Args& a, const std::string& key) {
    std::map<std::string, std::string> out;
    auto it = a.options.find(key);
    if (it == a.options.end()) return out;
    for (const auto& v : it->second) {
        const auto eq = v.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        out[v.substr(0, eq)] = v.substr(eq + 1);
    }
    return out;
}

std::unique_ptr<Services> open_services(const Args& a) {
    StudioConfig config;
    config.data_dir = a.get("--data-dir").value_or("var/studio");
    auto s = Services::open(config);
    if (!s) {
        std::cerr << "error: " << s.error().to_string() << "\n";
        return nullptr;
    }
    return std::move(s).value();
}

int seed(Services& services, const fs::path& example, bool telemetry) {
    std::cout << "Seeding " << example << " (validation, alignment, compilation and packaging run for real)...\n";
    SeedOptions options;
    options.telemetry_history = telemetry;
    auto r = seed_example(services, example, options);
    if (!r) {
        std::cerr << "error: " << r.error().to_string() << "\n";
        return 1;
    }
    std::cout << "  twin:       " << r.value()["twin"].get<std::string>() << "\n"
              << "  artefacts:  " << r.value()["artifacts"].dump() << "\n"
              << "  package:    " << r.value()["bootstrap"]["package"]["id"].get<std::string>() << " ("
              << r.value()["bootstrap"]["package"]["packageHash"].get<std::string>().substr(0, 16) << "…)\n"
              << "  telemetry:  " << r.value()["telemetrySamples"] << " samples\n";
    return 0;
}

int serve(Services& services, const Args& a, std::vector<fs::path> feeds) {
    ServerOptions options;
    options.host = a.get("--host").value_or("127.0.0.1");
    try {
        options.port = std::stoi(a.get("--port").value_or("8080"));
    } catch (const std::exception&) {
        return usage("--port must be a number");
    }
    if (auto w = a.get("--web-root")) options.web_root = *w;
    options.runtime_urls = pairs(a, "--runtime");
    options.world_urls = pairs(a, "--world");
    for (const auto& [twin, url] : options.runtime_urls) {
        // Persist the association so other tools see which runtime serves a twin.
        auto t = services.twin_record(twin);
        if (t) {
            auto rec = t.value();
            rec.runtime_url = url;
            (void)services.upsert_twin(rec);
        } else {
            std::cerr << "warning: --runtime names unknown twin '" << twin << "'\n";
        }
    }
    if (auto f = a.options.find("--simulate-telemetry"); f != a.options.end()) {
        for (const auto& p : f->second) feeds.emplace_back(p);
    }

    StudioServer server(services, options);
    auto port = server.bind();
    if (!port) {
        std::cerr << "error: " << port.error().to_string() << "\n";
        return 1;
    }
    g_server.store(&server);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::atomic<bool> stop{false};
    std::vector<std::thread> feed_threads;
    for (const auto& f : feeds) {
        feed_threads.emplace_back([&services, f, &stop] { run_demo_feed(services, f, stop); });
    }
    std::cout << "Verified Twin Studio listening on http://" << options.host << ":" << port.value() << "\n"
              << "  data dir:  " << services.config().data_dir << "\n"
              << "  web UI:    " << (options.web_root ? options.web_root->string() : std::string("(not served; use the Vite dev server)")) << "\n";
    for (const auto& [t, u] : options.runtime_urls) std::cout << "  runtime:   " << t << " -> " << u << "\n";
    for (const auto& f : feeds) std::cout << "  simulated telemetry feed: " << f << "\n";

    auto st = server.listen();
    stop.store(true);
    for (auto& t : feed_threads) t.join();
    g_server.store(nullptr);
    if (!st) {
        std::cerr << "error: " << st.error().to_string() << "\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    auto args = parse(argc, argv);
    if (!args) return usage();
    const Args& a = *args;
    if (a.command == "version") {
        StudioConfig config;
        config.data_dir = fs::temp_directory_path() / "twin-studio-version";
        auto s = Services::open(config);
        std::cout << (s ? s.value()->about().dump(2) : std::string("twin-studio")) << "\n";
        return 0;
    }
    if (a.command == "seed") {
        auto ex = a.get("--example");
        if (!ex) return usage("seed requires --example DIR");
        auto services = open_services(a);
        if (!services) return 1;
        return seed(*services, *ex, !a.flag("--no-telemetry"));
    }
    if (a.command == "serve") {
        auto services = open_services(a);
        if (!services) return 1;
        return serve(*services, a, {});
    }
    if (a.command == "demo") {
        auto services = open_services(a);
        if (!services) return 1;
        const fs::path example = a.get("--example").value_or("examples/industrial-pump");
        auto twins = services->twins();
        if (twins && twins.value().empty()) {
            if (const int rc = seed(*services, example, true); rc != 0) return rc;
        }
        return serve(*services, a, {example});
    }
    return usage("unknown command '" + a.command + "'");
}
