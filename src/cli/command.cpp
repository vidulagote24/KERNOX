#include "kernox/cli/command.hpp"
#include "kernox/core/child_process.hpp"
#include "kernox/core/logger.hpp"
#include "kernox/core/result.hpp"
#include "kernox/core/timer.hpp"
#include "kernox/core/unique_fd.hpp"
#include "kernox/modules/sim/cpu_sched.hpp"
#include "kernox/modules/sim/disk_sched.hpp"
#include "kernox/modules/sim/deadlock.hpp"
#include "kernox/modules/sim/memory.hpp"

#include <iostream>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

namespace kernox::cli {

void CommandDispatcher::print_help() {
    std::cout << "KERNOX — Linux OS Resource Management & Benchmarking Platform\n"
              << "Usage: kernox [command] [options]\n\n"
              << "Available Commands:\n"
              << "  --help, -h       Display this help message and exit\n"
              << "  --version, -v    Display application version and build information\n"
              << "  status           Report current platform and foundation status\n"
              << "  self-test        Execute deterministic internal core health checks\n"
              << "  schedule, sched  Run CPU scheduling algorithm simulation\n"
              << "  disk             Run disk-head scheduling simulation (FCFS, SSTF, SCAN, etc.)\n"
              << "  deadlock         Run deadlock simulation (Banker's, detection, RAG cycles)\n"
              << "  memory, mem      Run memory management simulation (allocation, paging, replacement)\n\n"
              << "CPU Scheduling Options (kernox schedule [options]):\n"
              << "  --algorithm, -a <name>  Algorithm: fcfs | sjf | srtf | rr | priority | mlfq\n"
              << "  --workload, -w <name>   Workload:  basic | arrivals | priority-demo\n"
              << "  --quantum, -q <num>     Time quantum for Round Robin / MLFQ (default: 2)\n"
              << "  --boost, -b <num>       Priority boost interval for MLFQ (default: 10)\n\n"
              << "Disk Scheduling Options (kernox disk schedule [options]):\n"
              << "  --algorithm, -a <name>  Algorithm: fcfs | sstf | scan | cscan | look | clook\n"
              << "  --head, -h <num>        Initial head cylinder position (default: 53)\n"
              << "  --cylinders, -c <num>   Total disk cylinders (default: 200)\n"
              << "  --requests, -r <csv>    Cylinder requests (e.g. 98,183,37,122,14,124,65,67)\n"
              << "  --direction, -d <dir>   Initial head direction: up | down (default: up)\n"
              << "  --workload, -w <name>   Built-in workload: canonical | secondary\n\n"
              << "Deadlock Simulation Subcommands (kernox deadlock <subcommand> [options]):\n"
              << "  banker   --workload <safe|unsafe>\n"
              << "  detect   --workload <detection-demo|detection-free>\n"
              << "  graph    --workload <cycle-demo|cycle-multi|no-cycle>\n"
              << "  request  --workload safe --process <id> --request <csv>\n\n"
              << "Memory Simulation Subcommands (kernox memory <subcommand> [options]):\n"
              << "  allocate --algorithm <first-fit|best-fit|worst-fit|next-fit> --workload allocation-demo\n"
              << "  paging   --workload paging-demo\n"
              << "  translate --process <id> --address <num> --workload paging-demo\n"
              << "  replace  --algorithm <fifo|lru|optimal> --workload page-reference-demo [--frames <num>]\n"
              << "  ws       --workload page-reference-demo [--window <num>]\n\n"
              << "Examples:\n"
              << "  kernox memory allocate --algorithm first-fit --workload allocation-demo\n"
              << "  kernox memory paging --workload paging-demo\n"
              << "  kernox memory translate --process 0 --address 4096 --workload paging-demo\n"
              << "  kernox memory replace --algorithm lru --workload page-reference-demo --frames 3\n\n"
              << "Documentation: docs/CPU_SCHEDULING.md, docs/DISK_SCHEDULING.md, docs/DEADLOCK_MANAGEMENT.md, docs/MEMORY_MANAGEMENT.md\n";
}

void CommandDispatcher::print_version() {
    std::cout << "KERNOX v0.1.0 (Phase 2 Scaffolding & Core Foundations)\n"
              << "Standard: Modern C++20 | Target: Linux/POSIX (WSL 2 / Ubuntu 24.04)\n";
}

int CommandDispatcher::execute_status() {
    std::cout << "=========================================================\n"
              << "                KERNOX SYSTEM STATUS REPORT              \n"
              << "=========================================================\n"
              << "Phase: Milestone 0 / Phase 2 (Scaffolding & Core Foundations)\n\n"
              << "Active Foundation Utilities:\n"
              << "  [OK] Error & Result<T, Error>  - Type-safe error handling\n"
              << "  [OK] UniqueFd                  - RAII POSIX descriptor manager\n"
              << "  [OK] ChildProcess              - Safe process lifecycle controller\n"
              << "  [OK] Monotonic Timer           - std::chrono::steady_clock benchmark timer\n"
              << "  [OK] Structured Logger         - Thread-safe severity logger\n"
              << "  [OK] Command Dispatcher        - Modular CLI entry point\n\n";

    struct utsname sys_info{};
    if (::uname(&sys_info) == 0) {
        std::cout << "Host Environment Telemetry:\n"
                  << "  Operating System : " << sys_info.sysname << "\n"
                  << "  Node Name        : " << sys_info.nodename << "\n"
                  << "  Kernel Release   : " << sys_info.release << "\n"
                  << "  Kernel Version   : " << sys_info.version << "\n"
                  << "  Architecture     : " << sys_info.machine << "\n\n";
    }

    std::cout << "Notice: Real system telemetry (/proc parsers, CPU load, memory stats)\n"
              << "        and algorithmic simulation engines will be activated in\n"
              << "        subsequent milestones per docs/TECHNICAL_DESIGN.md.\n"
              << "=========================================================\n";
    return 0;
}

int CommandDispatcher::execute_self_test() {
    std::cout << "[KERNOX SELF-TEST] Starting foundational self-tests...\n";
    size_t passed = 0;
    size_t total = 0;

    // Test 1: Result<T> mechanics
    total++;
    {
        auto ok_res = kernox::core::Result<int>::ok(42);
        auto err_res = kernox::core::Result<int>::err(
            kernox::core::ErrorCode::InvalidArgument, "Test error");

        if (ok_res.is_ok() && ok_res.value() == 42 &&
            err_res.is_err() && err_res.error().code == kernox::core::ErrorCode::InvalidArgument) {
            std::cout << "  [PASS] Result<T> type-safety and error propagation\n";
            passed++;
        } else {
            std::cerr << "  [FAIL] Result<T> validation failed\n";
        }
    }

    // Test 2: UniqueFd and Pipe creation
    total++;
    {
        auto pipe_res = kernox::core::UniqueFd::create_pipe();
        if (pipe_res.is_ok()) {
            auto [read_fd, write_fd] = std::move(pipe_res.value());
            if (read_fd.is_valid() && write_fd.is_valid()) {
                const char test_data[] = "KERNOX";
                ssize_t written = ::write(write_fd.get(), test_data, sizeof(test_data));
                char buf[16] = {0};
                ssize_t read_bytes = ::read(read_fd.get(), buf, sizeof(buf));

                if (written == sizeof(test_data) && read_bytes == sizeof(test_data) &&
                    std::string_view(buf) == "KERNOX") {
                    std::cout << "  [PASS] UniqueFd RAII lifecycle and pipe IPC\n";
                    passed++;
                } else {
                    std::cerr << "  [FAIL] Pipe read/write data mismatch\n";
                }
            } else {
                std::cerr << "  [FAIL] Invalid pipe descriptors\n";
            }
        } else {
            std::cerr << "  [FAIL] Pipe creation failed: " << pipe_res.error().to_string() << "\n";
        }
    }

    // Test 3: Monotonic Timer
    total++;
    {
        kernox::core::Timer timer;
        ::usleep(5000); // 5ms
        int64_t elapsed_us = timer.elapsed_microseconds();
        if (elapsed_us > 0) {
            std::cout << "  [PASS] Monotonic Timer measurement (" << elapsed_us << " us)\n";
            passed++;
        } else {
            std::cerr << "  [FAIL] Monotonic Timer reported non-positive elapsed time\n";
        }
    }

    // Test 4: ChildProcess execution
    total++;
    {
        auto proc_res = kernox::core::ChildProcess::spawn("/usr/bin/true", {});
        if (proc_res.is_err()) {
            // Fallback to /bin/true if /usr/bin/true is not found
            proc_res = kernox::core::ChildProcess::spawn("/bin/true", {});
        }

        if (proc_res.is_ok()) {
            auto exit_res = proc_res.value().wait();
            if (exit_res.is_ok() && exit_res.value().is_success()) {
                std::cout << "  [PASS] ChildProcess launch and exit status reaping\n";
                passed++;
            } else {
                std::cerr << "  [FAIL] Child process did not exit with success\n";
            }
        } else {
            std::cerr << "  [FAIL] Failed to spawn child process: " << proc_res.error().to_string() << "\n";
        }
    }

    std::cout << "[KERNOX SELF-TEST] Summary: " << passed << "/" << total << " tests passed.\n";
    return (passed == total) ? 0 : 1;
}

int CommandDispatcher::execute_schedule(const std::vector<std::string>& args) {
    std::string algo_str;
    std::string workload_str;
    sim::SimTime quantum = 2;
    sim::SimTime boost = 10;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--algorithm" || arg == "-a") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox schedule: missing argument for " << arg << "\n";
                return 1;
            }
            algo_str = args[++i];
        } else if (arg == "--workload" || arg == "-w") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox schedule: missing argument for " << arg << "\n";
                return 1;
            }
            workload_str = args[++i];
        } else if (arg == "--quantum" || arg == "-q") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox schedule: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val <= 0) {
                    std::cerr << "kernox schedule: time quantum must be a positive integer (> 0)\n";
                    return 1;
                }
                quantum = static_cast<sim::SimTime>(val);
            } catch (...) {
                std::cerr << "kernox schedule: invalid numeric value for time quantum: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--boost" || arg == "-b") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox schedule: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val < 0) {
                    std::cerr << "kernox schedule: boost interval must be non-negative\n";
                    return 1;
                }
                boost = static_cast<sim::SimTime>(val);
            } catch (...) {
                std::cerr << "kernox schedule: invalid numeric value for boost interval: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kernox schedule --algorithm <algo> --workload <workload> [options]\n\n"
                      << "Options:\n"
                      << "  --algorithm, -a <name>  fcfs | sjf | srtf | rr | priority | mlfq\n"
                      << "  --workload, -w <name>   basic | arrivals | priority-demo\n"
                      << "  --quantum, -q <num>     Time quantum for Round Robin / MLFQ (default: 2)\n"
                      << "  --boost, -b <num>       Priority boost interval for MLFQ (default: 10)\n";
            return 0;
        } else {
            std::cerr << "kernox schedule: unknown option '" << arg << "'\n"
                      << "Try 'kernox schedule --help' for usage.\n";
            return 1;
        }
    }

    if (algo_str.empty()) {
        std::cerr << "kernox schedule: missing required option '--algorithm' (e.g. --algorithm fcfs)\n"
                  << "Try 'kernox schedule --help' for usage.\n";
        return 1;
    }

    if (workload_str.empty()) {
        std::cerr << "kernox schedule: missing required option '--workload' (e.g. --workload basic)\n"
                  << "Try 'kernox schedule --help' for usage.\n";
        return 1;
    }

    auto algo_opt = sim::parse_algorithm(algo_str);
    if (!algo_opt.has_value()) {
        std::cerr << "kernox schedule: unknown algorithm '" << algo_str << "'.\n"
                  << "Supported algorithms: fcfs, sjf, srtf, rr, priority, mlfq\n";
        return 1;
    }

    std::vector<sim::ProcessTask> tasks;
    if (workload_str == "basic") {
        tasks = sim::CpuScheduler::get_workload_basic();
    } else if (workload_str == "arrivals") {
        tasks = sim::CpuScheduler::get_workload_arrivals();
    } else if (workload_str == "priority-demo" || workload_str == "priority") {
        tasks = sim::CpuScheduler::get_workload_priority_demo();
    } else {
        std::cerr << "kernox schedule: unknown workload '" << workload_str << "'.\n"
                  << "Supported workloads: basic, arrivals, priority-demo\n";
        return 1;
    }

    sim::MlfqConfig mlfq_cfg = sim::MlfqConfig::default_config();
    mlfq_cfg.priority_boost_interval = boost;
    if (quantum > 0 && !mlfq_cfg.queues.empty()) {
        mlfq_cfg.queues[0].time_quantum = quantum;
        if (mlfq_cfg.queues.size() > 1) {
            mlfq_cfg.queues[1].time_quantum = quantum * 2;
        }
    }

    auto result = sim::CpuScheduler::run(*algo_opt, tasks, quantum, mlfq_cfg);
    if (result.is_err()) {
        std::cerr << "Simulation Error: " << result.error().to_string() << "\n";
        return 1;
    }

    const auto& res = result.value();
    std::cout << "================================================================================\n"
              << "KERNOX CPU SCHEDULING SIMULATION: " << res.algorithm_name << "\n"
              << "Workload: " << workload_str << " (" << tasks.size() << " processes)\n"
              << "Policy: " << res.policy_notes << "\n"
              << "================================================================================\n\n"
              << "PER-PROCESS EXECUTION METRICS:\n"
              << sim::CpuScheduler::format_metrics_table(res) << "\n"
              << "GANTT CHART TIMELINE:\n"
              << sim::CpuScheduler::format_gantt_chart(res.gantt_chart) << "\n"
              << sim::CpuScheduler::format_summary_metrics(res) << "\n"
              << "--------------------------------------------------------------------------------\n"
              << "Notice: This simulation models theoretical OS CPU scheduling in user-space.\n"
              << "        It does not alter or monitor actual Linux kernel CFS thread scheduling.\n"
              << "================================================================================\n";
    return 0;
}

int CommandDispatcher::run(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        print_help();
        return 0;
    }

    if (args[0] == "--version" || args[0] == "-v") {
        print_version();
        return 0;
    }

    if (args[0] == "status") {
        return execute_status();
    }

    if (args[0] == "self-test") {
        return execute_self_test();
    }

    if (args[0] == "schedule" || args[0] == "sched") {
        std::vector<std::string> sub_args;
        if (args.size() > 1) {
            sub_args.assign(args.begin() + 1, args.end());
        }
        return execute_schedule(sub_args);
    }

    if (args[0] == "disk") {
        std::vector<std::string> sub_args;
        if (args.size() > 1) {
            if (args[1] == "schedule" || args[1] == "sched") {
                if (args.size() > 2) {
                    sub_args.assign(args.begin() + 2, args.end());
                }
            } else {
                sub_args.assign(args.begin() + 1, args.end());
            }
        }
        return execute_disk_schedule(sub_args);
    }

    if (args[0] == "deadlock") {
        std::vector<std::string> sub_args;
        if (args.size() > 1) {
            sub_args.assign(args.begin() + 1, args.end());
        }
        return execute_deadlock(sub_args);
    }

    if (args[0] == "memory" || args[0] == "mem") {
        std::vector<std::string> sub_args;
        if (args.size() > 1) {
            sub_args.assign(args.begin() + 1, args.end());
        }
        return execute_memory(sub_args);
    }

    std::cerr << "kernox: unknown command '" << args[0] << "'\n"
              << "Try 'kernox --help' for available commands.\n";
    return 1;
}

int CommandDispatcher::execute_disk_schedule(const std::vector<std::string>& args) {
    if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) {
        std::cout << "Usage: kernox disk schedule [options]\n\n"
                  << "Options:\n"
                  << "  --algorithm, -a <name>  Algorithm: fcfs | sstf | scan | cscan | look | clook\n"
                  << "  --head, -h <num>        Initial head cylinder position (default: 53)\n"
                  << "  --cylinders, -c <num>   Total disk cylinder count (default: 200)\n"
                  << "  --requests, -r <csv>    Cylinder requests (e.g. 98,183,37,122,14,124,65,67)\n"
                  << "  --direction, -d <dir>   Initial head direction: up | down (default: up)\n"
                  << "  --workload, -w <name>   Built-in workload: canonical | secondary\n\n"
                  << "Examples:\n"
                  << "  kernox disk schedule --algorithm sstf --head 53 --cylinders 200 --requests 98,183,37,122,14,124,65,67\n"
                  << "  kernox disk schedule --algorithm scan --direction up --workload canonical\n"
                  << "  kernox disk schedule --algorithm cscan --direction down --workload secondary\n";
        return 0;
    }

    std::string algo_str;
    std::string workload_str;
    std::string requests_str;
    std::string direction_str;
    std::optional<uint32_t> custom_head;
    std::optional<uint32_t> custom_cylinders;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--algorithm" || arg == "-a") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox disk: missing argument for " << arg << "\n";
                return 1;
            }
            algo_str = args[++i];
        } else if (arg == "--workload" || arg == "-w") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox disk: missing argument for " << arg << "\n";
                return 1;
            }
            workload_str = args[++i];
        } else if (arg == "--head" || arg == "-h") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox disk: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val < 0) {
                    std::cerr << "kernox disk: initial head position cannot be negative\n";
                    return 1;
                }
                custom_head = static_cast<uint32_t>(val);
            } catch (...) {
                std::cerr << "kernox disk: invalid numeric value for initial head: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--cylinders" || arg == "-c") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox disk: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val <= 0) {
                    std::cerr << "kernox disk: cylinder count must be strictly positive (> 0)\n";
                    return 1;
                }
                custom_cylinders = static_cast<uint32_t>(val);
            } catch (...) {
                std::cerr << "kernox disk: invalid numeric value for cylinder count: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--requests" || arg == "-r") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox disk: missing argument for " << arg << "\n";
                return 1;
            }
            requests_str = args[++i];
        } else if (arg == "--direction" || arg == "-d") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox disk: missing argument for " << arg << "\n";
                return 1;
            }
            direction_str = args[++i];
        } else {
            std::cerr << "kernox disk: unknown option '" << arg << "'\n"
                      << "Try 'kernox disk --help' for usage.\n";
            return 1;
        }
    }

    if (algo_str.empty()) {
        std::cerr << "kernox disk: missing required option '--algorithm' (e.g. --algorithm sstf)\n"
                  << "Try 'kernox disk --help' for usage.\n";
        return 1;
    }

    auto algo_opt = sim::parse_disk_algorithm(algo_str);
    if (!algo_opt.has_value()) {
        std::cerr << "kernox disk: unknown algorithm '" << algo_str << "'.\n"
                  << "Supported algorithms: fcfs, sstf, scan, cscan, look, clook\n";
        return 1;
    }

    sim::DiskScheduleConfig config{};
    if (!workload_str.empty()) {
        if (workload_str == "canonical") {
            config = sim::DiskScheduler::get_canonical_workload();
        } else if (workload_str == "secondary") {
            config = sim::DiskScheduler::get_secondary_workload();
        } else {
            std::cerr << "kernox disk: unknown workload '" << workload_str << "'. Valid workloads: canonical, secondary\n";
            return 1;
        }
    } else if (requests_str.empty()) {
        std::cerr << "kernox disk: either --requests or --workload must be specified.\n";
        return 1;
    }

    if (custom_cylinders.has_value()) {
        config.cylinder_count = *custom_cylinders;
    } else if (workload_str.empty()) {
        config.cylinder_count = 200;
    }

    if (custom_head.has_value()) {
        config.initial_head = *custom_head;
    } else if (workload_str.empty()) {
        config.initial_head = 53;
    }

    if (!direction_str.empty()) {
        auto dir_opt = sim::parse_direction(direction_str);
        if (!dir_opt.has_value()) {
            std::cerr << "kernox disk: invalid direction '" << direction_str << "'. Valid directions: up, down\n";
            return 1;
        }
        config.direction = *dir_opt;
    }

    // Parse requests CSV if specified
    if (!requests_str.empty()) {
        config.requests.clear();
        std::stringstream ss(requests_str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t start = token.find_first_not_of(" \t\r\n");
            size_t end = token.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cerr << "kernox disk: malformed requests list contains empty element\n";
                return 1;
            }
            std::string trimmed = token.substr(start, end - start + 1);
            try {
                size_t idx = 0;
                long long val = std::stoll(trimmed, &idx);
                if (idx != trimmed.length()) {
                    std::cerr << "kernox disk: malformed cylinder request: '" << trimmed << "'\n";
                    return 1;
                }
                if (val < 0) {
                    std::cerr << "kernox disk: cylinder request cannot be negative: '" << trimmed << "'\n";
                    return 1;
                }
                config.requests.push_back(static_cast<uint32_t>(val));
            } catch (...) {
                std::cerr << "kernox disk: malformed cylinder request: '" << trimmed << "'\n";
                return 1;
            }
        }
    }

    // Validate config through domain model
    auto val_res = sim::DiskScheduler::validate_config(config);
    if (val_res.is_err()) {
        std::cerr << "Disk Simulation Error: " << val_res.error().to_string() << "\n";
        return 1;
    }

    auto sim_res = sim::DiskScheduler::run(*algo_opt, config);
    if (sim_res.is_err()) {
        std::cerr << "Disk Simulation Error: " << sim_res.error().to_string() << "\n";
        return 1;
    }

    const auto& res = sim_res.value();
    std::cout << "================================================================================\n"
              << "KERNOX DISK SCHEDULING SIMULATION: " << res.algorithm_name << "\n"
              << "Cylinders: " << res.cylinder_count << " (range 0-" << (res.cylinder_count - 1) << ") | "
              << "Initial Head: " << res.initial_head << " | "
              << "Initial Direction: " << sim::to_string(res.initial_direction) << "\n"
              << "Policy: " << res.policy_notes << "\n"
              << "================================================================================\n\n"
              << "HEAD MOVEMENT TRACE:\n"
              << sim::DiskScheduler::format_trace_table(res) << "\n"
              << sim::DiskScheduler::format_summary(res) << "\n"
              << "--------------------------------------------------------------------------------\n"
              << "Notice: Simulated cylinder movement models classic magnetic disk seek scheduling in user space.\n"
              << "        It does not reflect physical spindle/head latency, rotational latency, track skew,\n"
              << "        or modern solid-state flash/NVMe wear-leveling and parallel channel architectures.\n"
              << "================================================================================\n";
    return 0;
}

int CommandDispatcher::execute_deadlock(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::cout << "Usage: kernox deadlock <subcommand> [options]\n\n"
                  << "Subcommands:\n"
                  << "  banker   Run Banker's safety algorithm on a workload\n"
                  << "  detect   Run deadlock detection on current outstanding requests\n"
                  << "  graph    Analyze Resource Allocation Graph (RAG) topology and cycles\n"
                  << "  request  Evaluate transactional resource request under Banker's algorithm\n\n"
                  << "Options:\n"
                  << "  --workload, -w <name>   Workload preset:\n"
                  << "                            banker  : safe | unsafe\n"
                  << "                            detect  : detection-demo | detection-free\n"
                  << "                            graph   : cycle-demo | cycle-multi | no-cycle\n"
                  << "                            request : safe | unsafe\n"
                  << "  --process, -p <id>      Process index for resource request (default: 0)\n"
                  << "  --request, -r <csv>     Requested resource instances (e.g. 1,0,2)\n\n"
                  << "Examples:\n"
                  << "  kernox deadlock banker --workload safe\n"
                  << "  kernox deadlock banker --workload unsafe\n"
                  << "  kernox deadlock detect --workload detection-demo\n"
                  << "  kernox deadlock graph --workload cycle-demo\n"
                  << "  kernox deadlock request --workload safe --process 1 --request 1,0,2\n";
        return (args.empty() ? 1 : 0);
    }

    const std::string& subcmd = args[0];
    std::string workload_str;
    std::optional<size_t> proc_id;
    std::string req_str;

    for (size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--workload" || arg == "-w") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox deadlock: missing argument for " << arg << "\n";
                return 1;
            }
            workload_str = args[++i];
        } else if (arg == "--process" || arg == "-p") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox deadlock: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val < 0) {
                    std::cerr << "kernox deadlock: process ID cannot be negative\n";
                    return 1;
                }
                proc_id = static_cast<size_t>(val);
            } catch (...) {
                std::cerr << "kernox deadlock: invalid numeric value for process ID: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--request" || arg == "-r") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox deadlock: missing argument for " << arg << "\n";
                return 1;
            }
            req_str = args[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kernox deadlock " << subcmd << " [options]\n";
            return 0;
        } else {
            std::cerr << "kernox deadlock: unknown option '" << arg << "'\n"
                      << "Try 'kernox deadlock --help' for usage.\n";
            return 1;
        }
    }

    if (subcmd == "banker") {
        if (workload_str.empty()) workload_str = "safe";
        sim::BankerState state;
        if (workload_str == "safe") {
            state = sim::DeadlockEngine::get_banker_safe_workload();
        } else if (workload_str == "unsafe") {
            state = sim::DeadlockEngine::get_banker_unsafe_workload();
        } else {
            std::cerr << "kernox deadlock: unknown banker workload '" << workload_str
                      << "'. Supported workloads: safe, unsafe\n";
            return 1;
        }

        auto res = sim::DeadlockEngine::run_banker_safety(state);
        if (res.is_err()) {
            std::cerr << "Banker's Algorithm Error: " << res.error().to_string() << "\n";
            return 1;
        }

        std::cout << "================================================================================\n"
                  << "KERNOX DEADLOCK SIMULATION: Banker's Safety Algorithm\n"
                  << "Workload: " << workload_str << " (" << state.process_count << " processes, "
                  << state.resource_count << " resource types)\n"
                  << "================================================================================\n\n"
                  << sim::DeadlockEngine::format_banker_matrices(state) << "\n"
                  << sim::DeadlockEngine::format_safety_summary(res.value(), state.resource_count) << "\n"
                  << "State Modified: NO (read-only safety evaluation)\n"
                  << "================================================================================\n";
        return 0;
    }

    if (subcmd == "detect") {
        if (workload_str.empty()) workload_str = "detection-demo";
        sim::DetectionState state;
        if (workload_str == "detection-demo" || workload_str == "deadlock") {
            state = sim::DeadlockEngine::get_detection_deadlock_workload();
        } else if (workload_str == "detection-free" || workload_str == "no-deadlock") {
            state = sim::DeadlockEngine::get_detection_free_workload();
        } else {
            std::cerr << "kernox deadlock: unknown detect workload '" << workload_str
                      << "'. Supported workloads: detection-demo, detection-free\n";
            return 1;
        }

        auto res = sim::DeadlockEngine::run_deadlock_detection(state);
        if (res.is_err()) {
            std::cerr << "Deadlock Detection Error: " << res.error().to_string() << "\n";
            return 1;
        }

        std::cout << "================================================================================\n"
                  << "KERNOX DEADLOCK SIMULATION: Deadlock Detection Algorithm (Multiple Instances)\n"
                  << "Workload: " << workload_str << " (" << state.process_count << " processes, "
                  << state.resource_count << " resource types)\n"
                  << "================================================================================\n\n"
                  << sim::DeadlockEngine::format_detection_matrices(state) << "\n"
                  << sim::DeadlockEngine::format_detection_summary(res.value(), state.resource_count) << "\n"
                  << "State Modified: NO (read-only deadlock detection)\n"
                  << "================================================================================\n";
        return 0;
    }

    if (subcmd == "graph") {
        if (workload_str.empty()) workload_str = "cycle-demo";
        sim::ResourceAllocationGraph rag;
        if (workload_str == "cycle-demo" || workload_str == "cycle-single") {
            rag = sim::DeadlockEngine::get_rag_single_cycle_workload();
        } else if (workload_str == "cycle-multi") {
            rag = sim::DeadlockEngine::get_rag_multi_cycle_workload();
        } else if (workload_str == "no-cycle") {
            rag = sim::DeadlockEngine::get_rag_no_cycle_workload();
        } else {
            std::cerr << "kernox deadlock: unknown graph workload '" << workload_str
                      << "'. Supported workloads: cycle-demo, cycle-multi, no-cycle\n";
            return 1;
        }

        auto cycle_res = sim::DeadlockEngine::analyze_rag(rag);
        if (cycle_res.is_err()) {
            std::cerr << "Graph Analysis Error: " << cycle_res.error().to_string() << "\n";
            return 1;
        }

        std::cout << "================================================================================\n"
                  << "KERNOX DEADLOCK SIMULATION: Resource Allocation Graph Cycle Analysis\n"
                  << "Workload: " << workload_str << "\n"
                  << "================================================================================\n\n"
                  << sim::DeadlockEngine::format_rag_summary(rag, cycle_res.value()) << "\n"
                  << "State Modified: NO (read-only graph analysis)\n"
                  << "================================================================================\n";
        return 0;
    }

    if (subcmd == "request") {
        if (workload_str.empty()) workload_str = "safe";
        sim::BankerState state;
        if (workload_str == "safe") {
            state = sim::DeadlockEngine::get_banker_safe_workload();
        } else if (workload_str == "unsafe") {
            state = sim::DeadlockEngine::get_banker_unsafe_workload();
        } else {
            std::cerr << "kernox deadlock: unknown workload '" << workload_str
                      << "'. Supported workloads: safe, unsafe\n";
            return 1;
        }

        if (!proc_id.has_value()) {
            std::cerr << "kernox deadlock: missing required option '--process' (e.g. --process 1)\n";
            return 1;
        }

        if (req_str.empty()) {
            std::cerr << "kernox deadlock: missing required option '--request' (e.g. --request 1,0,2)\n";
            return 1;
        }

        sim::ResourceVector req_vec;
        std::stringstream ss(req_str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t start = token.find_first_not_of(" \t\r\n");
            size_t end = token.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cerr << "kernox deadlock: malformed request vector contains empty element\n";
                return 1;
            }
            std::string trimmed = token.substr(start, end - start + 1);
            try {
                size_t idx = 0;
                long long val = std::stoll(trimmed, &idx);
                if (idx != trimmed.length()) {
                    std::cerr << "kernox deadlock: malformed request element: '" << trimmed << "'\n";
                    return 1;
                }
                if (val < 0) {
                    std::cerr << "kernox deadlock: request element cannot be negative: '" << trimmed << "'\n";
                    return 1;
                }
                req_vec.push_back(static_cast<uint32_t>(val));
            } catch (...) {
                std::cerr << "kernox deadlock: malformed request element: '" << trimmed << "'\n";
                return 1;
            }
        }

        auto res = sim::DeadlockEngine::process_resource_request(state, *proc_id, req_vec);
        if (res.is_err()) {
            std::cerr << "Resource Request Error: " << res.error().to_string() << "\n";
            return 1;
        }

        const auto& r = res.value();
        std::cout << "================================================================================\n"
                  << "KERNOX DEADLOCK SIMULATION: Banker's Resource Request Operation\n"
                  << "Workload: " << workload_str << " | Target Process: P" << *proc_id << "\n"
                  << "Request Vector: [";
        for (size_t j = 0; j < req_vec.size(); ++j) {
            std::cout << "R" << j << "=" << req_vec[j] << (j + 1 < req_vec.size() ? ", " : "]\n");
        }
        std::cout << "================================================================================\n\n"
                  << "REQUEST EVALUATION RESULT:\n"
                  << "  Status               : " << sim::to_string(r.status) << "\n"
                  << "  Message              : " << r.message << "\n";

        if (r.tentative_safety.has_value()) {
            std::cout << "  Tentative Safety     : "
                      << (r.tentative_safety->is_safe ? "SAFE" : "UNSAFE") << "\n";
            if (r.tentative_safety->is_safe) {
                std::cout << "  Safe Sequence        : <";
                for (size_t k = 0; k < r.tentative_safety->safe_sequence.size(); ++k) {
                    std::cout << "P" << r.tentative_safety->safe_sequence[k]
                              << (k + 1 < r.tentative_safety->safe_sequence.size() ? " -> " : ">\n");
                }
            }
        }

        bool modified = (r.status == sim::RequestStatus::Granted);
        std::cout << "  State Modified       : "
                  << (modified ? "YES (Transaction Committed)" : "NO (Transaction Rolled Back / Preserved)") << "\n\n";

        if (modified) {
            std::cout << "COMMITTED SYSTEM STATE:\n"
                      << sim::DeadlockEngine::format_banker_matrices(r.resulting_state) << "\n";
        } else {
            std::cout << "PRESERVED ORIGINAL STATE:\n"
                      << sim::DeadlockEngine::format_banker_matrices(r.resulting_state) << "\n";
        }

        std::cout << "================================================================================\n";
        return 0;
    }

    std::cerr << "kernox deadlock: unknown subcommand '" << subcmd << "'\n"
              << "Try 'kernox deadlock --help' for available subcommands.\n";
    return 1;
}

int CommandDispatcher::execute_memory(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::cout << "Usage: kernox memory <subcommand> [options]\n\n"
                  << "Subcommands:\n"
                  << "  allocate   Simulate contiguous memory allocation (First-Fit, Best-Fit, Worst-Fit, Next-Fit)\n"
                  << "  paging     Inspect physical paging configuration and per-process page tables\n"
                  << "  translate  Perform logical-to-physical address translation\n"
                  << "  replace    Simulate page replacement algorithms (FIFO, LRU, Optimal)\n"
                  << "  ws         Perform working-set model locality analysis\n\n"
                  << "Options:\n"
                  << "  --algorithm, -a <name>  Algorithm name\n"
                  << "                            allocate: first-fit | best-fit | worst-fit | next-fit\n"
                  << "                            replace : fifo | lru | optimal\n"
                  << "  --workload, -w <name>   Workload preset:\n"
                  << "                            allocate : allocation-demo\n"
                  << "                            paging   : paging-demo\n"
                  << "                            translate: paging-demo\n"
                  << "                            replace  : page-reference-demo | canonical\n"
                  << "                            ws       : page-reference-demo\n"
                  << "  --process, -p <id>      Process ID (default: 4 for allocate, 0 for translate)\n"
                  << "  --size, -s <bytes>      Allocation size in bytes/KB (default: 120)\n"
                  << "  --address, -addr <num>  Logical address to translate (e.g. 4096 or 0x1000)\n"
                  << "  --frames, -f <num>      Physical frame count for replacement (default: 3)\n"
                  << "  --window, -d <num>      Working-set window delta (default: 4)\n\n"
                  << "Examples:\n"
                  << "  kernox memory allocate --algorithm first-fit --workload allocation-demo\n"
                  << "  kernox memory allocate --algorithm best-fit --workload allocation-demo\n"
                  << "  kernox memory paging --workload paging-demo\n"
                  << "  kernox memory translate --process 0 --address 4096 --workload paging-demo\n"
                  << "  kernox memory replace --algorithm lru --workload page-reference-demo --frames 3\n"
                  << "  kernox memory ws --workload page-reference-demo --window 4\n";
        return (args.empty() ? 1 : 0);
    }

    const std::string& subcmd = args[0];
    std::string algo_str;
    std::string workload_str;
    std::optional<uint32_t> proc_id;
    std::optional<uint64_t> alloc_size;
    std::optional<uint64_t> address;
    std::optional<size_t> frames;
    std::optional<size_t> window_delta;

    for (size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--algorithm" || arg == "-a") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox memory: missing argument for " << arg << "\n";
                return 1;
            }
            algo_str = args[++i];
        } else if (arg == "--workload" || arg == "-w") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox memory: missing argument for " << arg << "\n";
                return 1;
            }
            workload_str = args[++i];
        } else if (arg == "--process" || arg == "-p") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox memory: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val < 0) {
                    std::cerr << "kernox memory: process ID cannot be negative\n";
                    return 1;
                }
                proc_id = static_cast<uint32_t>(val);
            } catch (...) {
                std::cerr << "kernox memory: invalid numeric value for process ID: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--size" || arg == "-s") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox memory: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val <= 0) {
                    std::cerr << "kernox memory: size must be strictly positive (> 0)\n";
                    return 1;
                }
                alloc_size = static_cast<uint64_t>(val);
            } catch (...) {
                std::cerr << "kernox memory: invalid numeric value for size: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--address" || arg == "-addr") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox memory: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                std::string addr_str = args[++i];
                if (addr_str.rfind("0x", 0) == 0 || addr_str.rfind("0X", 0) == 0) {
                    address = std::stoull(addr_str, nullptr, 16);
                } else {
                    address = std::stoull(addr_str, nullptr, 10);
                }
            } catch (...) {
                std::cerr << "kernox memory: invalid numeric value for address: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--frames" || arg == "-f") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox memory: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val <= 0) {
                    std::cerr << "kernox memory: frame count must be positive (> 0)\n";
                    return 1;
                }
                frames = static_cast<size_t>(val);
            } catch (...) {
                std::cerr << "kernox memory: invalid numeric value for frame count: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--window" || arg == "--delta" || arg == "-d") {
            if (i + 1 >= args.size()) {
                std::cerr << "kernox memory: missing argument for " << arg << "\n";
                return 1;
            }
            try {
                long long val = std::stoll(args[++i]);
                if (val <= 0) {
                    std::cerr << "kernox memory: window size must be positive (> 0)\n";
                    return 1;
                }
                window_delta = static_cast<size_t>(val);
            } catch (...) {
                std::cerr << "kernox memory: invalid numeric value for window size: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kernox memory " << subcmd << " [options]\n";
            return 0;
        } else {
            std::cerr << "kernox memory: unknown option '" << arg << "'\n"
                      << "Try 'kernox memory --help' for usage.\n";
            return 1;
        }
    }

    if (subcmd == "allocate") {
        if (algo_str.empty()) {
            std::cerr << "kernox memory allocate: missing required option '--algorithm' (e.g. --algorithm first-fit)\n";
            return 1;
        }
        auto strat_opt = sim::parse_contiguous_strategy(algo_str);
        if (!strat_opt.has_value()) {
            std::cerr << "kernox memory allocate: unknown algorithm '" << algo_str
                      << "'. Supported algorithms: first-fit, best-fit, worst-fit, next-fit\n";
            return 1;
        }
        if (workload_str.empty()) workload_str = "allocation-demo";
        if (workload_str != "allocation-demo" && workload_str != "canonical") {
            std::cerr << "kernox memory allocate: unknown workload '" << workload_str
                      << "'. Supported workloads: allocation-demo\n";
            return 1;
        }

        uint32_t pid = proc_id.value_or(4);
        uint64_t req_size = alloc_size.value_or(120);

        auto mgr = sim::MemoryEngine::get_contiguous_workload();
        std::cout << "================================================================================\n"
                  << "KERNOX MEMORY SIMULATION: Contiguous Memory Allocation (" << sim::to_string(*strat_opt) << ")\n"
                  << "Workload: " << workload_str << " | Request: Process P" << pid << ", Size " << req_size << " bytes\n"
                  << "================================================================================\n\n"
                  << "INITIAL MEMORY LAYOUT:\n"
                  << sim::MemoryEngine::format_contiguous_memory(mgr) << "\n";

        auto alloc_res = mgr.allocate(*strat_opt, pid, req_size);
        if (alloc_res.is_err()) {
            std::cerr << "Allocation Error: " << alloc_res.error().to_string() << "\n";
            return 1;
        }

        std::cout << sim::MemoryEngine::format_allocation_result(alloc_res.value()) << "\n"
                  << "POST-ALLOCATION MEMORY LAYOUT:\n"
                  << sim::MemoryEngine::format_contiguous_memory(mgr) << "\n"
                  << "================================================================================\n";
        return 0;
    }

    if (subcmd == "paging") {
        if (workload_str.empty()) workload_str = "paging-demo";
        if (workload_str != "paging-demo" && workload_str != "canonical") {
            std::cerr << "kernox memory paging: unknown workload '" << workload_str
                      << "'. Supported workloads: paging-demo\n";
            return 1;
        }

        auto sys = sim::MemoryEngine::get_paging_workload();
        const auto& cfg = sys.config();
        std::cout << "================================================================================\n"
                  << "KERNOX MEMORY SIMULATION: Paging Architecture & Memory Topology\n"
                  << "Workload: " << workload_str << "\n"
                  << "================================================================================\n\n"
                  << "PAGING SYSTEM CONFIGURATION:\n"
                  << "  Logical Address Space  : " << cfg.logical_address_space_size << " bytes ("
                  << (cfg.logical_address_space_size / 1024) << " KB)\n"
                  << "  Physical Memory Size   : " << cfg.physical_memory_size << " bytes ("
                  << (cfg.physical_memory_size / 1024) << " KB)\n"
                  << "  Page Size (Frame Size) : " << cfg.page_size << " bytes ("
                  << (cfg.page_size / 1024) << " KB)\n"
                  << "  Total Logical Pages    : " << cfg.page_count() << "\n"
                  << "  Total Physical Frames  : " << cfg.frame_count() << "\n\n";

        const auto* table = sys.get_page_table(0);
        if (table) {
            std::cout << sim::MemoryEngine::format_page_table(0, *table) << "\n";
        }
        std::cout << "================================================================================\n";
        return 0;
    }

    if (subcmd == "translate") {
        if (workload_str.empty()) workload_str = "paging-demo";
        if (workload_str != "paging-demo" && workload_str != "canonical") {
            std::cerr << "kernox memory translate: unknown workload '" << workload_str
                      << "'. Supported workloads: paging-demo\n";
            return 1;
        }
        if (!address.has_value()) {
            std::cerr << "kernox memory translate: missing required option '--address' (e.g. --address 4096)\n";
            return 1;
        }

        uint32_t pid = proc_id.value_or(0);
        auto sys = sim::MemoryEngine::get_paging_workload();

        auto trans_res = sys.translate(pid, *address);
        if (trans_res.is_err()) {
            std::cerr << "Translation Error: " << trans_res.error().to_string() << "\n";
            return 1;
        }

        const auto& t = trans_res.value();
        std::cout << "================================================================================\n"
                  << "KERNOX MEMORY SIMULATION: Address Translation Unit (MMU Model)\n"
                  << "Workload: " << workload_str << " | Target Process: P" << pid << "\n"
                  << "================================================================================\n\n"
                  << sim::MemoryEngine::format_translation(t) << "\n"
                  << "================================================================================\n";
        return 0;
    }

    if (subcmd == "replace") {
        if (algo_str.empty()) {
            std::cerr << "kernox memory replace: missing required option '--algorithm' (e.g. --algorithm lru)\n";
            return 1;
        }
        auto algo_opt = sim::parse_page_replacement_algo(algo_str);
        if (!algo_opt.has_value()) {
            std::cerr << "kernox memory replace: unknown algorithm '" << algo_str
                      << "'. Supported algorithms: fifo, lru, optimal\n";
            return 1;
        }
        if (workload_str.empty()) workload_str = "page-reference-demo";

        auto ref_str = sim::MemoryEngine::get_canonical_reference_string();
        size_t frame_count = frames.value_or(3);

        auto rep_res = sim::PageReplacementSimulator::run(*algo_opt, ref_str, frame_count);
        if (rep_res.is_err()) {
            std::cerr << "Replacement Error: " << rep_res.error().to_string() << "\n";
            return 1;
        }

        const auto& res = rep_res.value();
        std::cout << "================================================================================\n"
                  << "KERNOX MEMORY SIMULATION: Virtual Memory Page Replacement (" << sim::to_string(*algo_opt) << ")\n"
                  << "Workload: " << workload_str << " | References: " << ref_str.size() << " | Physical Frames: " << frame_count << "\n"
                  << "================================================================================\n\n"
                  << sim::MemoryEngine::format_replacement_trace(res) << "\n"
                  << sim::MemoryEngine::format_replacement_summary(res) << "\n"
                  << "================================================================================\n";
        return 0;
    }

    if (subcmd == "ws" || subcmd == "working-set") {
        if (workload_str.empty()) workload_str = "page-reference-demo";
        auto ref_str = sim::MemoryEngine::get_canonical_reference_string();
        size_t win = window_delta.value_or(4);

        auto ws_res = sim::PageReplacementSimulator::run_working_set(ref_str, win);
        if (ws_res.is_err()) {
            std::cerr << "Working Set Error: " << ws_res.error().to_string() << "\n";
            return 1;
        }

        const auto& res = ws_res.value();
        std::cout << "================================================================================\n"
                  << "KERNOX MEMORY SIMULATION: Working-Set Model Locality Analysis\n"
                  << "Workload: " << workload_str << " | Window Size Delta: " << win << "\n"
                  << "================================================================================\n\n"
                  << sim::MemoryEngine::format_working_set(res) << "\n"
                  << "================================================================================\n";
        return 0;
    }

    std::cerr << "kernox memory: unknown subcommand '" << subcmd << "'\n"
              << "Try 'kernox memory --help' for available subcommands.\n";
    return 1;
}

int CommandDispatcher::run(int argc, char* argv[]) {
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return run(args);
}

} // namespace kernox::cli
