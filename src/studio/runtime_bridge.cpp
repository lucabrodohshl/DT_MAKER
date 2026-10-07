/**
 * @file runtime_bridge.cpp
 * @brief Runtime telemetry → Studio history (see runtime_bridge.hpp).
 */
#include "twin/studio/runtime_bridge.hpp"

#include <httplib.h>

#include <chrono>
#include <optional>
#include <map>
#include <thread>

#include "twin/platform/clock.hpp"

namespace twin::studio {

using namespace twin::platform;

std::string runtime_source_prefix(std::string_view twin_id) { return "runtime:" + std::string(twin_id) + "/telemetry#"; }

namespace {

/// @brief field name -> channel, for channels bound to this twin's runtime telemetry.
std::map<std::string, TelemetryChannel> bound_channels(Services& services, const std::string& twin_id) {
    std::map<std::string, TelemetryChannel> out;
    auto all = services.telemetry_channels_all();
    if (!all) return out;
    const std::string prefix = runtime_source_prefix(twin_id);
    for (const auto& c : all.value()) {
        if (c.source.rfind(prefix, 0) == 0) out.emplace(c.source.substr(prefix.size()), c);
    }
    return out;
}

/// @brief Incremental SSE parser: feeds complete events ("\n\n"-terminated blocks) to a callback.
class SseParser {
public:
    template <class F>
    void feed(const char* data, std::size_t len, F&& on_event) {
        buffer_.append(data, len);
        for (;;) {
            const auto end = buffer_.find("\n\n");
            if (end == std::string::npos) break;
            const std::string block = buffer_.substr(0, end);
            buffer_.erase(0, end + 2);
            std::string event = "message";
            std::string payload;
            std::uint64_t id = 0;
            std::size_t pos = 0;
            while (pos <= block.size()) {
                auto nl = block.find('\n', pos);
                if (nl == std::string::npos) nl = block.size();
                const std::string line = block.substr(pos, nl - pos);
                pos = nl + 1;
                auto field_value = [&](std::size_t prefix) {
                    std::string v = line.substr(prefix);
                    if (!v.empty() && v.front() == ' ') v.erase(0, 1);
                    return v;
                };
                if (line.rfind("id:", 0) == 0) {
                    try {
                        id = std::stoull(field_value(3));
                    } catch (const std::exception&) {
                        id = 0;
                    }
                } else if (line.rfind("event:", 0) == 0) event = field_value(6);
                else if (line.rfind("data:", 0) == 0) payload += field_value(5);
            }
            on_event(event, payload, id);
        }
        if (buffer_.size() > (1U << 22)) buffer_.clear();  // defensive bound against malformed streams
    }

private:
    std::string buffer_;
};

}  // namespace

void run_runtime_bridge(Services& services, const std::string& twin_id, const std::string& runtime_url,
                        const std::atomic<bool>& stop) {
    int backoff_ms = 1000;
    std::uint64_t last_id = 0;  // resume cursor: never re-ingest the runtime's replay buffer
    while (!stop.load()) {
        const auto channels = bound_channels(services, twin_id);
        if (channels.empty()) {
            services.app_log().write(LogLevel::Info, "studio.runtime-bridge",
                                     "no channels bound to runtime telemetry; bridge idle", {{"twin", twin_id}});
            for (int i = 0; i < 300 && !stop.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        httplib::Client cli(runtime_url);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(60, 0);
        SseParser parser;
        std::int64_t stored = 0;
        services.app_log().write(LogLevel::Info, "studio.runtime-bridge", "connecting to runtime stream",
                                 {{"twin", twin_id}, {"url", runtime_url}, {"channels", channels.size()}});
        httplib::Headers headers;
        if (last_id > 0) headers.emplace("Last-Event-ID", std::to_string(last_id));
        // Ask for the whole replay buffer on the first connection (no sample is lost between the
        // runtime starting and the bridge connecting), and resume from the last id afterwards.
        auto result = cli.Get("/runtime/stream?after=" + std::to_string(last_id), headers, [&](const char* data, std::size_t len) {
            if (stop.load()) return false;
            parser.feed(data, len, [&](const std::string& event, const std::string& payload, std::uint64_t id) {
                if (id > 0) {
                    if (id <= last_id && last_id - id > 1) {
                        last_id = 0;  // ids went backwards: the runtime restarted; start a new cursor
                    } else if (id <= last_id) {
                        return;  // duplicate delivery
                    }
                    last_id = id;
                }
                if (event != "telemetry") return;
                auto j = json::parse(payload);
                if (!j || !j.value().is_object()) return;
                const std::int64_t now = services.clock().now_ms();
                std::optional<std::int64_t> logical;
                if (auto at = j.value().find("at"); at != j.value().end() && at->is_number_integer()) {
                    logical = at->get<std::int64_t>();
                }
                for (const auto& [field, channel] : channels) {
                    auto it = j.value().find(field);
                    if (it == j.value().end()) continue;
                    TelemetrySample s;
                    s.observed_ms = now;
                    s.ingested_ms = now;
                    s.logical_ticks = logical;
                    if (channel.value_type == "number" && it->is_number()) {
                        s.number = it->get<double>();
                        // Unit conversion of the binding (canonical unit of the data contract), if any.
                        const json::Json& tf = channel.presentation.value("transform", json::Json());
                        if (tf.is_object()) {
                            const auto dec = [&](const char* k, double fallback) {
                                const json::Json& v = tf.value(k, json::Json());
                                if (v.is_number()) return v.get<double>();
                                if (v.is_string()) return std::strtod(v.get<std::string>().c_str(), nullptr);
                                return fallback;
                            };
                            s.number = *s.number * dec("scale", 1.0) + dec("offset", 0.0);
                        }
                    } else if (channel.value_type == "boolean" && it->is_boolean()) {
                        s.text = it->get<bool>() ? "true" : "false";
                    } else if ((channel.value_type == "category" || channel.value_type == "string") && it->is_string()) {
                        s.text = it->get<std::string>();
                    } else {
                        continue;  // type mismatch: never coerce silently
                    }
                    if (services.ingest_samples(channel.id, {s})) ++stored;
                }
            });
            backoff_ms = 1000;
            return !stop.load();
        });
        if (stop.load()) break;
        services.app_log().write(LogLevel::Warn, "studio.runtime-bridge", "runtime stream ended; reconnecting",
                                 {{"twin", twin_id},
                                  {"error", result ? std::string("closed by runtime") : httplib::to_string(result.error())},
                                  {"samplesStored", stored},
                                  {"retryMs", backoff_ms}});
        for (int waited = 0; waited < backoff_ms && !stop.load(); waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        backoff_ms = std::min(backoff_ms * 2, 30000);
    }
}

}  // namespace twin::studio
