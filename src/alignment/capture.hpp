/**
 * @file capture.hpp
 * @brief Internal: redirect std::cout while the aligner runs (it prints reports).
 */
#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace twin::alignment::detail {

/// @brief Redirects std::cout into a buffer for its lifetime.
class CoutCapture {
public:
    CoutCapture() : previous_(std::cout.rdbuf(buffer_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(previous_); }
    CoutCapture(const CoutCapture&) = delete;
    CoutCapture& operator=(const CoutCapture&) = delete;
    CoutCapture(CoutCapture&&) = delete;
    CoutCapture& operator=(CoutCapture&&) = delete;
    /// @brief Everything printed so far.
    [[nodiscard]] std::string text() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::streambuf* previous_;
};

}  // namespace twin::alignment::detail
