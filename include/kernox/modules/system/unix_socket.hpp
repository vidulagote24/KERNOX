#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"
#include "kernox/core/unique_fd.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <utility>
#include <vector>

namespace kernox::system {

// ----------------------------------------------------------------------------
// Enums and Supporting Data Structures
// ----------------------------------------------------------------------------

enum class SocketType : int {
    Stream = SOCK_STREAM,
    Datagram = SOCK_DGRAM
};

[[nodiscard]] constexpr std::string_view to_string(SocketType type) noexcept {
    switch (type) {
        case SocketType::Stream: return "Stream";
        case SocketType::Datagram: return "Datagram";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, SocketType type) {
    return os << to_string(type);
}

enum class ShutdownHow : int {
    Read = SHUT_RD,
    Write = SHUT_WR,
    Both = SHUT_RDWR
};

/**
 * @brief Credentials of the connected peer on a UNIX domain socket (SO_PEERCRED).
 */
struct PeerCredentials {
    pid_t pid{0};  // Process ID of the peer
    uid_t uid{0};  // Effective User ID of the peer
    gid_t gid{0};  // Effective Group ID of the peer
};

// ----------------------------------------------------------------------------
// UnixSocket
// ----------------------------------------------------------------------------

/**
 * @brief Move-only RAII abstraction for Linux UNIX Domain Sockets (AF_UNIX).
 *
 * Supports:
 *  - Connection-oriented stream sockets (SOCK_STREAM) with reliable send-all.
 *  - Connectionless datagram sockets (SOCK_DGRAM) preserving message boundaries.
 *  - Server lifecycle: bind, listen, accept (with SOCK_CLOEXEC).
 *  - Client lifecycle: connect with bounded timeout support.
 *  - File descriptor passing via ancillary data (SCM_RIGHTS / MSG_CMSG_CLOEXEC).
 *  - Peer identity discovery via SO_PEERCRED.
 *  - Graceful half-duplex and full-duplex shutdown (SHUT_RD, SHUT_WR, SHUT_RDWR).
 *  - Controlled filesystem unlinking: only the instance that successfully bound
 *    a path is designated as owner and unlinks the path on destruction.
 */
class UnixSocket {
public:
    static constexpr size_t MAX_PATH_LENGTH = sizeof(sockaddr_un::sun_path) - 1;

    // --- Static Factory Methods ---

    /**
     * @brief Creates an unbound UNIX domain stream socket (SOCK_STREAM | SOCK_CLOEXEC).
     */
    static core::Result<UnixSocket> create_stream();

    /**
     * @brief Creates an unbound UNIX domain datagram socket (SOCK_DGRAM | SOCK_CLOEXEC).
     */
    static core::Result<UnixSocket> create_datagram();

    /**
     * @brief Creates a connected pair of anonymous UNIX domain sockets using socketpair(2).
     */
    static core::Result<std::pair<UnixSocket, UnixSocket>> create_pair(
        SocketType type = SocketType::Stream);

    /**
     * @brief Creates a stream socket, binds it to the specified path, and begins listening.
     * The resulting socket owns the bound path and unlinks it upon destruction.
     */
    static core::Result<UnixSocket> bind_and_listen(
        const std::string& path,
        int backlog = 128);

    /**
     * @brief Creates a stream socket and connects it to the specified server path.
     */
    static core::Result<UnixSocket> connect(
        const std::string& path,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Creates a datagram socket and binds it to the specified path.
     * The resulting socket owns the bound path and unlinks it upon destruction.
     */
    static core::Result<UnixSocket> bind_datagram(
        const std::string& path);

    // --- Lifecycle and RAII Constructors ---

    UnixSocket() noexcept = default;

    explicit UnixSocket(
        core::UniqueFd fd,
        SocketType type,
        std::string bound_path = "",
        bool is_bound_owner = false) noexcept;

    ~UnixSocket() noexcept;

    UnixSocket(UnixSocket&& other) noexcept;
    UnixSocket& operator=(UnixSocket&& other) noexcept;

    UnixSocket(const UnixSocket&) = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;

    // --- Server & Connection Management ---

    /**
     * @brief Binds the socket to a filesystem path.
     * Marks this instance as the path owner upon success.
     */
    core::Result<void> bind(const std::string& path);

    /**
     * @brief Puts a bound stream socket into passive listening mode.
     */
    core::Result<void> listen(int backlog = 128);

    /**
     * @brief Accepts an incoming client connection on a listening stream socket.
     * Atomically sets SOCK_CLOEXEC on the accepted socket.
     */
    core::Result<UnixSocket> accept(
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Connects to a listening UNIX domain stream server.
     */
    core::Result<void> connect_to(
        const std::string& path,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Shuts down part or all of the full-duplex connection.
     */
    core::Result<void> shutdown(ShutdownHow how = ShutdownHow::Both) noexcept;

    /**
     * @brief Closes the descriptor and unlinks the bound path if owned.
     */
    void close() noexcept;

    // --- Data Transmission (Binary) ---

    /**
     * @brief Sends all bytes across a stream socket, looping until all bytes are sent.
     * Stream sockets do not preserve message boundaries.
     */
    core::Result<size_t> send_all(
        const void* data,
        size_t size,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Sends up to size bytes across a stream or connected datagram socket.
     */
    core::Result<size_t> send(
        const void* data,
        size_t size,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Sends a datagram message to a specific destination path.
     * Preserves datagram message boundaries.
     */
    core::Result<size_t> send_to(
        const std::string& dest_path,
        const void* data,
        size_t size,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Receives up to buffer_size bytes from the socket.
     * Returns 0 if peer gracefully performed orderly shutdown (EOF).
     */
    core::Result<size_t> receive(
        void* buffer,
        size_t buffer_size,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Receives a datagram message and optionally records the sender's path.
     */
    core::Result<size_t> receive_from(
        void* buffer,
        size_t buffer_size,
        std::string* sender_path = nullptr,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // --- Data Transmission (String Convenience Overloads) ---

    core::Result<void> send_string(
        std::string_view msg,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    core::Result<std::string> receive_string(
        size_t max_bytes = 4096,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    core::Result<void> send_to_string(
        const std::string& dest_path,
        std::string_view msg,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    core::Result<std::pair<std::string, std::string>> receive_from_string(
        size_t max_bytes = 4096,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // --- File Descriptor Transfer (SCM_RIGHTS) ---

    /**
     * @brief Transmits a single open file descriptor across the socket using SCM_RIGHTS.
     * An optional data payload may be sent alongside the descriptor.
     */
    core::Result<void> send_fd(
        int fd_to_send,
        const void* data = nullptr,
        size_t size = 0,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Transmits multiple open file descriptors across the socket using SCM_RIGHTS.
     */
    core::Result<void> send_fds(
        std::span<const int> fds_to_send,
        const void* data = nullptr,
        size_t size = 0,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Receives a single file descriptor via SCM_RIGHTS, wrapped immediately in RAII UniqueFd.
     */
    core::Result<core::UniqueFd> receive_fd(
        void* buffer = nullptr,
        size_t buffer_size = 0,
        size_t* bytes_received = nullptr,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Receives up to max_fds file descriptors via SCM_RIGHTS into a vector of UniqueFd.
     * All received descriptors are immediately owned by UniqueFd to guarantee leak-free RAII cleanup.
     */
    core::Result<std::vector<core::UniqueFd>> receive_fds(
        size_t max_fds = 8,
        void* buffer = nullptr,
        size_t buffer_size = 0,
        size_t* bytes_received = nullptr,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // --- Peer Identity & Metadata ---

    /**
     * @brief Inspects peer process identity (PID, UID, GID) using SO_PEERCRED.
     */
    core::Result<PeerCredentials> get_peer_credentials() const;

    // --- State and Accessors ---

    [[nodiscard]] bool is_valid() const noexcept { return fd_.is_valid(); }
    [[nodiscard]] int native_handle() const noexcept { return fd_.get(); }
    [[nodiscard]] SocketType type() const noexcept { return type_; }
    [[nodiscard]] const std::string& bound_path() const noexcept { return bound_path_; }
    [[nodiscard]] bool is_bound_owner() const noexcept { return is_bound_owner_; }

    [[nodiscard]] core::UniqueFd release() noexcept { return std::move(fd_); }

    /**
     * @brief Explicitly unlinks the bound socket path if this instance is the owner.
     */
    core::Result<void> unlink_bound_path() noexcept;

    /**
     * @brief Validates a filesystem socket path against AF_UNIX constraints.
     */
    static core::Result<void> validate_path(std::string_view path);

private:
    static core::Result<sockaddr_un> init_sockaddr(std::string_view path);

    core::UniqueFd fd_{};
    SocketType type_{SocketType::Stream};
    std::string bound_path_{};
    bool is_bound_owner_{false};
    pid_t owner_pid_{0};
};

} // namespace kernox::system

namespace kernox::sys {
    using SocketType = kernox::system::SocketType;
    using ShutdownHow = kernox::system::ShutdownHow;
    using PeerCredentials = kernox::system::PeerCredentials;
    using UnixSocket = kernox::system::UnixSocket;
}
