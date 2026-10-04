/**
 * @file object_store.hpp
 * @brief Content-addressed, immutable blob store (SHA-256).
 * @ingroup platform
 *
 * Every formal artefact text (ontology, interpretation, model) and every
 * evidence document is stored here under its SHA-256:
 * `<root>/objects/<h[0:2]>/<h>`. A blob is never rewritten: storing the same
 * bytes again is a no-op, and reading verifies the hash, so silent corruption
 * or tampering of stored content is detected (ErrorCode::IntegrityError).
 *
 * Content addressing is what makes historical reproducibility possible: a
 * version record names a hash, and that hash can only ever resolve to the
 * exact bytes that were validated, checked and packaged.
 */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "twin/core/result.hpp"

namespace twin::platform {

/// @brief See file documentation.
class ObjectStore {
public:
    /// @brief Use (and create) `<root>/objects`.
    [[nodiscard]] static Result<ObjectStore> open(const std::filesystem::path& root);

    /// @brief Store @p bytes; returns their SHA-256 (idempotent).
    [[nodiscard]] Result<std::string> put(std::string_view bytes) const;

    /// @brief Read the blob with hash @p sha256 and verify it.
    /// @return NotFound, or IntegrityError if the stored bytes no longer hash to @p sha256.
    [[nodiscard]] Result<std::string> get(std::string_view sha256) const;

    /// @brief Whether a blob exists (without verifying it).
    [[nodiscard]] bool contains(std::string_view sha256) const;

    /// @brief Directory of the store.
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    explicit ObjectStore(std::filesystem::path root) : root_(std::move(root)) {}
    [[nodiscard]] std::filesystem::path path_for(std::string_view sha256) const;
    std::filesystem::path root_;
};

}  // namespace twin::platform
