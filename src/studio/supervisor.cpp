/**
 * @file supervisor.cpp
 * @brief Deployment supervisor: instance processes (see supervisor.hpp).
 */
#include "twin/studio/supervisor.hpp"

#include <httplib.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "twin/platform/clock.hpp"
#include "twin/studio/runtime_bridge.hpp"
#include "twin/studio/services.hpp"

extern char** environ;

namespace twin::studio {

namespace fs = std::filesystem;
using json::Json;
using platform::LogLevel;

/// @brief One supervised process.
struct Process {
    std::string name;       ///< "twin-runtime", "twin-world", "twin-pt-feed".
    pid_t pid{-1};          ///< Process id (its own process group).
    int port{0};            ///< Port it serves on (0 = none).
    std::string url;        ///< Base URL (empty if none).
    fs::path log;           ///< Output log.
    std::string state;      ///< "starting", "running", "exited", "stopped".
    int exit_status{0};     ///< Exit code or -signal.
    std::string started_at; ///< ISO 8601 UTC.
};

struct Supervisor::Instance {
    std::string id;
    std::string state{"starting"};  ///< "starting", "running", "stopped", "failed".
    std::string message;
    std::vector<Process> processes;
    std::atomic<bool> bridge_stop{false};
    std::thread bridge;
    std::string started_at;
};

fs::path executable_dir() {
#if defined(__APPLE__)
    char buf[4096];
    std::uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) return fs::weakly_canonical(fs::path(buf)).parent_path();
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return fs::current_path();
}

namespace {

bool port_free(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool ok = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

std::string tail_of(const fs::path& log, std::size_t lines) {
    std::ifstream in(log);
    std::vector<std::string> all;
    for (std::string line; std::getline(in, line);) all.push_back(line);
    std::string out;
    for (std::size_t i = all.size() > lines ? all.size() - lines : 0; i < all.size(); ++i) out += all[i] + "\n";
    return out;
}

Result<pid_t> spawn(const fs::path& program, const std::vector<std::string>& args, const fs::path& log) {
    std::vector<std::string> argv_store;
    argv_store.push_back(program.string());
    argv_store.insert(argv_store.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : argv_store) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, program.c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        return make_error(ErrorCode::Unavailable, "cannot start " + program.filename().string() + ": " + std::strerror(rc));
    }
    return pid;
}

/// SIGTERM the process group of @p pid, SIGKILL after 5 s; returns the exit status (code or -signal).
int terminate_pid(pid_t pid) {
    if (pid <= 0) return 0;
    ::kill(-pid, SIGTERM);
    for (int i = 0; i < 50; ++i) {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
        if (r < 0) return 0;  // already reaped
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ::kill(-pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return -SIGKILL;
}

Json process_json(const Process& p) {
    return Json{{"name", p.name}, {"pid", p.pid}, {"port", p.port}, {"url", p.url}, {"log", p.log.string()},
                {"state", p.state}, {"exitStatus", p.exit_status}, {"startedAt", p.started_at}};
}

}  // namespace

Supervisor::Supervisor(Services& services, fs::path bin_dir, int port_begin, int port_end)
    : services_(services), bin_dir_(bin_dir.empty() ? executable_dir() : std::move(bin_dir)), port_begin_(port_begin),
      port_end_(port_end) {
    watcher_ = std::thread([this] { watch(); });
}

Supervisor::~Supervisor() {
    stop_all();
    stopping_.store(true);
    if (watcher_.joinable()) watcher_.join();
}

bool Supervisor::available() const {
    std::error_code ec;
    return fs::is_regular_file(bin_dir_ / "twin-runtime", ec);
}

int Supervisor::free_port() {
    std::set<int> used;
    for (const auto& [id, inst] : instances_) {
        for (const Process& p : inst->processes) {
            if (p.state == "running" || p.state == "starting") used.insert(p.port);
        }
    }
    for (int p = port_begin_; p <= port_end_; ++p) {
        if (!used.count(p) && port_free(p)) return p;
    }
    return 0;
}

Result<Json> Supervisor::start(const LaunchPlan& plan) {
    if (!available()) {
        return make_error(ErrorCode::Unavailable,
                          "the runtime binaries were not found next to twin-studio; build them (make build) or start "
                          "twin-studio with --bin-dir")
            .with("binDir", bin_dir_.string());
    }
    stop(plan.instance_id, "restart");
    auto inst = std::make_shared<Instance>();
    inst->id = plan.instance_id;
    inst->started_at = platform::iso8601_utc(services_.clock().now_ms());
    std::error_code ec;
    const fs::path logs = plan.work_dir / "logs";
    const fs::path ledgers = plan.work_dir / "ledgers";
    fs::create_directories(logs, ec);
    fs::create_directories(ledgers, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot create instance directories: " + ec.message());

    auto launch = [&](const std::string& name, const std::vector<std::string>& args, int port) -> Result<Process> {
        Process p;
        p.name = name;
        p.port = port;
        p.url = port > 0 ? "http://127.0.0.1:" + std::to_string(port) : std::string();
        p.log = logs / (name + ".log");
        p.started_at = platform::iso8601_utc(services_.clock().now_ms());
        { std::ofstream(p.log, std::ios::app) << "\n--- " << p.started_at << " started by the Studio supervisor\n"; }
        auto pid = spawn(bin_dir_ / name, args, p.log);
        if (!pid) return std::move(pid).error();
        p.pid = pid.value();
        p.state = "starting";
        return p;
    };
    auto wait_healthy = [&](Process& p) -> Status {
        httplib::Client cli(p.url);
        cli.set_connection_timeout(0, 300000);
        cli.set_read_timeout(2, 0);
        for (int i = 0; i < 300; ++i) {
            int status = 0;
            if (::waitpid(p.pid, &status, WNOHANG) == p.pid) {
                p.state = "exited";
                p.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
                return make_error(ErrorCode::Unavailable, p.name + " exited during start-up (status " + std::to_string(p.exit_status) + ")")
                    .with("log", tail_of(p.log, 12));
            }
            if (auto r = cli.Get("/health"); r && r->status == 200) {
                p.state = "running";
                return {};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return make_error(ErrorCode::Unavailable, p.name + " did not become healthy within 30 s").with("log", tail_of(p.log, 12));
    };
    auto fail = [&](Error e) -> Result<Json> {
        for (Process& p : inst->processes) {
            if (p.state == "running" || p.state == "starting") {
                p.exit_status = terminate_pid(p.pid);
                p.state = "stopped";
            }
        }
        {
            std::lock_guard l(mu_);
            inst->state = "failed";
            inst->message = e.message;
            instances_[plan.instance_id] = inst;
        }
        services_.app_log().write(LogLevel::Error, "studio.supervisor", "instance failed to start",
                                  {{"instance", plan.instance_id}, {"error", e.to_string()}});
        return e;
    };

    std::string world_url;
    std::string runtime_url;
    {
        std::lock_guard l(mu_);
        // Reserve ports while holding the lock so concurrent deployments never collide.
        instances_[plan.instance_id] = inst;
    }
    if (plan.mode == "cosimulation") {
        if (!plan.world_scenario) return fail(make_error(ErrorCode::InvalidArgument, "co-simulation needs a world scenario"));
        int port = 0;
        {
            std::lock_guard l(mu_);
            port = free_port();
        }
        if (port == 0) return fail(make_error(ErrorCode::Unavailable, "no free local port for twin-world"));
        auto w = launch("twin-world", {"--scenario", plan.world_scenario->string(), "--port", std::to_string(port)}, port);
        if (!w) return fail(w.error());
        {
            std::lock_guard l(mu_);
            inst->processes.push_back(w.value());
        }
        if (auto st = wait_healthy(inst->processes.back()); !st) return fail(st.error());
        world_url = inst->processes.back().url;
    }
    int runtime_port = 0;
    {
        std::lock_guard l(mu_);
        runtime_port = free_port();
    }
    if (runtime_port == 0) return fail(make_error(ErrorCode::Unavailable, "no free local port for twin-runtime"));
    std::vector<std::string> args = {"--package", plan.package_dir.string(), "--port", std::to_string(runtime_port), "--ledger-dir",
                                     ledgers.string()};
    for (const fs::path& store : plan.package_store) {
        args.push_back("--package-store");
        args.push_back(store.string());
    }
    if (plan.mode == "cosimulation") {
        args.insert(args.end(), {"--world", world_url});
        std::ostringstream speed;
        speed << plan.speed;
        args.insert(args.end(), {"--speed", speed.str()});
        if (plan.paused) args.push_back("--paused");
    } else {
        args.push_back("--monitor");
    }
    auto rt = launch("twin-runtime", args, runtime_port);
    if (!rt) return fail(rt.error());
    {
        std::lock_guard l(mu_);
        inst->processes.push_back(rt.value());
    }
    if (auto st = wait_healthy(inst->processes.back()); !st) return fail(st.error());
    runtime_url = inst->processes.back().url;
    if (plan.feed) {
        std::ostringstream speed;
        speed << plan.speed;
        auto f = launch("twin-pt-feed", {"--feed", plan.feed->string(), "--runtime", runtime_url, "--speed", speed.str()}, 0);
        if (!f) return fail(f.error());
        f.value().state = "running";
        std::lock_guard l(mu_);
        inst->processes.push_back(f.value());
    }

    // Register the URLs and start the telemetry bridge: Operate needs no configuration.
    auto twin = services_.twin_record(plan.instance_id);
    if (twin) {
        auto rec = twin.value();
        rec.runtime_url = runtime_url;
        rec.world_url = world_url.empty() ? std::nullopt : std::optional<std::string>(world_url);
        rec.desired_state = "running";
        (void)services_.upsert_twin(rec);
    }
    inst->bridge = std::thread([this, inst, runtime_url] {
        run_runtime_bridge(services_, inst->id, runtime_url, inst->bridge_stop);
    });
    {
        std::lock_guard l(mu_);
        inst->state = "running";
        inst->message = "running";
    }
    services_.app_log().write(LogLevel::Info, "studio.supervisor", "instance started",
                              {{"instance", plan.instance_id}, {"runtime", runtime_url}, {"world", world_url}, {"mode", plan.mode}});
    services_.events().publish("instance", {{"instance", plan.instance_id}, {"state", "running"}},
                               platform::iso8601_utc(services_.clock().now_ms()));
    return status(plan.instance_id);
}

void Supervisor::stop(const std::string& instance_id, const std::string& reason) {
    std::shared_ptr<Instance> inst;
    {
        std::lock_guard l(mu_);
        auto it = instances_.find(instance_id);
        if (it == instances_.end()) return;
        inst = it->second;
    }
    inst->bridge_stop.store(true);
    // Feeds first, then the runtime, then the world (the runtime drives the world). The watcher
    // ignores processes marked "stopping", so only this thread reaps them.
    std::vector<std::pair<std::size_t, pid_t>> victims;
    {
        std::lock_guard l(mu_);
        for (std::size_t k = inst->processes.size(); k-- > 0;) {
            Process& p = inst->processes[k];
            if (p.state == "running" || p.state == "starting") {
                p.state = "stopping";
                victims.emplace_back(k, p.pid);
            }
        }
    }
    std::vector<std::pair<std::size_t, int>> statuses;
    for (const auto& [k, pid] : victims) statuses.emplace_back(k, terminate_pid(pid));
    if (inst->bridge.joinable()) inst->bridge.join();
    {
        std::lock_guard l(mu_);
        for (const auto& [k, st] : statuses) {
            inst->processes[k].state = "stopped";
            inst->processes[k].exit_status = st;
        }
        if (inst->state != "failed") inst->state = "stopped";
        inst->message = reason;
    }
    services_.app_log().write(LogLevel::Info, "studio.supervisor", "instance stopped", {{"instance", instance_id}, {"reason", reason}});
}

void Supervisor::stop_all() {
    std::vector<std::string> ids;
    {
        std::lock_guard l(mu_);
        for (const auto& [id, inst] : instances_) ids.push_back(id);
    }
    for (const std::string& id : ids) stop(id, "Studio shutting down");
}

Json Supervisor::status(const std::string& instance_id) const {
    std::lock_guard l(mu_);
    auto it = instances_.find(instance_id);
    if (it == instances_.end()) return Json{{"instance", instance_id}, {"state", "not_started"}, {"processes", Json::array()}};
    const Instance& i = *it->second;
    Json procs = Json::array();
    std::string runtime_url;
    std::string world_url;
    for (const Process& p : i.processes) {
        procs.push_back(process_json(p));
        if (p.name == "twin-runtime") runtime_url = p.url;
        if (p.name == "twin-world") world_url = p.url;
    }
    return Json{{"instance", i.id},
                {"state", i.state},
                {"message", i.message},
                {"startedAt", i.started_at},
                {"runtimeUrl", runtime_url.empty() ? Json(nullptr) : Json(runtime_url)},
                {"worldUrl", world_url.empty() ? Json(nullptr) : Json(world_url)},
                {"processes", procs}};
}

Json Supervisor::status_all() const {
    std::vector<std::string> ids;
    {
        std::lock_guard l(mu_);
        for (const auto& [id, inst] : instances_) ids.push_back(id);
    }
    Json out = Json::array();
    for (const std::string& id : ids) out.push_back(status(id));
    return out;
}

void Supervisor::watch() {
    while (!stopping_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::vector<std::pair<std::string, std::string>> failures;
        {
            std::lock_guard l(mu_);
            for (auto& [id, inst] : instances_) {
                if (inst->state != "running") continue;  // starting instances are reaped by start()
                for (Process& p : inst->processes) {
                    if (p.pid <= 0 || (p.state != "running" && p.state != "starting")) continue;
                    int status = 0;
                    if (::waitpid(p.pid, &status, WNOHANG) != p.pid) continue;
                    p.state = "exited";
                    p.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
                    if (inst->state == "running" && p.name != "twin-pt-feed") {
                        inst->state = "failed";
                        inst->message = p.name + " exited unexpectedly (status " + std::to_string(p.exit_status) +
                                        "); not restarted automatically — inspect the log and redeploy";
                        failures.emplace_back(id, inst->message);
                    }
                }
            }
        }
        for (const auto& [id, message] : failures) {
            services_.app_log().write(LogLevel::Error, "studio.supervisor", message, {{"instance", id}});
            services_.events().publish("instance", {{"instance", id}, {"state", "failed"}, {"message", message}},
                                       platform::iso8601_utc(services_.clock().now_ms()));
        }
    }
}

}  // namespace twin::studio
