# KERNOX — UNIX Domain Socket Subsystem (`AF_UNIX`)

## 1. Overview & Architectural Principles

The KERNOX UNIX Domain Socket subsystem provides high-performance, robust, and type-safe local Inter-Process Communication (IPC) for Linux systems. Operating under the `AF_UNIX` (or `AF_LOCAL`) address family, UNIX domain sockets bypass network protocol stacks, checksum calculations, and routing logic, delivering memory-speed bidirectional communication between processes on the same host.

The subsystem is implemented in C++20 and adheres strictly to core KERNOX architectural standards:
- **Strict Move-Only RAII Semantics**: Socket descriptors are encapsulated in `core::UniqueFd`. Copying is deleted, and resource leaks are impossible.
- **Explicit Lifecycle Ownership**: Creation and destruction of filesystem paths is bound to the owning process (`owner_pid_`). Child processes that inherit or access socket objects cannot accidentally unlink the active listening socket file.
- **Deterministic Error Handling**: All operations return `core::Result<T>` with rich `core::Error` contexts, preserving `errno` values without throwing C++ exceptions.
- **Ancillary Control Message Safety**: Support for kernel-level descriptor passing (`SCM_RIGHTS`) and peer identity inspection (`SO_PEERCRED`) with automated `FD_CLOEXEC` application.

---

## 2. Supported Socket Types & Semantics

| Socket Type | POSIX Constant | Semantics & Behaviors |
| :--- | :--- | :--- |
| **Stream** | `SOCK_STREAM` | Reliable, bidirectional, ordered byte stream. Connection-oriented. Does not preserve message boundaries; requires structured application protocols or delimiter framing. |
| **Datagram** | `SOCK_DGRAM` | Connectionless, unordered message transfers preserving explicit record/message boundaries. Zero network loss on local loopback; supports bidirectional sender attribution via `receive_from()`. |

---

## 3. Class Design & API Summary

Located in `include/kernox/modules/system/unix_socket.hpp` and `src/modules/system/unix_socket.cpp`:

```cpp
namespace kernox::modules::system {

enum class SocketType {
    Stream,
    Datagram
};

enum class ShutdownHow {
    Read,
    Write,
    Both
};

struct PeerCredentials {
    pid_t pid{0};
    uid_t uid{0};
    gid_t gid{0};
};

class UnixSocket {
public:
    // Factory constructors
    static core::Result<UnixSocket> create(SocketType type);
    static core::Result<UnixSocket> bind_and_listen(const std::string& path, int backlog = 128);
    static core::Result<UnixSocket> connect(const std::string& path, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    static core::Result<std::pair<UnixSocket, UnixSocket>> create_pair(SocketType type);

    // Lifecycle & Movement
    UnixSocket() noexcept = default;
    ~UnixSocket();
    UnixSocket(UnixSocket&& other) noexcept;
    UnixSocket& operator=(UnixSocket&& other) noexcept;

    UnixSocket(const UnixSocket&) = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;

    // Stream Client / Server Operations
    core::Result<void> bind(const std::string& path);
    core::Result<void> listen(int backlog = 128);
    core::Result<UnixSocket> accept(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<void> connect_to(const std::string& path, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<void> shutdown(ShutdownHow how);
    void close();

    // Data Transfer (Stream & Datagram)
    core::Result<size_t> send(const void* data, size_t size, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<size_t> receive(void* buffer, size_t buffer_size, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<void> send_all(const void* data, size_t size, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<void> receive_exact(void* buffer, size_t size, std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Datagram Operations
    core::Result<size_t> send_to(const std::string& dest_path, const void* data, size_t size, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<size_t> receive_from(void* buffer, size_t buffer_size, std::string* sender_path, std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Ancillary Data & File Descriptor Passing (SCM_RIGHTS)
    core::Result<void> send_fds(std::span<const int> fds, const void* data = nullptr, size_t size = 0, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<void> send_fd(int fd, const void* data = nullptr, size_t size = 0, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<std::vector<core::UniqueFd>> receive_fds(size_t max_fds, void* buffer = nullptr, size_t buffer_size = 0, size_t* bytes_received = nullptr, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<core::UniqueFd> receive_fd(void* buffer = nullptr, size_t buffer_size = 0, size_t* bytes_received = nullptr, std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Peer Credentials & Metadata
    core::Result<PeerCredentials> get_peer_credentials() const;
    int native_handle() const noexcept;
    bool is_valid() const noexcept;
    SocketType type() const noexcept;
    const std::string& bound_path() const noexcept;
};

} // namespace kernox::modules::system
```

---

## 4. Advanced POSIX Features

### 4.1. Peer Credential Inspection (`SO_PEERCRED`)
On Linux, stream sockets support querying the identity of the connected peer at the kernel layer using `getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &len)`.
- `PeerCredentials::pid`: Process ID of the peer process.
- `PeerCredentials::uid`: Effective User ID of the peer process.
- `PeerCredentials::gid`: Effective Group ID of the peer process.

This feature enables robust zero-trust security checks and authentication before exchanging privileged operational commands or transferring file descriptors.

### 4.2. File Descriptor Passing (`SCM_RIGHTS`)
Local sockets allow processes to transfer open file descriptors (pipes, files, other sockets, eventfds) directly through the Linux kernel using `sendmsg()` and `recvmsg()` with control messages (`struct cmsghdr`):
1. **Transmission**: The sender writes the file descriptors into the `CMSG_DATA` buffer with level `SOL_SOCKET` and type `SCM_RIGHTS`.
2. **Kernel Duplication**: The kernel creates duplicate entries in the recipient's file descriptor table pointing to the same underlying `struct file` description.
3. **Atomic Cloexec (`MSG_CMSG_CLOEXEC`)**: In KERNOX, all received file descriptors are requested with `MSG_CMSG_CLOEXEC` (and defensive `fcntl(F_SETFD, FD_CLOEXEC)`) to prevent accidental descriptor leakage across subsequent `execve()` calls.
4. **Immediate RAII Adoption**: Descriptors are instantly wrapped into `core::UniqueFd`.

### 4.3. Ownership-Bound Path Lifecycle
Unlike anonymous pipes or shared memory objects with POSIX unlink semantics, UNIX domain socket files persist in the Linux VFS until explicitly unlinked:
- When a server calls `bind()`, KERNOX sets `owner_pid_ = ::getpid()` and `is_bound_owner_ = true`.
- Upon `close()` or destruction, `::unlink()` is strictly executed **only if** `::getpid() == owner_pid_`.
- When child processes fork, their inherited `UnixSocket` instances share memory flags, but their `::getpid()` differs from `owner_pid_`, ensuring child teardown never disrupts the active server socket path.

---

## 5. CLI Demonstration

KERNOX provides an automated cross-process demonstration showcasing all key features:

```bash
./build/release/bin/kernox ipc unix-demo
```

### Execution Output
```
================================================================================
KERNOX IPC DEMONSTRATION: UNIX Domain Sockets (AF_UNIX)
Server Path: /tmp/kernox_unix_demo_374.sock
Capabilities: Full-Duplex Stream, SO_PEERCRED, SCM_RIGHTS FD Passing
================================================================================

[Step 1] Initializing UNIX domain socket server on /tmp/kernox_unix_demo_374.sock...
  -> Server listening successfully (FD: 3)

[Step 2] Forking worker client process to connect to server...

[Step 3] Client connection accepted. Inspecting peer credentials via SO_PEERCRED...
  Peer Process ID (PID)  : 375 (Verified match with child worker)
  Peer User ID (UID)     : 1001
  Peer Group ID (GID)    : 1001

[Step 4] Exchanging stream messages...
  <- Received from client : "CLIENT_HELLO: KERNOX UNIX IPC"
  -> Sent to client       : "SERVER_ACK: CONNECTION ACCEPTED"

[Step 5] Demonstrating file descriptor transfer via SCM_RIGHTS...
  -> Sent pipe read descriptor (4) to client via ancillary data
  <- Client confirmed     : "FD_VERIFIED_SUCCESS" (Read secret data successfully)

[Step 6] Cleaning up and unlinking socket path...
  -> Socket file unlinked : [OK] Verified removed from filesystem

================================================================================
DEMONSTRATION SUMMARY:
  Socket Protocol          : AF_UNIX (SOCK_STREAM)
  Bidirectional Messaging  : SUCCESS (Messages exchanged without loss)
  Peer Identity (PID/UID)  : VERIFIED via SO_PEERCRED
  Descriptor Transfer      : SUCCESS via SCM_RIGHTS (Zero copy FD passing)
  Filesystem Cleanup       : SUCCESS (Socket unlinked automatically by owner)
================================================================================
```

---

## 6. Verification and Test Suite

Automated verification is located in `tests/test_unix_socket.cpp`, covering 10 distinct integration and stress test suites (92 assertions total):

1. **Test 1**: Stream Socket Client-Server Echo Communication.
2. **Test 2**: Datagram Communication and Message Boundary Preservation.
3. **Test 3**: Multiple Stream Messages & 100% Byte Integrity (100 sequential chunks).
4. **Test 4**: Peer Credential Inspection (`SO_PEERCRED` validation of PID/UID/GID).
5. **Test 5**: File Descriptor Transfer (`SCM_RIGHTS` anonymous pipe passing).
6. **Test 6**: Invalid Socket Paths & Defensive Validation (empty, oversized, nulls).
7. **Test 7**: Connection Refusal and Missing Paths.
8. **Test 8**: Socket Cleanup and Path Ownership Semantics (`owner_pid_` protection).
9. **Test 9**: Non-blocking and Bounded Timeout Behavior (`accept()` and `receive()` timeouts).
10. **Test 10**: Move Semantics, Ownership Transfer, and `socketpair()`.

All test suites execute cleanly with zero errors and zero leaks under `dev-debug`, `dev-release`, and `dev-asan` (AddressSanitizer + UndefinedBehaviorSanitizer).
