#include "kernox/core/child_process.hpp"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace kernox::core {

Result<ChildProcess> ChildProcess::spawn(
    const std::string& executable,
    const std::vector<std::string>& arguments) {
    if (executable.empty()) {
        return Result<ChildProcess>::err(
            ErrorCode::InvalidArgument, "Executable path cannot be empty");
    }

    // Prepare argument array in the parent BEFORE forking
    // argv[0] is conventionally the executable name or path
    std::vector<std::string> full_args;
    full_args.reserve(arguments.size() + 1);
    full_args.push_back(executable);
    for (const auto& arg : arguments) {
        full_args.push_back(arg);
    }

    std::vector<char*> c_argv;
    c_argv.reserve(full_args.size() + 1);
    for (auto& s : full_args) {
        c_argv.push_back(s.data());
    }
    c_argv.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0) {
        return Result<ChildProcess>::err(
            Error::from_errno(ErrorCode::SystemError, "fork() failed", errno));
    }

    if (pid == 0) {
        // Child process execution
        // MUST NOT perform heap allocations, std::cout, or complex operations here.
        ::execvp(executable.c_str(), c_argv.data());

        // If execvp returns, an error occurred
        ::_exit(127);
    }

    // Parent process
    return Result<ChildProcess>::ok(ChildProcess(pid));
}

ChildProcess::~ChildProcess() noexcept {
    cleanup();
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : pid_(other.pid_), reaped_(other.reaped_), cached_status_(other.cached_status_) {
    other.pid_ = -1;
    other.reaped_ = true;
    other.cached_status_ = std::nullopt;
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        cleanup();
        pid_ = other.pid_;
        reaped_ = other.reaped_;
        cached_status_ = other.cached_status_;

        other.pid_ = -1;
        other.reaped_ = true;
        other.cached_status_ = std::nullopt;
    }
    return *this;
}

Result<std::optional<ProcessExitStatus>> ChildProcess::poll_status() {
    if (reaped_) {
        return Result<std::optional<ProcessExitStatus>>::ok(cached_status_);
    }
    if (pid_ <= 0) {
        return Result<std::optional<ProcessExitStatus>>::err(
            ErrorCode::InvalidState, "Invalid child process PID");
    }

    int raw_status = 0;
    pid_t result = 0;

    do {
        result = ::waitpid(pid_, &raw_status, WNOHANG);
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        // Process is still running
        return Result<std::optional<ProcessExitStatus>>::ok(std::nullopt);
    }

    if (result < 0) {
        if (errno == ECHILD) {
            // Already reaped elsewhere
            reaped_ = true;
            return Result<std::optional<ProcessExitStatus>>::ok(cached_status_);
        }
        return Result<std::optional<ProcessExitStatus>>::err(
            Error::from_errno(ErrorCode::SystemError, "waitpid(WNOHANG) failed", errno));
    }

    // Process completed
    ProcessExitStatus status{};
    if (WIFEXITED(raw_status)) {
        status.exited = true;
        status.exit_code = WEXITSTATUS(raw_status);
    } else if (WIFSIGNALED(raw_status)) {
        status.signaled = true;
        status.term_signal = WTERMSIG(raw_status);
    }

    reaped_ = true;
    cached_status_ = status;
    return Result<std::optional<ProcessExitStatus>>::ok(status);
}

Result<ProcessExitStatus> ChildProcess::wait() {
    if (reaped_) {
        if (cached_status_.has_value()) {
            return Result<ProcessExitStatus>::ok(*cached_status_);
        }
        return Result<ProcessExitStatus>::err(
            ErrorCode::InvalidState, "Process already reaped with unknown status");
    }
    if (pid_ <= 0) {
        return Result<ProcessExitStatus>::err(
            ErrorCode::InvalidState, "Invalid child process PID");
    }

    int raw_status = 0;
    pid_t result = 0;

    do {
        result = ::waitpid(pid_, &raw_status, 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        return Result<ProcessExitStatus>::err(
            Error::from_errno(ErrorCode::SystemError, "waitpid() failed", errno));
    }

    ProcessExitStatus status{};
    if (WIFEXITED(raw_status)) {
        status.exited = true;
        status.exit_code = WEXITSTATUS(raw_status);
    } else if (WIFSIGNALED(raw_status)) {
        status.signaled = true;
        status.term_signal = WTERMSIG(raw_status);
    }

    reaped_ = true;
    cached_status_ = status;
    return Result<ProcessExitStatus>::ok(status);
}

Result<ProcessExitStatus> ChildProcess::wait_for(std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        auto poll_res = poll_status();
        if (poll_res.is_err()) {
            return Result<ProcessExitStatus>::err(poll_res.error());
        }
        if (poll_res.value().has_value()) {
            return Result<ProcessExitStatus>::ok(*poll_res.value());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return Result<ProcessExitStatus>::err(
        ErrorCode::Timeout, "Process did not terminate within timeout");
}

Result<void> ChildProcess::send_signal(int signal_number) {
    if (reaped_ || pid_ <= 0) {
        return Result<void>::err(
            ErrorCode::InvalidState, "Cannot signal an inactive or reaped process");
    }

    if (::kill(pid_, signal_number) != 0) {
        return Result<void>::err(
            Error::from_errno(ErrorCode::SystemError, "kill() failed", errno));
    }
    return Result<void>::ok();
}

void ChildProcess::cleanup() noexcept {
    if (reaped_ || pid_ <= 0) {
        return;
    }

    // Non-blocking check if process has already terminated
    int raw_status = 0;
    pid_t res = ::waitpid(pid_, &raw_status, WNOHANG);
    if (res > 0 || (res < 0 && errno == ECHILD)) {
        reaped_ = true;
        return;
    }

    // Escalate: send SIGTERM first
    ::kill(pid_, SIGTERM);

    // Give child a brief window to exit gracefully
    for (int i = 0; i < 5; ++i) {
        res = ::waitpid(pid_, &raw_status, WNOHANG);
        if (res > 0) {
            reaped_ = true;
            return;
        }
        ::usleep(10000); // 10ms
    }

    // Escalation: force kill with SIGKILL to guarantee no zombies remain
    ::kill(pid_, SIGKILL);
    ::waitpid(pid_, &raw_status, 0);
    reaped_ = true;
}

} // namespace kernox::core
