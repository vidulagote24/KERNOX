#include "kernox/core/child_process.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <csignal>
#include <unistd.h>

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

int main() {
    std::string true_bin = find_binary("true");
    std::string false_bin = find_binary("false");
    std::string sleep_bin = find_binary("sleep");

    // 1. Successful execution with exit status 0
    {
        auto proc_res = ChildProcess::spawn(true_bin, {});
        KERNOX_ASSERT_TRUE(proc_res.is_ok());

        auto& proc = proc_res.value();
        KERNOX_ASSERT_TRUE(proc.is_valid());
        KERNOX_ASSERT_TRUE(proc.pid() > 0);

        auto wait_res = proc.wait();
        KERNOX_ASSERT_TRUE(wait_res.is_ok());
        KERNOX_ASSERT_TRUE(wait_res.value().exited);
        KERNOX_ASSERT_EQ(wait_res.value().exit_code, 0);
        KERNOX_ASSERT_TRUE(wait_res.value().is_success());
        KERNOX_ASSERT_FALSE(wait_res.value().signaled);
        KERNOX_ASSERT_FALSE(proc.is_valid()); // Reaped
    }

    // 2. Non-zero exit status
    {
        auto proc_res = ChildProcess::spawn(false_bin, {});
        KERNOX_ASSERT_TRUE(proc_res.is_ok());

        auto wait_res = proc_res.value().wait();
        KERNOX_ASSERT_TRUE(wait_res.is_ok());
        KERNOX_ASSERT_TRUE(wait_res.value().exited);
        KERNOX_ASSERT_NE(wait_res.value().exit_code, 0);
        KERNOX_ASSERT_FALSE(wait_res.value().is_success());
    }

    // 3. Polling and timeout handling
    {
        auto proc_res = ChildProcess::spawn(sleep_bin, {"1"});
        KERNOX_ASSERT_TRUE(proc_res.is_ok());

        auto& proc = proc_res.value();

        // Immediate poll should indicate still running
        auto poll_res = proc.poll_status();
        KERNOX_ASSERT_TRUE(poll_res.is_ok());
        KERNOX_ASSERT_FALSE(poll_res.value().has_value());

        // Wait with a small timeout (50ms) should return Timeout error
        auto timeout_res = proc.wait_for(std::chrono::milliseconds(50));
        KERNOX_ASSERT_TRUE(timeout_res.is_err());
        KERNOX_ASSERT_EQ(timeout_res.error().code, ErrorCode::Timeout);

        // Terminate process with SIGTERM
        auto sig_res = proc.send_signal(SIGTERM);
        KERNOX_ASSERT_TRUE(sig_res.is_ok());

        // Wait for termination
        auto exit_res = proc.wait();
        KERNOX_ASSERT_TRUE(exit_res.is_ok());
        // Must be either signaled with SIGTERM or exited
        KERNOX_ASSERT_TRUE(exit_res.value().signaled || exit_res.value().exited);
    }

    // 4. Safe RAII cleanup in destructor without explicit wait
    {
        auto proc_res = ChildProcess::spawn(sleep_bin, {"5"});
        KERNOX_ASSERT_TRUE(proc_res.is_ok());
        // Destructor executes here and cleanly terminates and reaps the child process
    }

    // 5. Invalid binary execution
    {
        auto proc_res = ChildProcess::spawn("/non_existent_binary_xyz_123", {});
        // fork() succeeds, child calls _exit(127) when execvp fails
        KERNOX_ASSERT_TRUE(proc_res.is_ok());
        auto wait_res = proc_res.value().wait();
        KERNOX_ASSERT_TRUE(wait_res.is_ok());
        KERNOX_ASSERT_EQ(wait_res.value().exit_code, 127);
    }

    std::cout << "[PASS] test_child_process completed successfully.\n";
    return 0;
}
