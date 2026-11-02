#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "kernox/modules/system/unix_socket.hpp"
#include "kernox/modules/system/ipc.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace kernox::system {

// ----------------------------------------------------------------------------
// Path Validation and Helper Functions
// ----------------------------------------------------------------------------

core::Result<void> UnixSocket::validate_path(std::string_view path) {
    if (path.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "UNIX domain socket path cannot be empty");
    }

    if (path.length() > MAX_PATH_LENGTH) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "UNIX domain socket path length (" + std::to_string(path.length()) +
            ") exceeds maximum allowed length (" + std::to_string(MAX_PATH_LENGTH) + ")");
    }

    if (path.find('\0') != std::string_view::npos) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "UNIX domain socket path cannot contain embedded null characters");
    }

    return core::Result<void>::ok();
}

core::Result<sockaddr_un> UnixSocket::init_sockaddr(std::string_view path) {
    auto val = validate_path(path);
    if (val.is_err()) {
        return core::Result<sockaddr_un>::err(val.error());
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.data(), path.length());
    addr.sun_path[path.length()] = '\0';

    return core::Result<sockaddr_un>::ok(addr);
}

// ----------------------------------------------------------------------------
// Constructors & Factory Methods
// ----------------------------------------------------------------------------

UnixSocket::UnixSocket(
    core::UniqueFd fd,
    SocketType type,
    std::string bound_path,
    bool is_bound_owner) noexcept
    : fd_(std::move(fd)),
      type_(type),
      bound_path_(std::move(bound_path)),
      is_bound_owner_(is_bound_owner),
      owner_pid_(is_bound_owner ? ::getpid() : 0) {}

UnixSocket::~UnixSocket() noexcept {
    close();
}

UnixSocket::UnixSocket(UnixSocket&& other) noexcept
    : fd_(std::move(other.fd_)),
      type_(other.type_),
      bound_path_(std::move(other.bound_path_)),
      is_bound_owner_(other.is_bound_owner_),
      owner_pid_(other.owner_pid_) {
    other.is_bound_owner_ = false;
    other.bound_path_.clear();
    other.owner_pid_ = 0;
}

UnixSocket& UnixSocket::operator=(UnixSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = std::move(other.fd_);
        type_ = other.type_;
        bound_path_ = std::move(other.bound_path_);
        is_bound_owner_ = other.is_bound_owner_;
        owner_pid_ = other.owner_pid_;

        other.is_bound_owner_ = false;
        other.bound_path_.clear();
        other.owner_pid_ = 0;
    }
    return *this;
}

core::Result<UnixSocket> UnixSocket::create_stream() {
    int s = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) {
        return core::Result<UnixSocket>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "socket(AF_UNIX, SOCK_STREAM) failed",
                errno));
    }
    return core::Result<UnixSocket>::ok(
        UnixSocket(core::UniqueFd(s), SocketType::Stream));
}

core::Result<UnixSocket> UnixSocket::create_datagram() {
    int s = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (s < 0) {
        return core::Result<UnixSocket>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "socket(AF_UNIX, SOCK_DGRAM) failed",
                errno));
    }
    return core::Result<UnixSocket>::ok(
        UnixSocket(core::UniqueFd(s), SocketType::Datagram));
}

core::Result<std::pair<UnixSocket, UnixSocket>> UnixSocket::create_pair(SocketType type) {
    int fds[2]{-1, -1};
    int raw_type = (type == SocketType::Stream ? SOCK_STREAM : SOCK_DGRAM) | SOCK_CLOEXEC;
    if (::socketpair(AF_UNIX, raw_type, 0, fds) != 0) {
        return core::Result<std::pair<UnixSocket, UnixSocket>>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "socketpair(AF_UNIX) failed",
                errno));
    }

    return core::Result<std::pair<UnixSocket, UnixSocket>>::ok(
        std::make_pair(
            UnixSocket(core::UniqueFd(fds[0]), type),
            UnixSocket(core::UniqueFd(fds[1]), type)));
}

core::Result<UnixSocket> UnixSocket::bind_and_listen(const std::string& path, int backlog) {
    auto sock_res = create_stream();
    if (sock_res.is_err()) {
        return sock_res;
    }

    UnixSocket sock = std::move(sock_res.value());
    auto b_res = sock.bind(path);
    if (b_res.is_err()) {
        return core::Result<UnixSocket>::err(b_res.error());
    }

    auto l_res = sock.listen(backlog);
    if (l_res.is_err()) {
        return core::Result<UnixSocket>::err(l_res.error());
    }

    return core::Result<UnixSocket>::ok(std::move(sock));
}

core::Result<UnixSocket> UnixSocket::connect(
    const std::string& path,
    std::optional<std::chrono::milliseconds> timeout) {
    auto sock_res = create_stream();
    if (sock_res.is_err()) {
        return sock_res;
    }

    UnixSocket sock = std::move(sock_res.value());
    auto c_res = sock.connect_to(path, timeout);
    if (c_res.is_err()) {
        return core::Result<UnixSocket>::err(c_res.error());
    }

    return core::Result<UnixSocket>::ok(std::move(sock));
}

core::Result<UnixSocket> UnixSocket::bind_datagram(const std::string& path) {
    auto sock_res = create_datagram();
    if (sock_res.is_err()) {
        return sock_res;
    }

    UnixSocket sock = std::move(sock_res.value());
    auto b_res = sock.bind(path);
    if (b_res.is_err()) {
        return core::Result<UnixSocket>::err(b_res.error());
    }

    return core::Result<UnixSocket>::ok(std::move(sock));
}

// ----------------------------------------------------------------------------
// Socket Operations
// ----------------------------------------------------------------------------

core::Result<void> UnixSocket::bind(const std::string& path) {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot bind: socket is invalid or uninitialized");
    }

    auto addr_res = init_sockaddr(path);
    if (addr_res.is_err()) {
        return core::Result<void>::err(addr_res.error());
    }

    sockaddr_un addr = addr_res.value();
    if (::bind(fd_.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        int err = errno;
        if (err == EADDRINUSE) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidState,
                "Socket path already in use: " + path);
        }
        if (err == EACCES) {
            return core::Result<void>::err(
                core::ErrorCode::PermissionDenied,
                "Permission denied binding socket path: " + path);
        }
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "bind() failed for UNIX socket: " + path,
                err));
    }

    bound_path_ = path;
    is_bound_owner_ = true;
    owner_pid_ = ::getpid();
    return core::Result<void>::ok();
}

core::Result<void> UnixSocket::listen(int backlog) {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot listen: socket is invalid or uninitialized");
    }
    if (type_ != SocketType::Stream) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot listen on datagram socket (only supported on SOCK_STREAM)");
    }

    if (::listen(fd_.get(), backlog) != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "listen() failed",
                errno));
    }

    return core::Result<void>::ok();
}

core::Result<UnixSocket> UnixSocket::accept(std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<UnixSocket>::err(
            core::ErrorCode::InvalidState,
            "Cannot accept: socket is invalid or uninitialized");
    }
    if (type_ != SocketType::Stream) {
        return core::Result<UnixSocket>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot accept on datagram socket (only supported on SOCK_STREAM)");
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd_.get(), POLLIN, *timeout);
        if (ready.is_err()) {
            return core::Result<UnixSocket>::err(ready.error());
        }
        if (!ready.value()) {
            return core::Result<UnixSocket>::err(
                core::ErrorCode::Timeout,
                "Timed out waiting for incoming connection in accept()");
        }
    }

    sockaddr_un client_addr{};
    socklen_t len = sizeof(client_addr);
    int client_fd = -1;

    do {
#if defined(_GNU_SOURCE) && defined(SOCK_CLOEXEC)
        client_fd = ::accept4(fd_.get(), reinterpret_cast<sockaddr*>(&client_addr), &len, SOCK_CLOEXEC);
#else
        client_fd = ::accept(fd_.get(), reinterpret_cast<sockaddr*>(&client_addr), &len);
#endif
    } while (client_fd < 0 && errno == EINTR);

    if (client_fd < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return core::Result<UnixSocket>::err(
                core::ErrorCode::Timeout,
                "No connection ready to accept");
        }
        return core::Result<UnixSocket>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "accept() failed",
                err));
    }

#if !defined(SOCK_CLOEXEC)
    ::fcntl(client_fd, F_SETFD, FD_CLOEXEC);
#endif

    return core::Result<UnixSocket>::ok(
        UnixSocket(core::UniqueFd(client_fd), SocketType::Stream));
}

core::Result<void> UnixSocket::connect_to(
    const std::string& path,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot connect: socket is invalid or uninitialized");
    }

    auto addr_res = init_sockaddr(path);
    if (addr_res.is_err()) {
        return core::Result<void>::err(addr_res.error());
    }
    sockaddr_un addr = addr_res.value();

    if (!timeout.has_value()) {
        int rc = 0;
        do {
            rc = ::connect(fd_.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        } while (rc < 0 && errno == EINTR);

        if (rc != 0) {
            int err = errno;
            if (err == ENOENT || err == ECONNREFUSED) {
                return core::Result<void>::err(
                    core::ErrorCode::NotFound,
                    "Cannot connect to UNIX socket (server not listening or path missing): " + path);
            }
            if (err == EACCES) {
                return core::Result<void>::err(
                    core::ErrorCode::PermissionDenied,
                    "Permission denied connecting to socket: " + path);
            }
            return core::Result<void>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    "connect() failed for " + path,
                    err));
        }
        return core::Result<void>::ok();
    }

    // Non-blocking connect with timeout
    int flags = ::fcntl(fd_.get(), F_GETFL, 0);
    if (flags < 0) {
        return core::Result<void>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fcntl(F_GETFL) failed", errno));
    }
    if (::fcntl(fd_.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
        return core::Result<void>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fcntl(F_SETFL, O_NONBLOCK) failed", errno));
    }

    int rc = ::connect(fd_.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        // Immediate connection
        ::fcntl(fd_.get(), F_SETFL, flags);
        return core::Result<void>::ok();
    }

    if (errno != EINPROGRESS) {
        int err = errno;
        ::fcntl(fd_.get(), F_SETFL, flags);
        if (err == ENOENT || err == ECONNREFUSED) {
            return core::Result<void>::err(
                core::ErrorCode::NotFound,
                "Cannot connect to UNIX socket: " + path);
        }
        return core::Result<void>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "connect() failed", err));
    }

    // Wait for writability
    auto ready = poll_fd(fd_.get(), POLLOUT, *timeout);
    ::fcntl(fd_.get(), F_SETFL, flags); // Restore blocking mode

    if (ready.is_err()) {
        return core::Result<void>::err(ready.error());
    }
    if (!ready.value()) {
        return core::Result<void>::err(
            core::ErrorCode::Timeout,
            "Timed out connecting to UNIX socket: " + path);
    }

    int so_error = 0;
    socklen_t optlen = sizeof(so_error);
    if (::getsockopt(fd_.get(), SOL_SOCKET, SO_ERROR, &so_error, &optlen) != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "getsockopt(SO_ERROR) failed", errno));
    }

    if (so_error != 0) {
        if (so_error == ECONNREFUSED || so_error == ENOENT) {
            return core::Result<void>::err(
                core::ErrorCode::NotFound,
                "Connection refused or target socket missing: " + path);
        }
        return core::Result<void>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "Asynchronous connect failed", so_error));
    }

    return core::Result<void>::ok();
}

core::Result<void> UnixSocket::shutdown(ShutdownHow how) noexcept {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot shutdown: socket is invalid or uninitialized");
    }

    if (::shutdown(fd_.get(), static_cast<int>(how)) != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "shutdown() failed",
                errno));
    }

    return core::Result<void>::ok();
}

void UnixSocket::close() noexcept {
    if (fd_.is_valid()) {
        fd_.reset(-1);
    }
    if (is_bound_owner_ && owner_pid_ == ::getpid() && !bound_path_.empty()) {
        ::unlink(bound_path_.c_str());
        is_bound_owner_ = false;
        bound_path_.clear();
        owner_pid_ = 0;
    }
}

core::Result<void> UnixSocket::unlink_bound_path() noexcept {
    if (is_bound_owner_ && owner_pid_ == ::getpid() && !bound_path_.empty()) {
        int rc = ::unlink(bound_path_.c_str());
        is_bound_owner_ = false;
        bound_path_.clear();
        owner_pid_ = 0;
        if (rc != 0 && errno != ENOENT) {
            return core::Result<void>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    "unlink() failed for bound socket path",
                    errno));
        }
    }
    return core::Result<void>::ok();
}

// ----------------------------------------------------------------------------
// Data Transmission (Binary)
// ----------------------------------------------------------------------------

core::Result<size_t> UnixSocket::send_all(
    const void* data,
    size_t size,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Cannot send: socket is invalid or uninitialized");
    }
    if (!data && size > 0) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument,
            "send_all: data pointer cannot be null with non-zero size");
    }
    if (size == 0) {
        return core::Result<size_t>::ok(0);
    }

    const auto* ptr = static_cast<const uint8_t*>(data);
    size_t total_sent = 0;
    auto start = std::chrono::steady_clock::now();

    while (total_sent < size) {
        if (timeout.has_value()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed >= *timeout) {
                return core::Result<size_t>::err(
                    core::ErrorCode::Timeout,
                    "send_all timed out waiting for socket writability");
            }
            auto remaining = *timeout - elapsed;
            auto ready = poll_fd(fd_.get(), POLLOUT, remaining);
            if (ready.is_err()) return core::Result<size_t>::err(ready.error());
            if (!ready.value()) {
                return core::Result<size_t>::err(
                    core::ErrorCode::Timeout,
                    "send_all timed out waiting for socket writability");
            }
        }

        ssize_t rc = ::send(fd_.get(), ptr + total_sent, size - total_sent, MSG_NOSIGNAL);
        if (rc > 0) {
            total_sent += static_cast<size_t>(rc);
            continue;
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                return core::Result<size_t>::err(
                    core::ErrorCode::IoError,
                    "Socket connection closed by peer (EPIPE / ECONNRESET)");
            }
            return core::Result<size_t>::err(
                core::Error::from_errno(core::ErrorCode::SystemError, "send() failed", errno));
        }
    }

    return core::Result<size_t>::ok(total_sent);
}

core::Result<size_t> UnixSocket::send(
    const void* data,
    size_t size,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Cannot send: socket is invalid or uninitialized");
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd_.get(), POLLOUT, *timeout);
        if (ready.is_err()) return core::Result<size_t>::err(ready.error());
        if (!ready.value()) {
            return core::Result<size_t>::err(
                core::ErrorCode::Timeout,
                "Timed out waiting for socket writability");
        }
    }

    ssize_t rc = 0;
    do {
        rc = ::send(fd_.get(), data, size, MSG_NOSIGNAL);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        if (errno == EPIPE || errno == ECONNRESET) {
            return core::Result<size_t>::err(
                core::ErrorCode::IoError,
                "Socket connection closed by peer (EPIPE / ECONNRESET)");
        }
        return core::Result<size_t>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "send() failed", errno));
    }

    return core::Result<size_t>::ok(static_cast<size_t>(rc));
}

core::Result<size_t> UnixSocket::send_to(
    const std::string& dest_path,
    const void* data,
    size_t size,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Cannot send_to: socket is invalid or uninitialized");
    }

    auto addr_res = init_sockaddr(dest_path);
    if (addr_res.is_err()) {
        return core::Result<size_t>::err(addr_res.error());
    }
    sockaddr_un addr = addr_res.value();

    if (timeout.has_value()) {
        auto ready = poll_fd(fd_.get(), POLLOUT, *timeout);
        if (ready.is_err()) return core::Result<size_t>::err(ready.error());
        if (!ready.value()) {
            return core::Result<size_t>::err(
                core::ErrorCode::Timeout,
                "Timed out waiting for socket writability in send_to");
        }
    }

    ssize_t rc = 0;
    do {
        rc = ::sendto(
            fd_.get(),
            data,
            size,
            MSG_NOSIGNAL,
            reinterpret_cast<const sockaddr*>(&addr),
            sizeof(addr));
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        return core::Result<size_t>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "sendto() failed", errno));
    }

    return core::Result<size_t>::ok(static_cast<size_t>(rc));
}

core::Result<size_t> UnixSocket::receive(
    void* buffer,
    size_t buffer_size,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Cannot receive: socket is invalid or uninitialized");
    }
    if (!buffer && buffer_size > 0) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument,
            "receive: buffer pointer cannot be null with non-zero size");
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd_.get(), POLLIN, *timeout);
        if (ready.is_err()) return core::Result<size_t>::err(ready.error());
        if (!ready.value()) {
            return core::Result<size_t>::err(
                core::ErrorCode::Timeout,
                "Timed out waiting to receive data from socket");
        }
    }

    ssize_t rc = 0;
    do {
        rc = ::recv(fd_.get(), buffer, buffer_size, 0);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        if (errno == ECONNRESET) {
            return core::Result<size_t>::err(
                core::ErrorCode::IoError,
                "Connection reset by peer (ECONNRESET)");
        }
        return core::Result<size_t>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "recv() failed", errno));
    }

    return core::Result<size_t>::ok(static_cast<size_t>(rc));
}

core::Result<size_t> UnixSocket::receive_from(
    void* buffer,
    size_t buffer_size,
    std::string* sender_path,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Cannot receive_from: socket is invalid or uninitialized");
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd_.get(), POLLIN, *timeout);
        if (ready.is_err()) return core::Result<size_t>::err(ready.error());
        if (!ready.value()) {
            return core::Result<size_t>::err(
                core::ErrorCode::Timeout,
                "Timed out waiting to receive datagram");
        }
    }

    sockaddr_un src_addr{};
    socklen_t len = sizeof(src_addr);
    ssize_t rc = 0;
    do {
        rc = ::recvfrom(
            fd_.get(),
            buffer,
            buffer_size,
            0,
            reinterpret_cast<sockaddr*>(&src_addr),
            &len);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        return core::Result<size_t>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "recvfrom() failed", errno));
    }

    if (sender_path != nullptr) {
        if (len > sizeof(sa_family_t) && src_addr.sun_path[0] != '\0') {
            size_t path_max = len - sizeof(sa_family_t);
            size_t actual_len = strnlen(src_addr.sun_path, path_max);
            *sender_path = std::string(src_addr.sun_path, actual_len);
        } else {
            sender_path->clear();
        }
    }

    return core::Result<size_t>::ok(static_cast<size_t>(rc));
}

// ----------------------------------------------------------------------------
// String Convenience Overloads
// ----------------------------------------------------------------------------

core::Result<void> UnixSocket::send_string(
    std::string_view msg,
    std::optional<std::chrono::milliseconds> timeout) {
    auto res = (type_ == SocketType::Stream)
        ? send_all(msg.data(), msg.size(), timeout)
        : send(msg.data(), msg.size(), timeout);
    if (res.is_err()) {
        return core::Result<void>::err(res.error());
    }
    return core::Result<void>::ok();
}

core::Result<std::string> UnixSocket::receive_string(
    size_t max_bytes,
    std::optional<std::chrono::milliseconds> timeout) {
    std::string buf;
    buf.resize(max_bytes);

    auto res = receive(buf.data(), buf.size(), timeout);
    if (res.is_err()) {
        return core::Result<std::string>::err(res.error());
    }

    buf.resize(res.value());
    return core::Result<std::string>::ok(std::move(buf));
}

core::Result<void> UnixSocket::send_to_string(
    const std::string& dest_path,
    std::string_view msg,
    std::optional<std::chrono::milliseconds> timeout) {
    auto res = send_to(dest_path, msg.data(), msg.size(), timeout);
    if (res.is_err()) {
        return core::Result<void>::err(res.error());
    }
    return core::Result<void>::ok();
}

core::Result<std::pair<std::string, std::string>> UnixSocket::receive_from_string(
    size_t max_bytes,
    std::optional<std::chrono::milliseconds> timeout) {
    std::string buf;
    buf.resize(max_bytes);
    std::string sender;

    auto res = receive_from(buf.data(), buf.size(), &sender, timeout);
    if (res.is_err()) {
        return core::Result<std::pair<std::string, std::string>>::err(res.error());
    }

    buf.resize(res.value());
    return core::Result<std::pair<std::string, std::string>>::ok(
        std::make_pair(std::move(buf), std::move(sender)));
}

// ----------------------------------------------------------------------------
// File Descriptor Passing (SCM_RIGHTS)
// ----------------------------------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

core::Result<void> UnixSocket::send_fds(
    std::span<const int> fds_to_send,
    const void* data,
    size_t size,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot send_fds: socket is invalid or uninitialized");
    }
    if (fds_to_send.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "fds_to_send span cannot be empty");
    }

    for (int fd : fds_to_send) {
        if (fd < 0) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Cannot pass invalid file descriptor: " + std::to_string(fd));
        }
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd_.get(), POLLOUT, *timeout);
        if (ready.is_err()) return core::Result<void>::err(ready.error());
        if (!ready.value()) {
            return core::Result<void>::err(
                core::ErrorCode::Timeout,
                "Timed out waiting for socket writability in send_fds");
        }
    }

    struct msghdr msg{};
    struct iovec iov[1];
    char dummy = 0;

    iov[0].iov_base = (data && size > 0) ? const_cast<void*>(data) : &dummy;
    iov[0].iov_len = (data && size > 0) ? size : 1;

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    size_t fds_bytes = fds_to_send.size() * sizeof(int);
    size_t cmsg_space = CMSG_SPACE(fds_bytes);
    std::vector<char> cmsg_buf(cmsg_space, 0);

    msg.msg_control = cmsg_buf.data();
    msg.msg_controllen = static_cast<socklen_t>(cmsg_space);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg) {
        return core::Result<void>::err(
            core::ErrorCode::SystemError,
            "Failed to allocate CMSG header for descriptor transfer");
    }

    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = static_cast<socklen_t>(CMSG_LEN(fds_bytes));

    std::memcpy(CMSG_DATA(cmsg), fds_to_send.data(), fds_bytes);

    ssize_t rc = 0;
    do {
        rc = ::sendmsg(fd_.get(), &msg, MSG_NOSIGNAL);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        return core::Result<void>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "sendmsg() failed", errno));
    }

    return core::Result<void>::ok();
}

core::Result<void> UnixSocket::send_fd(
    int fd_to_send,
    const void* data,
    size_t size,
    std::optional<std::chrono::milliseconds> timeout) {
    const int fds[1] = {fd_to_send};
    return send_fds(std::span<const int>(fds, 1), data, size, timeout);
}

core::Result<std::vector<core::UniqueFd>> UnixSocket::receive_fds(
    size_t max_fds,
    void* buffer,
    size_t buffer_size,
    size_t* bytes_received,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<std::vector<core::UniqueFd>>::err(
            core::ErrorCode::InvalidState,
            "Cannot receive_fds: socket is invalid or uninitialized");
    }
    if (max_fds == 0) {
        return core::Result<std::vector<core::UniqueFd>>::err(
            core::ErrorCode::InvalidArgument,
            "max_fds must be greater than zero");
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd_.get(), POLLIN, *timeout);
        if (ready.is_err()) return core::Result<std::vector<core::UniqueFd>>::err(ready.error());
        if (!ready.value()) {
            return core::Result<std::vector<core::UniqueFd>>::err(
                core::ErrorCode::Timeout,
                "Timed out waiting to receive descriptors from socket");
        }
    }

    struct msghdr msg{};
    struct iovec iov[1];
    char dummy = 0;

    iov[0].iov_base = (buffer && buffer_size > 0) ? buffer : &dummy;
    iov[0].iov_len = (buffer && buffer_size > 0) ? buffer_size : 1;

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    size_t cmsg_space = CMSG_SPACE(max_fds * sizeof(int));
    std::vector<char> cmsg_buf(cmsg_space, 0);

    msg.msg_control = cmsg_buf.data();
    msg.msg_controllen = static_cast<socklen_t>(cmsg_space);

    int flags = 0;
#if defined(MSG_CMSG_CLOEXEC)
    flags |= MSG_CMSG_CLOEXEC;
#endif

    ssize_t rc = 0;
    do {
        rc = ::recvmsg(fd_.get(), &msg, flags);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        return core::Result<std::vector<core::UniqueFd>>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "recvmsg() failed", errno));
    }

    if (rc == 0) {
        return core::Result<std::vector<core::UniqueFd>>::err(
            core::ErrorCode::IoError,
            "Connection closed by peer before descriptor reception");
    }

    if (bytes_received != nullptr) {
        *bytes_received = (buffer && buffer_size > 0) ? static_cast<size_t>(rc) : 0;
    }

    if ((msg.msg_flags & MSG_CTRUNC) != 0) {
        return core::Result<std::vector<core::UniqueFd>>::err(
            core::ErrorCode::IoError,
            "Ancillary data was truncated (insufficient control buffer for file descriptors)");
    }

    std::vector<core::UniqueFd> extracted_fds;

    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            if (cmsg->cmsg_len < CMSG_LEN(sizeof(int))) {
                continue;
            }
            size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
            size_t count = data_len / sizeof(int);

            const auto* raw_fds = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
            for (size_t i = 0; i < count; ++i) {
                int extracted = raw_fds[i];
                if (extracted >= 0) {
                    ::fcntl(extracted, F_SETFD, FD_CLOEXEC);
                    extracted_fds.emplace_back(extracted);
                }
            }
        }
    }

    return core::Result<std::vector<core::UniqueFd>>::ok(std::move(extracted_fds));
}

core::Result<core::UniqueFd> UnixSocket::receive_fd(
    void* buffer,
    size_t buffer_size,
    size_t* bytes_received,
    std::optional<std::chrono::milliseconds> timeout) {
    auto fds_res = receive_fds(1, buffer, buffer_size, bytes_received, timeout);
    if (fds_res.is_err()) {
        return core::Result<core::UniqueFd>::err(fds_res.error());
    }

    auto& fds = fds_res.value();
    if (fds.empty()) {
        return core::Result<core::UniqueFd>::err(
            core::ErrorCode::IoError,
            "No file descriptor was received in socket ancillary data");
    }

    core::UniqueFd result = std::move(fds.front());
    return core::Result<core::UniqueFd>::ok(std::move(result));
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

// ----------------------------------------------------------------------------
// Peer Credentials & Metadata
// ----------------------------------------------------------------------------

core::Result<PeerCredentials> UnixSocket::get_peer_credentials() const {
    if (!is_valid()) {
        return core::Result<PeerCredentials>::err(
            core::ErrorCode::InvalidState,
            "Cannot get_peer_credentials: socket is invalid or uninitialized");
    }

    struct ucred cred{};
    socklen_t len = sizeof(cred);

    if (::getsockopt(fd_.get(), SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        int err = errno;
        if (err == ENOTCONN) {
            return core::Result<PeerCredentials>::err(
                core::ErrorCode::InvalidState,
                "Socket is not connected (SO_PEERCRED requires a connected peer)");
        }
        return core::Result<PeerCredentials>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "getsockopt(SO_PEERCRED) failed",
                err));
    }

    PeerCredentials result;
    result.pid = cred.pid;
    result.uid = cred.uid;
    result.gid = cred.gid;

    return core::Result<PeerCredentials>::ok(result);
}

} // namespace kernox::system
