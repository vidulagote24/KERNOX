#include "kernox/cli/command.hpp"
#include "kernox/core/child_process.hpp"
#include "kernox/core/logger.hpp"
#include "kernox/core/result.hpp"
#include "kernox/core/timer.hpp"
#include "kernox/core/unique_fd.hpp"
#include "kernox/modules/intel/process_model.hpp"
#include "kernox/modules/system/telemetry.hpp"
#include "kernox/modules/adb/types.hpp"
#include "kernox/modules/adb/bridge.hpp"
#include "kernox/modules/adb/service.hpp"
#include "kernox/modules/experiment/workload.hpp"
#include "kernox/modules/experiment/experiment_runner.hpp"
#include "kernox/modules/experiment/exporter.hpp"
#include "kernox/modules/reclaim/reclamation_policy.hpp"
#include "kernox/modules/simulator/interactive_menu.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

namespace kernox::cli {

void CommandDispatcher::print_help() {
    std::cout << "KERNOX — Android Resource Intelligence & Memory Reclamation Platform\n"
              << "Usage: kernox [command] [options]\n\n"
              << "Available Commands:\n"
              << "  --help, -h       Display this help message and exit\n"
              << "  --version, -v    Display application version and build information\n"
              << "  status           Report current platform and host telemetry status\n"
              << "  self-test        Execute deterministic internal core health checks\n"
              << "  simulate, sim    Interactive Android-inspired resource simulation environment\n"
              << "  experiment, exp  Run workload experiments & compare memory reclamation policies\n"
              << "  adb              Android device observation (read-only discovery, meminfo, processes, snapshot)\n\n"
              << "Simulator Options (kernox simulate android / kernox sim android):\n"
              << "  android              Launch the interactive menu-driven Android resource simulation environment\n\n"
              << "Experiment Options (kernox experiment <run|compare> [options]):\n"
              << "  run [options]        Execute a simulated workload under a chosen reclamation policy\n"
              << "      --scenario, -s <name> Workload scenario: normal | high-pressure | background-heavy | foreground-protection | mixed (default: normal)\n"
              << "      --policy, -p <name>   Reclamation policy: fifo | lru | priority | importance-aware (default: importance-aware)\n"
              << "      --seed <num>          Deterministic PRNG seed (default: 42)\n"
              << "      --processes, -n <num> Total simulated process population (default: per scenario)\n"
              << "      --capacity, -c <bytes> Total memory capacity in bytes (default: 1073741824 / 1 GiB)\n"
              << "      --format, -f <fmt>    Output format: text | json | csv (default: text)\n"
              << "      --output, -o <path>   Write output to file instead of stdout\n"
              << "  compare [options]    Execute all 4 policies on identical workload and compare metrics\n"
              << "      --scenario, -s <name> Workload scenario (default: normal)\n"
              << "      --seed <num>          Deterministic PRNG seed (default: 42)\n"
              << "      --processes, -n <num> Total simulated process population\n"
              << "      --capacity, -c <bytes> Total memory capacity in bytes\n"
              << "      --format, -f <fmt>    Output format: text | json | csv (default: text)\n"
              << "      --output, -o <path>   Write output to file instead of stdout\n\n"
              << "Android ADB Options (kernox adb <subcommand> [options]):\n"
              << "  devices              List all attached Android devices and their status\n"
              << "  status               Inspect ADB availability and active device state\n"
              << "  memory [options]     Inspect real Android memory telemetry (/proc/meminfo or dumpsys)\n"
              << "      --serial, -s <id> Target specific device serial\n"
              << "      --format, -f <fmt> Output format: text | json (default: text)\n"
              << "      --output, -o <path> Write output to file\n"
              << "  processes [options]  Inspect active Android processes and memory footprints\n"
              << "      --serial, -s <id> Target specific device serial\n"
              << "      --format, -f <fmt> Output format: text | json (default: text)\n"
              << "      --output, -o <path> Write output to file\n"
              << "  snapshot [options]   Generate consolidated observation snapshot report\n"
              << "      --serial, -s <id> Target specific device serial\n"
              << "      --format, -f <fmt> Output format: text | json (default: text)\n"
              << "      --output, -o <path> Write output to file\n\n"
              << "Examples:\n"
              << "  kernox status\n"
              << "  kernox self-test\n"
              << "  kernox simulate android\n"
              << "  kernox experiment run --scenario high-pressure --policy importance-aware\n"
              << "  kernox experiment compare --scenario high-pressure\n"
              << "  kernox adb status\n"
              << "  kernox adb devices\n\n"
              << "Documentation: docs/ARCHITECTURE.md, docs/ANDROID_SIMULATOR.md, docs/CLI_REFERENCE.md\n";
}

void CommandDispatcher::print_version() {
    std::cout << "KERNOX v2.0.0 (Android Resource Intelligence & Memory Reclamation Platform)\n"
              << "Standard: Modern C++20 | Target: Linux/POSIX (WSL 2 / Ubuntu 24.04)\n";
}

int CommandDispatcher::execute_status() {
    std::cout << "=========================================================\n"
              << "                KERNOX SYSTEM STATUS REPORT              \n"
              << "=========================================================\n"
              << "Phase: KERNOX 2.0 (Android Resource Intelligence & Memory Reclamation Platform)\n"
              << "Project Status: Complete (All Implemented Modules Operational)\n\n"
              << "Active Subsystems & Implemented Modules:\n"
              << "  [OK] Error & Result<T, Error>  - Type-safe error handling\n"
              << "  [OK] UniqueFd                  - RAII POSIX descriptor manager\n"
              << "  [OK] ChildProcess              - Safe process lifecycle controller\n"
              << "  [OK] Monotonic Timer           - std::chrono::steady_clock benchmark timer\n"
              << "  [OK] Structured Logger         - Thread-safe severity logger\n"
              << "  [OK] System Telemetry          - Linux /proc live resource monitor\n"
              << "  [OK] Command Dispatcher        - Modular CLI entry point\n"
              << "  [OK] Process Intelligence      - Android importance classification & lifecycle tracking\n"
              << "  [OK] Memory Reclamation Engine - FIFO, LRU, Priority, Importance-Aware policies\n"
              << "  [OK] Experiment & Benchmark    - Multi-policy comparative workload benchmark\n"
              << "  [OK] Android ADB Observation   - Passive device inspection & dumpsys parsing\n"
              << "  [OK] Interactive Simulator     - Menu-driven terminal UI & ASCII visualizer\n"
              << "  [OK] Python Analytics Bridge   - Automated report generation & plotting\n\n";

    struct utsname sys_info{};
    if (::uname(&sys_info) == 0) {
        std::cout << "Host Environment Telemetry:\n"
                  << "  Operating System : " << sys_info.sysname << "\n"
                  << "  Node Name        : " << sys_info.nodename << "\n"
                  << "  Kernel Release   : " << sys_info.release << "\n"
                  << "  Kernel Version   : " << sys_info.version << "\n"
                  << "  Architecture     : " << sys_info.machine << "\n\n";
    }

    kernox::system::SystemTelemetry telemetry;
    auto host = telemetry.collect_telemetry(std::chrono::milliseconds(100));

    std::cout << "Live Host Telemetry (/proc):\n";

    // CPU Utilization
    std::cout << "  CPU Utilization  : ";
    if (host.cpu_utilization_percent.is_ok()) {
        std::cout << std::fixed << std::setprecision(2)
                  << host.cpu_utilization_percent.value() << "% (sampled over 100ms)\n";
    } else {
        std::cout << "[UNAVAILABLE: " << host.cpu_utilization_percent.error().message << "]\n";
    }

    // Memory Info
    std::cout << "  Memory Status    : ";
    if (host.memory.is_ok()) {
        const auto& mem = host.memory.value();
        double total_mb = static_cast<double>(mem.total_bytes) / (1024.0 * 1024.0);
        double used_mb = static_cast<double>(mem.used_bytes) / (1024.0 * 1024.0);
        double avail_mb = static_cast<double>(mem.available_bytes) / (1024.0 * 1024.0);

        std::cout << std::fixed << std::setprecision(2)
                  << "Used " << used_mb << " MB / Total " << total_mb << " MB ("
                  << mem.usage_percent << "%)\n"
                  << "  Memory Available : " << std::fixed << std::setprecision(2)
                  << avail_mb << " MB";
        if (mem.used_fallback_available) {
            std::cout << " (estimated via Free+Buffers+Cached)";
        }
        std::cout << "\n";
    } else {
        std::cout << "[UNAVAILABLE: " << host.memory.error().message << "]\n";
    }

    // Load Average
    std::cout << "  Load Average     : ";
    if (host.load_avg.is_ok()) {
        const auto& la = host.load_avg.value();
        std::cout << std::fixed << std::setprecision(2)
                  << la.load_1min << " (1m), " << la.load_5min << " (5m), "
                  << la.load_15min << " (15m)\n";
    } else {
        std::cout << "[UNAVAILABLE: " << host.load_avg.error().message << "]\n";
    }

    // System Uptime
    std::cout << "  System Uptime    : ";
    if (host.uptime.is_ok()) {
        const auto& up = host.uptime.value();
        uint64_t total_sec = static_cast<uint64_t>(up.uptime_seconds);
        uint64_t hours = total_sec / 3600;
        uint64_t mins = (total_sec % 3600) / 60;
        uint64_t secs = total_sec % 60;
        std::cout << hours << "h " << mins << "m " << secs << "s ("
                  << std::fixed << std::setprecision(2) << up.uptime_seconds << "s)\n";
    } else {
        std::cout << "[UNAVAILABLE: " << host.uptime.error().message << "]\n";
    }

    std::cout << "\nNotice: Real system telemetry is now operational via Linux /proc.\n"
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

    // Test 5: System Telemetry Parser
    total++;
    {
        const std::string mem_sample = "MemTotal: 4000000 kB\nMemAvailable: 3000000 kB\n";
        auto mem_res = kernox::system::TelemetryParser::parse_proc_meminfo(mem_sample);
        if (mem_res.is_ok() && mem_res.value().total_bytes == 4000000ULL * 1024ULL) {
            std::cout << "  [PASS] Telemetry string parser and unit conversion\n";
            passed++;
        } else {
            std::cerr << "  [FAIL] Telemetry parser validation failed\n";
        }
    }

    // Test 6: Process Intelligence Model & Importance Classification
    total++;
    {
        auto proc_res = kernox::intel::SimulatedProcess::create(
            "self_test_proc", 10, kernox::intel::ProcessImportance::Foreground,
            1024 * 1024, 100, 0);
        if (proc_res.is_ok() &&
            proc_res.value().importance() == kernox::intel::ProcessImportance::Foreground &&
            proc_res.value().cpu_priority() == 10) {
            std::cout << "  [PASS] Process Intelligence & lifecycle importance modeling\n";
            passed++;
        } else {
            std::cerr << "  [FAIL] Process Intelligence model self-test failed\n";
        }
    }

    std::cout << "[KERNOX SELF-TEST] Summary: " << passed << "/" << total << " tests passed.\n";
    return (passed == total) ? 0 : 1;
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

    if (args[0] == "experiment" || args[0] == "exp") {
        std::vector<std::string> sub_args;
        if (args.size() > 1) {
            sub_args.assign(args.begin() + 1, args.end());
        }
        return execute_experiment(sub_args);
    }

    if (args[0] == "adb") {
        std::vector<std::string> sub_args;
        if (args.size() > 1) {
            sub_args.assign(args.begin() + 1, args.end());
        }
        return execute_adb(sub_args);
    }

    if (args[0] == "simulate" || args[0] == "sim") {
        std::vector<std::string> sub_args;
        if (args.size() > 1) {
            sub_args.assign(args.begin() + 1, args.end());
        }
        return execute_simulate(sub_args);
    }

    std::cerr << "kernox: unknown command '" << args[0] << "'\n"
              << "Try 'kernox --help' for available commands.\n";
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

int CommandDispatcher::execute_experiment(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        std::cout << "Usage: kernox experiment <run|compare> [options]\n\n"
                  << "Subcommands:\n"
                  << "  run       Execute a single policy against a workload scenario\n"
                  << "  compare   Compare all 4 policies on an identical workload\n\n"
                  << "Options:\n"
                  << "  --scenario, -s <name>    Workload scenario: normal | high-pressure | background-heavy | foreground-protection | mixed (default: normal)\n"
                  << "  --policy, -p <name>      Reclamation policy: fifo | lru | priority | importance-aware (default: importance-aware) [run only]\n"
                  << "  --seed <num>             Deterministic PRNG seed (default: 42)\n"
                  << "  --processes, -n <num>    Total simulated process count (default: per scenario)\n"
                  << "  --capacity, -c <bytes>   Total memory capacity in bytes (default: 1073741824 / 1 GiB)\n"
                  << "  --format, -f <fmt>       Output format: text | json | csv (default: text)\n"
                  << "  --output, -o <path>      Output file path (default: stdout)\n";
        return 0;
    }

    const std::string subcommand = args[0];
    if (subcommand != "run" && subcommand != "compare") {
        std::cerr << "kernox experiment: unknown subcommand '" << subcommand << "'.\n"
                  << "Try 'kernox experiment --help' for available subcommands.\n";
        return 1;
    }

    std::string scenario_str = "normal";
    std::string policy_str = "importance-aware";
    uint64_t seed = 42;
    std::optional<size_t> processes_opt = std::nullopt;
    uint64_t capacity = 1024ULL * 1024 * 1024; // 1 GiB
    std::string format = "text";
    std::string output_path;

    for (size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kernox experiment " << subcommand << " [options]\n\n"
                      << "Options:\n"
                      << "  --scenario, -s <name>    Workload scenario: normal | high-pressure | background-heavy | foreground-protection | mixed\n"
                      << "  --policy, -p <name>      Reclamation policy: fifo | lru | priority | importance-aware (run only)\n"
                      << "  --seed <num>             Deterministic PRNG seed (default: 42)\n"
                      << "  --processes, -n <num>    Total simulated process count\n"
                      << "  --capacity, -c <bytes>   Total memory capacity in bytes\n"
                      << "  --format, -f <fmt>       Output format: text | json | csv\n"
                      << "  --output, -o <path>      Output file path\n";
            return 0;
        }

        if (arg == "--scenario" || arg == "-s") {
            if (++i >= args.size()) {
                std::cerr << "kernox experiment: missing value for " << arg << "\n";
                return 1;
            }
            scenario_str = args[i];
        } else if (arg == "--policy" || arg == "-p") {
            if (++i >= args.size()) {
                std::cerr << "kernox experiment: missing value for " << arg << "\n";
                return 1;
            }
            policy_str = args[i];
        } else if (arg == "--seed") {
            if (++i >= args.size()) {
                std::cerr << "kernox experiment: missing value for --seed\n";
                return 1;
            }
            try {
                seed = std::stoull(args[i]);
            } catch (...) {
                std::cerr << "kernox experiment: invalid numeric value for --seed: '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--processes" || arg == "-n") {
            if (++i >= args.size()) {
                std::cerr << "kernox experiment: missing value for " << arg << "\n";
                return 1;
            }
            try {
                processes_opt = static_cast<size_t>(std::stoull(args[i]));
            } catch (...) {
                std::cerr << "kernox experiment: invalid numeric value for " << arg << ": '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--capacity" || arg == "-c") {
            if (++i >= args.size()) {
                std::cerr << "kernox experiment: missing value for " << arg << "\n";
                return 1;
            }
            try {
                capacity = std::stoull(args[i]);
            } catch (...) {
                std::cerr << "kernox experiment: invalid numeric value for " << arg << ": '" << args[i] << "'\n";
                return 1;
            }
        } else if (arg == "--format" || arg == "-f") {
            if (++i >= args.size()) {
                std::cerr << "kernox experiment: missing value for " << arg << "\n";
                return 1;
            }
            format = args[i];
            for (char& c : format) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (format != "text" && format != "json" && format != "csv") {
                std::cerr << "kernox experiment: unsupported format '" << format << "'. Supported: text, json, csv\n";
                return 1;
            }
        } else if (arg == "--output" || arg == "-o") {
            if (++i >= args.size()) {
                std::cerr << "kernox experiment: missing value for " << arg << "\n";
                return 1;
            }
            output_path = args[i];
        } else {
            std::cerr << "kernox experiment: unknown option '" << arg << "'\n"
                      << "Try 'kernox experiment --help' for options.\n";
            return 1;
        }
    }

    auto scen_opt = experiment::parse_scenario_type(scenario_str);
    if (!scen_opt.has_value()) {
        std::cerr << "kernox experiment: unknown scenario '" << scenario_str << "'.\n"
                  << "Supported scenarios: normal, high-pressure, background-heavy, foreground-protection, mixed\n";
        return 1;
    }

    experiment::WorkloadGeneratorConfig gen_cfg;
    gen_cfg.scenario = *scen_opt;
    gen_cfg.seed = seed;
    gen_cfg.total_memory_bytes = capacity;
    if (processes_opt.has_value()) {
        gen_cfg.process_count = *processes_opt;
    }

    auto wkl_res = experiment::WorkloadGenerator::generate(gen_cfg);
    if (!wkl_res.is_ok()) {
        std::cerr << "kernox experiment: workload generation error: " << wkl_res.error().to_string() << "\n";
        return 1;
    }

    const auto& workload = wkl_res.value();

    if (subcommand == "compare") {
        auto comp_res = experiment::ExperimentRunner::compare_all_policies(workload);
        if (!comp_res.is_ok()) {
            std::cerr << "kernox experiment: comparison error: " << comp_res.error().to_string() << "\n";
            return 1;
        }
        const auto& comp = comp_res.value();
        std::string content;
        if (format == "json") {
            content = experiment::ExperimentExporter::to_json(comp);
        } else if (format == "csv") {
            content = experiment::ExperimentExporter::to_csv(comp);
        } else {
            content = comp.to_summary_string();
        }

        if (!output_path.empty()) {
            auto write_res = experiment::ExperimentExporter::write_to_file(output_path, content);
            if (!write_res.is_ok()) {
                std::cerr << "kernox experiment: error writing to '" << output_path << "': "
                          << write_res.error().to_string() << "\n";
                return 1;
            }
            std::cout << "Successfully exported comparison to " << output_path << "\n";
        } else {
            std::cout << content;
        }
        return 0;
    }

    // subcommand == "run"
    std::string pol_lower;
    pol_lower.reserve(policy_str.size());
    for (char c : policy_str) {
        pol_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    std::unique_ptr<reclaim::IReclamationPolicy> policy;
    if (pol_lower == "fifo") {
        policy = std::make_unique<reclaim::FifoReclamationPolicy>();
    } else if (pol_lower == "lru") {
        policy = std::make_unique<reclaim::LruReclamationPolicy>();
    } else if (pol_lower == "priority") {
        policy = std::make_unique<reclaim::PriorityReclamationPolicy>();
    } else if (pol_lower == "importance-aware" || pol_lower == "importance_aware" ||
               pol_lower == "importanceaware" || pol_lower == "importance") {
        policy = std::make_unique<reclaim::ImportanceAwareReclamationPolicy>();
    } else {
        std::cerr << "kernox experiment: unknown reclamation policy '" << policy_str << "'.\n"
                  << "Supported policies: fifo, lru, priority, importance-aware\n";
        return 1;
    }

    auto run_res = experiment::ExperimentRunner::run_policy(workload, *policy);
    if (!run_res.is_ok()) {
        std::cerr << "kernox experiment: execution error: " << run_res.error().to_string() << "\n";
        return 1;
    }

    const auto& result = run_res.value();
    std::string content;
    if (format == "json") {
        content = experiment::ExperimentExporter::to_json(result);
    } else if (format == "csv") {
        content = experiment::ExperimentExporter::to_decisions_csv(result);
    } else {
        content = result.to_string();
    }

    if (!output_path.empty()) {
        auto write_res = experiment::ExperimentExporter::write_to_file(output_path, content);
        if (!write_res.is_ok()) {
            std::cerr << "kernox experiment: error writing to '" << output_path << "': "
                      << write_res.error().to_string() << "\n";
            return 1;
        }
        std::cout << "Successfully exported experiment result to " << output_path << "\n";
    } else {
        std::cout << content;
    }

    return 0;
}

int CommandDispatcher::execute_adb(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        std::cout << "Usage: kernox adb <subcommand> [options]\n\n"
                  << "Subcommands:\n"
                  << "  devices              List all attached Android devices and their status\n"
                  << "  status               Inspect ADB availability and target device telemetry\n"
                  << "  memory [options]     Inspect real Android memory telemetry (/proc/meminfo or dumpsys)\n"
                  << "  processes [options]  Inspect active Android processes and memory footprints\n"
                  << "  snapshot [options]   Generate consolidated observation snapshot report\n\n"
                  << "Options:\n"
                  << "  --serial, -s <id>    Target specific device serial (required if multiple devices)\n"
                  << "  --format, -f <fmt>   Output format: text | json (default: text)\n"
                  << "  --output, -o <path>  Write output to file instead of stdout\n";
        return 0;
    }

    const std::string subcommand = args[0];
    if (subcommand != "devices" && subcommand != "status" && subcommand != "memory" &&
        subcommand != "processes" && subcommand != "snapshot") {
        std::cerr << "kernox adb: unknown subcommand '" << subcommand << "'.\n"
                  << "Try 'kernox adb --help' for available subcommands.\n";
        return 1;
    }

    std::optional<std::string> serial_opt = std::nullopt;
    std::string format = "text";
    std::string output_path;

    for (size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kernox adb " << subcommand << " [options]\n\n"
                      << "Options:\n"
                      << "  --serial, -s <id>    Target specific device serial\n"
                      << "  --format, -f <fmt>   Output format: text | json (default: text)\n"
                      << "  --output, -o <path>  Write output to file\n";
            return 0;
        }

        if (arg == "--serial" || arg == "-s") {
            if (++i >= args.size()) {
                std::cerr << "kernox adb: missing value for " << arg << "\n";
                return 1;
            }
            serial_opt = args[i];
        } else if (arg == "--format" || arg == "-f") {
            if (++i >= args.size()) {
                std::cerr << "kernox adb: missing value for " << arg << "\n";
                return 1;
            }
            format = args[i];
            for (char& c : format) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (format != "text" && format != "json") {
                std::cerr << "kernox adb: unsupported format '" << format << "'. Supported: text, json\n";
                return 1;
            }
        } else if (arg == "--output" || arg == "-o") {
            if (++i >= args.size()) {
                std::cerr << "kernox adb: missing value for " << arg << "\n";
                return 1;
            }
            output_path = args[i];
        } else {
            std::cerr << "kernox adb: unknown option '" << arg << "'\n"
                      << "Try 'kernox adb --help' for options.\n";
            return 1;
        }
    }

    adb::AdbObservationService service;

    auto write_or_print = [&](const std::string& content) -> int {
        if (!output_path.empty()) {
            std::ofstream out(output_path);
            if (!out.is_open()) {
                std::cerr << "kernox adb: failed to open output file '" << output_path << "' for writing.\n";
                return 1;
            }
            out << content;
            if (!out.good()) {
                std::cerr << "kernox adb: error occurred while writing output to '" << output_path << "'.\n";
                return 1;
            }
            std::cout << "Successfully exported ADB observation to " << output_path << "\n";
        } else {
            std::cout << content;
        }
        return 0;
    };

    if (subcommand == "devices") {
        auto dev_res = service.discover_devices();
        if (!dev_res.is_ok()) {
            std::cerr << "kernox adb error: " << dev_res.error().to_string() << "\n";
            return 1;
        }
        const auto& devices = dev_res.value();
        if (format == "json") {
            std::ostringstream oss;
            oss << "[\n";
            for (size_t i = 0; i < devices.size(); ++i) {
                std::string d_json = devices[i].to_json();
                std::istringstream iss(d_json);
                std::string line;
                bool first = true;
                while (std::getline(iss, line)) {
                    if (!first) oss << "\n";
                    oss << "  " << line;
                    first = false;
                }
                if (i + 1 < devices.size()) oss << ",";
                oss << "\n";
            }
            oss << "]\n";
            return write_or_print(oss.str());
        } else {
            std::ostringstream oss;
            oss << "================================================================================\n"
                << "ATTACHED ANDROID DEVICES (" << devices.size() << " detected)\n"
                << "================================================================================\n";
            if (devices.empty()) {
                oss << "  No Android devices attached.\n";
            } else {
                for (const auto& d : devices) {
                    oss << "  * " << d.to_string() << "\n";
                }
            }
            oss << "================================================================================\n";
            return write_or_print(oss.str());
        }
    }

    if (subcommand == "status") {
        bool adb_avail = service.bridge()->is_available();
        std::ostringstream oss;
        oss << "================================================================================\n"
            << "KERNOX ANDROID OBSERVATION STATUS\n"
            << "================================================================================\n"
            << "ADB Binary Available : " << (adb_avail ? "[YES] (" + service.bridge()->adb_path() + ")" : "[NO] Missing from PATH") << "\n";
        if (!adb_avail) {
            oss << "\nNOTICE: ADB is not installed on this host system.\n"
                << "KERNOX functions normally in simulation mode without ADB.\n"
                << "To enable Android device observation, install Android Platform Tools:\n"
                << "  Ubuntu/Debian: sudo apt install android-tools-adb\n";
        } else {
            auto dev_res = service.discover_devices();
            if (dev_res.is_ok()) {
                const auto& devs = dev_res.value();
                oss << "Devices Attached     : " << devs.size() << "\n";
                for (const auto& d : devs) {
                    oss << "  - " << d.to_string() << "\n";
                }
            } else {
                oss << "Device Discovery     : Failed (" << dev_res.error().message << ")\n";
            }
        }
        oss << "================================================================================\n";
        return write_or_print(oss.str());
    }

    if (subcommand == "memory") {
        auto mem_res = service.get_memory_snapshot(serial_opt ? std::optional<std::string_view>(*serial_opt) : std::nullopt);
        if (!mem_res.is_ok()) {
            std::cerr << "kernox adb error: " << mem_res.error().to_string() << "\n";
            return 1;
        }
        const auto& snap = mem_res.value();
        std::string content = (format == "json") ? (snap.to_json() + "\n") : (snap.to_string() + "\n");
        return write_or_print(content);
    }

    if (subcommand == "processes") {
        auto procs_res = service.get_process_observations(serial_opt ? std::optional<std::string_view>(*serial_opt) : std::nullopt);
        if (!procs_res.is_ok()) {
            std::cerr << "kernox adb error: " << procs_res.error().to_string() << "\n";
            return 1;
        }
        const auto& procs = procs_res.value();
        if (format == "json") {
            std::ostringstream oss;
            oss << "[\n";
            for (size_t i = 0; i < procs.size(); ++i) {
                std::string p_json = procs[i].to_json();
                std::istringstream iss(p_json);
                std::string line;
                bool first = true;
                while (std::getline(iss, line)) {
                    if (!first) oss << "\n";
                    oss << "  " << line;
                    first = false;
                }
                if (i + 1 < procs.size()) oss << ",";
                oss << "\n";
            }
            oss << "]\n";
            return write_or_print(oss.str());
        } else {
            std::ostringstream oss;
            oss << "================================================================================\n"
                << "ANDROID PROCESS OBSERVATIONS (" << procs.size() << " processes monitored)\n"
                << "================================================================================\n";
            for (const auto& p : procs) {
                oss << "  " << p.to_string() << "\n";
            }
            oss << "================================================================================\n";
            return write_or_print(oss.str());
        }
    }

    if (subcommand == "snapshot") {
        auto rep_res = service.generate_snapshot_report(serial_opt ? std::optional<std::string_view>(*serial_opt) : std::nullopt);
        if (!rep_res.is_ok()) {
            std::cerr << "kernox adb error: " << rep_res.error().to_string() << "\n";
            return 1;
        }
        const auto& rep = rep_res.value();
        std::string content = (format == "json") ? (rep.to_json() + "\n") : (rep.to_string() + "\n");
        return write_or_print(content);
    }

    return 0;
}

int CommandDispatcher::execute_simulate(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        std::cout << "Usage: kernox simulate <subcommand> [options]\n"
                  << "       kernox sim <subcommand> [options]\n\n"
                  << "Subcommands:\n"
                  << "  android    Launch the interactive Android resource simulation environment\n\n"
                  << "Options:\n"
                  << "  --help, -h Display this help message and exit\n";
        return 0;
    }

    const std::string subcommand = args[0];
    if (subcommand == "android") {
        simulator::InteractiveSimulator sim;
        return sim.run();
    }

    std::cerr << "kernox simulate: unknown subcommand '" << subcommand << "'.\n"
              << "Try 'kernox simulate --help' for available subcommands.\n";
    return 1;
}

} // namespace kernox::cli
