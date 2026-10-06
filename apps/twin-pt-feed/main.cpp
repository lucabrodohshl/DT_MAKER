/**
 * @file main.cpp
 * @brief twin-pt-feed: plays a scripted Physical Twin into a monitor-mode twin-runtime.
 *
 * Usage:
 *   twin-pt-feed --feed <feed.json> [--runtime http://127.0.0.1:8092] [--speed 1] [--cycles 0]
 *
 * The feed's cycle (see include/twin/ptfeed/feed.hpp) is replayed forever
 * (--cycles 0) or a given number of times. Logical time continues from the
 * runtime's current logical time; wall-clock time only paces the replay
 * (--speed = logical seconds per wall-clock second). Every PT event goes to
 * POST /runtime/pt-event (the runtime translates it through the verified label
 * equivalence E and its kernel decides); every sample goes to
 * POST /runtime/telemetry. If the runtime restarts, the feed resynchronises.
 */
#include <httplib.h>

#include <chrono>
#include <csignal>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "twin/ptfeed/feed.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int /*sig*/) { g_stop = 1; }

constexpr const char* kUsage =
    "usage: twin-pt-feed --feed <feed.json> [--runtime <url>] [--speed <x>] [--cycles <n, 0 = forever>]\n";

/// Logical time to continue from: one second after the runtime's committed time.
std::optional<twin::Ticks> runtime_time(httplib::Client& cli) {
    const httplib::Result r = cli.Get("/runtime/state");
    if (!r || r->status != 200) return std::nullopt;
    twin::Result<twin::json::Json> j = twin::json::parse(r->body);
    if (!j) return std::nullopt;
    const twin::Ticks t = j.value().value("time", twin::json::Json::object()).value("ticks", twin::Ticks{0});
    return (t / 1000 + 1) * 1000;
}

}  // namespace

/// @brief Entry point of `twin-pt-feed` (plays a scripted Physical Twin into a runtime).
int main(int argc, char** argv) {
    std::string feed_path;
    std::string runtime = "http://127.0.0.1:8092";
    double speed = 1.0;
    std::uint64_t cycles = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        const std::string v = argv[i + 1];
        if (a == "--feed") feed_path = v;
        else if (a == "--runtime") runtime = v;
        else if (a == "--speed") speed = std::max(0.05, std::stod(v));
        else if (a == "--cycles") cycles = std::stoull(v);
        else {
            std::cerr << kUsage;
            return 2;
        }
    }
    if (feed_path.empty() || argc % 2 == 0) {
        std::cerr << kUsage;
        return 2;
    }
    twin::Result<twin::ptfeed::Feed> feed = twin::ptfeed::load_feed(feed_path);
    if (!feed) {
        std::cerr << "twin-pt-feed: " << feed.error().to_string() << "\n";
        return 3;
    }
    (void)std::signal(SIGINT, on_signal);
    (void)std::signal(SIGTERM, on_signal);
    httplib::Client cli(runtime);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);
    std::cout << "twin-pt-feed: '" << feed.value().name << "' -> " << runtime << " at " << speed << "x" << std::endl;

    std::uint64_t cycle = 0;
    while (g_stop == 0 && (cycles == 0 || cycle < cycles)) {
        std::optional<twin::Ticks> base = runtime_time(cli);
        if (!base) {
            std::this_thread::sleep_for(std::chrono::seconds(2));  // runtime not (yet) reachable
            continue;
        }
        const auto wall0 = std::chrono::steady_clock::now();
        bool resync = false;
        for (const twin::ptfeed::Item& item : twin::ptfeed::cycle_items(feed.value(), 0, *base)) {
            if (g_stop != 0) break;
            const double wall_s = static_cast<double>(item.at - *base) / 1000.0 / speed;
            std::this_thread::sleep_until(wall0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                      std::chrono::duration<double>(wall_s)));
            const char* route = item.is_event ? "/runtime/pt-event" : "/runtime/telemetry";
            const httplib::Result r = cli.Post(route, item.body.dump(), "application/json");
            if (!r) {
                std::cerr << "twin-pt-feed: runtime unreachable; resynchronising\n";
                resync = true;
                break;
            }
            if (item.is_event) {
                std::cout << "t=" << static_cast<double>(item.at) / 1000.0 << " " << item.body.value("label", std::string())
                          << " -> " << (r->status == 200 ? "accepted" : "REJECTED (" + std::to_string(r->status) + ")")
                          << std::endl;
            }
        }
        if (!resync) ++cycle;
    }
    return 0;
}
