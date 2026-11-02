#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"
#include "kernox/core/unique_fd.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace kernox::system {

/**
 * @brief Low-level reliable I/O helpers.
 */

// Writes all bytes to fd, retrying on EINTR and looping until count bytes are written.
// Returns total bytes written or an IoError on system failure (such as EPIPE).
core::Result<size_t> write_all(int fd, const void* data, size_t count);

// Reads exactly count bytes from fd, retrying on EINTR and looping until full count is read.
// Returns an error if EOF occurs before count bytes have been received.
core::Result<void> read_exact(int fd, void* buffer, size_t count);

// Reads up to max_count bytes from fd. Returns actual number of bytes read (0 indicates EOF).
core::Result<size_t> read_up_to(int fd, void* buffer, size_t max_count);

// Maximum permitted payload size for framed messages (64 MiB) to prevent uncontrolled memory allocation.
inline constexpr size_t MAX_FRAME_PAYLOAD_SIZE = 64 * 1024 * 1024;

// Polls fd for readability or writability with a timeout.
// Returns true if ready, false if timed out, or error on failure (including POLLNVAL for invalid FDs).
core::Result<bool> poll_fd(int fd, short events, std::chrono::milliseconds timeout);

/**
 * @brief Sends a length-prefixed binary message across fd: [uint32_t big_endian_len][payload].
 * 
 * Wire protocol: 4-byte network-order length followed by raw payload bytes.
 * Binary-safe (payloads with null bytes are fully preserved).
 * Rejects messages exceeding MAX_FRAME_PAYLOAD_SIZE with ErrorCode::InvalidArgument.
 *
 * NOTE: If send_message returns an error after the header has been written to the descriptor,
 * the stream is in a desynchronized state and cannot be safely reused for subsequent framed messages.
 */
core::Result<void> send_message(
    int fd,
    const std::vector<uint8_t>& message,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

/**
 * @brief Receives a length-prefixed binary message from fd.
 *
 * Rejects frames exceeding MAX_FRAME_PAYLOAD_SIZE with ErrorCode::InvalidArgument to prevent
 * memory exhaustion from untrusted inputs.
 * Returns ErrorCode::IoError if EOF is encountered midway through a frame.
 *
 * NOTE: If receive_message returns an error after reading a partial frame, the stream is
 * in a desynchronized state and cannot be safely reused without resetting or reconnecting.
 */
core::Result<std::vector<uint8_t>> receive_message(
    int fd,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

/**
 * @brief Anonymous Pipe abstraction wrapping a unidirectional pipe2(O_CLOEXEC) pair.
 */
class AnonymousPipe {
public:
    static core::Result<AnonymousPipe> create(bool set_cloexec = true, size_t buffer_size = 0);

    AnonymousPipe() = default;
    ~AnonymousPipe() = default;

    AnonymousPipe(AnonymousPipe&&) noexcept = default;
    AnonymousPipe& operator=(AnonymousPipe&&) noexcept = default;

    AnonymousPipe(const AnonymousPipe&) = delete;
    AnonymousPipe& operator=(const AnonymousPipe&) = delete;

    [[nodiscard]] core::UniqueFd& read_fd() noexcept { return read_end_; }
    [[nodiscard]] const core::UniqueFd& read_fd() const noexcept { return read_end_; }

    [[nodiscard]] core::UniqueFd& write_fd() noexcept { return write_end_; }
    [[nodiscard]] const core::UniqueFd& write_fd() const noexcept { return write_end_; }

    void close_read() noexcept { read_end_.reset(-1); }
    void close_write() noexcept { write_end_.reset(-1); }

    [[nodiscard]] bool has_read_end() const noexcept { return read_end_.is_valid(); }
    [[nodiscard]] bool has_write_end() const noexcept { return write_end_.is_valid(); }

    [[nodiscard]] bool is_valid() const noexcept {
        return read_end_.is_valid() && write_end_.is_valid();
    }

private:
    AnonymousPipe(core::UniqueFd r, core::UniqueFd w)
        : read_end_(std::move(r)), write_end_(std::move(w)) {}

    core::UniqueFd read_end_;
    core::UniqueFd write_end_;
};

/**
 * @brief Named FIFO abstraction wrapping mkfifo(3).
 *
 * Implements strict ownership semantics: only FIFOs created by this instance are
 * unlinked on destruction. Pre-existing files or user paths are never unlinked.
 */
class NamedFifo {
public:
    // Creates a new FIFO node at path with specified permissions.
    // Fails with ErrorCode::InvalidState if a file/dir exists at path.
    static core::Result<NamedFifo> create(const std::string& path, mode_t mode = 0600);

    // References an existing FIFO without claiming ownership (will not unlink on destruction).
    static core::Result<NamedFifo> open_existing(const std::string& path);

    ~NamedFifo() noexcept;

    NamedFifo(NamedFifo&& other) noexcept;
    NamedFifo& operator=(NamedFifo&& other) noexcept;

    NamedFifo(const NamedFifo&) = delete;
    NamedFifo& operator=(const NamedFifo&) = delete;

    // Opens FIFO for reading with optional timeout to prevent indefinite hangs.
    core::Result<core::UniqueFd> open_read(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Opens FIFO for writing with optional timeout to prevent indefinite hangs (retries until reader arrives).
    core::Result<core::UniqueFd> open_write(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] bool is_owned() const noexcept { return is_owned_; }

    // Manually unlink FIFO from filesystem and revoke ownership.
    core::Result<void> unlink() noexcept;

private:
    NamedFifo(std::string path, bool is_owned)
        : path_(std::move(path)), is_owned_(is_owned) {}

    std::string path_{};
    bool is_owned_{false};
};

/**
 * @brief Performance Benchmark Structures and Configurations.
 */

struct IpcTransferConfig {
    size_t payload_size{1024};
    size_t iterations{100};
    std::chrono::milliseconds timeout{5000};
    bool verify_data{false};
};

struct ThroughputResult {
    size_t payload_size{0};
    size_t iterations{0};
    uint64_t total_bytes{0};
    std::chrono::microseconds elapsed_duration{0};
    double throughput_mib_s{0.0};
    double messages_per_sec{0.0};
    bool data_verified{true};
};

struct LatencyResult {
    size_t payload_size{0};
    size_t iterations{0};
    std::chrono::microseconds total_duration{0};
    double avg_round_trip_us{0.0};
    double min_round_trip_us{0.0};
    double max_round_trip_us{0.0};
    double p50_round_trip_us{0.0};
    double p99_round_trip_us{0.0};
    bool data_verified{true};
};

struct BenchmarkSuiteResult {
    std::string ipc_type; // "pipe", "fifo", or "all"
    std::vector<ThroughputResult> throughput_results;
    std::vector<LatencyResult> latency_results;
};

// Measures one-way steady-state throughput across an anonymous pipe.
core::Result<ThroughputResult> run_pipe_throughput_benchmark(
    const IpcTransferConfig& config,
    size_t warmup_iterations = 10);

// Measures round-trip ping-pong latency across two anonymous pipes.
core::Result<LatencyResult> run_pipe_latency_benchmark(
    const IpcTransferConfig& config,
    size_t warmup_iterations = 10);

// Measures one-way steady-state throughput across a named FIFO.
core::Result<ThroughputResult> run_fifo_throughput_benchmark(
    const std::string& fifo_dir,
    const IpcTransferConfig& config,
    size_t warmup_iterations = 10);

// Measures round-trip ping-pong latency across two named FIFOs.
core::Result<LatencyResult> run_fifo_latency_benchmark(
    const std::string& fifo_dir,
    const IpcTransferConfig& config,
    size_t warmup_iterations = 10);

// Runs full benchmark across specified payload sizes (default: 64B, 1KiB, 4KiB, 64KiB, 1MiB).
core::Result<BenchmarkSuiteResult> run_ipc_benchmark_suite(
    const std::string& type, // "pipe" or "fifo"
    const std::vector<size_t>& sizes,
    size_t iterations,
    size_t warmup_iterations = 10,
    const std::string& fifo_dir = "/tmp");

} // namespace kernox::system

namespace kernox::sys {
    using AnonymousPipe = kernox::system::AnonymousPipe;
    using NamedFifo = kernox::system::NamedFifo;
    using IpcTransferConfig = kernox::system::IpcTransferConfig;
    using ThroughputResult = kernox::system::ThroughputResult;
    using LatencyResult = kernox::system::LatencyResult;
    using BenchmarkSuiteResult = kernox::system::BenchmarkSuiteResult;
}
