/**
 * @file canonical.hpp
 * @brief Canonical JSON serialisation and typed, error-returning JSON access.
 * @ingroup json
 *
 * @defgroup json Canonical JSON
 * @brief Byte-exact serialisation of everything that is hashed (IR, manifests, ledger records).
 *
 * Everything that is hashed in this project (Twin IR, package manifests,
 * ledger records) is serialised in **canonical JSON**:
 *
 *  - UTF-8, no insignificant whitespace;
 *  - object members sorted by key in byte-wise lexicographic order;
 *  - numbers are integers only (no floating point anywhere in hashed data);
 *  - strings use the minimal JSON escaping produced by nlohmann::json.
 *
 * For the integer-only, sorted-key documents used here this coincides with
 * the RFC 8785 (JCS) canonical form for ASCII keys. Two documents with equal
 * content therefore have byte-identical serialisations and equal hashes.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "twin/core/result.hpp"

namespace twin::json {

/// @brief JSON value type. Objects keep keys sorted (std::map), as required for canonical form.
using Json = nlohmann::json;

/**
 * @brief Serialise @p value canonically.
 * @return ErrorCode::ValidationError if the document contains a floating-point
 *         number (forbidden in hashed data) or invalid UTF-8.
 */
[[nodiscard]] Result<std::string> canonical_dump(const Json& value);

/// @brief SHA-256 (lowercase hex) of canonical_dump(@p value).
[[nodiscard]] Result<std::string> canonical_sha256(const Json& value);

/**
 * @brief Parse JSON text without throwing.
 * @return ErrorCode::ParseError with the parser's position on malformed input.
 */
[[nodiscard]] Result<Json> parse(std::string_view text);

/**
 * @brief Parse @p text and require that it is already in canonical form.
 *
 * Rejects documents whose canonical re-serialisation differs from the input
 * bytes (e.g. duplicate keys, whitespace, unsorted keys, floats). Used for
 * every hashed artefact so that "the bytes that were hashed" and "the value
 * that is interpreted" cannot diverge.
 */
[[nodiscard]] Result<Json> parse_canonical(std::string_view text);

/// @name Typed member access returning structured errors (no exceptions).
/// @{
/// @brief Typed access to member @p key of @p object (ValidationError if absent or of another type).
[[nodiscard]] Result<const Json*> member(const Json& object, std::string_view key);
[[nodiscard]] Result<std::string> get_string(const Json& object, std::string_view key);
[[nodiscard]] Result<std::int64_t> get_int(const Json& object, std::string_view key);
[[nodiscard]] Result<bool> get_bool(const Json& object, std::string_view key);
[[nodiscard]] Result<const Json*> get_array(const Json& object, std::string_view key);
[[nodiscard]] Result<const Json*> get_object(const Json& object, std::string_view key);
/// @brief Fail unless @p object has exactly the keys in @p required plus any of @p optional.
[[nodiscard]] Status expect_keys(const Json& object, const std::vector<std::string_view>& required,
                                 const std::vector<std::string_view>& optional = {});
/// @}

}  // namespace twin::json
