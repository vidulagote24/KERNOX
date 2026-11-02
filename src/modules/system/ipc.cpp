#include "kernox/modules/system/ipc.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <iostream>
#include <unistd.h>

namespace kernox::system {

namespace {

// Helper to ignore SIGPIPE so write() returns -1 with errno=EPIPE instead of killing the process.
void ensure_sigpipe_ignored() noexcept {
    static const bool initialized = []() noexcept {
        struct sigaction sa{};
        sa.sa_handler = SIG_IGN;
        ::sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        ::sigaction(SIGPIPE, &sa, nullptr);
        return true;
    }();
    (void)initialized;
}

// Reaps a child process safely within a bounded timeout to prevent zombie processes.
void safe_reap_child(pid_t pid, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) noexcept {
    if (pid <= 0) {
        return;
    }

    auto start = std::chrono::steady_clock::now();
    int status = 0;

    while (std::chrono::steady_clock::now() - start < timeout) {
        pid_t res = 0;
        do {
            res = ::waitpid(pid, &status, WNOHANG);
        } while (res < 0 && errno == EINTR);

        if (res == pid || (res < 0 && errno == ECHILD)) {
            return; // Successfully reaped or already reaped
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Timeout expired; escalate to SIGTERM
    ::kill(pid, SIGTERM);
    auto term_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - term_start < std::chrono::milliseconds(200)) {
        pid_t res = 0;
        do {
            res = ::waitpid(pid, &status, WNOHANG);
        } while (res < 0 && errno == EINTR);

        if (res == pid || (res < 0 && errno == ECHILD)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Still running; escalate to SIGKILL
    ::kill(pid, SIGKILL);
    do {
        pid_t res = ::waitpid(pid, &status, 0);
        if (res == pid || (res < 0 && errno == ECHILD)) {
            break;
        }
    } while (errno == EINTR);
}

} // namespace

core::Result<size_t> write_all(int fd, const void* data, size_t count) {
    if (fd < 0) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument, "Invalid file descriptor for write_all");
    }
    if (count == 0) {
        return core::Result<size_t>::ok(0);
    }
    if (data == nullptr) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument, "Null data pointer for write_all");
    }

    ensure_sigpipe_ignored();

    const auto* buf = static_cast<const uint8_t*>(data);
    size_t total_written = 0;

    while (total_written < count) {
        ssize_t written = ::write(fd, buf + total_written, count - total_written);
        if (written > 0) {
            total_written += static_cast<size_t>(written);
        } else if (written == 0) {
            // Write returned 0; retry or break if no progress can be made
            break;
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EPIPE) {
                return core::Result<size_t>::err(
                    core::Error::from_errno(
                        core::ErrorCode::IoError,
                        "write_all failed: broken pipe (EPIPE)",
                        EPIPE));
            }
            return core::Result<size_t>::err(
                core::Error::from_errno(
                    core::ErrorCode::IoError,
                    "write_all failed",
                    errno));
        }
    }

    if (total_written < count) {
        return core::Result<size_t>::err(
            core::ErrorCode::IoError,
            "write_all could not write requested byte count");
    }

    return core::Result<size_t>::ok(total_written);
}

core::Result<void> read_exact(int fd, void* buffer, size_t count) {
    if (fd < 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Invalid file descriptor for read_exact");
    }
    if (count == 0) {
        return core::Result<void>::ok();
    }
    if (buffer == nullptr) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Null buffer pointer for read_exact");
    }

    auto* buf = static_cast<uint8_t*>(buffer);
    size_t total_read = 0;

    while (total_read < count) {
        ssize_t bytes_read = ::read(fd, buf + total_read, count - total_read);
        if (bytes_read > 0) {
            total_read += static_cast<size_t>(bytes_read);
        } else if (bytes_read == 0) {
            // EOF encountered prematurely
            return core::Result<void>::err(
                core::ErrorCode::IoError,
                "read_exact failed: unexpected EOF on IPC channel");
        } else {
            if (errno == EINTR) {
                continue;
            }
            return core::Result<void>::err(
                core::Error::from_errno(
                    core::ErrorCode::IoError,
                    "read_exact failed",
                    errno));
        }
    }

    return core::Result<void>::ok();
}

core::Result<size_t> read_up_to(int fd, void* buffer, size_t max_count) {
    if (fd < 0) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument, "Invalid file descriptor for read_up_to");
    }
    if (max_count == 0) {
        return core::Result<size_t>::ok(0);
    }
    if (buffer == nullptr) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument, "Null buffer pointer for read_up_to");
    }

    while (true) {
        ssize_t bytes_read = ::read(fd, buffer, max_count);
        if (bytes_read >= 0) {
            return core::Result<size_t>::ok(static_cast<size_t>(bytes_read));
        }
        if (errno == EINTR) {
            continue;
        }
        return core::Result<size_t>::err(
            core::Error::from_errno(
                core::ErrorCode::IoError,
                "read_up_to failed",
                errno));
    }
}

core::Result<bool> poll_fd(int fd, short events, std::chrono::milliseconds timeout) {
    if (fd < 0) {
        return core::Result<bool>::err(
            core::ErrorCode::InvalidArgument, "Invalid file descriptor for poll_fd");
    }

    auto start = std::chrono::steady_clock::now();
    int timeout_ms = static_cast<int>(std::max<long long>(0, timeout.count()));

    while (true) {
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = events;

        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret > 0) {
            if ((pfd.revents & POLLNVAL) != 0) {
                return core::Result<bool>::err(
                    core::ErrorCode::InvalidArgument,
                    "poll_fd: file descriptor is not open or invalid (POLLNVAL)");
            }
            if ((pfd.revents & (events | POLLHUP | POLLERR)) != 0) {
                return core::Result<bool>::ok(true);
            }
            return core::Result<bool>::ok(false);
        }
        if (ret == 0) {
            return core::Result<bool>::ok(false); // Timed out
        }
        if (errno == EINTR) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed >= timeout) {
                return core::Result<bool>::ok(false);
            }
            timeout_ms = static_cast<int>(std::max<long long>(0, (timeout - elapsed).count()));
            continue;
        }
        return core::Result<bool>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "poll failed",
                errno));
    }
}

core::Result<void> send_message(
    int fd,
    const std::vector<uint8_t>& message,
    std::optional<std::chrono::milliseconds> timeout) {
    if (fd < 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Invalid file descriptor for send_message");
    }

    if (message.size() > MAX_FRAME_PAYLOAD_SIZE) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "send_message: payload size (" + std::to_string(message.size()) +
            " bytes) exceeds maximum allowable frame size (" +
            std::to_string(MAX_FRAME_PAYLOAD_SIZE) + " bytes)");
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd, POLLOUT, *timeout);
        if (ready.is_err()) {
            return core::Result<void>::err(ready.error());
        }
        if (!ready.value()) {
            return core::Result<void>::err(
                core::ErrorCode::Timeout, "Timeout waiting to send message across IPC channel");
        }
    }

    uint32_t len = htonl(static_cast<uint32_t>(message.size()));
    auto hdr_res = write_all(fd, &len, sizeof(len));
    if (hdr_res.is_err()) {
        return core::Result<void>::err(hdr_res.error());
    }

    if (!message.empty()) {
        auto body_res = write_all(fd, message.data(), message.size());
        if (body_res.is_err()) {
            return core::Result<void>::err(body_res.error());
        }
    }

    return core::Result<void>::ok();
}

core::Result<std::vector<uint8_t>> receive_message(
    int fd,
    std::optional<std::chrono::milliseconds> timeout) {
    if (fd < 0) {
        return core::Result<std::vector<uint8_t>>::err(
            core::ErrorCode::InvalidArgument, "Invalid file descriptor for receive_message");
    }

    if (timeout.has_value()) {
        auto ready = poll_fd(fd, POLLIN, *timeout);
        if (ready.is_err()) {
            return core::Result<std::vector<uint8_t>>::err(ready.error());
        }
        if (!ready.value()) {
            return core::Result<std::vector<uint8_t>>::err(
                core::ErrorCode::Timeout, "Timeout waiting to receive message from IPC channel");
        }
    }

    uint32_t net_len = 0;
    auto hdr_res = read_exact(fd, &net_len, sizeof(net_len));
    if (hdr_res.is_err()) {
        return core::Result<std::vector<uint8_t>>::err(hdr_res.error());
    }

    uint32_t len = ntohl(net_len);
    if (len > MAX_FRAME_PAYLOAD_SIZE) {
        return core::Result<std::vector<uint8_t>>::err(
            core::ErrorCode::InvalidArgument,
            "receive_message: incoming frame length (" + std::to_string(len) +
            " bytes) exceeds maximum allowable frame size (" +
            std::to_string(MAX_FRAME_PAYLOAD_SIZE) + " bytes)");
    }

    std::vector<uint8_t> buffer;
    try {
        buffer.resize(len);
    } catch (const std::bad_alloc&) {
        return core::Result<std::vector<uint8_t>>::err(
            core::ErrorCode::ResourceExhausted,
            "receive_message: memory allocation failed for frame buffer");
    }

    if (len > 0) {
        auto body_res = read_exact(fd, buffer.data(), len);
        if (body_res.is_err()) {
            return core::Result<std::vector<uint8_t>>::err(body_res.error());
        }
    }

    return core::Result<std::vector<uint8_t>>::ok(std::move(buffer));
}

// ----------------------------------------------------------------------------
// AnonymousPipe implementation
// ----------------------------------------------------------------------------

core::Result<AnonymousPipe> AnonymousPipe::create(bool set_cloexec, size_t buffer_size) {
    int fds[2] = {-1, -1};
    int flags = set_cloexec ? O_CLOEXEC : 0;

    if (::pipe2(fds, flags) != 0) {
        return core::Result<AnonymousPipe>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pipe2() failed",
                errno));
    }

    if (buffer_size > 0) {
#if defined(F_SETPIPE_SZ)
        // Best-effort buffer size tuning; ignore error if unprivileged limit restricts it
        ::fcntl(fds[0], F_SETPIPE_SZ, static_cast<int>(buffer_size));
#endif
    }

    return core::Result<AnonymousPipe>::ok(
        AnonymousPipe(core::UniqueFd(fds[0]), core::UniqueFd(fds[1])));
}

// ----------------------------------------------------------------------------
// NamedFifo implementation
// ----------------------------------------------------------------------------

core::Result<NamedFifo> NamedFifo::create(const std::string& path, mode_t mode) {
    if (path.empty()) {
        return core::Result<NamedFifo>::err(
            core::ErrorCode::InvalidArgument, "NamedFifo path cannot be empty");
    }

    struct stat st{};
    if (::lstat(path.c_str(), &st) == 0) {
        // Path already exists! Protect pre-existing files and reject overwrite
        if (!S_ISFIFO(st.st_mode)) {
            return core::Result<NamedFifo>::err(
                core::ErrorCode::InvalidState,
                "Path exists and is not a FIFO; refusing to overwrite: " + path);
        }
        return core::Result<NamedFifo>::err(
            core::ErrorCode::InvalidState,
            "FIFO already exists at path: " + path);
    }

    if (::mkfifo(path.c_str(), mode) != 0) {
        return core::Result<NamedFifo>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "mkfifo() failed for path: " + path,
                errno));
    }

    return core::Result<NamedFifo>::ok(NamedFifo(path, true /* is_owned */));
}

core::Result<NamedFifo> NamedFifo::open_existing(const std::string& path) {
    if (path.empty()) {
        return core::Result<NamedFifo>::err(
            core::ErrorCode::InvalidArgument, "NamedFifo path cannot be empty");
    }

    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return core::Result<NamedFifo>::err(
            core::Error::from_errno(
                core::ErrorCode::NotFound,
                "FIFO does not exist at path: " + path,
                errno));
    }

    if (!S_ISFIFO(st.st_mode)) {
        return core::Result<NamedFifo>::err(
            core::ErrorCode::InvalidState,
            "Path exists but is not a FIFO: " + path);
    }

    return core::Result<NamedFifo>::ok(NamedFifo(path, false /* is_owned */));
}

NamedFifo::~NamedFifo() noexcept {
    auto res = unlink();
    (void)res;
}

NamedFifo::NamedFifo(NamedFifo&& other) noexcept
    : path_(std::move(other.path_)), is_owned_(other.is_owned_) {
    other.is_owned_ = false;
}

NamedFifo& NamedFifo::operator=(NamedFifo&& other) noexcept {
    if (this != &other) {
        auto res = unlink();
        (void)res;
        path_ = std::move(other.path_);
        is_owned_ = other.is_owned_;
        other.is_owned_ = false;
    }
    return *this;
}

core::Result<void> NamedFifo::unlink() noexcept {
    if (is_owned_ && !path_.empty()) {
        if (::unlink(path_.c_str()) != 0 && errno != ENOENT) {
            return core::Result<void>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    "Failed to unlink FIFO at: " + path_,
                    errno));
        }
        is_owned_ = false;
    }
    return core::Result<void>::ok();
}

core::Result<core::UniqueFd> NamedFifo::open_read(std::optional<std::chrono::milliseconds> timeout) {
    if (path_.empty()) {
        return core::Result<core::UniqueFd>::err(
            core::ErrorCode::InvalidState, "NamedFifo path is empty");
    }

    (void)timeout;
    // Under Linux (man 7 fifo), opening a FIFO with O_RDWR succeeds immediately and ensures
    // the descriptor never suffers from the POSIX 0-writer premature EOF race condition.
    // Portability notice: POSIX.1-2017 leaves O_RDWR on a FIFO undefined; BSD systems
    // may return EINVAL. On Linux, this is the canonical mechanism for robust FIFO readers.
    int fd = ::open(path_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return core::Result<core::UniqueFd>::err(
            core::Error::from_errno(
                core::ErrorCode::IoError,
                "open_read() failed on FIFO: " + path_,
                errno));
    }

    return core::Result<core::UniqueFd>::ok(core::UniqueFd(fd));
}

core::Result<core::UniqueFd> NamedFifo::open_write(std::optional<std::chrono::milliseconds> timeout) {
    if (path_.empty()) {
        return core::Result<core::UniqueFd>::err(
            core::ErrorCode::InvalidState, "NamedFifo path is empty");
    }

    ensure_sigpipe_ignored();

    auto start = std::chrono::steady_clock::now();
    auto max_wait = timeout.value_or(std::chrono::milliseconds(5000));

    while (true) {
        // In POSIX, O_WRONLY | O_NONBLOCK returns ENXIO if no reader is open yet
        int fd = ::open(path_.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            // Reader arrived; restore blocking mode for predictable reliable transfers
            int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags >= 0) {
                ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            }
            return core::Result<core::UniqueFd>::ok(core::UniqueFd(fd));
        }

        if (errno == ENXIO) {
            // No reader yet; check deadline
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed >= max_wait) {
                return core::Result<core::UniqueFd>::err(
                    core::ErrorCode::Timeout,
                    "Timeout waiting for reader to open FIFO: " + path_);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        return core::Result<core::UniqueFd>::err(
            core::Error::from_errno(
                core::ErrorCode::IoError,
                "open_write() failed on FIFO: " + path_,
                errno));
    }
}

// ----------------------------------------------------------------------------
// Benchmark Framework Implementation
// ----------------------------------------------------------------------------

core::Result<ThroughputResult> run_pipe_throughput_benchmark(
    const IpcTransferConfig& config,
    size_t warmup_iterations) {
    if (config.payload_size == 0 || config.iterations == 0) {
        return core::Result<ThroughputResult>::err(
            core::ErrorCode::InvalidArgument, "Payload size and iterations must be greater than 0");
    }

    ensure_sigpipe_ignored();

    // Create high-capacity data pipe and sync pipe
    auto data_pipe_res = AnonymousPipe::create(true, 1048576);
    if (data_pipe_res.is_err()) {
        return core::Result<ThroughputResult>::err(data_pipe_res.error());
    }
    auto sync_pipe_res = AnonymousPipe::create(true, 4096);
    if (sync_pipe_res.is_err()) {
        return core::Result<ThroughputResult>::err(sync_pipe_res.error());
    }

    auto data_pipe = std::move(data_pipe_res.value());
    auto sync_pipe = std::move(sync_pipe_res.value());

    // Prepare payload
    std::vector<uint8_t> payload(config.payload_size);
    for (size_t i = 0; i < config.payload_size; ++i) {
        payload[i] = static_cast<uint8_t>((i & 0xFF) ^ 0xA5);
    }

    pid_t child_pid = ::fork();
    if (child_pid < 0) {
        return core::Result<ThroughputResult>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fork() failed", errno));
    }

    if (child_pid == 0) {
        // Child: Receiver
        data_pipe.close_write();
        sync_pipe.close_read();

        int data_fd = data_pipe.read_fd().get();
        int sync_fd = sync_pipe.write_fd().get();

        // 1. Signal parent we are ready to receive
        uint8_t ready_byte = 1;
        auto wr_res = write_all(sync_fd, &ready_byte, 1);
        if (wr_res.is_err()) {
            ::_exit(1);
        }

        std::vector<uint8_t> rx_buf(config.payload_size);
        bool verification_passed = true;

        // 2. Warmup phase
        for (size_t i = 0; i < warmup_iterations; ++i) {
            auto rd_res = read_exact(data_fd, rx_buf.data(), config.payload_size);
            if (rd_res.is_err()) {
                ::_exit(2);
            }
        }

        // 3. Measured phase
        for (size_t i = 0; i < config.iterations; ++i) {
            auto rd_res = read_exact(data_fd, rx_buf.data(), config.payload_size);
            if (rd_res.is_err()) {
                ::_exit(3);
            }
            if (config.verify_data) {
                if (std::memcmp(rx_buf.data(), payload.data(), config.payload_size) != 0) {
                    verification_passed = false;
                }
            }
        }

        // 4. Signal parent that all bytes were received
        uint8_t ack_byte = verification_passed ? 0 : 4;
        (void)write_all(sync_fd, &ack_byte, 1);

        ::_exit(verification_passed ? 0 : 5);
    }

    // Parent: Sender
    data_pipe.close_read();
    sync_pipe.close_write();

    int data_fd = data_pipe.write_fd().get();
    int sync_fd = sync_pipe.read_fd().get();

    // 1. Wait for child ready signal
    uint8_t sync_byte = 0;
    auto rd_sync = read_exact(sync_fd, &sync_byte, 1);
    if (rd_sync.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<ThroughputResult>::err(rd_sync.error());
    }

    // 2. Warmup phase (not timed)
    for (size_t i = 0; i < warmup_iterations; ++i) {
        auto wr = write_all(data_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<ThroughputResult>::err(wr.error());
        }
    }

    // 3. Measured steady-state phase
    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < config.iterations; ++i) {
        auto wr = write_all(data_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<ThroughputResult>::err(wr.error());
        }
    }

    // Wait for child to acknowledge receipt of all bytes
    auto rd_ack = read_exact(sync_fd, &sync_byte, 1);
    auto t_end = std::chrono::steady_clock::now();

    // Close data pipe so child exits if still running, then reap
    data_pipe.close_write();
    safe_reap_child(child_pid);

    if (rd_ack.is_err()) {
        return core::Result<ThroughputResult>::err(rd_ack.error());
    }

    ThroughputResult res{};
    res.payload_size = config.payload_size;
    res.iterations = config.iterations;
    res.total_bytes = static_cast<uint64_t>(config.payload_size) * config.iterations;
    res.elapsed_duration = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start);
    res.data_verified = (sync_byte == 0);

    double sec = static_cast<double>(res.elapsed_duration.count()) / 1000000.0;
    if (sec > 0.0) {
        double mib = static_cast<double>(res.total_bytes) / (1024.0 * 1024.0);
        res.throughput_mib_s = mib / sec;
        res.messages_per_sec = static_cast<double>(config.iterations) / sec;
    }

    return core::Result<ThroughputResult>::ok(res);
}

core::Result<LatencyResult> run_pipe_latency_benchmark(
    const IpcTransferConfig& config,
    size_t warmup_iterations) {
    if (config.payload_size == 0 || config.iterations == 0) {
        return core::Result<LatencyResult>::err(
            core::ErrorCode::InvalidArgument, "Payload size and iterations must be greater than 0");
    }

    ensure_sigpipe_ignored();

    auto pipe_fwd_res = AnonymousPipe::create(true, 1048576);
    if (pipe_fwd_res.is_err()) {
        return core::Result<LatencyResult>::err(pipe_fwd_res.error());
    }
    auto pipe_bwd_res = AnonymousPipe::create(true, 1048576);
    if (pipe_bwd_res.is_err()) {
        return core::Result<LatencyResult>::err(pipe_bwd_res.error());
    }

    auto pipe_fwd = std::move(pipe_fwd_res.value());
    auto pipe_bwd = std::move(pipe_bwd_res.value());

    std::vector<uint8_t> payload(config.payload_size);
    for (size_t i = 0; i < config.payload_size; ++i) {
        payload[i] = static_cast<uint8_t>((i & 0xFF) ^ 0x3C);
    }

    pid_t child_pid = ::fork();
    if (child_pid < 0) {
        return core::Result<LatencyResult>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fork() failed", errno));
    }

    if (child_pid == 0) {
        // Child: Echo responder
        pipe_fwd.close_write();
        pipe_bwd.close_read();

        int in_fd = pipe_fwd.read_fd().get();
        int out_fd = pipe_bwd.write_fd().get();

        std::vector<uint8_t> echo_buf(config.payload_size);
        size_t total_runs = warmup_iterations + config.iterations;

        for (size_t i = 0; i < total_runs; ++i) {
            auto rd = read_exact(in_fd, echo_buf.data(), config.payload_size);
            if (rd.is_err()) {
                ::_exit(1);
            }
            auto wr = write_all(out_fd, echo_buf.data(), config.payload_size);
            if (wr.is_err()) {
                ::_exit(2);
            }
        }
        ::_exit(0);
    }

    // Parent: Request initiator & timer
    pipe_fwd.close_read();
    pipe_bwd.close_write();

    int out_fd = pipe_fwd.write_fd().get();
    int in_fd = pipe_bwd.read_fd().get();

    std::vector<uint8_t> rx_buf(config.payload_size);
    bool verification_passed = true;

    // Warmup phase
    for (size_t i = 0; i < warmup_iterations; ++i) {
        auto wr = write_all(out_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(wr.error());
        }
        auto rd = read_exact(in_fd, rx_buf.data(), config.payload_size);
        if (rd.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(rd.error());
        }
    }

    // Measured round-trip phase
    std::vector<double> sample_latencies_us;
    sample_latencies_us.reserve(config.iterations);

    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < config.iterations; ++i) {
        auto iter_start = std::chrono::steady_clock::now();

        auto wr = write_all(out_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(wr.error());
        }
        auto rd = read_exact(in_fd, rx_buf.data(), config.payload_size);
        if (rd.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(rd.error());
        }

        auto iter_end = std::chrono::steady_clock::now();
        double lat_us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - iter_start).count()) / 1000.0;
        sample_latencies_us.push_back(lat_us);

        if (config.verify_data && verification_passed) {
            if (std::memcmp(rx_buf.data(), payload.data(), config.payload_size) != 0) {
                verification_passed = false;
            }
        }
    }

    auto t_end = std::chrono::steady_clock::now();

    pipe_fwd.close_write();
    safe_reap_child(child_pid);

    std::sort(sample_latencies_us.begin(), sample_latencies_us.end());

    LatencyResult res{};
    res.payload_size = config.payload_size;
    res.iterations = config.iterations;
    res.total_duration = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start);
    res.data_verified = verification_passed;

    if (!sample_latencies_us.empty()) {
        res.min_round_trip_us = sample_latencies_us.front();
        res.max_round_trip_us = sample_latencies_us.back();

        double sum = 0.0;
        for (double v : sample_latencies_us) {
            sum += v;
        }
        res.avg_round_trip_us = sum / static_cast<double>(sample_latencies_us.size());

        size_t p50_idx = sample_latencies_us.size() / 2;
        res.p50_round_trip_us = sample_latencies_us[p50_idx];

        size_t p99_idx = static_cast<size_t>(static_cast<double>(sample_latencies_us.size()) * 0.99);
        if (p99_idx >= sample_latencies_us.size()) {
            p99_idx = sample_latencies_us.size() - 1;
        }
        res.p99_round_trip_us = sample_latencies_us[p99_idx];
    }

    return core::Result<LatencyResult>::ok(res);
}

// ----------------------------------------------------------------------------
// Named FIFO Benchmark Implementation
// ----------------------------------------------------------------------------

core::Result<ThroughputResult> run_fifo_throughput_benchmark(
    const std::string& fifo_dir,
    const IpcTransferConfig& config,
    size_t warmup_iterations) {
    if (config.payload_size == 0 || config.iterations == 0) {
        return core::Result<ThroughputResult>::err(
            core::ErrorCode::InvalidArgument, "Payload size and iterations must be greater than 0");
    }

    ensure_sigpipe_ignored();

    pid_t parent_pid = ::getpid();
    std::string base = fifo_dir.empty() ? "/tmp" : fifo_dir;
    std::string data_path = base + "/kernox_fifo_data_" + std::to_string(parent_pid) + "_" + std::to_string(::rand());
    std::string sync_path = base + "/kernox_fifo_sync_" + std::to_string(parent_pid) + "_" + std::to_string(::rand());

    auto data_fifo_res = NamedFifo::create(data_path, 0600);
    if (data_fifo_res.is_err()) {
        return core::Result<ThroughputResult>::err(data_fifo_res.error());
    }
    auto sync_fifo_res = NamedFifo::create(sync_path, 0600);
    if (sync_fifo_res.is_err()) {
        return core::Result<ThroughputResult>::err(sync_fifo_res.error());
    }

    auto data_fifo = std::move(data_fifo_res.value());
    auto sync_fifo = std::move(sync_fifo_res.value());

    std::vector<uint8_t> payload(config.payload_size);
    for (size_t i = 0; i < config.payload_size; ++i) {
        payload[i] = static_cast<uint8_t>((i & 0xFF) ^ 0x5A);
    }

    pid_t child_pid = ::fork();
    if (child_pid < 0) {
        return core::Result<ThroughputResult>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fork() failed", errno));
    }

    if (child_pid == 0) {
        // Child: Receiver
        auto child_data = NamedFifo::open_existing(data_path);
        auto child_sync = NamedFifo::open_existing(sync_path);
        if (child_data.is_err() || child_sync.is_err()) {
            ::_exit(1);
        }

        auto rd_fd_res = child_data.value().open_read(config.timeout);
        if (rd_fd_res.is_err()) {
            ::_exit(2);
        }
        auto wr_fd_res = child_sync.value().open_write(config.timeout);
        if (wr_fd_res.is_err()) {
            ::_exit(3);
        }

        int data_fd = rd_fd_res.value().get();
        int sync_fd = wr_fd_res.value().get();

        // 1. Handshake: wait for parent's start token on data_fd, then confirm on sync_fd
        uint8_t start_token = 0;
        if (read_exact(data_fd, &start_token, 1).is_err()) {
            ::_exit(4);
        }
        uint8_t ready_byte = 1;
        if (write_all(sync_fd, &ready_byte, 1).is_err()) {
            ::_exit(5);
        }

        std::vector<uint8_t> rx_buf(config.payload_size);
        bool verification_passed = true;

        // 2. Warmup
        for (size_t i = 0; i < warmup_iterations; ++i) {
            if (read_exact(data_fd, rx_buf.data(), config.payload_size).is_err()) {
                ::_exit(6);
            }
        }

        // 3. Measured
        for (size_t i = 0; i < config.iterations; ++i) {
            if (read_exact(data_fd, rx_buf.data(), config.payload_size).is_err()) {
                ::_exit(7);
            }
            if (config.verify_data && verification_passed) {
                if (std::memcmp(rx_buf.data(), payload.data(), config.payload_size) != 0) {
                    verification_passed = false;
                }
            }
        }

        // 4. Ack
        uint8_t ack_byte = verification_passed ? 0 : 8;
        (void)write_all(sync_fd, &ack_byte, 1);
        ::_exit(verification_passed ? 0 : 9);
    }

    // Parent: Sender
    // Open data FIFO for writing first (child already opened read end)
    auto data_wr_res = data_fifo.open_write(config.timeout);
    if (data_wr_res.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<ThroughputResult>::err(data_wr_res.error());
    }
    auto sync_rd_res = sync_fifo.open_read(config.timeout);
    if (sync_rd_res.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<ThroughputResult>::err(sync_rd_res.error());
    }

    int sync_fd = sync_rd_res.value().get();
    int data_fd = data_wr_res.value().get();

    // 1. Handshake: send start token on data_fd, then wait for child ready on sync_fd
    uint8_t start_token = 0xA5;
    auto wr_token = write_all(data_fd, &start_token, 1);
    if (wr_token.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<ThroughputResult>::err(wr_token.error());
    }

    uint8_t sync_byte = 0;
    auto rd_sync = read_exact(sync_fd, &sync_byte, 1);
    if (rd_sync.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<ThroughputResult>::err(rd_sync.error());
    }

    // 2. Warmup
    for (size_t i = 0; i < warmup_iterations; ++i) {
        auto wr = write_all(data_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<ThroughputResult>::err(wr.error());
        }
    }

    // 3. Measured
    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < config.iterations; ++i) {
        auto wr = write_all(data_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<ThroughputResult>::err(wr.error());
        }
    }

    auto rd_ack = read_exact(sync_fd, &sync_byte, 1);
    auto t_end = std::chrono::steady_clock::now();

    data_wr_res.value().reset(-1);
    safe_reap_child(child_pid);

    if (rd_ack.is_err()) {
        return core::Result<ThroughputResult>::err(rd_ack.error());
    }

    ThroughputResult res{};
    res.payload_size = config.payload_size;
    res.iterations = config.iterations;
    res.total_bytes = static_cast<uint64_t>(config.payload_size) * config.iterations;
    res.elapsed_duration = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start);
    res.data_verified = (sync_byte == 0);

    double sec = static_cast<double>(res.elapsed_duration.count()) / 1000000.0;
    if (sec > 0.0) {
        double mib = static_cast<double>(res.total_bytes) / (1024.0 * 1024.0);
        res.throughput_mib_s = mib / sec;
        res.messages_per_sec = static_cast<double>(config.iterations) / sec;
    }

    return core::Result<ThroughputResult>::ok(res);
}

core::Result<LatencyResult> run_fifo_latency_benchmark(
    const std::string& fifo_dir,
    const IpcTransferConfig& config,
    size_t warmup_iterations) {
    if (config.payload_size == 0 || config.iterations == 0) {
        return core::Result<LatencyResult>::err(
            core::ErrorCode::InvalidArgument, "Payload size and iterations must be greater than 0");
    }

    ensure_sigpipe_ignored();

    pid_t parent_pid = ::getpid();
    std::string base = fifo_dir.empty() ? "/tmp" : fifo_dir;
    std::string req_path = base + "/kernox_fifo_req_" + std::to_string(parent_pid) + "_" + std::to_string(::rand());
    std::string resp_path = base + "/kernox_fifo_resp_" + std::to_string(parent_pid) + "_" + std::to_string(::rand());

    auto req_fifo_res = NamedFifo::create(req_path, 0600);
    if (req_fifo_res.is_err()) {
        return core::Result<LatencyResult>::err(req_fifo_res.error());
    }
    auto resp_fifo_res = NamedFifo::create(resp_path, 0600);
    if (resp_fifo_res.is_err()) {
        return core::Result<LatencyResult>::err(resp_fifo_res.error());
    }

    auto req_fifo = std::move(req_fifo_res.value());
    auto resp_fifo = std::move(resp_fifo_res.value());

    std::vector<uint8_t> payload(config.payload_size);
    for (size_t i = 0; i < config.payload_size; ++i) {
        payload[i] = static_cast<uint8_t>((i & 0xFF) ^ 0xC3);
    }

    pid_t child_pid = ::fork();
    if (child_pid < 0) {
        return core::Result<LatencyResult>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fork() failed", errno));
    }

    if (child_pid == 0) {
        // Child: Echo responder
        auto child_req = NamedFifo::open_existing(req_path);
        auto child_resp = NamedFifo::open_existing(resp_path);
        if (child_req.is_err() || child_resp.is_err()) {
            ::_exit(1);
        }

        auto rd_fd_res = child_req.value().open_read(config.timeout);
        if (rd_fd_res.is_err()) {
            ::_exit(2);
        }
        auto wr_fd_res = child_resp.value().open_write(config.timeout);
        if (wr_fd_res.is_err()) {
            ::_exit(3);
        }

        int in_fd = rd_fd_res.value().get();
        int out_fd = wr_fd_res.value().get();

        // Handshake: signal parent that child responder is ready and has write end open
        uint8_t ready_byte = 1;
        if (write_all(out_fd, &ready_byte, 1).is_err()) {
            ::_exit(4);
        }

        std::vector<uint8_t> echo_buf(config.payload_size);
        size_t total_runs = warmup_iterations + config.iterations;

        for (size_t i = 0; i < total_runs; ++i) {
            auto rd = read_exact(in_fd, echo_buf.data(), config.payload_size);
            if (rd.is_err()) {
                ::_exit(5);
            }
            auto wr = write_all(out_fd, echo_buf.data(), config.payload_size);
            if (wr.is_err()) {
                ::_exit(6);
            }
        }
        ::_exit(0);
    }

    // Parent: Request initiator & timer
    auto resp_rd_res = resp_fifo.open_read(config.timeout);
    if (resp_rd_res.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<LatencyResult>::err(resp_rd_res.error());
    }
    auto req_wr_res = req_fifo.open_write(config.timeout);
    if (req_wr_res.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<LatencyResult>::err(req_wr_res.error());
    }

    int in_fd = resp_rd_res.value().get();
    int out_fd = req_wr_res.value().get();

    // Handshake: wait for child responder ready
    uint8_t ready_byte = 0;
    auto rd_ready = read_exact(in_fd, &ready_byte, 1);
    if (rd_ready.is_err()) {
        safe_reap_child(child_pid);
        return core::Result<LatencyResult>::err(rd_ready.error());
    }

    std::vector<uint8_t> rx_buf(config.payload_size);
    bool verification_passed = true;

    // Warmup
    for (size_t i = 0; i < warmup_iterations; ++i) {
        auto wr = write_all(out_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(wr.error());
        }
        auto rd = read_exact(in_fd, rx_buf.data(), config.payload_size);
        if (rd.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(rd.error());
        }
    }

    // Measured
    std::vector<double> sample_latencies_us;
    sample_latencies_us.reserve(config.iterations);

    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < config.iterations; ++i) {
        auto iter_start = std::chrono::steady_clock::now();

        auto wr = write_all(out_fd, payload.data(), config.payload_size);
        if (wr.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(wr.error());
        }
        auto rd = read_exact(in_fd, rx_buf.data(), config.payload_size);
        if (rd.is_err()) {
            safe_reap_child(child_pid);
            return core::Result<LatencyResult>::err(rd.error());
        }

        auto iter_end = std::chrono::steady_clock::now();
        double lat_us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - iter_start).count()) / 1000.0;
        sample_latencies_us.push_back(lat_us);

        if (config.verify_data && verification_passed) {
            if (std::memcmp(rx_buf.data(), payload.data(), config.payload_size) != 0) {
                verification_passed = false;
            }
        }
    }

    auto t_end = std::chrono::steady_clock::now();

    req_wr_res.value().reset(-1);
    safe_reap_child(child_pid);

    std::sort(sample_latencies_us.begin(), sample_latencies_us.end());

    LatencyResult res{};
    res.payload_size = config.payload_size;
    res.iterations = config.iterations;
    res.total_duration = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start);
    res.data_verified = verification_passed;

    if (!sample_latencies_us.empty()) {
        res.min_round_trip_us = sample_latencies_us.front();
        res.max_round_trip_us = sample_latencies_us.back();

        double sum = 0.0;
        for (double v : sample_latencies_us) {
            sum += v;
        }
        res.avg_round_trip_us = sum / static_cast<double>(sample_latencies_us.size());

        size_t p50_idx = sample_latencies_us.size() / 2;
        res.p50_round_trip_us = sample_latencies_us[p50_idx];

        size_t p99_idx = static_cast<size_t>(static_cast<double>(sample_latencies_us.size()) * 0.99);
        if (p99_idx >= sample_latencies_us.size()) {
            p99_idx = sample_latencies_us.size() - 1;
        }
        res.p99_round_trip_us = sample_latencies_us[p99_idx];
    }

    return core::Result<LatencyResult>::ok(res);
}

core::Result<BenchmarkSuiteResult> run_ipc_benchmark_suite(
    const std::string& type,
    const std::vector<size_t>& sizes,
    size_t iterations,
    size_t warmup_iterations,
    const std::string& fifo_dir) {
    BenchmarkSuiteResult suite{};
    suite.ipc_type = type;

    std::vector<size_t> test_sizes = sizes;
    if (test_sizes.empty()) {
        test_sizes = {64, 1024, 4096, 65536, 1048576}; // 64B, 1KiB, 4KiB, 64KiB, 1MiB
    }

    bool test_pipe = (type == "pipe" || type == "all");
    bool test_fifo = (type == "fifo" || type == "all");

    if (test_pipe) {
        for (size_t sz : test_sizes) {
            // Scale iterations for large sizes to maintain quick deterministic execution
            size_t iters = iterations;
            if (sz >= 1048576) {
                iters = std::max<size_t>(10, iterations / 20);
            } else if (sz >= 65536) {
                iters = std::max<size_t>(50, iterations / 5);
            }

            IpcTransferConfig cfg{sz, iters, std::chrono::milliseconds(10000), true};

            auto tp_res = run_pipe_throughput_benchmark(cfg, warmup_iterations);
            if (tp_res.is_err()) {
                return core::Result<BenchmarkSuiteResult>::err(tp_res.error());
            }
            suite.throughput_results.push_back(tp_res.value());

            auto lat_res = run_pipe_latency_benchmark(cfg, warmup_iterations);
            if (lat_res.is_err()) {
                return core::Result<BenchmarkSuiteResult>::err(lat_res.error());
            }
            suite.latency_results.push_back(lat_res.value());
        }
    }

    if (test_fifo) {
        for (size_t sz : test_sizes) {
            size_t iters = iterations;
            if (sz >= 1048576) {
                iters = std::max<size_t>(10, iterations / 20);
            } else if (sz >= 65536) {
                iters = std::max<size_t>(50, iterations / 5);
            }

            IpcTransferConfig cfg{sz, iters, std::chrono::milliseconds(10000), true};

            auto tp_res = run_fifo_throughput_benchmark(fifo_dir, cfg, warmup_iterations);
            if (tp_res.is_err()) {
                return core::Result<BenchmarkSuiteResult>::err(tp_res.error());
            }
            suite.throughput_results.push_back(tp_res.value());

            auto lat_res = run_fifo_latency_benchmark(fifo_dir, cfg, warmup_iterations);
            if (lat_res.is_err()) {
                return core::Result<BenchmarkSuiteResult>::err(lat_res.error());
            }
            suite.latency_results.push_back(lat_res.value());
        }
    }

    return core::Result<BenchmarkSuiteResult>::ok(std::move(suite));
}

} // namespace kernox::system
