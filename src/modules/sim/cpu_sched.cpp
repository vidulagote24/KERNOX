#include "kernox/modules/sim/cpu_sched.hpp"

#include <algorithm>
#include <deque>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace kernox::sim {

std::string_view to_string(SchedAlgorithm algo) noexcept {
    switch (algo) {
        case SchedAlgorithm::FCFS:
            return "First-Come First-Served (FCFS)";
        case SchedAlgorithm::SJF:
            return "Shortest Job First (SJF Non-Preemptive)";
        case SchedAlgorithm::SRTF:
            return "Shortest Remaining Time First (SRTF Preemptive)";
        case SchedAlgorithm::RoundRobin:
            return "Round Robin (RR)";
        case SchedAlgorithm::Priority:
            return "Priority Scheduling (Non-Preemptive)";
        case SchedAlgorithm::MLFQ:
            return "Multilevel Feedback Queue (MLFQ)";
    }
    return "Unknown";
}

std::optional<SchedAlgorithm> parse_algorithm(std::string_view name) noexcept {
    if (name == "fcfs" || name == "FCFS") return SchedAlgorithm::FCFS;
    if (name == "sjf" || name == "SJF") return SchedAlgorithm::SJF;
    if (name == "srtf" || name == "SRTF") return SchedAlgorithm::SRTF;
    if (name == "rr" || name == "RR" || name == "round-robin") return SchedAlgorithm::RoundRobin;
    if (name == "priority" || name == "Priority") return SchedAlgorithm::Priority;
    if (name == "mlfq" || name == "MLFQ") return SchedAlgorithm::MLFQ;
    return std::nullopt;
}

MlfqConfig MlfqConfig::default_config() {
    return MlfqConfig{
        .queues = {
            MlfqQueueConfig{.priority_level = 0, .time_quantum = 2, .is_fcfs = false},
            MlfqQueueConfig{.priority_level = 1, .time_quantum = 4, .is_fcfs = false},
            MlfqQueueConfig{.priority_level = 2, .time_quantum = 0, .is_fcfs = true}
        },
        .priority_boost_interval = 10
    };
}

core::Result<void> CpuScheduler::validate_workload(const std::vector<ProcessTask>& workload) {
    if (workload.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Workload cannot be empty");
    }

    std::unordered_set<ProcessId> seen_pids;
    seen_pids.reserve(workload.size());

    for (const auto& task : workload) {
        if (!seen_pids.insert(task.pid).second) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Duplicate process ID detected: PID " + std::to_string(task.pid));
        }
        if (task.burst_time == 0) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Process burst time must be strictly positive: PID " + std::to_string(task.pid));
        }
    }
    return core::Result<void>::ok();
}

static void add_gantt_slice(
    std::vector<GanttInterval>& chart,
    SimTime start,
    SimTime end,
    std::optional<ProcessId> pid) {
    if (start >= end) {
        return;
    }
    if (!chart.empty() && chart.back().pid == pid && chart.back().end_time == start) {
        chart.back().end_time = end;
    } else {
        chart.push_back(GanttInterval{start, end, pid});
    }
}

static ScheduleMetrics compute_metrics(
    const std::vector<ProcessMetrics>& proc_metrics,
    const std::vector<GanttInterval>& chart,
    size_t preemption_count) {
    ScheduleMetrics sm{};
    if (proc_metrics.empty()) {
        return sm;
    }

    SimTime total_turnaround = 0;
    SimTime total_waiting = 0;
    SimTime total_response = 0;
    SimTime min_arrival = proc_metrics.front().arrival_time;
    SimTime max_completion = proc_metrics.front().completion_time;

    for (const auto& pm : proc_metrics) {
        total_turnaround += pm.turnaround_time;
        total_waiting += pm.waiting_time;
        total_response += pm.response_time;
        if (pm.arrival_time < min_arrival) min_arrival = pm.arrival_time;
        if (pm.completion_time > max_completion) max_completion = pm.completion_time;
    }

    auto count = static_cast<double>(proc_metrics.size());
    sm.average_turnaround_time = static_cast<double>(total_turnaround) / count;
    sm.average_waiting_time = static_cast<double>(total_waiting) / count;
    sm.average_response_time = static_cast<double>(total_response) / count;
    sm.makespan = (max_completion >= min_arrival) ? (max_completion - min_arrival) : 0;

    SimTime busy_time = 0;
    SimTime idle_time = 0;
    size_t dispatches = 0;
    size_t context_switches = 0;
    std::optional<ProcessId> last_active_pid = std::nullopt;

    for (const auto& slice : chart) {
        if (slice.is_idle()) {
            idle_time += slice.duration();
            last_active_pid = std::nullopt; // Direct transitions between distinct executing processes exclude idle
        } else {
            busy_time += slice.duration();
            dispatches++;
            if (last_active_pid.has_value() && *last_active_pid != *slice.pid) {
                context_switches++;
            }
            last_active_pid = slice.pid;
        }
    }

    sm.cpu_busy_time = busy_time;
    sm.total_idle_time = idle_time;
    sm.dispatch_count = dispatches;
    sm.preemption_count = preemption_count;
    sm.context_switch_count = context_switches;

    if (sm.makespan > 0) {
        sm.cpu_utilization = (static_cast<double>(busy_time) / static_cast<double>(sm.makespan)) * 100.0;
    } else {
        sm.cpu_utilization = 0.0;
    }

    return sm;
}

// Internal tracking structure for simulation runs
struct SimProcess {
    ProcessTask task;
    SimTime remaining_burst{0};
    std::optional<SimTime> first_execution_time{std::nullopt};
    SimTime completion_time{0};
    uint32_t current_queue{0};
    SimTime current_quantum_used{0};

    explicit SimProcess(const ProcessTask& t)
        : task(t), remaining_burst(t.burst_time) {}
};

// ----------------------------------------------------------------------------
// A. First-Come, First-Served (FCFS)
// ----------------------------------------------------------------------------
core::Result<ScheduleResult> CpuScheduler::run_fcfs(const std::vector<ProcessTask>& tasks) {
    auto val_res = validate_workload(tasks);
    if (val_res.is_err()) return core::Result<ScheduleResult>::err(val_res.error());

    std::vector<SimProcess> procs;
    procs.reserve(tasks.size());
    for (const auto& t : tasks) procs.emplace_back(t);

    // Sort by arrival time, then by PID for deterministic tie-breaking
    std::sort(procs.begin(), procs.end(), [](const SimProcess& a, const SimProcess& b) {
        if (a.task.arrival_time != b.task.arrival_time)
            return a.task.arrival_time < b.task.arrival_time;
        return a.task.pid < b.task.pid;
    });

    SimTime current_time = 0;
    std::vector<GanttInterval> chart;

    for (auto& p : procs) {
        if (current_time < p.task.arrival_time) {
            add_gantt_slice(chart, current_time, p.task.arrival_time, std::nullopt);
            current_time = p.task.arrival_time;
        }
        p.first_execution_time = current_time;
        SimTime end_time = current_time + p.task.burst_time;
        add_gantt_slice(chart, current_time, end_time, p.task.pid);
        p.completion_time = end_time;
        current_time = end_time;
    }

    std::vector<ProcessMetrics> metrics;
    metrics.reserve(procs.size());
    for (const auto& p : procs) {
        SimTime turn = p.completion_time - p.task.arrival_time;
        SimTime wait = turn - p.task.burst_time;
        SimTime resp = *p.first_execution_time - p.task.arrival_time;
        metrics.push_back(ProcessMetrics{
            .pid = p.task.pid,
            .arrival_time = p.task.arrival_time,
            .burst_time = p.task.burst_time,
            .priority = p.task.priority,
            .first_execution_time = *p.first_execution_time,
            .completion_time = p.completion_time,
            .turnaround_time = turn,
            .waiting_time = wait,
            .response_time = resp
        });
    }

    // Sort metrics back to PID order for consistent presentation
    std::sort(metrics.begin(), metrics.end(), [](const ProcessMetrics& a, const ProcessMetrics& b) {
        return a.pid < b.pid;
    });

    ScheduleResult res;
    res.algorithm_name = "First-Come First-Served (FCFS)";
    res.process_metrics = metrics;
    res.gantt_chart = chart;
    res.metrics = compute_metrics(metrics, chart, 0);
    res.policy_notes = "Non-preemptive; ordered by arrival time; tie-break by PID.";
    return core::Result<ScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// B. Shortest Job First (SJF Non-Preemptive)
// ----------------------------------------------------------------------------
core::Result<ScheduleResult> CpuScheduler::run_sjf(const std::vector<ProcessTask>& tasks) {
    auto val_res = validate_workload(tasks);
    if (val_res.is_err()) return core::Result<ScheduleResult>::err(val_res.error());

    std::vector<SimProcess> procs;
    procs.reserve(tasks.size());
    for (const auto& t : tasks) procs.emplace_back(t);

    std::vector<bool> completed(procs.size(), false);
    size_t completed_count = 0;
    SimTime current_time = 0;
    std::vector<GanttInterval> chart;

    while (completed_count < procs.size()) {
        size_t best_idx = procs.size();

        for (size_t i = 0; i < procs.size(); ++i) {
            if (!completed[i] && procs[i].task.arrival_time <= current_time) {
                if (best_idx == procs.size()) {
                    best_idx = i;
                } else {
                    const auto& a = procs[i];
                    const auto& b = procs[best_idx];
                    if (a.task.burst_time < b.task.burst_time) {
                        best_idx = i;
                    } else if (a.task.burst_time == b.task.burst_time) {
                        if (a.task.arrival_time < b.task.arrival_time) {
                            best_idx = i;
                        } else if (a.task.arrival_time == b.task.arrival_time && a.task.pid < b.task.pid) {
                            best_idx = i;
                        }
                    }
                }
            }
        }

        if (best_idx == procs.size()) {
            // No process ready: jump to next earliest arrival
            SimTime next_arrival = UINT64_MAX;
            for (size_t i = 0; i < procs.size(); ++i) {
                if (!completed[i] && procs[i].task.arrival_time < next_arrival) {
                    next_arrival = procs[i].task.arrival_time;
                }
            }
            add_gantt_slice(chart, current_time, next_arrival, std::nullopt);
            current_time = next_arrival;
            continue;
        }

        auto& p = procs[best_idx];
        p.first_execution_time = current_time;
        SimTime end_time = current_time + p.task.burst_time;
        add_gantt_slice(chart, current_time, end_time, p.task.pid);
        p.completion_time = end_time;
        current_time = end_time;
        completed[best_idx] = true;
        completed_count++;
    }

    std::vector<ProcessMetrics> metrics;
    metrics.reserve(procs.size());
    for (const auto& p : procs) {
        SimTime turn = p.completion_time - p.task.arrival_time;
        SimTime wait = turn - p.task.burst_time;
        SimTime resp = *p.first_execution_time - p.task.arrival_time;
        metrics.push_back(ProcessMetrics{
            .pid = p.task.pid,
            .arrival_time = p.task.arrival_time,
            .burst_time = p.task.burst_time,
            .priority = p.task.priority,
            .first_execution_time = *p.first_execution_time,
            .completion_time = p.completion_time,
            .turnaround_time = turn,
            .waiting_time = wait,
            .response_time = resp
        });
    }

    std::sort(metrics.begin(), metrics.end(), [](const ProcessMetrics& a, const ProcessMetrics& b) {
        return a.pid < b.pid;
    });

    ScheduleResult res;
    res.algorithm_name = "Shortest Job First (SJF Non-Preemptive)";
    res.process_metrics = metrics;
    res.gantt_chart = chart;
    res.metrics = compute_metrics(metrics, chart, 0);
    res.policy_notes = "Non-preemptive; selects shortest initial CPU burst; tie-break by arrival time, then PID.";
    return core::Result<ScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// C. Shortest Remaining Time First (SRTF Preemptive)
// ----------------------------------------------------------------------------
core::Result<ScheduleResult> CpuScheduler::run_srtf(const std::vector<ProcessTask>& tasks) {
    auto val_res = validate_workload(tasks);
    if (val_res.is_err()) return core::Result<ScheduleResult>::err(val_res.error());

    std::vector<SimProcess> procs;
    procs.reserve(tasks.size());
    for (const auto& t : tasks) procs.emplace_back(t);

    size_t completed_count = 0;
    SimTime current_time = 0;
    std::vector<GanttInterval> chart;
    size_t preemptions = 0;
    std::optional<size_t> running_idx = std::nullopt;

    while (completed_count < procs.size()) {
        size_t best_idx = procs.size();

        for (size_t i = 0; i < procs.size(); ++i) {
            if (procs[i].remaining_burst > 0 && procs[i].task.arrival_time <= current_time) {
                if (best_idx == procs.size()) {
                    best_idx = i;
                } else {
                    const auto& a = procs[i];
                    const auto& b = procs[best_idx];
                    if (a.remaining_burst < b.remaining_burst) {
                        best_idx = i;
                    } else if (a.remaining_burst == b.remaining_burst) {
                        if (a.task.arrival_time < b.task.arrival_time) {
                            best_idx = i;
                        } else if (a.task.arrival_time == b.task.arrival_time && a.task.pid < b.task.pid) {
                            best_idx = i;
                        }
                    }
                }
            }
        }

        if (best_idx == procs.size()) {
            // CPU is idle: jump to next earliest arrival
            SimTime next_arrival = UINT64_MAX;
            for (const auto& p : procs) {
                if (p.remaining_burst > 0 && p.task.arrival_time < next_arrival) {
                    next_arrival = p.task.arrival_time;
                }
            }
            add_gantt_slice(chart, current_time, next_arrival, std::nullopt);
            current_time = next_arrival;
            running_idx = std::nullopt;
            continue;
        }

        if (running_idx.has_value() && *running_idx != best_idx && procs[*running_idx].remaining_burst > 0) {
            preemptions++;
        }
        running_idx = best_idx;

        auto& p = procs[best_idx];
        if (!p.first_execution_time.has_value()) {
            p.first_execution_time = current_time;
        }

        // How long can this process run before the next event (either process completes or next arrival occurs)?
        SimTime next_event_time = current_time + p.remaining_burst;
        for (const auto& other : procs) {
            if (other.remaining_burst > 0 && other.task.arrival_time > current_time && other.task.arrival_time < next_event_time) {
                next_event_time = other.task.arrival_time;
            }
        }

        SimTime run_slice = next_event_time - current_time;
        add_gantt_slice(chart, current_time, next_event_time, p.task.pid);
        p.remaining_burst -= run_slice;
        current_time = next_event_time;

        if (p.remaining_burst == 0) {
            p.completion_time = current_time;
            completed_count++;
            running_idx = std::nullopt;
        }
    }

    std::vector<ProcessMetrics> metrics;
    metrics.reserve(procs.size());
    for (const auto& p : procs) {
        SimTime turn = p.completion_time - p.task.arrival_time;
        SimTime wait = turn - p.task.burst_time;
        SimTime resp = *p.first_execution_time - p.task.arrival_time;
        metrics.push_back(ProcessMetrics{
            .pid = p.task.pid,
            .arrival_time = p.task.arrival_time,
            .burst_time = p.task.burst_time,
            .priority = p.task.priority,
            .first_execution_time = *p.first_execution_time,
            .completion_time = p.completion_time,
            .turnaround_time = turn,
            .waiting_time = wait,
            .response_time = resp
        });
    }

    std::sort(metrics.begin(), metrics.end(), [](const ProcessMetrics& a, const ProcessMetrics& b) {
        return a.pid < b.pid;
    });

    ScheduleResult res;
    res.algorithm_name = "Shortest Remaining Time First (SRTF Preemptive)";
    res.process_metrics = metrics;
    res.gantt_chart = chart;
    res.metrics = compute_metrics(metrics, chart, preemptions);
    res.policy_notes = "Preemptive; selects shortest remaining CPU burst; tie-break by arrival time, then PID.";
    return core::Result<ScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// D. Round Robin (RR)
// ----------------------------------------------------------------------------
core::Result<ScheduleResult> CpuScheduler::run_round_robin(
    const std::vector<ProcessTask>& tasks,
    SimTime time_quantum) {
    auto val_res = validate_workload(tasks);
    if (val_res.is_err()) return core::Result<ScheduleResult>::err(val_res.error());

    if (time_quantum == 0) {
        return core::Result<ScheduleResult>::err(
            core::ErrorCode::InvalidArgument, "Round Robin time quantum must be strictly positive (> 0)");
    }

    std::vector<SimProcess> procs;
    procs.reserve(tasks.size());
    for (const auto& t : tasks) procs.emplace_back(t);

    // Initial sort of arrival pool by arrival time, then PID
    std::vector<size_t> arrival_indices(procs.size());
    for (size_t i = 0; i < procs.size(); ++i) arrival_indices[i] = i;
    std::sort(arrival_indices.begin(), arrival_indices.end(), [&](size_t a, size_t b) {
        if (procs[a].task.arrival_time != procs[b].task.arrival_time)
            return procs[a].task.arrival_time < procs[b].task.arrival_time;
        return procs[a].task.pid < procs[b].task.pid;
    });

    std::deque<size_t> ready_queue;
    size_t next_arrival_ptr = 0;
    SimTime current_time = 0;
    std::vector<GanttInterval> chart;
    size_t preemptions = 0;
    size_t completed_count = 0;

    // Load processes arriving at time 0
    while (next_arrival_ptr < arrival_indices.size() &&
           procs[arrival_indices[next_arrival_ptr]].task.arrival_time <= current_time) {
        ready_queue.push_back(arrival_indices[next_arrival_ptr]);
        next_arrival_ptr++;
    }

    while (completed_count < procs.size()) {
        if (ready_queue.empty()) {
            // Advance time to next arrival
            if (next_arrival_ptr < arrival_indices.size()) {
                SimTime next_arr = procs[arrival_indices[next_arrival_ptr]].task.arrival_time;
                add_gantt_slice(chart, current_time, next_arr, std::nullopt);
                current_time = next_arr;
                while (next_arrival_ptr < arrival_indices.size() &&
                       procs[arrival_indices[next_arrival_ptr]].task.arrival_time <= current_time) {
                    ready_queue.push_back(arrival_indices[next_arrival_ptr]);
                    next_arrival_ptr++;
                }
            }
            continue;
        }

        size_t idx = ready_queue.front();
        ready_queue.pop_front();
        auto& p = procs[idx];

        if (!p.first_execution_time.has_value()) {
            p.first_execution_time = current_time;
        }

        SimTime slice = std::min(p.remaining_burst, time_quantum);
        SimTime start_slice = current_time;
        SimTime end_slice = current_time + slice;

        add_gantt_slice(chart, start_slice, end_slice, p.task.pid);
        p.remaining_burst -= slice;
        current_time = end_slice;

        // Policy rule: Newly arriving processes arriving during (start_slice, end_slice]
        // are enqueued FIRST into the ready queue before the preempted process is requeued.
        while (next_arrival_ptr < arrival_indices.size() &&
               procs[arrival_indices[next_arrival_ptr]].task.arrival_time <= current_time) {
            ready_queue.push_back(arrival_indices[next_arrival_ptr]);
            next_arrival_ptr++;
        }

        if (p.remaining_burst > 0) {
            preemptions++;
            ready_queue.push_back(idx);
        } else {
            p.completion_time = current_time;
            completed_count++;
        }
    }

    std::vector<ProcessMetrics> metrics;
    metrics.reserve(procs.size());
    for (const auto& p : procs) {
        SimTime turn = p.completion_time - p.task.arrival_time;
        SimTime wait = turn - p.task.burst_time;
        SimTime resp = *p.first_execution_time - p.task.arrival_time;
        metrics.push_back(ProcessMetrics{
            .pid = p.task.pid,
            .arrival_time = p.task.arrival_time,
            .burst_time = p.task.burst_time,
            .priority = p.task.priority,
            .first_execution_time = *p.first_execution_time,
            .completion_time = p.completion_time,
            .turnaround_time = turn,
            .waiting_time = wait,
            .response_time = resp
        });
    }

    std::sort(metrics.begin(), metrics.end(), [](const ProcessMetrics& a, const ProcessMetrics& b) {
        return a.pid < b.pid;
    });

    ScheduleResult res;
    res.algorithm_name = "Round Robin (RR, Quantum=" + std::to_string(time_quantum) + ")";
    res.process_metrics = metrics;
    res.gantt_chart = chart;
    res.metrics = compute_metrics(metrics, chart, preemptions);
    res.policy_notes = "Preemptive with quantum=" + std::to_string(time_quantum) +
                       "; arrivals occurring at time-slice expiration join ready queue before requeued process.";
    return core::Result<ScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// E. Priority Scheduling (Non-Preemptive)
// ----------------------------------------------------------------------------
core::Result<ScheduleResult> CpuScheduler::run_priority(const std::vector<ProcessTask>& tasks) {
    auto val_res = validate_workload(tasks);
    if (val_res.is_err()) return core::Result<ScheduleResult>::err(val_res.error());

    std::vector<SimProcess> procs;
    procs.reserve(tasks.size());
    for (const auto& t : tasks) procs.emplace_back(t);

    std::vector<bool> completed(procs.size(), false);
    size_t completed_count = 0;
    SimTime current_time = 0;
    std::vector<GanttInterval> chart;

    while (completed_count < procs.size()) {
        size_t best_idx = procs.size();

        for (size_t i = 0; i < procs.size(); ++i) {
            if (!completed[i] && procs[i].task.arrival_time <= current_time) {
                if (best_idx == procs.size()) {
                    best_idx = i;
                } else {
                    const auto& a = procs[i];
                    const auto& b = procs[best_idx];
                    // Lower numerical value = higher priority
                    if (a.task.priority < b.task.priority) {
                        best_idx = i;
                    } else if (a.task.priority == b.task.priority) {
                        if (a.task.arrival_time < b.task.arrival_time) {
                            best_idx = i;
                        } else if (a.task.arrival_time == b.task.arrival_time && a.task.pid < b.task.pid) {
                            best_idx = i;
                        }
                    }
                }
            }
        }

        if (best_idx == procs.size()) {
            // Jump to next arrival
            SimTime next_arrival = UINT64_MAX;
            for (size_t i = 0; i < procs.size(); ++i) {
                if (!completed[i] && procs[i].task.arrival_time < next_arrival) {
                    next_arrival = procs[i].task.arrival_time;
                }
            }
            add_gantt_slice(chart, current_time, next_arrival, std::nullopt);
            current_time = next_arrival;
            continue;
        }

        auto& p = procs[best_idx];
        p.first_execution_time = current_time;
        SimTime end_time = current_time + p.task.burst_time;
        add_gantt_slice(chart, current_time, end_time, p.task.pid);
        p.completion_time = end_time;
        current_time = end_time;
        completed[best_idx] = true;
        completed_count++;
    }

    std::vector<ProcessMetrics> metrics;
    metrics.reserve(procs.size());
    for (const auto& p : procs) {
        SimTime turn = p.completion_time - p.task.arrival_time;
        SimTime wait = turn - p.task.burst_time;
        SimTime resp = *p.first_execution_time - p.task.arrival_time;
        metrics.push_back(ProcessMetrics{
            .pid = p.task.pid,
            .arrival_time = p.task.arrival_time,
            .burst_time = p.task.burst_time,
            .priority = p.task.priority,
            .first_execution_time = *p.first_execution_time,
            .completion_time = p.completion_time,
            .turnaround_time = turn,
            .waiting_time = wait,
            .response_time = resp
        });
    }

    std::sort(metrics.begin(), metrics.end(), [](const ProcessMetrics& a, const ProcessMetrics& b) {
        return a.pid < b.pid;
    });

    ScheduleResult res;
    res.algorithm_name = "Priority Scheduling (Non-Preemptive)";
    res.process_metrics = metrics;
    res.gantt_chart = chart;
    res.metrics = compute_metrics(metrics, chart, 0);
    res.policy_notes = "Non-preemptive; lower numerical value = higher priority (0 highest); tie-break by arrival, then PID.";
    return core::Result<ScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// F. Multilevel Feedback Queue (MLFQ)
// ----------------------------------------------------------------------------
core::Result<ScheduleResult> CpuScheduler::run_mlfq(
    const std::vector<ProcessTask>& tasks,
    const MlfqConfig& config) {
    auto val_res = validate_workload(tasks);
    if (val_res.is_err()) return core::Result<ScheduleResult>::err(val_res.error());

    if (config.queues.empty()) {
        return core::Result<ScheduleResult>::err(
            core::ErrorCode::InvalidArgument, "MLFQ configuration must contain at least one queue");
    }

    for (size_t i = 0; i < config.queues.size(); ++i) {
        const auto& q = config.queues[i];
        if (!q.is_fcfs && q.time_quantum == 0) {
            return core::Result<ScheduleResult>::err(
                core::ErrorCode::InvalidArgument,
                "MLFQ queue level " + std::to_string(i) + " has invalid zero quantum without being FCFS");
        }
    }

    std::vector<SimProcess> procs;
    procs.reserve(tasks.size());
    for (const auto& t : tasks) procs.emplace_back(t);

    std::vector<size_t> arrival_indices(procs.size());
    for (size_t i = 0; i < procs.size(); ++i) arrival_indices[i] = i;
    std::sort(arrival_indices.begin(), arrival_indices.end(), [&](size_t a, size_t b) {
        if (procs[a].task.arrival_time != procs[b].task.arrival_time)
            return procs[a].task.arrival_time < procs[b].task.arrival_time;
        return procs[a].task.pid < procs[b].task.pid;
    });

    size_t num_queues = config.queues.size();
    std::vector<std::deque<size_t>> queues(num_queues);
    size_t next_arrival_ptr = 0;
    SimTime current_time = 0;
    std::vector<GanttInterval> chart;
    size_t preemptions = 0;
    size_t completed_count = 0;
    SimTime next_boost_time = (config.priority_boost_interval > 0) ? config.priority_boost_interval : UINT64_MAX;

    auto admit_arrivals = [&](SimTime time_limit) {
        while (next_arrival_ptr < arrival_indices.size() &&
               procs[arrival_indices[next_arrival_ptr]].task.arrival_time <= time_limit) {
            size_t idx = arrival_indices[next_arrival_ptr];
            procs[idx].current_queue = 0;
            procs[idx].current_quantum_used = 0;
            queues[0].push_back(idx);
            next_arrival_ptr++;
        }
    };

    auto apply_priority_boost = [&]() {
        // Move all uncompleted ready processes to queue 0 and reset quantum usage
        for (size_t q = 1; q < num_queues; ++q) {
            while (!queues[q].empty()) {
                size_t p_idx = queues[q].front();
                queues[q].pop_front();
                procs[p_idx].current_queue = 0;
                procs[p_idx].current_quantum_used = 0;
                queues[0].push_back(p_idx);
            }
        }
        for (size_t idx : queues[0]) {
            procs[idx].current_quantum_used = 0;
        }
    };

    admit_arrivals(current_time);

    while (completed_count < procs.size()) {
        // Find highest priority non-empty queue
        size_t active_q = num_queues;
        for (size_t q = 0; q < num_queues; ++q) {
            if (!queues[q].empty()) {
                active_q = q;
                break;
            }
        }

        if (active_q == num_queues) {
            // All queues empty: jump to next event (either next arrival or next boost)
            SimTime next_time = UINT64_MAX;
            if (next_arrival_ptr < arrival_indices.size()) {
                next_time = procs[arrival_indices[next_arrival_ptr]].task.arrival_time;
            }
            if (next_boost_time < next_time && next_boost_time > current_time) {
                next_time = next_boost_time;
            }

            add_gantt_slice(chart, current_time, next_time, std::nullopt);
            current_time = next_time;

            if (current_time >= next_boost_time) {
                apply_priority_boost();
                next_boost_time += config.priority_boost_interval;
            }
            admit_arrivals(current_time);
            continue;
        }

        size_t idx = queues[active_q].front();
        queues[active_q].pop_front();
        auto& p = procs[idx];

        if (!p.first_execution_time.has_value()) {
            p.first_execution_time = current_time;
        }

        const auto& q_cfg = config.queues[active_q];
        SimTime allowed_quantum = q_cfg.is_fcfs ? p.remaining_burst : (q_cfg.time_quantum - p.current_quantum_used);

        // Maximum time this slice can execute without interruption
        SimTime slice_limit = std::min(p.remaining_burst, allowed_quantum);

        // Check if higher-priority queue preemption will occur via new arrival
        SimTime next_arrival_event = UINT64_MAX;
        if (active_q > 0 && next_arrival_ptr < arrival_indices.size()) {
            next_arrival_event = procs[arrival_indices[next_arrival_ptr]].task.arrival_time;
        }

        // Check if boost occurs during this slice
        SimTime target_end = current_time + slice_limit;
        bool boost_triggered = false;
        if (target_end >= next_boost_time) {
            target_end = next_boost_time;
            boost_triggered = true;
        }

        // Higher-priority arrival preemption check
        bool higher_arrival_triggered = false;
        if (next_arrival_event < target_end && next_arrival_event > current_time) {
            target_end = next_arrival_event;
            higher_arrival_triggered = true;
            boost_triggered = false;
        }

        SimTime actual_slice = target_end - current_time;
        add_gantt_slice(chart, current_time, target_end, p.task.pid);
        p.remaining_burst -= actual_slice;
        p.current_quantum_used += actual_slice;
        current_time = target_end;

        admit_arrivals(current_time);

        if (boost_triggered) {
            apply_priority_boost();
            next_boost_time += config.priority_boost_interval;
        }

        if (p.remaining_burst == 0) {
            p.completion_time = current_time;
            completed_count++;
        } else {
            // Process still needs CPU
            if (boost_triggered) {
                // Was boosted to queue 0
                p.current_queue = 0;
                p.current_quantum_used = 0;
                queues[0].push_back(idx);
                preemptions++;
            } else if (higher_arrival_triggered) {
                // Involuntarily preempted by higher queue arrival: return to front of current queue
                preemptions++;
                queues[active_q].push_front(idx);
            } else if (!q_cfg.is_fcfs && p.current_quantum_used >= q_cfg.time_quantum) {
                // Exhausted quantum: demote to next lower queue
                preemptions++;
                size_t next_q = std::min(active_q + 1, num_queues - 1);
                p.current_queue = static_cast<uint32_t>(next_q);
                p.current_quantum_used = 0;
                queues[next_q].push_back(idx);
            } else {
                // Did not exhaust quantum (e.g. FCFS or interrupted): requeue to current queue
                queues[active_q].push_back(idx);
            }
        }
    }

    std::vector<ProcessMetrics> metrics;
    metrics.reserve(procs.size());
    for (const auto& p : procs) {
        SimTime turn = p.completion_time - p.task.arrival_time;
        SimTime wait = turn - p.task.burst_time;
        SimTime resp = *p.first_execution_time - p.task.arrival_time;
        metrics.push_back(ProcessMetrics{
            .pid = p.task.pid,
            .arrival_time = p.task.arrival_time,
            .burst_time = p.task.burst_time,
            .priority = p.task.priority,
            .first_execution_time = *p.first_execution_time,
            .completion_time = p.completion_time,
            .turnaround_time = turn,
            .waiting_time = wait,
            .response_time = resp
        });
    }

    std::sort(metrics.begin(), metrics.end(), [](const ProcessMetrics& a, const ProcessMetrics& b) {
        return a.pid < b.pid;
    });

    ScheduleResult res;
    res.algorithm_name = "Multilevel Feedback Queue (MLFQ, " + std::to_string(num_queues) + " tiers)";
    res.process_metrics = metrics;
    res.gantt_chart = chart;
    res.metrics = compute_metrics(metrics, chart, preemptions);
    res.policy_notes = "Strict queue priority; quantum demotion across tiers; periodic priority boost every " +
                       std::to_string(config.priority_boost_interval) + " units.";
    return core::Result<ScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// Generic Dispatcher
// ----------------------------------------------------------------------------
core::Result<ScheduleResult> CpuScheduler::run(
    SchedAlgorithm algo,
    const std::vector<ProcessTask>& tasks,
    SimTime time_quantum,
    const std::optional<MlfqConfig>& mlfq_cfg) {
    switch (algo) {
        case SchedAlgorithm::FCFS:
            return run_fcfs(tasks);
        case SchedAlgorithm::SJF:
            return run_sjf(tasks);
        case SchedAlgorithm::SRTF:
            return run_srtf(tasks);
        case SchedAlgorithm::RoundRobin:
            return run_round_robin(tasks, time_quantum);
        case SchedAlgorithm::Priority:
            return run_priority(tasks);
        case SchedAlgorithm::MLFQ: {
            MlfqConfig cfg = mlfq_cfg.value_or(MlfqConfig::default_config());
            return run_mlfq(tasks, cfg);
        }
    }
    return core::Result<ScheduleResult>::err(
        core::ErrorCode::InvalidArgument, "Unsupported scheduling algorithm");
}

// ----------------------------------------------------------------------------
// Built-in Workloads
// ----------------------------------------------------------------------------
std::vector<ProcessTask> CpuScheduler::get_workload_basic() {
    return {
        ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 10, .priority = 3},
        ProcessTask{.pid = 2, .arrival_time = 0, .burst_time = 4,  .priority = 1},
        ProcessTask{.pid = 3, .arrival_time = 0, .burst_time = 2,  .priority = 2}
    };
}

std::vector<ProcessTask> CpuScheduler::get_workload_arrivals() {
    return {
        ProcessTask{.pid = 1, .arrival_time = 0,  .burst_time = 3, .priority = 2},
        ProcessTask{.pid = 2, .arrival_time = 2,  .burst_time = 6, .priority = 1},
        ProcessTask{.pid = 3, .arrival_time = 4,  .burst_time = 4, .priority = 3},
        ProcessTask{.pid = 4, .arrival_time = 15, .burst_time = 5, .priority = 1}
    };
}

std::vector<ProcessTask> CpuScheduler::get_workload_priority_demo() {
    return {
        ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 4, .priority = 3},
        ProcessTask{.pid = 2, .arrival_time = 1, .burst_time = 5, .priority = 1},
        ProcessTask{.pid = 3, .arrival_time = 2, .burst_time = 2, .priority = 2},
        ProcessTask{.pid = 4, .arrival_time = 3, .burst_time = 1, .priority = 0}
    };
}

// ----------------------------------------------------------------------------
// Formatters
// ----------------------------------------------------------------------------
std::string CpuScheduler::format_gantt_chart(const std::vector<GanttInterval>& chart) {
    if (chart.empty()) {
        return "[Empty Timeline]\n";
    }

    std::ostringstream timeline_bar;
    std::ostringstream time_labels;

    timeline_bar << "|";
    time_labels << chart.front().start_time;

    for (const auto& slice : chart) {
        std::string label = slice.is_idle() ? "IDLE" : ("P" + std::to_string(*slice.pid));
        timeline_bar << "  " << label << "  |";

        std::string end_str = std::to_string(slice.end_time);
        size_t current_len = time_labels.str().length();
        size_t target_len = timeline_bar.str().length() - 1;

        if (target_len > current_len) {
            time_labels << std::string(target_len - current_len, ' ');
        }
        time_labels << end_str;
    }

    std::ostringstream oss;
    oss << timeline_bar.str() << "\n" << time_labels.str() << "\n";
    return oss.str();
}

std::string CpuScheduler::format_metrics_table(const ScheduleResult& result) {
    std::ostringstream oss;
    oss << "+-----+---------+-------+----------+--------+------------+---------+----------+\n"
        << "| PID | Arrival | Burst | Priority | Finish | Turnaround | Waiting | Response |\n"
        << "+-----+---------+-------+----------+--------+------------+---------+----------+\n";

    for (const auto& pm : result.process_metrics) {
        oss << "| " << std::setw(3) << pm.pid << " | "
            << std::setw(7) << pm.arrival_time << " | "
            << std::setw(5) << pm.burst_time << " | "
            << std::setw(8) << pm.priority << " | "
            << std::setw(6) << pm.completion_time << " | "
            << std::setw(10) << pm.turnaround_time << " | "
            << std::setw(7) << pm.waiting_time << " | "
            << std::setw(8) << pm.response_time << " |\n";
    }
    oss << "+-----+---------+-------+----------+--------+------------+---------+----------+\n";
    return oss.str();
}

std::string CpuScheduler::format_summary_metrics(const ScheduleResult& result) {
    std::ostringstream oss;
    const auto& m = result.metrics;
    oss << std::fixed << std::setprecision(2);
    oss << "Aggregate Performance Metrics:\n"
        << "  Makespan (Total Elapsed Time) : " << m.makespan << " time units\n"
        << "  CPU Busy Time                 : " << m.cpu_busy_time << " time units\n"
        << "  CPU Idle Time                 : " << m.total_idle_time << " time units\n"
        << "  CPU Utilization               : " << m.cpu_utilization << "%\n"
        << "  Average Turnaround Time       : " << m.average_turnaround_time << " time units\n"
        << "  Average Waiting Time          : " << m.average_waiting_time << " time units\n"
        << "  Average Response Time         : " << m.average_response_time << " time units\n"
        << "  Process Dispatches            : " << m.dispatch_count << "\n"
        << "  Preemptions                   : " << m.preemption_count << "\n"
        << "  Estimated Context Switches    : " << m.context_switch_count << " (direct process-to-process transitions, excluding idle)\n";
    return oss.str();
}

} // namespace kernox::sim
