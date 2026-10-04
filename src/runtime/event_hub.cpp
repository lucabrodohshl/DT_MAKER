/**
 * @file event_hub.cpp
 * @brief Bounded publish/subscribe buffer with SSE framing.
 */
#include "twin/runtime/event_hub.hpp"

#include <chrono>

namespace twin::runtime {

std::uint64_t EventHub::publish(std::string type, json::Json data) {
    std::uint64_t seq = 0;
    {
        std::lock_guard lock(mutex_);
        seq = next_seq_++;
        buffer_.push_back(HubEvent{seq, std::move(type), std::move(data)});
        while (buffer_.size() > capacity_) buffer_.pop_front();
    }
    cv_.notify_all();
    return seq;
}

std::vector<HubEvent> EventHub::wait_after(std::uint64_t after, int timeout_ms) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                 [&] { return closed_ || (!buffer_.empty() && buffer_.back().seq > after); });
    std::vector<HubEvent> out;
    for (const HubEvent& e : buffer_) {
        if (e.seq > after) out.push_back(e);
    }
    return out;
}

std::uint64_t EventHub::head() const {
    std::lock_guard lock(mutex_);
    return next_seq_ - 1;
}

std::string EventHub::sse_frame(const HubEvent& e) {
    return "id: " + std::to_string(e.seq) + "\nevent: " + e.type + "\ndata: " + e.data.dump() + "\n\n";
}

void EventHub::close() {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
}

}  // namespace twin::runtime
