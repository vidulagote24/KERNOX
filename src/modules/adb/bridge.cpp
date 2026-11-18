#include "kernox/modules/adb/bridge.hpp"
#include "kernox/core/timer.hpp"
#include "kernox/core/unique_fd.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace kernox::adb {

// =============================================================================
// SystemCommandRunner Implementation
// =============================================================================

bool SystemCommandRunner::is_executable_available(const std::string& executable) {
    if (executable.empty()) return false;

    // If it contains a slash, check direct path
    if (executable.find('/') != std::string::npos) {
        return ::access(executable.c_str(), X_OK) == 0;
    }

    // Otherwise, search PATH
    const char* path_env = std::getenv("PATH");
    if (!path_env) return false;

    std::string path_str(path_env);
    size_t start = 0;
    while (start < path_str.size()) {
        size_t end = path_str.find(':', start);
        if (end == std::string::npos) end = path_str.size();

        std::string dir = path_str.substr(start, end - start);
        if (!dir.empty()) {
            std::string candidate = dir + "/" + executable;
            if (::access(candidate.c_str(), X_OK) == 0) {
                return true;
            }
        }
        start = end + 1;
    }

    return false;
}

core::Result<CommandOutput> SystemCommandRunner::execute(
    const std::string& executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout
) {
    if (executable.empty()) {
        return core::Result<CommandOutput>::err(
            core::ErrorCode::InvalidArgument, "Executable path cannot be empty"
        );
    }

    core::Timer timer;

    // Create pipes for stdout and stderr
    auto stdout_pipe_res = core::UniqueFd::create_pipe();
    if (!stdout_pipe_res.is_ok()) {
        return core::Result<CommandOutput>::err(stdout_pipe_res.error());
    }
    auto [stdout_read, stdout_write] = std::move(stdout_pipe_res).value();

    auto stderr_pipe_res = core::UniqueFd::create_pipe();
    if (!stderr_pipe_res.is_ok()) {
        return core::Result<CommandOutput>::err(stderr_pipe_res.error());
    }
    auto [stderr_read, stderr_write] = std::move(stderr_pipe_res).value();

    // Prepare argument array in the parent BEFORE fork
    std::vector<std::string> full_args;
    full_args.reserve(arguments.size() + 1);
    full_args.push_back(executable);
    for (const auto& a : arguments) {
        full_args.push_back(a);
    }

    std::vector<char*> c_argv;
    c_argv.reserve(full_args.size() + 1);
    for (auto& s : full_args) {
        c_argv.push_back(s.data());
    }
    c_argv.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0) {
        return core::Result<CommandOutput>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fork() failed for command execution", errno)
        );
    }

    if (pid == 0) {
        // Child Process
        // Close read ends of pipes
        stdout_read.reset();
        stderr_read.reset();

        // Redirect stdout
        if (::dup2(stdout_write.get(), STDOUT_FILENO) < 0) {
            ::_exit(126);
        }
        // Redirect stderr
        if (::dup2(stderr_write.get(), STDERR_FILENO) < 0) {
            ::_exit(126);
        }

        stdout_write.reset();
        stderr_write.reset();

        ::execvp(executable.c_str(), c_argv.data());
        ::_exit(127);
    }

    // Parent Process
    // Close write ends immediately so child EOF can be detected
    stdout_write.reset();
    stderr_write.reset();

    // Set non-blocking on pipe read descriptors to prevent deadlocks
    auto set_nonblock = [](int fd) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    };
    set_nonblock(stdout_read.get());
    set_nonblock(stderr_read.get());

    CommandOutput output{};
    char buffer[4096];

    struct pollfd fds[2];
    fds[0].fd = stdout_read.get();
    fds[0].events = POLLIN | POLLHUP | POLLERR;
    fds[1].fd = stderr_read.get();
    fds[1].events = POLLIN | POLLHUP | POLLERR;

    bool stdout_open = true;
    bool stderr_open = true;

    auto start_time = std::chrono::steady_clock::now();

    while (stdout_open || stderr_open) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        );
        if (elapsed >= timeout) {
            output.timed_out = true;
            break;
        }

        int remaining_ms = static_cast<int>((timeout - elapsed).count());
        int poll_timeout = std::clamp(remaining_ms, 1, 100);

        int pr = ::poll(fds, 2, poll_timeout);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (pr == 0) {
            continue; // Poll tick timeout; loop around to check deadline
        }

        // Drain stdout
        if (stdout_open && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t bytes = 0;
            while ((bytes = ::read(stdout_read.get(), buffer, sizeof(buffer))) > 0) {
                output.stdout_content.append(buffer, static_cast<size_t>(bytes));
            }
            if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                stdout_open = false;
                fds[0].fd = -1;
            }
        }

        // Drain stderr
        if (stderr_open && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t bytes = 0;
            while ((bytes = ::read(stderr_read.get(), buffer, sizeof(buffer))) > 0) {
                output.stderr_content.append(buffer, static_cast<size_t>(bytes));
            }
            if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                stderr_open = false;
                fds[1].fd = -1;
            }
        }
    }

    if (output.timed_out) {
        // Escalate from SIGTERM to SIGKILL to ensure child is cleanly reaped
        ::kill(pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        int status = 0;
        pid_t wr = ::waitpid(pid, &status, WNOHANG);
        if (wr == 0) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
        }
        output.exit_code = -1;
    } else {
        int status = 0;
        pid_t wr = 0;
        do {
            wr = ::waitpid(pid, &status, 0);
        } while (wr < 0 && errno == EINTR);

        if (wr > 0) {
            if (WIFEXITED(status)) {
                output.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                output.exit_code = 128 + WTERMSIG(status);
            }
        }
    }

    output.duration = std::chrono::microseconds(timer.elapsed_microseconds());
    return core::Result<CommandOutput>::ok(std::move(output));
}

// =============================================================================
// MockCommandRunner Implementation
// =============================================================================

core::Result<CommandOutput> MockCommandRunner::execute(
    const std::string& executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout
) {
    (void)timeout;

    std::vector<std::string> call;
    call.reserve(arguments.size() + 1);
    call.push_back(executable);
    for (const auto& a : arguments) {
        call.push_back(a);
    }
    executed_calls_.push_back(call);

    // Build key from arguments
    std::string key;
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) key += " ";
        key += arguments[i];
    }

    for (const auto& r : responses_) {
        if (r.first == key || r.first == executable || (!key.empty() && key.find(r.first) != std::string::npos)) {
            return core::Result<CommandOutput>::ok(r.second);
        }
    }

    if (default_response_.has_value()) {
        return core::Result<CommandOutput>::ok(*default_response_);
    }

    // Default clean output
    CommandOutput def{};
    def.exit_code = 0;
    return core::Result<CommandOutput>::ok(def);
}

// =============================================================================
// AdbBridge Implementation
// =============================================================================

AdbBridge::AdbBridge(std::shared_ptr<ICommandRunner> runner, std::string adb_path)
    : runner_(runner ? std::move(runner) : std::make_shared<SystemCommandRunner>()),
      adb_path_(std::move(adb_path)) {}

bool AdbBridge::is_available() {
    return runner_->is_executable_available(adb_path_);
}

namespace {

bool contains_disallowed_characters(std::string_view token) noexcept {
    for (char ch : token) {
        const auto c = static_cast<unsigned char>(ch);
        // Reject control characters, whitespace, and non-ASCII
        if (c <= 32 || c >= 127) {
            return true;
        }
        // Reject shell metacharacters and dangerous tokens
        switch (ch) {
            case ';':
            case '&':
            case '|':
            case '>':
            case '<':
            case '$':
            case '`':
            case '\\':
            case '!':
            case '"':
            case '\'':
            case '(':
            case ')':
            case '{':
            case '}':
            case '[':
            case ']':
            case '*':
            case '?':
            case '~':
                return true;
            default:
                break;
        }
    }
    return false;
}

bool is_valid_serial(std::string_view serial) noexcept {
    if (serial.empty() || serial.size() > 64) {
        return false;
    }
    // Must not start with '-' (prevent option injection like -d, -e)
    if (serial.front() == '-') {
        return false;
    }
    for (char c : serial) {
        const bool is_alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        const bool is_sep = (c == '.' || c == '_' || c == ':' || c == '-');
        if (!is_alnum && !is_sep) {
            return false;
        }
    }
    return true;
}

bool is_valid_property_name(std::string_view prop) noexcept {
    if (prop.empty() || prop.size() > 128) {
        return false;
    }
    if (prop.front() == '-') {
        return false;
    }
    for (char c : prop) {
        const bool is_alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        const bool is_sep = (c == '.' || c == '_' || c == '-');
        if (!is_alnum && !is_sep) {
            return false;
        }
    }
    return true;
}

bool is_valid_package_or_flag(std::string_view target) noexcept {
    if (target.empty() || target.size() > 128) {
        return false;
    }
    if (target.front() == '-') {
        return target == "-c" || target == "-s" || target == "-d" || target == "--package";
    }
    for (char c : target) {
        const bool is_alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        const bool is_sep = (c == '.' || c == '_' || c == '-');
        if (!is_alnum && !is_sep) {
            return false;
        }
    }
    return true;
}

bool is_valid_ps_columns(std::string_view cols) noexcept {
    if (cols.empty() || cols.size() > 128) {
        return false;
    }
    if (cols.front() == ',' || cols.back() == ',') {
        return false;
    }
    for (char c : cols) {
        const bool is_alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        const bool is_sep = (c == ',' || c == '%' || c == '_' || c == '-');
        if (!is_alnum && !is_sep) {
            return false;
        }
    }
    return true;
}

} // namespace

bool AdbBridge::is_safe_read_only_command(const std::vector<std::string>& arguments) noexcept {
    if (arguments.empty()) {
        return false;
    }

    // 1. Sanity check: no empty tokens, no control characters, whitespace, or shell metacharacters
    for (const auto& arg : arguments) {
        if (arg.empty() || contains_disallowed_characters(arg)) {
            return false;
        }
    }

    // 2. Optional device-selection prefix: -s <serial>
    size_t idx = 0;
    if (arguments[0] == "-s") {
        if (arguments.size() < 3) {
            return false; // Missing serial or subcommand
        }
        if (!is_valid_serial(arguments[1])) {
            return false; // Malformed or invalid serial
        }
        idx = 2;
    }

    const size_t rem = arguments.size() - idx;
    if (rem == 0) {
        return false;
    }

    const std::string& cmd = arguments[idx];

    // Positive Allowlist Enforcement:

    // Form 1: adb devices [-l]
    if (cmd == "devices") {
        if (rem == 1) {
            return true;
        }
        if (rem == 2 && arguments[idx + 1] == "-l") {
            return true;
        }
        return false;
    }

    // Form 2: adb get-state
    if (cmd == "get-state") {
        return rem == 1;
    }

    // Forms 3-6: adb shell ...
    if (cmd == "shell") {
        if (rem < 2) {
            return false; // Reject interactive shell invocation
        }

        const std::string& shell_cmd = arguments[idx + 1];

        // Form 3: adb shell getprop [property]
        if (shell_cmd == "getprop") {
            if (rem == 2) {
                return true;
            }
            if (rem == 3) {
                return is_valid_property_name(arguments[idx + 2]);
            }
            return false;
        }

        // Form 4: adb shell dumpsys meminfo [package_or_flag] OR dumpsys activity processes
        if (shell_cmd == "dumpsys") {
            if (rem < 3) {
                return false;
            }
            const std::string& service = arguments[idx + 2];
            if (service == "meminfo") {
                if (rem == 3) {
                    return true;
                }
                if (rem == 4) {
                    return is_valid_package_or_flag(arguments[idx + 3]);
                }
                return false;
            }
            if (service == "activity") {
                return rem == 4 && arguments[idx + 3] == "processes";
            }
            return false;
        }

        // Form 5: adb shell cat /proc/meminfo
        if (shell_cmd == "cat") {
            return rem == 3 && arguments[idx + 2] == "/proc/meminfo";
        }

        // Form 6: adb shell ps [-A] [-A -o <columns>]
        if (shell_cmd == "ps") {
            if (rem == 2) {
                return true;
            }
            if (rem == 3 && arguments[idx + 2] == "-A") {
                return true;
            }
            if (rem == 5 && arguments[idx + 2] == "-A" && arguments[idx + 3] == "-o") {
                return is_valid_ps_columns(arguments[idx + 4]);
            }
            return false;
        }

        // All other shell commands rejected
        return false;
    }

    // All other ADB subcommands rejected
    return false;
}

core::Result<CommandOutput> AdbBridge::run_adb(
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout
) {
    if (!is_safe_read_only_command(arguments)) {
        return core::Result<CommandOutput>::err(
            core::ErrorCode::PermissionDenied,
            "Security Violation: Rejected mutating or destructive command. KERNOX ADB operations are strictly read-only."
        );
    }

    return runner_->execute(adb_path_, arguments, timeout);
}

core::Result<CommandOutput> AdbBridge::run_device_shell(
    const std::string& serial,
    const std::vector<std::string>& shell_args,
    std::chrono::milliseconds timeout
) {
    std::vector<std::string> args;
    args.reserve(shell_args.size() + 3);
    if (!serial.empty()) {
        args.push_back("-s");
        args.push_back(serial);
    }
    args.push_back("shell");
    for (const auto& a : shell_args) {
        args.push_back(a);
    }

    return run_adb(args, timeout);
}

core::Result<CommandOutput> AdbBridge::list_devices() {
    return run_adb({"devices", "-l"});
}

core::Result<CommandOutput> AdbBridge::get_state(const std::string& serial) {
    if (serial.empty()) {
        return run_adb({"get-state"});
    }
    return run_adb({"-s", serial, "get-state"});
}

core::Result<CommandOutput> AdbBridge::getprop(const std::string& serial, const std::string& property) {
    if (property.empty()) {
        return run_device_shell(serial, {"getprop"});
    }
    return run_device_shell(serial, {"getprop", property});
}

core::Result<CommandOutput> AdbBridge::cat_proc_meminfo(const std::string& serial) {
    return run_device_shell(serial, {"cat", "/proc/meminfo"});
}

core::Result<CommandOutput> AdbBridge::dumpsys_meminfo(const std::string& serial, const std::string& package) {
    if (package.empty()) {
        return run_device_shell(serial, {"dumpsys", "meminfo"});
    }
    return run_device_shell(serial, {"dumpsys", "meminfo", package});
}

core::Result<CommandOutput> AdbBridge::dumpsys_activity_processes(const std::string& serial) {
    return run_device_shell(serial, {"dumpsys", "activity", "processes"});
}

} // namespace kernox::adb
