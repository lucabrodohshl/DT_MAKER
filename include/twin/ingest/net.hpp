/**
 * @file net.hpp
 * @brief Minimal blocking TCP helpers (POSIX) for the MQTT endpoint and the broker client.
 * @ingroup ingest
 *
 * Sockets never raise SIGPIPE (SO_NOSIGPIPE on macOS, MSG_NOSIGNAL elsewhere); every read takes
 * a timeout so that connection threads can notice a stop request.
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

#include "twin/core/result.hpp"

namespace twin::ingest::net {

/// @brief Owning socket descriptor (closed on destruction; movable, not copyable).
class Socket {
public:
    Socket() = default;
    /// @brief Takes ownership of @p fd.
    explicit Socket(int fd) noexcept : fd_(fd) {}
    ~Socket() { close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    /// @brief Move.
    Socket(Socket&& o) noexcept : fd_(o.release()) {}
    /// @brief Move-assign (closes the current descriptor).
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) {
            close();
            fd_ = o.release();
        }
        return *this;
    }

    /// @brief The descriptor (-1 if none).
    [[nodiscard]] int fd() const noexcept { return fd_; }
    /// @brief Whether a descriptor is held.
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    /// @brief Give up ownership.
    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    /// @brief Close (idempotent).
    void close() noexcept;

private:
    int fd_{-1};
};

/// @brief Unblock every thread waiting on @p fd (shutdown in both directions; the owner still closes it).
void shutdown(int fd) noexcept;

/// @brief Listen on @p host (IPv4 literal, e.g. "0.0.0.0" or "127.0.0.1") and @p port (0 = any free port).
[[nodiscard]] Result<Socket> listen_tcp(const std::string& host, int port, int backlog = 64);
/// @brief Port a bound socket listens on.
[[nodiscard]] int local_port(int fd) noexcept;
/// @brief Accept one connection, waiting at most @p timeout; an invalid Socket on timeout.
[[nodiscard]] Result<Socket> accept_for(int listen_fd, std::chrono::milliseconds timeout);
/// @brief "a.b.c.d:port" of the peer of @p fd ("" if unknown).
[[nodiscard]] std::string peer_address(int fd);

/// @brief Connect to @p host (name or address) and @p port, waiting at most @p timeout.
[[nodiscard]] Result<Socket> connect_tcp(const std::string& host, int port, std::chrono::milliseconds timeout);

/// @brief Outcome of read_for().
struct ReadResult {
    /// @brief What happened.
    enum class Kind { Data, Timeout, Closed } kind{Kind::Timeout};
    std::size_t bytes{0};  ///< Bytes read (Kind::Data).
};

/// @brief Read up to @p size bytes into @p buffer, waiting at most @p timeout.
[[nodiscard]] Result<ReadResult> read_for(int fd, char* buffer, std::size_t size, std::chrono::milliseconds timeout);
/// @brief Write all of @p data (blocking).
[[nodiscard]] Status write_all(int fd, std::string_view data);

}  // namespace twin::ingest::net
