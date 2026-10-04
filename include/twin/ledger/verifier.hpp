/**
 * @file verifier.hpp
 * @brief Ledger verification: chain integrity, continuity, identity, anchors.
 * @ingroup ledger
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::ledger {

/**
 * @brief An externally kept checkpoint {seq, hash} of a ledger.
 *
 * Publishing anchors to storage the ledger writer cannot modify (WORM storage,
 * a transparency log, a counter-signed message) makes truncation and wholesale
 * rewriting of the ledger detectable.
 */
struct Anchor {
    std::string session;  ///< Session id.
    std::uint64_t seq{0}; ///< Sequence number of the anchored record.
    std::string hash;     ///< Chain hash of that record.
};

/// @brief Encode / decode an anchor ("twin-ledger-anchor/1").
[[nodiscard]] json::Json to_json(const Anchor& anchor);
[[nodiscard]] Result<Anchor> anchor_from_json(const json::Json& j);

/// @brief Verification options.
struct VerifyOptions {
    std::optional<std::string> expected_package_hash;  ///< Records must name this package.
    std::optional<Anchor> anchor;                      ///< Anchor the ledger must contain.
    bool require_end_record{false};  ///< Treat a missing "end" record as an error (closed sessions).
};

/// @brief One problem found in a ledger.
struct Issue {
    std::uint64_t line{0};  ///< 1-based line number (0 = whole file).
    std::string code;       ///< Stable code, e.g. "hash_mismatch".
    std::string message;    ///< Explanation.
};

/// @brief Result of a verification.
struct VerificationReport {
    bool valid{false};              ///< No issues.
    std::uint64_t records{0};       ///< Lines examined.
    std::string session;            ///< Session id from the genesis record.
    std::string package_hash;       ///< Package hash from the genesis record.
    std::uint64_t head_seq{0};      ///< Sequence number of the last valid record.
    std::string head_hash;          ///< Chain hash of the last valid record.
    bool has_end_record{false};     ///< The session was closed in an orderly way.
    std::vector<Issue> issues;      ///< All problems found.
};

/// @brief Encode a report (for APIs and CLIs).
[[nodiscard]] json::Json to_json(const VerificationReport& report);

/// @brief Verify ledger text (JSON Lines).
[[nodiscard]] VerificationReport verify_text(std::string_view text, const VerifyOptions& options = {});

/// @brief Verify a ledger file.
[[nodiscard]] VerificationReport verify_file(const std::filesystem::path& path,
                                             const VerifyOptions& options = {});

/**
 * @brief Tamper drill: verify a copy of @p text in which record @p line_index
 * (0-based) has one field altered. Never touches the original. Used by the
 * runtime's demonstration endpoint to show that a modification is detected.
 */
[[nodiscard]] VerificationReport tamper_drill(std::string_view text, std::uint64_t line_index);

}  // namespace twin::ledger
