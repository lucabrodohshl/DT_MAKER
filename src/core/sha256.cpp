/**
 * @file sha256.cpp
 * @brief SHA-256 as specified in FIPS 180-4, section 6.2.
 */
#include "twin/core/sha256.hpp"

#include <algorithm>

namespace twin {
namespace {

// FIPS 180-4, 4.2.2: first 32 bits of the fractional parts of the cube roots of
// the first 64 primes.
constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

// FIPS 180-4, 5.3.3: initial hash value.
constexpr std::array<std::uint32_t, 8> kInitialState = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                                        0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                                        0x1f83d9abU, 0x5be0cd19U};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32U - n));
}

constexpr char kHexDigits[] = "0123456789abcdef";

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

Sha256::Sha256() noexcept { reset(); }

void Sha256::reset() noexcept {
    state_ = kInitialState;
    buffer_.fill(0);
    buffered_ = 0;
    total_bytes_ = 0;
}

void Sha256::compress(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t t = 0; t < 16; ++t) {
        w[t] = (static_cast<std::uint32_t>(block[4 * t]) << 24U) |
               (static_cast<std::uint32_t>(block[4 * t + 1]) << 16U) |
               (static_cast<std::uint32_t>(block[4 * t + 2]) << 8U) |
               static_cast<std::uint32_t>(block[4 * t + 3]);
    }
    for (std::size_t t = 16; t < 64; ++t) {
        const std::uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3U);
        const std::uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10U);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t t = 0; t < 64; ++t) {
        const std::uint32_t big_s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + big_s1 + ch + kRoundConstants[t] + w[t];
        const std::uint32_t big_s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = big_s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> bytes) noexcept {
    total_bytes_ += bytes.size();
    std::size_t offset = 0;
    if (buffered_ > 0) {
        const std::size_t take = std::min(bytes.size(), buffer_.size() - buffered_);
        std::copy_n(bytes.begin(), take, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
        buffered_ += take;
        offset = take;
        if (buffered_ == buffer_.size()) {
            compress(buffer_.data());
            buffered_ = 0;
        }
    }
    while (bytes.size() - offset >= 64) {
        compress(bytes.data() + offset);
        offset += 64;
    }
    const std::size_t rest = bytes.size() - offset;
    if (rest > 0) {
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), rest, buffer_.begin());
        buffered_ = rest;
    }
}

void Sha256::update(std::string_view text) noexcept {
    update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),  // NOLINT
                                         text.size()));
}

Digest Sha256::finish() noexcept {
    const std::uint64_t bit_length = total_bytes_ * 8U;
    // Padding: 0x80, zeros, then the 64-bit big-endian message length.
    std::array<std::uint8_t, 72> padding{};
    padding[0] = 0x80;
    const std::size_t pad_len = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
    for (std::size_t i = 0; i < 8; ++i) {
        padding[pad_len + i] = static_cast<std::uint8_t>(bit_length >> (56U - 8U * i));
    }
    const std::uint64_t saved_total = total_bytes_;
    update(std::span<const std::uint8_t>(padding.data(), pad_len + 8));
    (void)saved_total;
    Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<std::uint8_t>(state_[i] >> 24U);
        out[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16U);
        out[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8U);
        out[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    reset();
    return out;
}

Digest sha256(std::string_view text) noexcept {
    Sha256 h;
    h.update(text);
    return h.finish();
}

std::string sha256_hex(std::string_view text) { return to_hex(sha256(text)); }

std::string to_hex(const Digest& digest) {
    std::string out;
    out.reserve(64);
    for (std::uint8_t byte : digest) {
        out.push_back(kHexDigits[byte >> 4U]);
        out.push_back(kHexDigits[byte & 0x0FU]);
    }
    return out;
}

Result<Digest> digest_from_hex(std::string_view hex) {
    if (hex.size() != 64) {
        return make_error(ErrorCode::ParseError, "a SHA-256 digest has 64 hex characters")
            .with("length", std::to_string(hex.size()));
    }
    Digest out{};
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = hex_value(hex[2 * i]);
        const int lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return make_error(ErrorCode::ParseError, "invalid hex character in digest");
        }
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

bool is_canonical_hex_digest(std::string_view hex) noexcept {
    if (hex.size() != 64) {
        return false;
    }
    return std::all_of(hex.begin(), hex.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

}  // namespace twin
