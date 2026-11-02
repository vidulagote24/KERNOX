#include "kernox/cli/command.hpp"
#include "test_harness.hpp"

#include <vector>

using namespace kernox::cli;

int main() {
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

    // 6. Unknown command returns non-zero
    KERNOX_ASSERT_NE(CommandDispatcher::run({"invalid_cmd_xyz"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"--unknown-flag"}), 0);

    // 7. Schedule valid commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"schedule", "--algorithm", "fcfs", "--workload", "basic"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"schedule", "--algorithm", "sjf", "--workload", "basic"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"schedule", "--algorithm", "srtf", "--workload", "arrivals"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"schedule", "--algorithm", "rr", "--quantum", "2", "--workload", "basic"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"schedule", "--algorithm", "priority", "--workload", "priority-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"schedule", "--algorithm", "mlfq", "--workload", "arrivals"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"sched", "--help"}), 0);

    // 8. Schedule invalid commands and validation failures
    KERNOX_ASSERT_NE(CommandDispatcher::run({"schedule"}), 0); // missing args
    KERNOX_ASSERT_NE(CommandDispatcher::run({"schedule", "--algorithm", "unknown_algo", "--workload", "basic"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"schedule", "--algorithm", "fcfs", "--workload", "unknown_wl"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"schedule", "--algorithm", "rr", "--quantum", "0", "--workload", "basic"}), 0);

    // 9. Disk schedule help and valid commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "-h"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "schedule", "--algorithm", "sstf", "--head", "53", "--cylinders", "200", "--requests", "98,183,37,122,14,124,65,67"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "--algorithm", "fcfs", "--workload", "canonical"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "schedule", "--algorithm", "scan", "--direction", "up", "--workload", "canonical"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "schedule", "--algorithm", "cscan", "--direction", "down", "--workload", "secondary"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "schedule", "--algorithm", "look", "--head", "53", "--requests", "98,183,37,122,14,124,65,67", "--direction", "up"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"disk", "schedule", "--algorithm", "clook", "--workload", "canonical"}), 0);

    // 10. Disk schedule invalid commands and validation failures
    KERNOX_ASSERT_NE(CommandDispatcher::run({"disk", "schedule"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"disk", "schedule", "--algorithm", "invalid_algo", "--workload", "canonical"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"disk", "schedule", "--algorithm", "sstf", "--requests", "10,abc,20"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"disk", "schedule", "--algorithm", "sstf", "--head", "250", "--cylinders", "200", "--requests", "10,20"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"disk", "schedule", "--algorithm", "sstf", "--cylinders", "0", "--requests", "10"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"disk", "schedule", "--algorithm", "sstf", "--direction", "sideways", "--workload", "canonical"}), 0);

    // 11. Deadlock simulation valid commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "-h"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "banker", "--workload", "safe"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "banker", "--workload", "unsafe"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "detect", "--workload", "detection-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "detect", "--workload", "detection-free"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "graph", "--workload", "cycle-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "graph", "--workload", "cycle-multi"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "graph", "--workload", "no-cycle"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"deadlock", "request", "--workload", "safe", "--process", "1", "--request", "1,0,2"}), 0);

    // 12. Deadlock simulation invalid commands and validation failures
    KERNOX_ASSERT_NE(CommandDispatcher::run({"deadlock"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"deadlock", "invalid_subcmd"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"deadlock", "banker", "--workload", "unknown_wl"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"deadlock", "request", "--workload", "safe", "--process", "99", "--request", "1,0,2"}), 0);
    // 13. Memory simulation valid commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "-h"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "allocate", "--algorithm", "first-fit", "--workload", "allocation-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "allocate", "--algorithm", "best-fit", "--workload", "allocation-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "allocate", "--algorithm", "worst-fit", "--workload", "allocation-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "allocate", "--algorithm", "next-fit", "--workload", "allocation-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "paging", "--workload", "paging-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "translate", "--process", "0", "--address", "4096", "--workload", "paging-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "replace", "--algorithm", "fifo", "--workload", "page-reference-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "replace", "--algorithm", "lru", "--workload", "page-reference-demo", "--frames", "3"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "replace", "--algorithm", "optimal", "--workload", "page-reference-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"memory", "ws", "--workload", "page-reference-demo", "--window", "4"}), 0);

    // 14. Memory simulation invalid commands and validation failures
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory", "invalid_subcmd"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory", "allocate"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory", "allocate", "--algorithm", "invalid_algo"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory", "translate", "--process", "0"}), 0); // missing address
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory", "replace"}), 0); // missing algo
    KERNOX_ASSERT_NE(CommandDispatcher::run({"memory", "replace", "--algorithm", "invalid_algo"}), 0);

    // 15. Synchronization CLI commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"sync", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"sync", "demo", "--messages", "2", "--capacity", "2"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"sync", "invalid_subcmd"}), 0);

    // 16. IPC CLI commands (including UNIX domain sockets and POSIX message queues)
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"ipc", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"ipc", "unix-demo", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"ipc", "unix-demo"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"ipc", "mq-demo", "--help"}), 0);
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"ipc", "mq-demo"}), 0);
    KERNOX_ASSERT_NE(CommandDispatcher::run({"ipc", "invalid_subcmd"}), 0);

    // 17. Process telemetry CLI commands
    KERNOX_ASSERT_EQ(CommandDispatcher::run({"process", "stat", "1"}), 0);

    std::cout << "[PASS] test_cli completed successfully.\n";
    return 0;
}
