#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/types.h>
#include <vector>

namespace kernox::system {

/**
 * @brief Stage of child process setup during launch.
 */
enum class ChildErrorStage : uint32_t {
    ResourceLimit = 1,
    Exec = 2
};

/**
 * @brief Fixed-size error payload written across the O_CLOEXEC pipe on launch failure.
 */
struct ChildErrorPayload {
    ChildErrorStage stage{ChildErrorStage::Exec};
    int error_number{0};

    /**
     * @brief Validates payload structure and fields.
     */
    [[nodiscard]] core::Result<void> validate() const;
};

/**
 * @brief Structured outcome of child cleanup and reaping operations.
 */
struct ChildCleanupResult {
    bool reaped{false};         // True if child is confirmed no longer running (reaped or already reaped)
    bool status_valid{false};   // True if valid exit status was collected via wait4 (NOT valid on ECHILD)
    int raw_status{0};          // Raw wait4 status (valid only when status_valid is true)
};

/**
 * @brief Represents a soft and hard limit pair for POSIX resource limits.
 */
struct LimitSetting {
    uint64_t soft{0};
    uint64_t hard{0};

    constexpr LimitSetting() noexcept = default;
    constexpr LimitSetting(uint64_t value) noexcept : soft(value), hard(value) {}
    constexpr LimitSetting(uint64_t s, uint64_t h) noexcept : soft(s), hard(h) {}
};

/**
 * @brief Configurable POSIX resource limits applied via setrlimit(2) before exec.
 */
struct ProcessLimits {
    std::optional<LimitSetting> cpu_time_seconds{std::nullopt};    // RLIMIT_CPU
    std::optional<LimitSetting> address_space_bytes{std::nullopt}; // RLIMIT_AS
    std::optional<LimitSetting> max_open_files{std::nullopt};       // RLIMIT_NOFILE
    std::optional<LimitSetting> max_file_size_bytes{std::nullopt};  // RLIMIT_FSIZE

    /**
     * @brief Validates configured limits for sanity and soft <= hard invariants.
     */
    [[nodiscard]] core::Result<void> validate() const;
};

/**
 * @brief Configuration parameters for launching and monitoring a process.
 */
struct ProcessConfig {
    std::string executable{};
    std::vector<std::string> arguments{};
    std::vector<std::string> environment{}; // KEY=VALUE pairs; empty means inherit parent environment
    std::optional<std::chrono::milliseconds> timeout{std::nullopt};
    // Interval between SIGTERM and SIGKILL during timeout escalation.
    // A value of 0ms causes immediate escalation to SIGKILL if the process does
    // not exit immediately upon receiving SIGTERM.
    std::chrono::milliseconds grace_period{200};
    ProcessLimits limits{};
};

/**
 * @brief Per-child kernel resource accounting collected via wait4(2).
 */
struct ProcessResourceUsage {
    int64_t user_cpu_time_us{0};
    int64_t system_cpu_time_us{0};
    int64_t max_rss_kb{0};
    int64_t minor_page_faults{0};
    int64_t major_page_faults{0};
    int64_t voluntary_context_switches{0};
    int64_t involuntary_context_switches{0};
};

/**
 * @brief Execution outcome of a monitored child process.
 */
struct ProcessExecutionResult {
    pid_t pid{-1};
    bool exited{false};
    int exit_code{-1};
    bool signaled{false};
    int term_signal{-1};
    bool timed_out{false};
    std::chrono::microseconds wall_clock_duration{0};
    ProcessResourceUsage resource_usage{};

    [[nodiscard]] bool is_success() const noexcept {
        return exited && exit_code == 0 && !timed_out;
    }
};

/**
 * @brief Parsed attributes from Linux /proc/<pid>/stat.
 */
struct ProcessStatInfo {
    pid_t pid{-1};
    std::string comm{};
    char state{'?'};
    pid_t ppid{-1};
    uint64_t utime_ticks{0};
    uint64_t stime_ticks{0};
    uint64_t vsize_bytes{0};
    int64_t rss_pages{0};
};

/**
 * @brief Deterministic parsing routines for process telemetry.
 */
class ProcessParser {
public:
    /**
     * @brief Parses a /proc/<pid>/stat line.
     *
     * Correctly handles comm names enclosed in parentheses that may contain
     * spaces or nested parentheses (e.g., "(my prog (sub))").
     */
    static core::Result<ProcessStatInfo> parse_proc_pid_stat(std::string_view content);
};

/**
 * @brief Subsystem managing process lifecycles, sandboxed limits, and monitoring.
 *
 * @note Ownership and Reaping Contract:
 *       ProcessManager assumes exclusive ownership of the lifecycle and reaping of
 *       children it creates. Embedding applications must not concurrently reap
 *       KERNOX-managed children using broad calls such as wait(NULL) or waitpid(-1, ...).
 *       Applications must not configure SIGCHLD to be ignored (SIG_IGN) or enable
 *       automatic child reaping (SA_NOCLDWAIT) for managed children. Violating this
 *       ownership contract can cause child-status loss (wait4 returning ECHILD) and
 *       introduces potential PID-reuse-related signaling race conditions.
 */
class ProcessManager {

public:
    /**
     * @brief Launches an executable with explicit arguments, applies resource limits,
     *        enforces timeouts with SIGTERM -> SIGKILL escalation, and collects per-child rusage.
     *
     * @param config Process launch and execution parameters.
     * @return ProcessExecutionResult on completion, or Error if launching failed.
     */
    static core::Result<ProcessExecutionResult> run(const ProcessConfig& config);

    /**
     * @brief Reads and parses /proc/<pid>/stat for an active or inspectable process.
     *
     * @param pid Target process ID.
     * @param proc_dir Base proc directory (default "/proc", injectable for testing).
     */
    static core::Result<ProcessStatInfo> read_process_stat(
        pid_t pid,
        const std::string& proc_dir = "/proc");

    /**
     * @brief Safely cleans up and reaps a child process with bounded waiting, guarding against signaling reaped PIDs under single-owner execution.
     *
     * @param pid Target child process ID.
     * @param grace Max time to wait for voluntary child exit before escalating to SIGKILL (default: 50ms).
     * @param out_ru Optional pointer to rusage struct to receive resource accounting data.
     * @return ChildCleanupResult indicating whether the child was reaped and if status was captured.
     */
    static ChildCleanupResult reap_child(
        pid_t pid,
        std::chrono::milliseconds grace = std::chrono::milliseconds(50),
        struct rusage* out_ru = nullptr) noexcept;
};


} // namespace kernox::system

namespace kernox::sys {
    using ChildErrorStage = kernox::system::ChildErrorStage;
    using ChildErrorPayload = kernox::system::ChildErrorPayload;
    using ChildCleanupResult = kernox::system::ChildCleanupResult;
    using LimitSetting = kernox::system::LimitSetting;
    using ProcessLimits = kernox::system::ProcessLimits;
    using ProcessConfig = kernox::system::ProcessConfig;
    using ProcessResourceUsage = kernox::system::ProcessResourceUsage;
    using ProcessExecutionResult = kernox::system::ProcessExecutionResult;
    using ProcessStatInfo = kernox::system::ProcessStatInfo;
    using ProcessParser = kernox::system::ProcessParser;
    using ProcessManager = kernox::system::ProcessManager;
}
