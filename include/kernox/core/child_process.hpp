#pragma once

#include "kernox/core/result.hpp"

#include <chrono>
#include <string>
#include <sys/types.h>
#include <vector>

namespace kernox::core {

struct ProcessExitStatus {
    bool exited{false};
    int exit_code{-1};
    bool signaled{false};
    int term_signal{-1};

    [[nodiscard]] bool is_success() const noexcept {
        return exited && exit_code == 0;
    }
};

class ChildProcess {
public:
    // Spawns a child process executing the specified binary with arguments directly (no shell).
    // The argv arguments are prepared in the parent before fork().
    static Result<ChildProcess> spawn(
        const std::string& executable,
        const std::vector<std::string>& arguments);

    // Destructor reaps child process if still running to prevent zombies.
    // Escalates from SIGTERM to SIGKILL if the process does not terminate.
    ~ChildProcess() noexcept;

    // Non-copyable
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Move constructible & assignable
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;

    // Get the process ID
    [[nodiscard]] pid_t pid() const noexcept {
        return pid_;
    }

    // Check if the process handle is valid and active
    [[nodiscard]] bool is_valid() const noexcept {
        return pid_ > 0 && !reaped_;
    }

    // Non-blocking poll of the child process status
    Result<std::optional<ProcessExitStatus>> poll_status();

    // Blocking wait for the child process to finish
    Result<ProcessExitStatus> wait();

    // Controlled wait with a timeout in milliseconds
    Result<ProcessExitStatus> wait_for(std::chrono::milliseconds timeout);

    // Send a POSIX signal to the child process (e.g. SIGTERM, SIGKILL)
    Result<void> send_signal(int signal_number);

private:
    explicit ChildProcess(pid_t pid) noexcept : pid_(pid), reaped_(false) {}

    void cleanup() noexcept;

    pid_t pid_{-1};
    bool reaped_{true};
    std::optional<ProcessExitStatus> cached_status_{std::nullopt};
};

} // namespace kernox::core
