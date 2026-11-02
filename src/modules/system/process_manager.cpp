#include "kernox/modules/system/process_manager.hpp"
#include "kernox/core/unique_fd.hpp"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace kernox::system {

core::Result<void> ChildErrorPayload::validate() const {
    if (stage != ChildErrorStage::ResourceLimit && stage != ChildErrorStage::Exec) {
        return core::Result<void>::err(
            core::ErrorCode::SystemError,
            "Protocol error: unrecognized child error stage (" +
                std::to_string(static_cast<uint32_t>(stage)) + ")");
    }
    if (error_number <= 0) {
        return core::Result<void>::err(
            core::ErrorCode::SystemError,
            "Protocol error: invalid errno in child error payload (" +
                std::to_string(error_number) + ")");
    }
    return core::Result<void>::ok();
}

ChildCleanupResult ProcessManager::reap_child(
    pid_t pid,
    std::chrono::milliseconds grace,
    struct rusage* out_ru) noexcept {
    ChildCleanupResult res{};
    if (pid <= 0) {
        return res;
    }

    struct rusage dummy_ru{};
    struct rusage* ru_ptr = out_ru ? out_ru : &dummy_ru;
    int raw_status = 0;

    // Phase 1: Bounded poll with WNOHANG up to `grace` to see if child exits voluntarily
    auto start = std::chrono::steady_clock::now();
    bool child_still_running = true;

    while (true) {
        pid_t wr = 0;
        do {
            wr = ::wait4(pid, &raw_status, WNOHANG, ru_ptr);
        } while (wr < 0 && errno == EINTR);

        if (wr == pid) {
            res.reaped = true;
            res.status_valid = true;
            res.raw_status = raw_status;
            return res; // Reaped cleanly; never signal after successful reaping
        }
        if (wr < 0 && errno == ECHILD) {
            // Child was already reaped externally. Status is unavailable!
            // Do NOT treat ECHILD as proof of a valid exit status.
            res.reaped = true;
            res.status_valid = false;
            return res;
        }
        if (wr < 0) {
            // Unexpected wait4 error; do not assume child is reaped.
            child_still_running = true;
            break;
        }

        // wr == 0: child still running
        if (grace.count() == 0 || std::chrono::steady_clock::now() - start >= grace) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Phase 2: Child is still running or grace period expired.
    // Verify child is still waitable before signaling to prevent PID reuse race.
    if (child_still_running) {
        pid_t check_wr = 0;
        do {
            check_wr = ::wait4(pid, &raw_status, WNOHANG, ru_ptr);
        } while (check_wr < 0 && errno == EINTR);

        if (check_wr == pid) {
            // Exited just before signal; reaped without signaling!
            res.reaped = true;
            res.status_valid = true;
            res.raw_status = raw_status;
            return res;
        }
        if (check_wr < 0 && errno == ECHILD) {
            // Child was already reaped elsewhere; do NOT signal!
            res.reaped = true;
            res.status_valid = false;
            return res;
        }
        if (check_wr == 0) {
            // Child is confirmed alive, waitable, and not yet reaped.
            // Positively safe to signal SIGKILL without PID reuse risk.
            ::kill(pid, SIGKILL);

            // Bounded wait for termination after SIGKILL (max 500ms)
            auto kill_start = std::chrono::steady_clock::now();
            auto kill_timeout = std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() - kill_start <= kill_timeout) {
                pid_t kwr = 0;
                do {
                    kwr = ::wait4(pid, &raw_status, WNOHANG, ru_ptr);
                } while (kwr < 0 && errno == EINTR);

                if (kwr == pid) {
                    res.reaped = true;
                    res.status_valid = true;
                    res.raw_status = raw_status;
                    return res;
                }
                if (kwr < 0 && errno == ECHILD) {
                    res.reaped = true;
                    res.status_valid = false;
                    return res;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            // Kernel limitation: Child did not exit within timeout even after SIGKILL
            // (e.g. uninterruptible kernel sleep / D-state). We do NOT hang indefinitely.
            res.reaped = false;
            res.status_valid = false;
        } else {
            // check_wr < 0 && errno != ECHILD: wait4 itself returned unexpected system error.
            // Kernel/API limitation: cannot verify child state; do not signal to avoid signaling a recycled PID.
            res.reaped = false;
            res.status_valid = false;
        }
    }

    return res;
}



core::Result<void> ProcessLimits::validate() const {
    auto check_pair = [](const std::optional<LimitSetting>& setting, const char* name) -> core::Result<void> {
        if (!setting.has_value()) {
            return core::Result<void>::ok();
        }
        if (setting->soft > setting->hard) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                std::string("Resource limit '") + name + "' soft limit exceeds hard limit (" +
                std::to_string(setting->soft) + " > " + std::to_string(setting->hard) + ")");
        }
        return core::Result<void>::ok();
    };

    auto res = check_pair(cpu_time_seconds, "cpu_time_seconds");
    if (res.is_err()) {
        return res;
    }
    res = check_pair(address_space_bytes, "address_space_bytes");
    if (res.is_err()) {
        return res;
    }
    res = check_pair(max_open_files, "max_open_files");
    if (res.is_err()) {
        return res;
    }
    res = check_pair(max_file_size_bytes, "max_file_size_bytes");
    if (res.is_err()) {
        return res;
    }

    if (max_open_files.has_value() && max_open_files->soft == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Resource limit 'max_open_files' soft limit cannot be 0");
    }

    return core::Result<void>::ok();
}

core::Result<ProcessStatInfo> ProcessParser::parse_proc_pid_stat(std::string_view content) {
    if (content.empty()) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "stat content is empty");
    }

    // Process name (comm) is enclosed in parentheses and may contain spaces and parentheses.
    // Invariant: comm starts after the first '(' and ends before the last ')'.
    size_t open_paren = content.find('(');
    size_t close_paren = content.rfind(')');
    if (open_paren == std::string_view::npos || close_paren == std::string_view::npos ||
        close_paren <= open_paren) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Malformed /proc/<pid>/stat: missing comm parentheses");
    }

    // Field 1: PID before open_paren
    std::string_view pid_sv = content.substr(0, open_paren);
    while (!pid_sv.empty() && std::isspace(static_cast<unsigned char>(pid_sv.front()))) {
        pid_sv.remove_prefix(1);
    }
    while (!pid_sv.empty() && std::isspace(static_cast<unsigned char>(pid_sv.back()))) {
        pid_sv.remove_suffix(1);
    }
    if (pid_sv.empty()) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Malformed /proc/<pid>/stat: missing PID");
    }

    ProcessStatInfo info{};
    char* endptr = nullptr;
    std::string pid_str(pid_sv);
    long parsed_pid = std::strtol(pid_str.c_str(), &endptr, 10);
    if (*endptr != '\0' || parsed_pid <= 0) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Invalid PID in /proc/<pid>/stat");
    }
    info.pid = static_cast<pid_t>(parsed_pid);

    // Field 2: comm
    info.comm = std::string(content.substr(open_paren + 1, close_paren - (open_paren + 1)));

    // Fields 3+: tokenize text following close_paren
    std::string_view after_paren = content.substr(close_paren + 1);
    std::vector<std::string_view> tokens;
    tokens.reserve(32);
    size_t idx = 0;
    while (idx < after_paren.size()) {
        while (idx < after_paren.size() && std::isspace(static_cast<unsigned char>(after_paren[idx]))) {
            ++idx;
        }
        if (idx >= after_paren.size()) {
            break;
        }
        size_t tok_start = idx;
        while (idx < after_paren.size() && !std::isspace(static_cast<unsigned char>(after_paren[idx]))) {
            ++idx;
        }
        tokens.push_back(after_paren.substr(tok_start, idx - tok_start));
    }

    // We require through field 24 (RSS), which is index 21 of tokens (field 3 is index 0)
    if (tokens.size() < 22) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument,
            "Insufficient fields in /proc/<pid>/stat (expected at least 24 fields)");
    }

    // Token 0 -> Field 3: state
    if (tokens[0].empty()) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Missing state in /proc/<pid>/stat");
    }
    info.state = tokens[0].front();

    auto parse_long = [](std::string_view tok, long& out) -> bool {
        std::string s(tok);
        char* ep = nullptr;
        out = std::strtol(s.c_str(), &ep, 10);
        return *ep == '\0';
    };

    auto parse_ulong = [](std::string_view tok, unsigned long long& out) -> bool {
        std::string s(tok);
        char* ep = nullptr;
        out = std::strtoull(s.c_str(), &ep, 10);
        return *ep == '\0';
    };

    // Token 1 -> Field 4: ppid
    long ppid_val = 0;
    if (!parse_long(tokens[1], ppid_val)) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Invalid ppid in /proc/<pid>/stat");
    }
    info.ppid = static_cast<pid_t>(ppid_val);

    // Token 11 -> Field 14: utime
    unsigned long long utime_val = 0;
    if (!parse_ulong(tokens[11], utime_val)) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Invalid utime in /proc/<pid>/stat");
    }
    info.utime_ticks = utime_val;

    // Token 12 -> Field 15: stime
    unsigned long long stime_val = 0;
    if (!parse_ulong(tokens[12], stime_val)) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Invalid stime in /proc/<pid>/stat");
    }
    info.stime_ticks = stime_val;

    // Token 20 -> Field 23: vsize
    unsigned long long vsize_val = 0;
    if (!parse_ulong(tokens[20], vsize_val)) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Invalid vsize in /proc/<pid>/stat");
    }
    info.vsize_bytes = vsize_val;

    // Token 21 -> Field 24: rss
    long rss_val = 0;
    if (!parse_long(tokens[21], rss_val)) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Invalid rss in /proc/<pid>/stat");
    }
    info.rss_pages = rss_val;

    return core::Result<ProcessStatInfo>::ok(info);
}

core::Result<ProcessStatInfo> ProcessManager::read_process_stat(
    pid_t pid,
    const std::string& proc_dir) {
    if (pid <= 0) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::InvalidArgument, "Invalid process ID");
    }

    std::string path = proc_dir + "/" + std::to_string(pid) + "/stat";
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::NotFound, "Process with PID " + std::to_string(pid) + " not found");
    }

    std::string line;
    if (!std::getline(ifs, line)) {
        return core::Result<ProcessStatInfo>::err(
            core::ErrorCode::IoError, "Failed to read from " + path);
    }

    return ProcessParser::parse_proc_pid_stat(line);
}

core::Result<ProcessExecutionResult> ProcessManager::run(const ProcessConfig& config) {
    if (config.executable.empty()) {
        return core::Result<ProcessExecutionResult>::err(
            core::ErrorCode::InvalidArgument, "Executable path cannot be empty");
    }

    auto limits_val = config.limits.validate();
    if (limits_val.is_err()) {
        return core::Result<ProcessExecutionResult>::err(limits_val.error());
    }

    // Build argument list before fork
    std::vector<std::string> full_args;
    full_args.reserve(config.arguments.size() + 1);
    full_args.push_back(config.executable);
    for (const auto& arg : config.arguments) {
        full_args.push_back(arg);
    }

    std::vector<char*> c_argv;
    c_argv.reserve(full_args.size() + 1);
    for (auto& s : full_args) {
        c_argv.push_back(s.data());
    }
    c_argv.push_back(nullptr);

    // Build environment list if explicitly specified
    std::vector<std::string> env_copy;
    std::vector<char*> c_envp;
    if (!config.environment.empty()) {
        env_copy = config.environment;
        c_envp.reserve(env_copy.size() + 1);
        for (auto& s : env_copy) {
            c_envp.push_back(s.data());
        }
        c_envp.push_back(nullptr);
    }

    // Create CLOEXEC pipe for error reporting
    auto pipe_res = core::UniqueFd::create_pipe();
    if (pipe_res.is_err()) {
        return core::Result<ProcessExecutionResult>::err(pipe_res.error());
    }
    core::UniqueFd read_fd = std::move(pipe_res.value().first);
    core::UniqueFd write_fd = std::move(pipe_res.value().second);

    auto start_time = std::chrono::steady_clock::now();
    pid_t pid = ::fork();
    if (pid < 0) {
        return core::Result<ProcessExecutionResult>::err(
            core::Error::from_errno(core::ErrorCode::SystemError, "fork() failed", errno));
    }

    if (pid == 0) {
        // Child process
        read_fd.reset();

        auto report_and_exit = [&](ChildErrorStage stage, int err) noexcept {
            ChildErrorPayload payload{stage, err};
            const char* buf = reinterpret_cast<const char*>(&payload);
            size_t written = 0;
            while (written < sizeof(payload)) {
                ssize_t w = ::write(write_fd.get(), buf + written, sizeof(payload) - written);
                if (w > 0) {
                    written += static_cast<size_t>(w);
                } else if (w < 0 && errno == EINTR) {
                    continue;
                } else {
                    break;
                }
            }
            ::_exit(127);
        };

        auto apply_limit = [&](int resource, const std::optional<LimitSetting>& setting) noexcept {
            if (!setting.has_value()) {
                return true;
            }
            struct rlimit rlim{};
            rlim.rlim_cur = static_cast<rlim_t>(setting->soft);
            rlim.rlim_max = static_cast<rlim_t>(setting->hard);
            return ::setrlimit(resource, &rlim) == 0;
        };

        if (!apply_limit(RLIMIT_CPU, config.limits.cpu_time_seconds)) {
            report_and_exit(ChildErrorStage::ResourceLimit, errno);
        }
        if (!apply_limit(RLIMIT_AS, config.limits.address_space_bytes)) {
            report_and_exit(ChildErrorStage::ResourceLimit, errno);
        }
        if (!apply_limit(RLIMIT_NOFILE, config.limits.max_open_files)) {
            report_and_exit(ChildErrorStage::ResourceLimit, errno);
        }
        if (!apply_limit(RLIMIT_FSIZE, config.limits.max_file_size_bytes)) {
            report_and_exit(ChildErrorStage::ResourceLimit, errno);
        }

        if (c_envp.empty()) {
            ::execvp(config.executable.c_str(), c_argv.data());
        } else {
            if (config.executable.find('/') != std::string::npos) {
                ::execve(config.executable.c_str(), c_argv.data(), c_envp.data());
            } else {
                ::execvpe(config.executable.c_str(), c_argv.data(), c_envp.data());
            }
        }

        report_and_exit(ChildErrorStage::Exec, errno);
    }

    // Parent process: close write end of pipe immediately
    write_fd.reset();

    ChildErrorPayload err_payload{};
    char* err_buf = reinterpret_cast<char*>(&err_payload);
    size_t total_read = 0;
    bool read_error = false;
    int read_errno = 0;

    while (total_read < sizeof(err_payload)) {
        ssize_t n = ::read(read_fd.get(), err_buf + total_read, sizeof(err_payload) - total_read);
        if (n > 0) {
            total_read += static_cast<size_t>(n);
        } else if (n == 0) {
            // EOF reached (pipe closed on exec or exit)
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            read_error = true;
            read_errno = errno;
            break;
        }
    }
    read_fd.reset();

    if (read_error) {
        auto cleanup = reap_child(pid, std::chrono::milliseconds(0));
        std::string msg = "Failed to read from child process error-reporting pipe";
        if (!cleanup.reaped) {
            msg += " (warning: failed to reap child process)";
        }
        return core::Result<ProcessExecutionResult>::err(
            core::Error::from_errno(
                core::ErrorCode::IoError,
                msg,
                read_errno));
    }

    if (total_read > 0 && total_read < sizeof(err_payload)) {
        auto cleanup = reap_child(pid, std::chrono::milliseconds(0));
        std::string msg = "Truncated or malformed error payload received from child process";
        if (!cleanup.reaped) {
            msg += " (warning: failed to reap child process)";
        }
        return core::Result<ProcessExecutionResult>::err(
            core::ErrorCode::SystemError,
            msg);
    }

    if (total_read == sizeof(err_payload)) {
        // Child failed setup or exec; reap child and return structured error
        auto cleanup = reap_child(pid, std::chrono::milliseconds(50));

        // Validate payload field by field
        auto val_res = err_payload.validate();
        if (val_res.is_err()) {
            std::string msg = val_res.error().message;
            if (!cleanup.reaped) {
                msg += " (warning: failed to reap child process)";
            }
            return core::Result<ProcessExecutionResult>::err(
                core::ErrorCode::SystemError,
                msg);
        }

        if (!cleanup.reaped) {
            return core::Result<ProcessExecutionResult>::err(
                core::ErrorCode::SystemError,
                "Child process error reported but child could not be reaped");
        }


        if (err_payload.stage == ChildErrorStage::ResourceLimit) {
            return core::Result<ProcessExecutionResult>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    "Failed to apply resource limit in child process",
                    err_payload.error_number));
        }

        if (err_payload.error_number == ENOENT) {
            return core::Result<ProcessExecutionResult>::err(
                core::Error::from_errno(
                    core::ErrorCode::NotFound,
                    "Executable not found",
                    ENOENT));
        }
        if (err_payload.error_number == EACCES) {
            return core::Result<ProcessExecutionResult>::err(
                core::Error::from_errno(
                    core::ErrorCode::PermissionDenied,
                    "Permission denied executing binary",
                    EACCES));
        }
        return core::Result<ProcessExecutionResult>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "Failed to execute process",
                err_payload.error_number));
    }

    // Child exec succeeded. Monitor lifecycle and collect rusage with wait4.
    ProcessExecutionResult result{};
    result.pid = pid;
    struct rusage ru{};
    std::memset(&ru, 0, sizeof(ru));
    int raw_status = 0;
    bool status_valid = false;

    auto populate_rusage = [](const struct rusage& r, ProcessResourceUsage& u) {
        u.user_cpu_time_us = static_cast<int64_t>(r.ru_utime.tv_sec) * 1000000 + r.ru_utime.tv_usec;
        u.system_cpu_time_us = static_cast<int64_t>(r.ru_stime.tv_sec) * 1000000 + r.ru_stime.tv_usec;
        u.max_rss_kb = static_cast<int64_t>(r.ru_maxrss);
        u.minor_page_faults = static_cast<int64_t>(r.ru_minflt);
        u.major_page_faults = static_cast<int64_t>(r.ru_majflt);
        u.voluntary_context_switches = static_cast<int64_t>(r.ru_nvcsw);
        u.involuntary_context_switches = static_cast<int64_t>(r.ru_nivcsw);
    };

    auto populate_status = [](int status, ProcessExecutionResult& res) {
        if (WIFEXITED(status)) {
            res.exited = true;
            res.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            res.signaled = true;
            res.term_signal = WTERMSIG(status);
        }
    };

    if (!config.timeout.has_value()) {
        pid_t wr = 0;
        do {
            wr = ::wait4(pid, &raw_status, 0, &ru);
        } while (wr < 0 && errno == EINTR);

        if (wr < 0) {
            int err = errno;
            if (err == ECHILD) {
                return core::Result<ProcessExecutionResult>::err(
                    core::ErrorCode::InvalidState,
                    "wait4() failed: child process does not exist or was already reaped (ECHILD)");
            }
            // For other unexpected errors, perform safe cleanup where child is known to be waitable
            auto cleanup = reap_child(pid, std::chrono::milliseconds(0));
            std::string msg = "wait4() failed";
            if (!cleanup.reaped) {
                msg += " (warning: child process could not be confirmed reaped; kernel wait4 error)";
            }
            return core::Result<ProcessExecutionResult>::err(
                core::Error::from_errno(core::ErrorCode::SystemError, msg, err));
        }
        if (wr != pid) {
            auto cleanup = reap_child(pid, std::chrono::milliseconds(0));
            std::string msg = "wait4() returned unexpected PID: " + std::to_string(wr) + ", expected: " + std::to_string(pid);
            if (!cleanup.reaped) {
                msg += " (warning: child process could not be confirmed reaped)";
            }
            return core::Result<ProcessExecutionResult>::err(
                core::ErrorCode::SystemError,
                msg);
        }

        auto end_time = std::chrono::steady_clock::now();
        result.wall_clock_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        populate_status(raw_status, result);
        populate_rusage(ru, result.resource_usage);
        return core::Result<ProcessExecutionResult>::ok(result);
    }

    // Monitored execution with timeout
    bool child_reaped = false;
    while (std::chrono::steady_clock::now() - start_time < *config.timeout) {
        pid_t wr = 0;
        do {
            wr = ::wait4(pid, &raw_status, WNOHANG, &ru);
        } while (wr < 0 && errno == EINTR);

        if (wr == pid) {
            child_reaped = true;
            status_valid = true;
            break;
        }
        if (wr == 0) {
            // Child is still running normally, continue waiting
        } else if (wr < 0) {
            int err = errno;
            if (err == ECHILD) {
                return core::Result<ProcessExecutionResult>::err(
                    core::ErrorCode::InvalidState,
                    "Child process was unexpectedly reaped or does not exist (ECHILD)");
            }
            // Unexpected wait4 error: terminate child safely without unbounded wait or PID reuse race
            auto cleanup = reap_child(pid, std::chrono::milliseconds(0));
            std::string msg = "wait4() failed during execution monitoring";
            if (!cleanup.reaped) {
                msg += " (warning: child cleanup incomplete)";
            }
            return core::Result<ProcessExecutionResult>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    msg,
                    err));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (child_reaped) {
        if (!status_valid) {
            return core::Result<ProcessExecutionResult>::err(
                core::ErrorCode::SystemError,
                "Child process reaped without valid status collection");
        }
        auto end_time = std::chrono::steady_clock::now();
        result.wall_clock_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        populate_status(raw_status, result);
        populate_rusage(ru, result.resource_usage);
        result.timed_out = false;
        return core::Result<ProcessExecutionResult>::ok(result);
    }

    // Timeout expired! Initiate graceful shutdown with SIGTERM
    result.timed_out = true;
    ::kill(pid, SIGTERM);

    auto term_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - term_time < config.grace_period) {
        pid_t wr = 0;
        do {
            wr = ::wait4(pid, &raw_status, WNOHANG, &ru);
        } while (wr < 0 && errno == EINTR);

        if (wr == pid) {
            child_reaped = true;
            status_valid = true;
            break;
        }
        if (wr == 0) {
            // Child still running
        } else if (wr < 0) {
            int err = errno;
            if (err == ECHILD) {
                return core::Result<ProcessExecutionResult>::err(
                    core::ErrorCode::InvalidState,
                    "Child process was unexpectedly reaped or does not exist during grace period (ECHILD)");
            }
            // Unexpected wait4 error: terminate child safely without unbounded wait or PID reuse race
            auto cleanup = reap_child(pid, std::chrono::milliseconds(0));
            std::string msg = "wait4() failed during grace period polling";
            if (!cleanup.reaped) {
                msg += " (warning: child cleanup incomplete)";
            }
            return core::Result<ProcessExecutionResult>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    msg,
                    err));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!child_reaped) {
        // Grace period expired without exit; escalate to SIGKILL with safe bounded wait
        auto cleanup = reap_child(pid, std::chrono::milliseconds(0), &ru);
        if (cleanup.reaped && cleanup.status_valid) {
            child_reaped = true;
            status_valid = true;
            raw_status = cleanup.raw_status;
        } else if (cleanup.reaped && !cleanup.status_valid) {
            return core::Result<ProcessExecutionResult>::err(
                core::ErrorCode::InvalidState,
                "Child process was unexpectedly reaped or does not exist during SIGKILL escalation (ECHILD)");
        } else {
            return core::Result<ProcessExecutionResult>::err(
                core::ErrorCode::SystemError,
                "wait4() failed during SIGKILL reaping: process did not exit within timeout");
        }
    }

    if (!child_reaped || !status_valid) {
        return core::Result<ProcessExecutionResult>::err(
            core::ErrorCode::SystemError,
            "Failed to collect valid exit status from child process");
    }

    auto end_time = std::chrono::steady_clock::now();
    result.wall_clock_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    populate_status(raw_status, result);
    populate_rusage(ru, result.resource_usage);
    return core::Result<ProcessExecutionResult>::ok(result);
}

} // namespace kernox::system
