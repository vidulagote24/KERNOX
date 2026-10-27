#pragma once

#include "kernox/core/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::sim {

using SimTime = uint64_t;
using ProcessId = uint32_t;

// Priority convention: Lower numerical value denotes higher scheduling priority.
// Priority 0 is the highest priority.
constexpr uint32_t HIGHEST_PRIORITY = 0;

struct ProcessTask {
    ProcessId pid{0};
    SimTime arrival_time{0};
    SimTime burst_time{0};
    uint32_t priority{HIGHEST_PRIORITY};
};

struct ProcessMetrics {
    ProcessId pid{0};
    SimTime arrival_time{0};
    SimTime burst_time{0};
    uint32_t priority{HIGHEST_PRIORITY};
    SimTime first_execution_time{0};
    SimTime completion_time{0};
    SimTime turnaround_time{0}; // completion - arrival
    SimTime waiting_time{0};    // turnaround - burst
    SimTime response_time{0};   // first_execution - arrival
};

struct GanttInterval {
    SimTime start_time{0};
    SimTime end_time{0};
    std::optional<ProcessId> pid{std::nullopt}; // nullopt denotes CPU idle

    [[nodiscard]] bool is_idle() const noexcept {
        return !pid.has_value();
    }

    [[nodiscard]] SimTime duration() const noexcept {
        return end_time - start_time;
    }
};

struct ScheduleMetrics {
    double average_turnaround_time{0.0};
    double average_waiting_time{0.0};
    double average_response_time{0.0};
    SimTime makespan{0};            // Total elapsed time from first arrival to final completion
    SimTime cpu_busy_time{0};       // Sum of all active process execution intervals
    SimTime total_idle_time{0};     // Sum of all CPU idle intervals
    double cpu_utilization{0.0};    // (cpu_busy_time / makespan) * 100.0 (over makespan)
    size_t dispatch_count{0};       // Total process dispatches onto the CPU
    size_t preemption_count{0};     // Involuntary process preemptions
    size_t context_switch_count{0}; // Switches between distinct processes (excluding idle)
};

struct ScheduleResult {
    std::string algorithm_name;
    std::vector<ProcessMetrics> process_metrics;
    std::vector<GanttInterval> gantt_chart;
    ScheduleMetrics metrics;
    std::string policy_notes;
};

enum class SchedAlgorithm {
    FCFS,
    SJF,
    SRTF,
    RoundRobin,
    Priority,
    MLFQ
};

[[nodiscard]] std::string_view to_string(SchedAlgorithm algo) noexcept;
[[nodiscard]] std::optional<SchedAlgorithm> parse_algorithm(std::string_view name) noexcept;

struct MlfqQueueConfig {
    uint32_t priority_level{0};
    SimTime time_quantum{0}; // Quantum for Round Robin. If 0, operates as FCFS
    bool is_fcfs{false};     // Explicitly treat as non-preemptive FCFS queue
};

struct MlfqConfig {
    std::vector<MlfqQueueConfig> queues;
    SimTime priority_boost_interval{0}; // 0 = no periodic boost; > 0 = reset all processes to Q0 every N units

    static MlfqConfig default_config();
};

class CpuScheduler {
public:
    // Workload validation
    static core::Result<void> validate_workload(const std::vector<ProcessTask>& workload);

    // Core Algorithms
    static core::Result<ScheduleResult> run_fcfs(const std::vector<ProcessTask>& tasks);
    static core::Result<ScheduleResult> run_sjf(const std::vector<ProcessTask>& tasks);
    static core::Result<ScheduleResult> run_srtf(const std::vector<ProcessTask>& tasks);
    static core::Result<ScheduleResult> run_round_robin(const std::vector<ProcessTask>& tasks, SimTime time_quantum);
    static core::Result<ScheduleResult> run_priority(const std::vector<ProcessTask>& tasks);
    static core::Result<ScheduleResult> run_mlfq(const std::vector<ProcessTask>& tasks, const MlfqConfig& config);

    // Generic runner
    static core::Result<ScheduleResult> run(
        SchedAlgorithm algo,
        const std::vector<ProcessTask>& tasks,
        SimTime time_quantum = 2,
        const std::optional<MlfqConfig>& mlfq_cfg = std::nullopt);

    // Built-in sample workloads
    static std::vector<ProcessTask> get_workload_basic();
    static std::vector<ProcessTask> get_workload_arrivals();
    static std::vector<ProcessTask> get_workload_priority_demo();

    // Formatters
    static std::string format_gantt_chart(const std::vector<GanttInterval>& chart);
    static std::string format_metrics_table(const ScheduleResult& result);
    static std::string format_summary_metrics(const ScheduleResult& result);
};

} // namespace kernox::sim
