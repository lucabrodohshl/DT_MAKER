/**
 * @file sha256.hpp
 * @brief SHA-256 (FIPS 180-4) digests and hexadecimal encoding.
 * @ingroup core
 *
 * Used for content addressing (package manifests, IR hashes) and for the
 * hash chain of the tamper-evident execution ledger. The implementation is
 * self-contained and validated against the NIST FIPS 180-4 / CAVP test
 * vectors in tests/unit/core/sha256_test.cpp.
 *
 * Hashes provide provenance and integrity evidence. They do not, by
 * themselves, prove semantic correctness of the hashed artefact.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "twin/core/result.hpp"

namespace twin {

/// @brief A 256-bit digest.
using Digest = std::array<std::uint8_t, 32>;

/**
 * @brief Incremental SHA-256 hasher.
 *
 * Usage:
 * @code
 *   twin::Sha256 h;
 *   h.update("abc");
 *   twin::Digest d = h.finish();
 * @endcode
 * After finish() the object is reset and may be reused.
 */
class Sha256 {
public:
    Sha256() noexcept;

    /// @brief Absorb raw bytes.
    void update(std::span<const std::uint8_t> bytes) noexcept;
    /// @brief Absorb the bytes of a string (no terminator).
    void update(std::string_view text) noexcept;
    /// @brief Finalise and return the digest; resets the hasher.
    [[nodiscard]] Digest finish() noexcept;

private:
    void reset() noexcept;
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{0};
    std::uint64_t total_bytes_{0};
};

/// @brief One-shot digest of a string.
[[nodiscard]] Digest sha256(std::string_view text) noexcept;
/// @brief One-shot digest rendered as 64 lowercase hex characters.
[[nodiscard]] std::string sha256_hex(std::string_view text);
/// @brief Lowercase hexadecimal rendering of a digest.
[[nodiscard]] std::string to_hex(const Digest& digest);
/// @brief Parse 64 hex characters (either case) into a digest.
[[nodiscard]] Result<Digest> digest_from_hex(std::string_view hex);
/// @brief Check that @p hex is 64 lowercase hex characters (the canonical rendering).
[[nodiscard]] bool is_canonical_hex_digest(std::string_view hex) noexcept;

}  // namespace twin
