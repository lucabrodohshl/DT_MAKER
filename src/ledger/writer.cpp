/**
 * @file writer.cpp
 * @brief POSIX append-only ledger writer with per-record durability.
 */
#include "twin/ledger/writer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "twin/core/version.hpp"
#include "twin/core/wall_clock.hpp"

namespace twin::ledger {
namespace {

constexpr std::array<std::string_view, 8> kReserved = {"schema",    "session", "seq",            "kind",
                                                       "prev_hash", "package", "kernel_version", "wall_time"};

}  // namespace

Result<std::unique_ptr<LedgerWriter>> LedgerWriter::create(const std::filesystem::path& path,
                                                           Identity identity, WriterOptions options) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    // O_EXCL: a ledger is never overwritten; O_APPEND: writes only ever extend it.
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        return make_error(ErrorCode::IoError, "cannot create ledger file")
            .with("file", path.string())
            .with("errno", std::strerror(errno));
    }
    return std::unique_ptr<LedgerWriter>(new LedgerWriter(fd, path, std::move(identity), options));  // NOLINT
}

LedgerWriter::~LedgerWriter() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

Result<Receipt> LedgerWriter::append(std::string_view kind, json::Json fields) {
    if (failed_) {
        return make_error(ErrorCode::Unavailable, "ledger writer is in a failed state");
    }
    if (options_.fail_after_records && next_seq_ >= *options_.fail_after_records) {
        failed_ = true;
        return make_error(ErrorCode::Unavailable, "ledger storage unavailable (injected fault)");
    }
    if (!fields.is_object()) {
        return make_error(ErrorCode::InvalidArgument, "record fields must be a JSON object");
    }
    for (std::string_view key : kReserved) {
        if (fields.contains(key)) {
            return make_error(ErrorCode::InvalidArgument, "record fields use a reserved key")
                .with("key", std::string(key));
        }
    }
    json::Json body = std::move(fields);
    body["schema"] = std::string(version::kLedgerSchema);
    body["session"] = identity_.session;
    body["seq"] = next_seq_;
    body["kind"] = std::string(kind);
    body["prev_hash"] = head_hash_;
    body["package"] = encode_identity(identity_);
    body["kernel_version"] = std::string(version::kKernel);
    body["wall_time"] = options_.deterministic ? std::string() : now_utc_millis();

    Result<std::string> body_text = json::canonical_dump(body);
    if (!body_text) {
        return std::move(body_text).error();
    }
    Result<std::string> hash = chain_hash(head_hash_, body_text.value());
    if (!hash) {
        return std::move(hash).error();
    }
    json::Json line = json::Json{{"body", std::move(body)}, {"hash", hash.value()}};
    Result<std::string> line_text = json::canonical_dump(line);
    if (!line_text) {
        return std::move(line_text).error();
    }
    std::string bytes = std::move(line_text).value();
    bytes.push_back('\n');
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(fd_, bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            failed_ = true;
            return make_error(ErrorCode::Unavailable, "ledger write failed").with("errno", std::strerror(errno));
        }
        written += static_cast<std::size_t>(n);
    }
    if (options_.fsync_each_record && ::fsync(fd_) != 0) {
        failed_ = true;
        return make_error(ErrorCode::Unavailable, "ledger fsync failed").with("errno", std::strerror(errno));
    }
    Receipt receipt{next_seq_, hash.value(), std::move(line)};
    head_hash_ = std::move(hash).value();
    ++next_seq_;
    return receipt;
}

}  // namespace twin::ledger
