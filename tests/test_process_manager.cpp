#include "kernox/modules/system/process_manager.hpp"
#include "kernox/cli/command.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <csignal>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace kernox::system;
using namespace kernox::core;

static std::string find_binary(const std::string& name) {
    std::vector<std::string> search_paths = {"/usr/bin/" + name, "/bin/" + name};
    for (const auto& path : search_paths) {
        if (::access(path.c_str(), X_OK) == 0) {
            return path;
        }
    }
    return name;
}

static std::string find_test_helper(const std::string& name) {
    char buf[1024];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string exe_path(buf);
        size_t last_slash = exe_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string candidate = exe_path.substr(0, last_slash + 1) + name;
            if (::access(candidate.c_str(), X_OK) == 0) {
                return candidate;
            }
        }
    }
    std::vector<std::string> search_paths = {
        "./" + name,
        "./tests/" + name,
        "../tests/" + name,
        name
    };
    for (const auto& path : search_paths) {
        if (::access(path.c_str(), X_OK) == 0) {
            return path;
        }
    }
    return name;
}

static size_t count_open_fds() {
    size_t count = 0;
    DIR* dir = ::opendir("/proc/self/fd");
    if (!dir) {
        return 0;
    }
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    ::closedir(dir);
    return count;
}

int main() {
    std::string true_bin = find_binary("true");
    std::string false_bin = find_binary("false");
    std::string echo_bin = find_binary("echo");
    std::string sleep_bin = find_binary("sleep");
    std::string sh_bin = find_binary("sh");

    // =========================================================================
    // 1. Launch behavior
    // =========================================================================

    // 1.1 Child that exits successfully
    {
        ProcessConfig config;
        config.executable = true_bin;
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& exec_res = res.value();
        KERNOX_ASSERT_TRUE(exec_res.pid > 0);
        KERNOX_ASSERT_TRUE(exec_res.exited);
        KERNOX_ASSERT_EQ(exec_res.exit_code, 0);
        KERNOX_ASSERT_FALSE(exec_res.signaled);
        KERNOX_ASSERT_FALSE(exec_res.timed_out);
        KERNOX_ASSERT_TRUE(exec_res.is_success());
    }

    // 1.2 Child that exits with nonzero status
    {
        ProcessConfig config;
        config.executable = false_bin;
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& exec_res = res.value();
        KERNOX_ASSERT_TRUE(exec_res.pid > 0);
        KERNOX_ASSERT_TRUE(exec_res.exited);
        KERNOX_ASSERT_NE(exec_res.exit_code, 0);
        KERNOX_ASSERT_FALSE(exec_res.is_success());
    }

    // 1.3 Executable that does not exist
    {
        ProcessConfig config;
        config.executable = "/nonexistent/binary_path_kernox_12345";
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::NotFound);
    }

    // 1.4 Executable or argument containing spaces
    {
        ProcessConfig config;
        config.executable = echo_bin;
        config.arguments = {"hello world", "kernox process manager test"};
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.value().is_success());
    }

    // 1.5 Arguments containing shell metacharacters (must remain literal argument data)
    {
        ProcessConfig config;
        config.executable = echo_bin;
        config.arguments = {"; rm -rf /; $(whoami); | > < & * $VAR `cmd`"};
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.value().is_success());
    }

    // 1.6 Explicit child environment configuration (ProcessConfig::environment)
    {
        std::string helper_bin = find_test_helper("env_test_helper");
        // Positive check: variable exists and matches exact value -> helper exits with code 0
        {
            ProcessConfig config;
            config.executable = helper_bin;
            config.arguments = {"KERNOX_CUSTOM_ENV", "ACTIVE_VALUE"};
            config.environment = {"KERNOX_CUSTOM_ENV=ACTIVE_VALUE", "OTHER_SETTING=1"};
            auto res = ProcessManager::run(config);
            KERNOX_ASSERT_TRUE(res.is_ok());
            KERNOX_ASSERT_TRUE(res.value().is_success());
            KERNOX_ASSERT_EQ(res.value().exit_code, 0);
        }

        // Exact value verification: variable exists but value differs -> helper exits with code 4
        {
            ProcessConfig config;
            config.executable = helper_bin;
            config.arguments = {"KERNOX_CUSTOM_ENV", "INCORRECT_VALUE"};
            config.environment = {"KERNOX_CUSTOM_ENV=ACTIVE_VALUE", "OTHER_SETTING=1"};
            auto res = ProcessManager::run(config);
            KERNOX_ASSERT_TRUE(res.is_ok());
            KERNOX_ASSERT_FALSE(res.value().is_success());
            KERNOX_ASSERT_EQ(res.value().exit_code, 4);
        }

        // Negative check: variable not in explicit environment -> helper exits with code 3
        {
            ProcessConfig config;
            config.executable = helper_bin;
            config.arguments = {"NONEXISTENT_VAR_KERNOX", "ACTIVE_VALUE"};
            config.environment = {"KERNOX_CUSTOM_ENV=ACTIVE_VALUE"};
            auto res = ProcessManager::run(config);
            KERNOX_ASSERT_TRUE(res.is_ok());
            KERNOX_ASSERT_FALSE(res.value().is_success());
            KERNOX_ASSERT_EQ(res.value().exit_code, 3);
        }
    }


    // =========================================================================
    // 2. Timeout and cleanup
    // =========================================================================

    // 2.1 Short-lived child that finishes before timeout
    {
        ProcessConfig config;
        config.executable = true_bin;
        config.timeout = std::chrono::milliseconds(2000);
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_FALSE(res.value().timed_out);
        KERNOX_ASSERT_TRUE(res.value().is_success());
    }

    // 2.2 Child that exceeds timeout (SIGTERM escalation)
    {
        ProcessConfig config;
        config.executable = sleep_bin;
        config.arguments = {"5"};
        config.timeout = std::chrono::milliseconds(50);
        config.grace_period = std::chrono::milliseconds(50);
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.value().timed_out);
        KERNOX_ASSERT_FALSE(res.value().is_success());
        KERNOX_ASSERT_TRUE(res.value().signaled);
        KERNOX_ASSERT_TRUE(res.value().term_signal == SIGTERM || res.value().term_signal == SIGKILL);
    }

    // 2.3 Repeated launches without descriptor leaks
    {
        size_t initial_fds = count_open_fds();
        for (int i = 0; i < 20; ++i) {
            ProcessConfig config;
            config.executable = true_bin;
            auto res = ProcessManager::run(config);
            KERNOX_ASSERT_TRUE(res.is_ok());
        }
        size_t final_fds = count_open_fds();
        KERNOX_ASSERT_EQ(initial_fds, final_fds);
    }

    // =========================================================================
    // 3. Resource limits
    // =========================================================================

    // 3.1 Validation: soft limit > hard limit rejected
    {
        ProcessLimits limits;
        limits.cpu_time_seconds = LimitSetting(10, 5); // soft=10, hard=5 -> invalid
        auto val_res = limits.validate();
        KERNOX_ASSERT_TRUE(val_res.is_err());
        KERNOX_ASSERT_EQ(val_res.error().code, ErrorCode::InvalidArgument);
    }

    // 3.2 Validation: max_open_files soft=0 rejected
    {
        ProcessLimits limits;
        limits.max_open_files = LimitSetting(0, 10);
        auto val_res = limits.validate();
        KERNOX_ASSERT_TRUE(val_res.is_err());
        KERNOX_ASSERT_EQ(val_res.error().code, ErrorCode::InvalidArgument);
    }

    // 3.3 Harmless child with valid limits applied
    {
        ProcessConfig config;
        config.executable = true_bin;
        config.limits.cpu_time_seconds = LimitSetting(10, 10);
        config.limits.max_file_size_bytes = LimitSetting(1048576, 1048576);
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.value().is_success());
    }

    // =========================================================================
    // 4. Process results and wait4 accounting
    // =========================================================================
    {
        ProcessConfig config;
        config.executable = true_bin;
        auto res = ProcessManager::run(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_TRUE(r.wall_clock_duration.count() >= 0);
        KERNOX_ASSERT_TRUE(r.resource_usage.minor_page_faults >= 0);
        KERNOX_ASSERT_TRUE(r.resource_usage.voluntary_context_switches >= 0);
        KERNOX_ASSERT_TRUE(r.resource_usage.involuntary_context_switches >= 0);
    }

    // =========================================================================
    // 5. Process Parser (/proc/<pid>/stat)
    // =========================================================================

    // 5.1 Standard stat line
    {
        std::string sample = "3016 (cat) R 3015 3016 3015 0 -1 4194304 88 0 0 0 12 34 0 0 20 0 1 0 148154637 5689344 266 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0";
        auto stat_res = ProcessParser::parse_proc_pid_stat(sample);
        KERNOX_ASSERT_TRUE(stat_res.is_ok());
        const auto& s = stat_res.value();
        KERNOX_ASSERT_EQ(s.pid, 3016);
        KERNOX_ASSERT_EQ(s.comm, "cat");
        KERNOX_ASSERT_EQ(s.state, 'R');
        KERNOX_ASSERT_EQ(s.ppid, 3015);
        KERNOX_ASSERT_EQ(s.utime_ticks, 12ULL);
        KERNOX_ASSERT_EQ(s.stime_ticks, 34ULL);
        KERNOX_ASSERT_EQ(s.vsize_bytes, 5689344ULL);
        KERNOX_ASSERT_EQ(s.rss_pages, 266);
    }

    // 5.2 Comm with spaces and nested parentheses
    {
        std::string sample = "1234 (my (cool) process name) S 100 200 300 0 -1 4194304 88 0 0 0 5 10 0 0 20 0 1 0 1000 8192000 512 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0";
        auto stat_res = ProcessParser::parse_proc_pid_stat(sample);
        KERNOX_ASSERT_TRUE(stat_res.is_ok());
        const auto& s = stat_res.value();
        KERNOX_ASSERT_EQ(s.pid, 1234);
        KERNOX_ASSERT_EQ(s.comm, "my (cool) process name");
        KERNOX_ASSERT_EQ(s.state, 'S');
        KERNOX_ASSERT_EQ(s.ppid, 100);
        KERNOX_ASSERT_EQ(s.vsize_bytes, 8192000ULL);
        KERNOX_ASSERT_EQ(s.rss_pages, 512);
    }

    // 5.3 Malformed stat lines
    {
        KERNOX_ASSERT_TRUE(ProcessParser::parse_proc_pid_stat("").is_err());
        KERNOX_ASSERT_TRUE(ProcessParser::parse_proc_pid_stat("1234 no_parentheses").is_err());
        KERNOX_ASSERT_TRUE(ProcessParser::parse_proc_pid_stat("(comm_only)").is_err());
        KERNOX_ASSERT_TRUE(ProcessParser::parse_proc_pid_stat("1234 (test) S").is_err()); // insufficient fields
    }

    // 5.4 Live read of self PID
    {
        pid_t my_pid = ::getpid();
        auto stat_res = ProcessManager::read_process_stat(my_pid);
        KERNOX_ASSERT_TRUE(stat_res.is_ok());
        KERNOX_ASSERT_EQ(stat_res.value().pid, my_pid);
    }

    // 5.5 Non-existent PID
    {
        auto stat_res = ProcessManager::read_process_stat(99999999);
        KERNOX_ASSERT_TRUE(stat_res.is_err());
        KERNOX_ASSERT_EQ(stat_res.error().code, ErrorCode::NotFound);
    }

    // =========================================================================
    // 6. CLI Integration tests
    // =========================================================================
    {
        // Help
        KERNOX_ASSERT_EQ(kernox::cli::CommandDispatcher::run({"process", "--help"}), 0);

        // Run successful
        KERNOX_ASSERT_EQ(kernox::cli::CommandDispatcher::run({"process", "run", true_bin}), 0);

        // Run failure
        KERNOX_ASSERT_NE(kernox::cli::CommandDispatcher::run({"process", "run", false_bin}), 0);

        // Missing executable
        KERNOX_ASSERT_NE(kernox::cli::CommandDispatcher::run({"process", "run"}), 0);

        // Nonexistent executable
        KERNOX_ASSERT_NE(kernox::cli::CommandDispatcher::run({"process", "run", "/nonexistent_bin_12345"}), 0);

        // Invalid option
        KERNOX_ASSERT_NE(kernox::cli::CommandDispatcher::run({"process", "run", "--unknown-opt", true_bin}), 0);

        // Timeout via CLI
        KERNOX_ASSERT_EQ(kernox::cli::CommandDispatcher::run({"process", "run", "--timeout", "50", sleep_bin, "2"}), 124);

        // Stat PID 1 (init / systemd / wslinit)
        KERNOX_ASSERT_EQ(kernox::cli::CommandDispatcher::run({"process", "stat", "1"}), 0);

        // Stat invalid PID
        KERNOX_ASSERT_NE(kernox::cli::CommandDispatcher::run({"process", "stat", "invalid_pid"}), 0);
    }

    // =========================================================================
    // 7. Protocol and ChildErrorPayload validation tests
    // =========================================================================
    {
        // 7.1 Valid payloads
        {
            ChildErrorPayload p1{ChildErrorStage::ResourceLimit, EPERM};
            KERNOX_ASSERT_TRUE(p1.validate().is_ok());

            ChildErrorPayload p2{ChildErrorStage::Exec, ENOENT};
            KERNOX_ASSERT_TRUE(p2.validate().is_ok());
        }

        // 7.2 Invalid stages
        {
            ChildErrorPayload p_zero_stage{static_cast<ChildErrorStage>(0), ENOENT};
            auto res = p_zero_stage.validate();
            KERNOX_ASSERT_TRUE(res.is_err());
            KERNOX_ASSERT_EQ(res.error().code, ErrorCode::SystemError);

            ChildErrorPayload p_bad_stage{static_cast<ChildErrorStage>(99), ENOENT};
            res = p_bad_stage.validate();
            KERNOX_ASSERT_TRUE(res.is_err());
            KERNOX_ASSERT_EQ(res.error().code, ErrorCode::SystemError);
        }

        // 7.3 Invalid errno values (zero or negative)
        {
            ChildErrorPayload p_zero_err{ChildErrorStage::Exec, 0};
            auto res = p_zero_err.validate();
            KERNOX_ASSERT_TRUE(res.is_err());
            KERNOX_ASSERT_EQ(res.error().code, ErrorCode::SystemError);

            ChildErrorPayload p_neg_err{ChildErrorStage::Exec, -1};
            res = p_neg_err.validate();
            KERNOX_ASSERT_TRUE(res.is_err());
            KERNOX_ASSERT_EQ(res.error().code, ErrorCode::SystemError);

            ChildErrorPayload p_neg_limit{ChildErrorStage::ResourceLimit, -100};
            res = p_neg_limit.validate();
            KERNOX_ASSERT_TRUE(res.is_err());
            KERNOX_ASSERT_EQ(res.error().code, ErrorCode::SystemError);
        }

        // 7.4 Non-existent binary launch failure maps to NotFound (via protocol)
        {
            ProcessConfig config;
            config.executable = "/nonexistent/path/to/binary/12345";
            auto res = ProcessManager::run(config);
            KERNOX_ASSERT_TRUE(res.is_err());
            KERNOX_ASSERT_EQ(res.error().code, ErrorCode::NotFound);
        }

        // 7.5 Permission denied launch failure maps to PermissionDenied (via protocol)
        {
            ProcessConfig config;
            config.executable = "/dev/null";
            auto res = ProcessManager::run(config);
            KERNOX_ASSERT_TRUE(res.is_err());
            KERNOX_ASSERT_EQ(res.error().code, ErrorCode::PermissionDenied);
        }
    }

    // =========================================================================
    // 8. Process cleanup consistency and lifecycle reaping tests
    // =========================================================================
    {
        // 8.1 Reap already-exited child process
        {
            pid_t pid = ::fork();
            KERNOX_ASSERT_TRUE(pid >= 0);
            if (pid == 0) {
                ::_exit(42);
            }
            auto cleanup = ProcessManager::reap_child(pid, std::chrono::milliseconds(200));
            KERNOX_ASSERT_TRUE(cleanup.reaped);
            KERNOX_ASSERT_TRUE(cleanup.status_valid);
            KERNOX_ASSERT_TRUE(WIFEXITED(cleanup.raw_status));
            KERNOX_ASSERT_EQ(WEXITSTATUS(cleanup.raw_status), 42);

            // 8.2 Idempotent cleanup / ECHILD handling (does not signal or fake status)
            auto second_cleanup = ProcessManager::reap_child(pid, std::chrono::milliseconds(50));
            KERNOX_ASSERT_TRUE(second_cleanup.reaped);
            KERNOX_ASSERT_FALSE(second_cleanup.status_valid);
        }

        // 8.3 Cleanup on actively running child process (bounded wait + SIGKILL termination)
        {
            pid_t pid = ::fork();
            KERNOX_ASSERT_TRUE(pid >= 0);
            if (pid == 0) {
                while (true) {
                    ::pause();
                }
                ::_exit(0);
            }
            auto cleanup = ProcessManager::reap_child(pid, std::chrono::milliseconds(0));
            KERNOX_ASSERT_TRUE(cleanup.reaped);
            KERNOX_ASSERT_TRUE(cleanup.status_valid);
            KERNOX_ASSERT_TRUE(WIFSIGNALED(cleanup.raw_status));
            KERNOX_ASSERT_EQ(WTERMSIG(cleanup.raw_status), SIGKILL);
        }

        // 8.4 Non-existent PID cleanup
        {
            auto cleanup = ProcessManager::reap_child(99999999, std::chrono::milliseconds(0));
            KERNOX_ASSERT_TRUE(cleanup.reaped);
            KERNOX_ASSERT_FALSE(cleanup.status_valid);
        }
    }

    std::cout << "[PASS] All process manager tests passed successfully!\n";
    return 0;
}


