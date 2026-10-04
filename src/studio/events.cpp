/**
 * @file events.cpp
 * @brief Event hub (see events.hpp).
 */
#include "twin/studio/events.hpp"

#include <array>
#include <cstdio>
#include <random>

namespace twin::studio {

EventHub::EventHub(std::size_t capacity) : capacity_(capacity) {
    std::random_device rd;
    std::array<char, 17> buf{};
    std::snprintf(buf.data(), buf.size(), "%08x%08x", rd(), rd());
    epoch_ = buf.data();
}

std::uint64_t EventHub::publish(std::string topic, json::Json data, std::string at) {
    std::uint64_t seq = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        seq = next_++;
        buffer_.push_back({seq, std::move(topic), std::move(data), std::move(at)});
        while (buffer_.size() > capacity_) buffer_.pop_front();
    }
    cv_.notify_all();
    return seq;
}

EventBatch EventHub::wait_after(std::uint64_t after, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [&] { return closed_ || next_ - 1 > after; });
    EventBatch batch;
    if (!buffer_.empty() && after + 1 < buffer_.front().seq) batch.gap = true;
    for (const auto& e : buffer_) {
        if (e.seq > after) batch.events.push_back(e);
    }
    return batch;
}

std::uint64_t EventHub::head() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return next_ - 1;
}

void EventHub::close() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
}

}  // namespace twin::studio
