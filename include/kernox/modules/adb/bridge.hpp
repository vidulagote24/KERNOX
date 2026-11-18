#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/adb/types.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::adb {

// Result of executing an external process command with stdout/stderr capture.
struct CommandOutput {
    int exit_code{-1};
    std::string stdout_content{};
    std::string stderr_content{};
    bool timed_out{false};
    std::chrono::microseconds duration{0};

    [[nodiscard]] bool is_success() const noexcept {
        return !timed_out && exit_code == 0;
    }
};

// Abstract command execution interface enabling deterministic unit tests without hardware or ADB daemons.
class ICommandRunner {
public:
    virtual ~ICommandRunner() = default;

    virtual core::Result<CommandOutput> execute(
        const std::string& executable,
        const std::vector<std::string>& arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    ) = 0;

    [[nodiscard]] virtual bool is_executable_available(const std::string& executable) = 0;
};

// Default POSIX command runner: uses fork, execvp, pipes, poll() non-blocking multiplexing,
// and SIGTERM -> SIGKILL timeout escalation without pipe buffer deadlocks.
class SystemCommandRunner final : public ICommandRunner {
public:
    core::Result<CommandOutput> execute(
        const std::string& executable,
        const std::vector<std::string>& arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    ) override;

    [[nodiscard]] bool is_executable_available(const std::string& executable) override;
};

// Mock command runner for unit tests and offline parser verification.
class MockCommandRunner final : public ICommandRunner {
public:
    void set_executable_available(bool available) noexcept {
        available_ = available;
    }

    void set_response(const std::string& command_key, CommandOutput output) {
        responses_[command_key] = std::move(output);
    }

    void set_default_response(CommandOutput output) {
        default_response_ = std::move(output);
    }

    [[nodiscard]] const std::vector<std::vector<std::string>>& executed_calls() const noexcept {
        return executed_calls_;
    }

    core::Result<CommandOutput> execute(
        const std::string& executable,
        const std::vector<std::string>& arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    ) override;

    [[nodiscard]] bool is_executable_available(const std::string& executable) override {
        (void)executable;
        return available_;
    }

private:
    bool available_{true};
    std::optional<CommandOutput> default_response_{std::nullopt};
    std::map<std::string, CommandOutput> responses_{};
    std::vector<std::vector<std::string>> executed_calls_{};
};

// Safe, strictly read-only adapter interfacing the host 'adb' binary.
class AdbBridge {
public:
    explicit AdbBridge(std::shared_ptr<ICommandRunner> runner = nullptr, std::string adb_path = "adb");

    // Validates whether the ADB binary is discoverable in PATH or at the configured path.
    [[nodiscard]] bool is_available();

    // Executes a low-level ADB command with strict read-only argument filtering.
    // Rejects any mutating or destructive commands (kill, force-stop, clear, setprop, su, remount, etc.).
    core::Result<CommandOutput> run_adb(
        const std::vector<std::string>& arguments,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    );

    // Executes a read-only shell command on a specific device identified by serial.
    // Injects '-s <serial>' and verifies shell command safety.
    core::Result<CommandOutput> run_device_shell(
        const std::string& serial,
        const std::vector<std::string>& shell_args,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    );

    // Read-only inspection helpers
    core::Result<CommandOutput> list_devices();
    core::Result<CommandOutput> get_state(const std::string& serial);
    core::Result<CommandOutput> getprop(const std::string& serial, const std::string& property);
    core::Result<CommandOutput> cat_proc_meminfo(const std::string& serial);
    core::Result<CommandOutput> dumpsys_meminfo(const std::string& serial, const std::string& package = "");
    core::Result<CommandOutput> dumpsys_activity_processes(const std::string& serial);

    // Safety validator: enforces a positive allowlist of explicitly supported read-only ADB command forms.
    [[nodiscard]] static bool is_safe_read_only_command(const std::vector<std::string>& arguments) noexcept;

    [[nodiscard]] const std::string& adb_path() const noexcept {
        return adb_path_;
    }

private:
    std::shared_ptr<ICommandRunner> runner_;
    std::string adb_path_;
};

} // namespace kernox::adb
