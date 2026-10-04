/**
 * @file object_store.cpp
 * @brief Content-addressed blob store (see object_store.hpp).
 */
#include "twin/platform/object_store.hpp"

#include <fstream>
#include <sstream>

#include "twin/core/sha256.hpp"

namespace twin::platform {

namespace fs = std::filesystem;

Result<ObjectStore> ObjectStore::open(const fs::path& root) {
    std::error_code ec;
    fs::create_directories(root / "objects", ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot create object store: " + ec.message());
    return ObjectStore(root / "objects");
}

fs::path ObjectStore::path_for(std::string_view sha256) const {
    return root_ / std::string(sha256.substr(0, 2)) / std::string(sha256);
}

bool ObjectStore::contains(std::string_view sha256) const {
    if (!is_canonical_hex_digest(sha256)) return false;
    std::error_code ec;
    return fs::exists(path_for(sha256), ec);
}

Result<std::string> ObjectStore::put(std::string_view bytes) const {
    std::string hash = sha256_hex(bytes);
    const fs::path target = path_for(hash);
    std::error_code ec;
    if (fs::exists(target, ec)) return hash;  // immutable: identical bytes already stored
    fs::create_directories(target.parent_path(), ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot create object directory: " + ec.message());
    // Write to a temporary name and rename, so a crash never leaves a partial blob under its hash.
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) return make_error(ErrorCode::IoError, "cannot write object " + hash);
    }
    fs::rename(tmp, target, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot store object " + hash + ": " + ec.message());
    return hash;
}

Result<std::string> ObjectStore::get(std::string_view sha256) const {
    if (!is_canonical_hex_digest(sha256)) {
        return make_error(ErrorCode::InvalidArgument, "not a SHA-256 digest").with("hash", std::string(sha256));
    }
    std::ifstream in(path_for(sha256), std::ios::binary);
    if (!in) return make_error(ErrorCode::NotFound, "object not found").with("hash", std::string(sha256));
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string bytes = ss.str();
    if (sha256_hex(bytes) != sha256) {
        return make_error(ErrorCode::IntegrityError, "stored object does not match its content hash")
            .with("hash", std::string(sha256));
    }
    return bytes;
}

}  // namespace twin::platform
