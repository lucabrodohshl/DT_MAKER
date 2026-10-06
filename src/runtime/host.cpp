/**
 * @file host.cpp
 * @brief Shared helpers of runtime hosts.
 */
#include "twin/runtime/host.hpp"

namespace twin::runtime {

json::Json ledger_event_summary(const SessionEvent& e) {
    const json::Json& body = e.record.at("body");
    json::Json summary{{"seq", body.value("seq", 0)},
                       {"kind", e.kind},
                       {"time_after", body.value("time_after", Ticks{0})},
                       {"prev_hash", body.value("prev_hash", std::string())},
                       {"hash", e.record.value("hash", std::string())}};
    if (body.contains("outcome") && !body.at("outcome").at("branches").empty()) {
        const json::Json& b = body.at("outcome").at("branches").at(0);
        summary["transition"] = b.value("transition", std::string());
        summary["label"] = b.value("label", std::string());
        summary["from"] = b.value("source", std::string());
        summary["to"] = b.value("target", std::string());
    }
    if (body.contains("input")) {
        summary["input"] = body.at("input").value("name", std::string());
        summary["source"] = body.at("input").value("source", std::string());
    }
    if (body.contains("error")) summary["error"] = body.at("error").value("code", std::string());
    if (body.contains("alarm")) summary["alarm"] = body.at("alarm");
    if (body.contains("topic")) summary["topic"] = body.at("topic");
    return summary;
}

void stream_ledger(TwinSession& session, EventHub& hub) {
    session.subscribe([&hub](const SessionEvent& e) { hub.publish("ledger", ledger_event_summary(e)); });
}

}  // namespace twin::runtime
