#include "kernox/cli/command.hpp"
#include "test_harness.hpp"

#include <iostream>
#include <vector>

using namespace kernox::cli;

int main() {
    std::cout << "=== Running KERNOX 2.0 CLI Command Dispatcher Tests ===\n";

    // 1. Default (no args) prints help and returns 0
    KERNOX_ASSERT_EQ(CommandDispatcher::run({}), 0);

    // 2. Help flags return 0
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"-h"}), 0);

    // 3. Version flags return 0
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"--version"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"-v"}), 0);

    // 4. Status command returns 0
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"status"}), 0);

    // 5. Self-test command returns 0
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"self-test"}), 0);

    // 6. Unknown command and flags return non-zero
    KERNOX_ASSERT_NE(CommandDispatcher::run({"invalid_cmd_xyz"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"--unknown-flag"}), 0);

    // 7. Experiment CLI commands (help, subcommands, options)
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"experiment", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"exp", "-h"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"experiment", "run", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"experiment", "compare", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"experiment", "run", "--scenario", "normal", "--policy", "fifo", "--processes", "10"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"experiment", "run", "--scenario", "high-pressure", "--policy", "importance-aware", "--processes", "10"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"experiment", "compare", "--scenario", "normal", "--processes", "10"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"experiment", "invalid_subcmd"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"experiment", "run", "--policy", "unknown_policy"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"experiment", "run", "--scenario", "unknown_scen"}), 0);

    // 8. ADB CLI commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"adb", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"adb", "-h"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"adb", "status"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"adb", "devices"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"adb", "invalid_subcmd"}), 0);

    // 9. Simulate CLI commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"simulate", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"sim", "-h"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"simulate", "invalid_subcmd"}), 0);

    // 10. Verify all removed legacy OS commands fail with non-zero exit code
    KERNOX_ASSERT_NE(CommandDispatcher::run({"schedule"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"sched"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"disk"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"deadlock"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"mem"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"process"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"proc"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"ipc"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"sync"}), 0);

    std::cout << "[PASS] test_cli completed successfully.\n";
    return 0;
}
