#include "kernox/modules/sim/cpu_sched.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <vector>

using namespace kernox::sim;
using namespace kernox::core;

// Helper to verify invariants on any valid ScheduleResult
static int verify_invariants(const std::vector<ProcessTask>& tasks, const ScheduleResult& res) {
    KERNOX_ASSERT_EQ(res.process_metrics.size(), tasks.size());

    SimTime total_burst_expected = 0;
    SimTime min_arrival = UINT64_MAX;
    SimTime max_completion = 0;

    for (const auto& task : tasks) {
        total_burst_expected += task.burst_time;
        if (task.arrival_time < min_arrival) min_arrival = task.arrival_time;
    }

    // Verify per-process metrics
    for (const auto& pm : res.process_metrics) {
        auto it = std::find_if(tasks.begin(), tasks.end(), [&](const ProcessTask& t) {
            return t.pid == pm.pid;
        });
        KERNOX_ASSERT_TRUE(it != tasks.end());
        KERNOX_ASSERT_EQ(pm.burst_time, it->burst_time);
        KERNOX_ASSERT_EQ(pm.arrival_time, it->arrival_time);

        // Invariant 1: No process executes before arrival
        KERNOX_ASSERT_TRUE(pm.first_execution_time >= pm.arrival_time);

        // Invariant 2: Completion time >= arrival + burst
        KERNOX_ASSERT_TRUE(pm.completion_time >= pm.arrival_time + pm.burst_time);

        // Invariant 3: Turnaround = Completion - Arrival
        KERNOX_ASSERT_EQ(pm.turnaround_time, pm.completion_time - pm.arrival_time);

        // Invariant 4: Waiting = Turnaround - Burst
        KERNOX_ASSERT_EQ(pm.waiting_time, pm.turnaround_time - pm.burst_time);

        // Invariant 5: Response = FirstExecution - Arrival
        KERNOX_ASSERT_EQ(pm.response_time, pm.first_execution_time - pm.arrival_time);

        if (pm.completion_time > max_completion) max_completion = pm.completion_time;
    }

    // Invariant 6: CPU Busy Time equals sum of all process bursts
    KERNOX_ASSERT_EQ(res.metrics.cpu_busy_time, total_burst_expected);

    // Invariant 7: Makespan equals max_completion - min_arrival
    SimTime expected_makespan = (max_completion >= min_arrival) ? (max_completion - min_arrival) : 0;
    KERNOX_ASSERT_EQ(res.metrics.makespan, expected_makespan);

    // Invariant 8: Gantt chart chronological order, positive durations, non-overlapping
    SimTime previous_end = 0;
    SimTime sum_gantt_busy = 0;
    for (size_t i = 0; i < res.gantt_chart.size(); ++i) {
        const auto& slice = res.gantt_chart[i];
        KERNOX_ASSERT_TRUE(slice.end_time > slice.start_time);
        if (i > 0) {
            KERNOX_ASSERT_EQ(slice.start_time, previous_end);
        }
        previous_end = slice.end_time;
        if (!slice.is_idle()) {
            sum_gantt_busy += slice.duration();
        }
    }
    KERNOX_ASSERT_EQ(sum_gantt_busy, total_burst_expected);
    return 0;
}

int main() {
    // ------------------------------------------------------------------------
    // 1. Validation & Edge Cases
    // ------------------------------------------------------------------------
    {
        // Empty workload
        auto err_empty = CpuScheduler::run_fcfs({});
        KERNOX_ASSERT_TRUE(err_empty.is_err());
        KERNOX_ASSERT_EQ(err_empty.error().code, ErrorCode::InvalidArgument);

        // Zero burst duration
        std::vector<ProcessTask> zero_burst = {
            ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 0, .priority = 1}
        };
        auto err_zero = CpuScheduler::run_fcfs(zero_burst);
        KERNOX_ASSERT_TRUE(err_zero.is_err());
        KERNOX_ASSERT_EQ(err_zero.error().code, ErrorCode::InvalidArgument);

        // Duplicate PID
        std::vector<ProcessTask> duplicate_pid = {
            ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 5, .priority = 1},
            ProcessTask{.pid = 1, .arrival_time = 2, .burst_time = 3, .priority = 2}
        };
        auto err_dup = CpuScheduler::run_fcfs(duplicate_pid);
        KERNOX_ASSERT_TRUE(err_dup.is_err());
        KERNOX_ASSERT_EQ(err_dup.error().code, ErrorCode::InvalidArgument);

        // Round Robin zero quantum
        std::vector<ProcessTask> valid_single = {
            ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 4, .priority = 1}
        };
        auto err_rr_zero = CpuScheduler::run_round_robin(valid_single, 0);
        KERNOX_ASSERT_TRUE(err_rr_zero.is_err());
        KERNOX_ASSERT_EQ(err_rr_zero.error().code, ErrorCode::InvalidArgument);
    }

    // ------------------------------------------------------------------------
    // 2. Single Process Execution
    // ------------------------------------------------------------------------
    {
        std::vector<ProcessTask> tasks = {
            ProcessTask{.pid = 42, .arrival_time = 5, .burst_time = 7, .priority = 0}
        };
        auto res = CpuScheduler::run_fcfs(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // Hand-calculated exact results
        KERNOX_ASSERT_EQ(r.process_metrics[0].first_execution_time, 5);
        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 12);
        KERNOX_ASSERT_EQ(r.process_metrics[0].turnaround_time, 7);
        KERNOX_ASSERT_EQ(r.process_metrics[0].waiting_time, 0);
        KERNOX_ASSERT_EQ(r.process_metrics[0].response_time, 0);
        KERNOX_ASSERT_EQ(r.metrics.total_idle_time, 5); // 0 to 5 is idle
    }

    // ------------------------------------------------------------------------
    // 3. FCFS - Basic Workload (All arrive at 0)
    // P1: burst 10, P2: burst 4, P3: burst 2
    // Expected Gantt: [0..10 P1], [10..14 P2], [14..16 P3]
    // ------------------------------------------------------------------------
    {
        auto tasks = CpuScheduler::get_workload_basic();
        auto res = CpuScheduler::run_fcfs(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // P1: C=10, Turn=10, Wait=0, Resp=0
        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 10);
        KERNOX_ASSERT_EQ(r.process_metrics[0].waiting_time, 0);
        // P2: C=14, Turn=14, Wait=10, Resp=10
        KERNOX_ASSERT_EQ(r.process_metrics[1].completion_time, 14);
        KERNOX_ASSERT_EQ(r.process_metrics[1].waiting_time, 10);
        // P3: C=16, Turn=16, Wait=14, Resp=14
        KERNOX_ASSERT_EQ(r.process_metrics[2].completion_time, 16);
        KERNOX_ASSERT_EQ(r.process_metrics[2].waiting_time, 14);

        // Average Waiting = (0 + 10 + 14) / 3 = 8.0
        KERNOX_ASSERT_EQ(r.metrics.average_waiting_time, 8.0);
    }

    // ------------------------------------------------------------------------
    // 4. SJF (Non-Preemptive) - Basic Workload
    // P1: burst 10, P2: burst 4, P3: burst 2 (all arrive 0)
    // Order: P3 (burst 2), then P2 (burst 4), then P1 (burst 10)
    // Expected Gantt: [0..2 P3], [2..6 P2], [6..16 P1]
    // ------------------------------------------------------------------------
    {
        auto tasks = CpuScheduler::get_workload_basic();
        auto res = CpuScheduler::run_sjf(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // P3 runs first: finish at 2, wait 0
        KERNOX_ASSERT_EQ(r.process_metrics[2].completion_time, 2);
        KERNOX_ASSERT_EQ(r.process_metrics[2].waiting_time, 0);
        // P2 runs second: finish at 6, wait 2
        KERNOX_ASSERT_EQ(r.process_metrics[1].completion_time, 6);
        KERNOX_ASSERT_EQ(r.process_metrics[1].waiting_time, 2);
        // P1 runs third: finish at 16, wait 6
        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 16);
        KERNOX_ASSERT_EQ(r.process_metrics[0].waiting_time, 6);

        // Avg Waiting = (0 + 2 + 6) / 3 = 8.0 / 3 = 2.6666...
        KERNOX_ASSERT_TRUE(r.metrics.average_waiting_time < 2.67 && r.metrics.average_waiting_time > 2.66);
    }

    // ------------------------------------------------------------------------
    // 5. SJF with Idle Gaps between processes
    // P1: arr 0, burst 3 -> runs [0..3]
    // P2: arr 6, burst 2 -> idle [3..6], runs [6..8]
    // ------------------------------------------------------------------------
    {
        std::vector<ProcessTask> tasks = {
            ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 3, .priority = 0},
            ProcessTask{.pid = 2, .arrival_time = 6, .burst_time = 2, .priority = 0}
        };
        auto res = CpuScheduler::run_sjf(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 3);
        KERNOX_ASSERT_EQ(r.process_metrics[1].completion_time, 8);
        KERNOX_ASSERT_EQ(r.metrics.total_idle_time, 3); // [3..6] idle
    }

    // ------------------------------------------------------------------------
    // 6. SRTF Preemption Test
    // P1: arrival 0, burst 8
    // P2: arrival 1, burst 2
    // At t=0, P1 starts. At t=1, P2 arrives with remaining burst 2 < P1 remaining 7.
    // P2 preempts P1, runs [1..3] and completes.
    // P1 resumes at 3, runs [3..10].
    // ------------------------------------------------------------------------
    {
        std::vector<ProcessTask> tasks = {
            ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 8, .priority = 0},
            ProcessTask{.pid = 2, .arrival_time = 1, .burst_time = 2, .priority = 0}
        };
        auto res = CpuScheduler::run_srtf(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // Preemption occurred
        KERNOX_ASSERT_EQ(r.metrics.preemption_count, 1);
        KERNOX_ASSERT_EQ(r.process_metrics[0].first_execution_time, 0);
        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 10);
        KERNOX_ASSERT_EQ(r.process_metrics[1].first_execution_time, 1);
        KERNOX_ASSERT_EQ(r.process_metrics[1].completion_time, 3);

        // Gantt chart: [0..1 P1], [1..3 P2], [3..10 P1]
        KERNOX_ASSERT_EQ(r.gantt_chart.size(), 3);
        KERNOX_ASSERT_EQ(r.gantt_chart[0].pid.value(), 1);
        KERNOX_ASSERT_EQ(r.gantt_chart[0].duration(), 1);
        KERNOX_ASSERT_EQ(r.gantt_chart[1].pid.value(), 2);
        KERNOX_ASSERT_EQ(r.gantt_chart[1].duration(), 2);
        KERNOX_ASSERT_EQ(r.gantt_chart[2].pid.value(), 1);
        KERNOX_ASSERT_EQ(r.gantt_chart[2].duration(), 7);
    }

    // ------------------------------------------------------------------------
    // 7. Round Robin - Multiple time slices & Quantum boundary arrival
    // P1: arr 0, burst 5
    // P2: arr 2, burst 3
    // Quantum = 2
    // t=0: P1 runs [0..2]. At t=2, P1 slice ends.
    // Exactly at t=2, P2 arrives!
    // Rule: P2 enqueued before P1 requeued. Ready queue: [P2, P1(rem 3)].
    // t=2..4: P2 runs (rem 1). Requeued.
    // t=4..6: P1 runs (rem 1). Requeued.
    // t=6..7: P2 runs (finishes at 7).
    // t=7..8: P1 runs (finishes at 8).
    // ------------------------------------------------------------------------
    {
        std::vector<ProcessTask> tasks = {
            ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 5, .priority = 0},
            ProcessTask{.pid = 2, .arrival_time = 2, .burst_time = 3, .priority = 0}
        };
        auto res = CpuScheduler::run_round_robin(tasks, 2);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // Check completions
        KERNOX_ASSERT_EQ(r.process_metrics[1].completion_time, 7); // P2 finishes at 7
        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 8); // P1 finishes at 8
    }

    // ------------------------------------------------------------------------
    // 8. Priority Scheduling (Non-Preemptive)
    // Priority convention: 0 is highest
    // P1: arr 0, burst 4, prio 3
    // P2: arr 1, burst 5, prio 1
    // P3: arr 2, burst 2, prio 2
    // P4: arr 3, burst 1, prio 0
    // At t=0: only P1 is ready -> runs [0..4] (non-preemptive).
    // At t=4: P2(prio 1), P3(prio 2), P4(prio 0) are ready.
    // Highest priority is P4 (prio 0) -> runs [4..5].
    // At t=5: P2(prio 1) vs P3(prio 2) -> P2 runs [5..10].
    // At t=10: P3 runs [10..12].
    // ------------------------------------------------------------------------
    {
        auto tasks = CpuScheduler::get_workload_priority_demo();
        auto res = CpuScheduler::run_priority(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // P1: C=4
        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 4);
        // P4: C=5
        KERNOX_ASSERT_EQ(r.process_metrics[3].completion_time, 5);
        // P2: C=10
        KERNOX_ASSERT_EQ(r.process_metrics[1].completion_time, 10);
        // P3: C=12
        KERNOX_ASSERT_EQ(r.process_metrics[2].completion_time, 12);
    }

    // ------------------------------------------------------------------------
    // 9. MLFQ Demotion & Priority Boost
    // ------------------------------------------------------------------------
    {
        std::vector<ProcessTask> tasks = {
            ProcessTask{.pid = 1, .arrival_time = 0, .burst_time = 8, .priority = 0},
            ProcessTask{.pid = 2, .arrival_time = 0, .burst_time = 2, .priority = 0}
        };
        MlfqConfig cfg{
            .queues = {
                MlfqQueueConfig{.priority_level = 0, .time_quantum = 2, .is_fcfs = false},
                MlfqQueueConfig{.priority_level = 1, .time_quantum = 4, .is_fcfs = false},
                MlfqQueueConfig{.priority_level = 2, .time_quantum = 0, .is_fcfs = true}
            },
            .priority_boost_interval = 10
        };
        auto res = CpuScheduler::run_mlfq(tasks, cfg);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // P1 runs [0..2] in Q0, uses quantum 2 -> demoted to Q1
        // P2 runs [2..4] in Q0, uses quantum 2 -> completes at t=4!
        KERNOX_ASSERT_EQ(r.process_metrics[1].completion_time, 4);
        // P1 runs in Q1 [4..8] (quantum 4) -> demoted to Q2
        // P1 finishes in Q2 [8..10] -> completes at t=10!
        KERNOX_ASSERT_EQ(r.process_metrics[0].completion_time, 10);
    }

    // ------------------------------------------------------------------------
    // 10. Regression Test: SRTF 'arrivals' workload context-switch audit
    // Workload: P1(arr 0, b 3), P2(arr 2, b 6), P3(arr 4, b 4), P4(arr 15, b 5)
    // Expected Gantt:
    //   [0, 3, P1], [3, 4, P2], [4, 8, P3], [8, 13, P2], [13, 15, IDLE], [15, 20, P4]
    // Transitions between distinct executing processes:
    //   1. P1 -> P2
    //   2. P2 -> P3
    //   3. P3 -> P2
    //   P2 -> IDLE is a yield to idle (not a process-to-process switch)
    //   IDLE -> P4 is a dispatch from idle (not a process-to-process switch)
    // Total context switches = EXACTLY 3.
    // ------------------------------------------------------------------------
    {
        auto tasks = CpuScheduler::get_workload_arrivals();
        auto res = CpuScheduler::run_srtf(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        // Verify Gantt slices
        KERNOX_ASSERT_EQ(r.gantt_chart.size(), 6);
        KERNOX_ASSERT_EQ(r.gantt_chart[0].pid.value(), 1);
        KERNOX_ASSERT_EQ(r.gantt_chart[0].start_time, 0);
        KERNOX_ASSERT_EQ(r.gantt_chart[0].end_time, 3);

        KERNOX_ASSERT_EQ(r.gantt_chart[1].pid.value(), 2);
        KERNOX_ASSERT_EQ(r.gantt_chart[1].start_time, 3);
        KERNOX_ASSERT_EQ(r.gantt_chart[1].end_time, 4);

        KERNOX_ASSERT_EQ(r.gantt_chart[2].pid.value(), 3);
        KERNOX_ASSERT_EQ(r.gantt_chart[2].start_time, 4);
        KERNOX_ASSERT_EQ(r.gantt_chart[2].end_time, 8);

        KERNOX_ASSERT_EQ(r.gantt_chart[3].pid.value(), 2);
        KERNOX_ASSERT_EQ(r.gantt_chart[3].start_time, 8);
        KERNOX_ASSERT_EQ(r.gantt_chart[3].end_time, 13);

        KERNOX_ASSERT_TRUE(r.gantt_chart[4].is_idle());
        KERNOX_ASSERT_EQ(r.gantt_chart[4].start_time, 13);
        KERNOX_ASSERT_EQ(r.gantt_chart[4].end_time, 15);

        KERNOX_ASSERT_EQ(r.gantt_chart[5].pid.value(), 4);
        KERNOX_ASSERT_EQ(r.gantt_chart[5].start_time, 15);
        KERNOX_ASSERT_EQ(r.gantt_chart[5].end_time, 20);

        // Verify exact metrics
        KERNOX_ASSERT_EQ(r.metrics.dispatch_count, 5);
        KERNOX_ASSERT_EQ(r.metrics.preemption_count, 1);
        KERNOX_ASSERT_EQ(r.metrics.context_switch_count, 3); // Must be 3, NOT 4!
        KERNOX_ASSERT_EQ(r.metrics.cpu_busy_time, 18);
        KERNOX_ASSERT_EQ(r.metrics.total_idle_time, 2);
        KERNOX_ASSERT_EQ(r.metrics.makespan, 20);
        KERNOX_ASSERT_EQ(r.metrics.cpu_utilization, 90.0);
        KERNOX_ASSERT_EQ(r.metrics.average_waiting_time, 1.25);
        KERNOX_ASSERT_EQ(r.metrics.average_turnaround_time, 5.75);
        KERNOX_ASSERT_EQ(r.metrics.average_response_time, 0.25);
    }

    // ------------------------------------------------------------------------
    // 11. Idle Separation Audit: Transitions across idle are not process context switches
    // Three processes separated entirely by idle intervals:
    // P1 [0..2], IDLE [2..5], P2 [5..7], IDLE [7..10], P3 [10..12]
    // Dispatches = 3, Preemptions = 0, Process-to-Process Context Switches = 0
    // ------------------------------------------------------------------------
    {
        std::vector<ProcessTask> tasks = {
            ProcessTask{.pid = 1, .arrival_time = 0,  .burst_time = 2, .priority = 0},
            ProcessTask{.pid = 2, .arrival_time = 5,  .burst_time = 2, .priority = 0},
            ProcessTask{.pid = 3, .arrival_time = 10, .burst_time = 2, .priority = 0}
        };
        auto res = CpuScheduler::run_fcfs(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_invariants(tasks, r), 0);

        KERNOX_ASSERT_EQ(r.metrics.dispatch_count, 3);
        KERNOX_ASSERT_EQ(r.metrics.preemption_count, 0);
        KERNOX_ASSERT_EQ(r.metrics.context_switch_count, 0); // 0 direct process-to-process transitions
        KERNOX_ASSERT_EQ(r.metrics.total_idle_time, 6);      // [2..5] and [7..10]
        KERNOX_ASSERT_EQ(r.metrics.cpu_busy_time, 6);
    }

    // ------------------------------------------------------------------------
    // 12. Formatters sanity check
    // ------------------------------------------------------------------------
    {
        auto tasks = CpuScheduler::get_workload_basic();
        auto res = CpuScheduler::run_fcfs(tasks);
        KERNOX_ASSERT_TRUE(res.is_ok());

        std::string table = CpuScheduler::format_metrics_table(res.value());
        KERNOX_ASSERT_TRUE(!table.empty());

        std::string chart = CpuScheduler::format_gantt_chart(res.value().gantt_chart);
        KERNOX_ASSERT_TRUE(!chart.empty());

        std::string summary = CpuScheduler::format_summary_metrics(res.value());
        KERNOX_ASSERT_TRUE(!summary.empty());
    }

    std::cout << "[PASS] test_cpu_sched completed successfully.\n";
    return 0;
}
