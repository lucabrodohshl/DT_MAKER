/**
 * @file net.cpp
 * @brief Minimal blocking TCP helpers (see net.hpp).
 */
#include "twin/ingest/net.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace twin::ingest::net {

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void no_sigpipe([[maybe_unused]] int fd) {
#if defined(SO_NOSIGPIPE)
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

std::string errno_text() { return std::strerror(errno); }

/// Wait until @p fd is readable (POLLIN) or writable (POLLOUT); false on timeout.
Result<bool> wait_for(int fd, short events, std::chrono::milliseconds timeout) {
    pollfd p{};
    p.fd = fd;
    p.events = events;
    for (;;) {
        const int r = ::poll(&p, 1, static_cast<int>(timeout.count()));
        if (r > 0) return true;
        if (r == 0) return false;
        if (errno != EINTR) return make_error(ErrorCode::IoError, "poll failed: " + errno_text());
    }
}

}  // namespace

void Socket::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void shutdown(int fd) noexcept {
    if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
}

Result<Socket> listen_tcp(const std::string& host, int port, int backlog) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        return make_error(ErrorCode::InvalidArgument, "listen address must be an IPv4 address such as 0.0.0.0").with("host", host);
    }
    Socket s(::socket(AF_INET, SOCK_STREAM, 0));
    if (!s.valid()) return make_error(ErrorCode::IoError, "cannot create a socket: " + errno_text());
    int one = 1;
    ::setsockopt(s.fd(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    no_sigpipe(s.fd());
    if (::bind(s.fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return make_error(ErrorCode::Unavailable, "cannot listen on " + host + ":" + std::to_string(port) + ": " + errno_text())
            .with("hint", errno == EADDRINUSE ? "another program uses this port; choose another port" : "");
    }
    if (::listen(s.fd(), backlog) != 0) return make_error(ErrorCode::IoError, "listen failed: " + errno_text());
    return s;
}

int local_port(int fd) noexcept {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

Result<Socket> accept_for(int listen_fd, std::chrono::milliseconds timeout) {
    auto ready = wait_for(listen_fd, POLLIN, timeout);
    if (!ready) return std::move(ready).error();
    if (!ready.value()) return Socket{};
    const int fd = ::accept(listen_fd, nullptr, nullptr);
    if (fd < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == ECONNABORTED) return Socket{};
        return make_error(ErrorCode::IoError, "accept failed: " + errno_text());
    }
    Socket s(fd);
    no_sigpipe(fd);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return s;
}

std::string peer_address(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0 || addr.sin_family != AF_INET) return {};
    char text[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text)) == nullptr) return {};
    return std::string(text) + ":" + std::to_string(ntohs(addr.sin_port));
}

Result<Socket> connect_tcp(const std::string& host, int port, std::chrono::milliseconds timeout) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (const int rc = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &found); rc != 0 || found == nullptr) {
        return make_error(ErrorCode::Unavailable, "cannot resolve '" + host + "': " + ::gai_strerror(rc));
    }
    std::string last = "no address";
    for (addrinfo* a = found; a != nullptr; a = a->ai_next) {
        Socket s(::socket(a->ai_family, a->ai_socktype, a->ai_protocol));
        if (!s.valid()) continue;
        no_sigpipe(s.fd());
        const int flags = ::fcntl(s.fd(), F_GETFL, 0);
        ::fcntl(s.fd(), F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(s.fd(), a->ai_addr, a->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            auto ready = wait_for(s.fd(), POLLOUT, timeout);
            if (ready && ready.value()) {
                int err = 0;
                socklen_t len = sizeof(err);
                ::getsockopt(s.fd(), SOL_SOCKET, SO_ERROR, &err, &len);
                rc = err == 0 ? 0 : -1;
                errno = err;
            } else {
                rc = -1;
                errno = ETIMEDOUT;
            }
        }
        if (rc == 0) {
            ::fcntl(s.fd(), F_SETFL, flags);
            int one = 1;
            ::setsockopt(s.fd(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            ::freeaddrinfo(found);
            return s;
        }
        last = errno_text();
    }
    ::freeaddrinfo(found);
    return make_error(ErrorCode::Unavailable, "cannot connect to " + host + ":" + service + ": " + last);
}

Result<ReadResult> read_for(int fd, char* buffer, std::size_t size, std::chrono::milliseconds timeout) {
    auto ready = wait_for(fd, POLLIN, timeout);
    if (!ready) return std::move(ready).error();
    if (!ready.value()) return ReadResult{ReadResult::Kind::Timeout, 0};
    for (;;) {
        const ssize_t n = ::recv(fd, buffer, size, 0);
        if (n > 0) return ReadResult{ReadResult::Kind::Data, static_cast<std::size_t>(n)};
        if (n == 0) return ReadResult{ReadResult::Kind::Closed, 0};
        if (errno == EINTR) continue;
        if (errno == ECONNRESET || errno == ENOTCONN || errno == EBADF) return ReadResult{ReadResult::Kind::Closed, 0};
        return make_error(ErrorCode::IoError, "read failed: " + errno_text());
    }
}

Status write_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const ssize_t n = ::send(fd, data.data(), data.size(), kSendFlags);
        if (n < 0) {
            if (errno == EINTR) continue;
            return make_error(ErrorCode::IoError, "write failed: " + errno_text());
        }
        data.remove_prefix(static_cast<std::size_t>(n));
    }
    return ok_status();
}

}  // namespace twin::ingest::net
