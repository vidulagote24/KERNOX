#include "kernox/modules/intel/process_model.hpp"
#include "kernox/modules/sim/cpu_sched.hpp"
#include "test_harness.hpp"

#include <iostream>
#include <limits>
#include <string>

using namespace kernox::intel;
using namespace kernox::core;
namespace sim = kernox::sim;

static int test_process_creation_and_pid_allocation() {
    std::cout << "Running test_process_creation_and_pid_allocation...\n";

    SimulatedProcess::reset_pid_generator(1000);

    auto p1_res = SimulatedProcess::create("system_server", 5, ProcessImportance::Foreground, 256 * 1024 * 1024, 100, 0);
    KERNOX_ASSERT_TRUE(p1_res.is_ok());
    const auto& p1 = p1_res.value();
    KERNOX_ASSERT_EQ(p1.pid(), 1000u);
    KERNOX_ASSERT_EQ(p1.name(), "system_server");
    KERNOX_ASSERT_EQ(p1.state(), ProcessLifecycleState::Created);
    KERNOX_ASSERT_EQ(p1.cpu_priority(), 5u);
    KERNOX_ASSERT_EQ(p1.importance(), ProcessImportance::Foreground);
    KERNOX_ASSERT_EQ(p1.memory().memory_requirement_bytes, 256u * 1024u * 1024u);
    KERNOX_ASSERT_EQ(p1.memory().memory_consumed_bytes, 0u);
    KERNOX_ASSERT_EQ(p1.memory().peak_memory_bytes, 0u);
    KERNOX_ASSERT_EQ(p1.cpu_burst_total(), 100u);
    KERNOX_ASSERT_EQ(p1.cpu_burst_remaining(), 100u);
    KERNOX_ASSERT_EQ(p1.cpu_time_executed(), 0u);
    KERNOX_ASSERT_EQ(p1.creation_time_ticks(), 0u);
    KERNOX_ASSERT_TRUE(p1.is_alive());
    KERNOX_ASSERT_FALSE(p1.is_terminal());

    auto p2_res = SimulatedProcess::create("surfaceflinger", 0, ProcessImportance::Foreground, 512 * 1024 * 1024, 50, 10);
    KERNOX_ASSERT_TRUE(p2_res.is_ok());
    const auto& p2 = p2_res.value();
    KERNOX_ASSERT_EQ(p2.pid(), 1001u);
    KERNOX_ASSERT_EQ(p2.name(), "surfaceflinger");
    KERNOX_ASSERT_EQ(p2.creation_time_ticks(), 10u);

    auto p3_res = SimulatedProcess::create("media.extractor", 20, ProcessImportance::Service, 64 * 1024 * 1024, 80, 20);
    KERNOX_ASSERT_TRUE(p3_res.is_ok());
    const auto& p3 = p3_res.value();
    KERNOX_ASSERT_EQ(p3.pid(), 1002u);

    // Test custom reset
    SimulatedProcess::reset_pid_generator(5000);
    auto p4_res = SimulatedProcess::create("custom_app", 50, ProcessImportance::Cached, 32 * 1024 * 1024, 10, 0);
    KERNOX_ASSERT_TRUE(p4_res.is_ok());
    KERNOX_ASSERT_EQ(p4_res.value().pid(), 5000u);

    return 0;
}

static int test_parameter_validation_and_rejections() {
    std::cout << "Running test_parameter_validation_and_rejections...\n";

    // 1. Empty name
    auto err_empty_name = SimulatedProcess::create("", 10, ProcessImportance::Foreground, 1024, 10);
    KERNOX_ASSERT_TRUE(err_empty_name.is_err());
    KERNOX_ASSERT_EQ(err_empty_name.error().code, ErrorCode::InvalidArgument);

    // 2. Whitespace-only name
    auto err_ws_name = SimulatedProcess::create("   \t\n  ", 10, ProcessImportance::Foreground, 1024, 10);
    KERNOX_ASSERT_TRUE(err_ws_name.is_err());
    KERNOX_ASSERT_EQ(err_ws_name.error().code, ErrorCode::InvalidArgument);

    // 3. Name trimming
    auto trimmed_res = SimulatedProcess::create("  chrome_browser  ", 10, ProcessImportance::Foreground, 1024, 10);
    KERNOX_ASSERT_TRUE(trimmed_res.is_ok());
    KERNOX_ASSERT_EQ(trimmed_res.value().name(), "chrome_browser");

    // 4. Invalid CPU Priority (> 99)
    auto err_prio = SimulatedProcess::create("bad_prio", 100, ProcessImportance::Foreground, 1024, 10);
    KERNOX_ASSERT_TRUE(err_prio.is_err());
    KERNOX_ASSERT_EQ(err_prio.error().code, ErrorCode::InvalidArgument);

    // Priority 99 is valid (boundary test)
    auto max_prio = SimulatedProcess::create("max_prio", 99, ProcessImportance::Cached, 1024, 10);
    KERNOX_ASSERT_TRUE(max_prio.is_ok());

    // 5. Zero CPU burst ticks
    auto err_burst = SimulatedProcess::create("bad_burst", 10, ProcessImportance::Foreground, 1024, 0);
    KERNOX_ASSERT_TRUE(err_burst.is_err());
    KERNOX_ASSERT_EQ(err_burst.error().code, ErrorCode::InvalidArgument);

    // 6. Zero memory requirement
    auto err_mem = SimulatedProcess::create("bad_mem", 10, ProcessImportance::Foreground, 0, 10);
    KERNOX_ASSERT_TRUE(err_mem.is_err());
    KERNOX_ASSERT_EQ(err_mem.error().code, ErrorCode::InvalidArgument);

    return 0;
}

static int test_lifecycle_standard_happy_paths() {
    std::cout << "Running test_lifecycle_standard_happy_paths...\n";

    // Flow 1: Created -> Ready -> Running -> Completed
    {
        auto p_res = SimulatedProcess::create("app_one", 10, ProcessImportance::Foreground, 1024, 50, 0);
        KERNOX_ASSERT_TRUE(p_res.is_ok());
        auto& p = p_res.value();
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Created);

        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Ready);

        KERNOX_ASSERT_TRUE(p.dispatch_running(10).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Running);
        KERNOX_ASSERT_EQ(p.last_active_time_ticks(), 10u);

        KERNOX_ASSERT_TRUE(p.complete(50).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Completed);
        KERNOX_ASSERT_EQ(p.completion_time_ticks(), 50u);
        KERNOX_ASSERT_EQ(p.last_active_time_ticks(), 50u);
        KERNOX_ASSERT_EQ(p.cpu_burst_remaining(), 0u);
        KERNOX_ASSERT_FALSE(p.is_alive());
        KERNOX_ASSERT_TRUE(p.is_terminal());
    }

    // Flow 2: Preemption: Created -> Ready -> Running -> Ready -> Running -> Completed
    {
        auto p_res = SimulatedProcess::create("app_two", 10, ProcessImportance::Foreground, 1024, 50, 0);
        KERNOX_ASSERT_TRUE(p_res.is_ok());
        auto& p = p_res.value();

        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running(5).is_ok());
        KERNOX_ASSERT_TRUE(p.preempt_to_ready().is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Ready);

        KERNOX_ASSERT_TRUE(p.dispatch_running(15).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Running);
        KERNOX_ASSERT_TRUE(p.complete(30).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Completed);
    }

    // Flow 3: Blocking: Created -> Ready -> Running -> Blocked -> Ready -> Running -> Completed
    {
        auto p_res = SimulatedProcess::create("app_three", 10, ProcessImportance::Foreground, 1024, 50, 0);
        KERNOX_ASSERT_TRUE(p_res.is_ok());
        auto& p = p_res.value();

        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running(5).is_ok());

        KERNOX_ASSERT_TRUE(p.block("Disk I/O").is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Blocked);

        KERNOX_ASSERT_TRUE(p.unblock_to_ready().is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Ready);

        KERNOX_ASSERT_TRUE(p.dispatch_running(25).is_ok());
        KERNOX_ASSERT_TRUE(p.complete(40).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Completed);
    }

    // Flow 4: mark_ready() from Blocked state directly
    {
        auto p_res = SimulatedProcess::create("app_four", 10, ProcessImportance::Foreground, 1024, 50, 0);
        KERNOX_ASSERT_TRUE(p_res.is_ok());
        auto& p = p_res.value();

        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running(0).is_ok());
        KERNOX_ASSERT_TRUE(p.block().is_ok());
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Ready);
    }

    return 0;
}

static int test_lifecycle_invalid_transitions() {
    std::cout << "Running test_lifecycle_invalid_transitions...\n";

    // 1. Invalid transitions from Created
    {
        auto p = SimulatedProcess::create("inv_created", 10, ProcessImportance::Foreground, 1024, 50).value();
        KERNOX_ASSERT_EQ(p.dispatch_running().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.preempt_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.block().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.unblock_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.complete().error().code, ErrorCode::InvalidState);
    }

    // 2. Invalid transitions from Ready
    {
        auto p = SimulatedProcess::create("inv_ready", 10, ProcessImportance::Foreground, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());

        KERNOX_ASSERT_EQ(p.mark_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.preempt_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.block().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.unblock_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.complete().error().code, ErrorCode::InvalidState);
    }

    // 3. Invalid transitions from Running
    {
        auto p = SimulatedProcess::create("inv_running", 10, ProcessImportance::Foreground, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running().is_ok());

        KERNOX_ASSERT_EQ(p.dispatch_running().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.unblock_to_ready().error().code, ErrorCode::InvalidState);
    }

    // 4. Invalid transitions from Blocked
    {
        auto p = SimulatedProcess::create("inv_blocked", 10, ProcessImportance::Foreground, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running().is_ok());
        KERNOX_ASSERT_TRUE(p.block().is_ok());

        KERNOX_ASSERT_EQ(p.dispatch_running().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.preempt_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.block().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.complete().error().code, ErrorCode::InvalidState);
    }

    // 5. Terminal state immutability: Completed
    {
        auto p = SimulatedProcess::create("inv_completed", 10, ProcessImportance::Foreground, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running().is_ok());
        KERNOX_ASSERT_TRUE(p.complete(100).is_ok());

        KERNOX_ASSERT_EQ(p.mark_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.dispatch_running().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.preempt_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.block().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.unblock_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.complete().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.terminate().error().code, ErrorCode::InvalidState);
    }

    // 6. Terminal state immutability: Terminated
    {
        auto p = SimulatedProcess::create("inv_terminated", 10, ProcessImportance::Foreground, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.terminate("abort", 20).is_ok());

        KERNOX_ASSERT_EQ(p.mark_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.dispatch_running().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.preempt_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.block().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.unblock_to_ready().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.complete().error().code, ErrorCode::InvalidState);
        KERNOX_ASSERT_EQ(p.terminate().error().code, ErrorCode::InvalidState);
    }

    return 0;
}

static int test_termination_and_under_pressure_flag() {
    std::cout << "Running test_termination_and_under_pressure_flag...\n";

    // 1. Terminate from Created
    {
        auto p = SimulatedProcess::create("kill_created", 10, ProcessImportance::Background, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.terminate("SIGTERM", 50, false).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Terminated);
        KERNOX_ASSERT_FALSE(p.was_killed_under_pressure());
        KERNOX_ASSERT_EQ(p.termination_reason(), "SIGTERM");
        KERNOX_ASSERT_EQ(p.completion_time_ticks(), 50u);
    }

    // 2. Terminate from Ready under memory pressure (LMKD simulation)
    {
        auto p = SimulatedProcess::create("lmk_victim", 10, ProcessImportance::Cached, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.terminate("LMKD_CRITICAL_PRESSURE", 120, true).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Terminated);
        KERNOX_ASSERT_TRUE(p.was_killed_under_pressure());
        KERNOX_ASSERT_EQ(p.termination_reason(), "LMKD_CRITICAL_PRESSURE");
        KERNOX_ASSERT_EQ(p.completion_time_ticks(), 120u);
    }

    // 3. Terminate from Running
    {
        auto p = SimulatedProcess::create("kill_running", 10, ProcessImportance::Background, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running(10).is_ok());
        KERNOX_ASSERT_TRUE(p.terminate("OOM", 25, true).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Terminated);
        KERNOX_ASSERT_TRUE(p.was_killed_under_pressure());
    }

    // 4. Terminate from Blocked
    {
        auto p = SimulatedProcess::create("kill_blocked", 10, ProcessImportance::Background, 1024, 50).value();
        KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
        KERNOX_ASSERT_TRUE(p.dispatch_running(10).is_ok());
        KERNOX_ASSERT_TRUE(p.block().is_ok());
        KERNOX_ASSERT_TRUE(p.terminate("reclaimed", 40, true).is_ok());
        KERNOX_ASSERT_EQ(p.state(), ProcessLifecycleState::Terminated);
    }

    return 0;
}

static int test_memory_accounting_and_invariants() {
    std::cout << "Running test_memory_accounting_and_invariants...\n";

    auto p = SimulatedProcess::create("mem_test", 10, ProcessImportance::Foreground, 1024 * 1024 * 100, 50).value();
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 0u);
    KERNOX_ASSERT_EQ(p.memory().peak_memory_bytes, 0u);

    // 1. Valid allocation
    KERNOX_ASSERT_TRUE(p.allocate_memory(1024 * 1024 * 40).is_ok());
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 1024u * 1024u * 40u);
    KERNOX_ASSERT_EQ(p.memory().peak_memory_bytes, 1024u * 1024u * 40u);

    // 2. Additional allocation updates peak
    KERNOX_ASSERT_TRUE(p.allocate_memory(1024 * 1024 * 20).is_ok());
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 1024u * 1024u * 60u);
    KERNOX_ASSERT_EQ(p.memory().peak_memory_bytes, 1024u * 1024u * 60u);

    // 3. Release memory decreases consumed, retains peak
    KERNOX_ASSERT_TRUE(p.release_memory(1024 * 1024 * 30).is_ok());
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 1024u * 1024u * 30u);
    KERNOX_ASSERT_EQ(p.memory().peak_memory_bytes, 1024u * 1024u * 60u);

    // 4. Release underflow protection
    auto underflow_err = p.release_memory(1024 * 1024 * 50);
    KERNOX_ASSERT_TRUE(underflow_err.is_err());
    KERNOX_ASSERT_EQ(underflow_err.error().code, ErrorCode::InvalidArgument);
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 1024u * 1024u * 30u);

    // 5. Zero byte checks
    KERNOX_ASSERT_EQ(p.allocate_memory(0).error().code, ErrorCode::InvalidArgument);
    KERNOX_ASSERT_EQ(p.release_memory(0).error().code, ErrorCode::InvalidArgument);

    // 6. 64-bit integer overflow protection
    auto overflow_err = p.allocate_memory(std::numeric_limits<uint64_t>::max() - 10);
    KERNOX_ASSERT_TRUE(overflow_err.is_err());
    KERNOX_ASSERT_EQ(overflow_err.error().code, ErrorCode::ResourceExhausted);

    // 7. Update memory consumption
    KERNOX_ASSERT_TRUE(p.update_memory_consumption(1024 * 1024 * 80).is_ok());
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 1024u * 1024u * 80u);
    KERNOX_ASSERT_EQ(p.memory().peak_memory_bytes, 1024u * 1024u * 80u);

    KERNOX_ASSERT_TRUE(p.update_memory_consumption(1024 * 1024 * 20).is_ok());
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 1024u * 1024u * 20u);
    KERNOX_ASSERT_EQ(p.memory().peak_memory_bytes, 1024u * 1024u * 80u);

    // 8. Reclaim all memory
    uint64_t reclaimed = p.reclaim_all_memory();
    KERNOX_ASSERT_EQ(reclaimed, 1024u * 1024u * 20u);
    KERNOX_ASSERT_EQ(p.memory().memory_consumed_bytes, 0u);
    KERNOX_ASSERT_EQ(p.memory().peak_memory_bytes, 1024u * 1024u * 80u);

    // 9. Allocation on terminal process is rejected
    KERNOX_ASSERT_TRUE(p.terminate().is_ok());
    KERNOX_ASSERT_EQ(p.allocate_memory(1024).error().code, ErrorCode::InvalidState);
    KERNOX_ASSERT_EQ(p.update_memory_consumption(1024).error().code, ErrorCode::InvalidState);

    return 0;
}

static int test_cpu_priority_and_importance_orthogonality() {
    std::cout << "Running test_cpu_priority_and_importance_orthogonality...\n";

    // Decoupling demonstration:
    // Process A: High CPU Priority (0), Lowest Importance (Cached, 500)
    auto pA = SimulatedProcess::create("bg_compiler", 0, ProcessImportance::Cached, 1024, 100).value();
    KERNOX_ASSERT_EQ(pA.cpu_priority(), 0u);
    KERNOX_ASSERT_EQ(pA.importance(), ProcessImportance::Cached);
    KERNOX_ASSERT_EQ(static_cast<uint32_t>(pA.importance()), 500u);

    // Process B: Low CPU Priority (90), Highest Importance (Foreground, 100)
    auto pB = SimulatedProcess::create("ebook_reader", 90, ProcessImportance::Foreground, 1024, 100).value();
    KERNOX_ASSERT_EQ(pB.cpu_priority(), 90u);
    KERNOX_ASSERT_EQ(pB.importance(), ProcessImportance::Foreground);
    KERNOX_ASSERT_EQ(static_cast<uint32_t>(pB.importance()), 100u);

    // Mutate importance without altering CPU priority
    pA.set_importance(ProcessImportance::Visible);
    KERNOX_ASSERT_EQ(pA.importance(), ProcessImportance::Visible);
    KERNOX_ASSERT_EQ(pA.cpu_priority(), 0u);

    // Mutate CPU priority without altering importance
    KERNOX_ASSERT_TRUE(pB.set_cpu_priority(15).is_ok());
    KERNOX_ASSERT_EQ(pB.cpu_priority(), 15u);
    KERNOX_ASSERT_EQ(pB.importance(), ProcessImportance::Foreground);

    // Invalid CPU priority bounds check
    auto inv_prio_err = pB.set_cpu_priority(105);
    KERNOX_ASSERT_TRUE(inv_prio_err.is_err());
    KERNOX_ASSERT_EQ(inv_prio_err.error().code, ErrorCode::InvalidArgument);
    KERNOX_ASSERT_EQ(pB.cpu_priority(), 15u);

    return 0;
}

static int test_cpu_burst_consumption_and_task_projection() {
    std::cout << "Running test_cpu_burst_consumption_and_task_projection...\n";

    auto p = SimulatedProcess::create("burst_worker", 12, ProcessImportance::Service, 1024, 100, 5).value();

    // CPU consume when not in Running state must fail
    KERNOX_ASSERT_EQ(p.consume_cpu_time(10).error().code, ErrorCode::InvalidState);

    KERNOX_ASSERT_TRUE(p.mark_ready().is_ok());
    KERNOX_ASSERT_TRUE(p.dispatch_running(10).is_ok());

    // 0 ticks invalid
    KERNOX_ASSERT_EQ(p.consume_cpu_time(0).error().code, ErrorCode::InvalidArgument);

    // Consume 30 ticks
    KERNOX_ASSERT_TRUE(p.consume_cpu_time(30, 20).is_ok());
    KERNOX_ASSERT_EQ(p.cpu_burst_remaining(), 70u);
    KERNOX_ASSERT_EQ(p.cpu_time_executed(), 30u);
    KERNOX_ASSERT_EQ(p.last_active_time_ticks(), 20u);

    // Project to task
    sim::ProcessTask task = p.to_task();
    KERNOX_ASSERT_EQ(task.pid, p.pid());
    KERNOX_ASSERT_EQ(task.arrival_time, 5u);
    KERNOX_ASSERT_EQ(task.burst_time, 70u);
    KERNOX_ASSERT_EQ(task.priority, 12u);

    // Consume remaining ticks with overshoot (100 ticks requested, only 70 remain)
    KERNOX_ASSERT_TRUE(p.consume_cpu_time(100, 40).is_ok());
    KERNOX_ASSERT_EQ(p.cpu_burst_remaining(), 0u);
    KERNOX_ASSERT_EQ(p.cpu_time_executed(), 100u);
    KERNOX_ASSERT_EQ(p.last_active_time_ticks(), 40u);

    // Projection reflects completed burst
    sim::ProcessTask finished_task = p.to_task();
    KERNOX_ASSERT_EQ(finished_task.burst_time, 0u);

    // Full compatibility check: feed projected task to CpuScheduler
    std::vector<sim::ProcessTask> tasks{task};
    auto sched_res = sim::CpuScheduler::run_fcfs(tasks);
    KERNOX_ASSERT_TRUE(sched_res.is_ok());
    KERNOX_ASSERT_EQ(sched_res.value().process_metrics.size(), 1u);
    KERNOX_ASSERT_EQ(sched_res.value().process_metrics[0].burst_time, 70u);

    return 0;
}

static int test_string_conversions_and_formatting() {
    std::cout << "Running test_string_conversions_and_formatting...\n";

    KERNOX_ASSERT_EQ(to_string(ProcessLifecycleState::Created), "Created");
    KERNOX_ASSERT_EQ(to_string(ProcessLifecycleState::Ready), "Ready");
    KERNOX_ASSERT_EQ(to_string(ProcessLifecycleState::Running), "Running");
    KERNOX_ASSERT_EQ(to_string(ProcessLifecycleState::Blocked), "Blocked");
    KERNOX_ASSERT_EQ(to_string(ProcessLifecycleState::Completed), "Completed");
    KERNOX_ASSERT_EQ(to_string(ProcessLifecycleState::Terminated), "Terminated");

    KERNOX_ASSERT_EQ(to_string(ProcessImportance::Foreground), "Foreground");
    KERNOX_ASSERT_EQ(to_string(ProcessImportance::Visible), "Visible");
    KERNOX_ASSERT_EQ(to_string(ProcessImportance::Service), "Service");
    KERNOX_ASSERT_EQ(to_string(ProcessImportance::Background), "Background");
    KERNOX_ASSERT_EQ(to_string(ProcessImportance::Cached), "Cached");

    auto p = SimulatedProcess::create("fmt_demo", 4, ProcessImportance::Visible, 1024 * 1024, 20).value();
    std::string str = p.to_string();
    KERNOX_ASSERT_TRUE(str.find("fmt_demo") != std::string::npos);
    KERNOX_ASSERT_TRUE(str.find("Created") != std::string::npos);
    KERNOX_ASSERT_TRUE(str.find("Visible") != std::string::npos);
    KERNOX_ASSERT_TRUE(str.find("CPU Prio: 4") != std::string::npos);

    return 0;
}

// Strong wrapper types demonstration for PID compile-time isolation
struct StrongSimulatedPid {
    uint32_t value{0};
    constexpr explicit StrongSimulatedPid(uint32_t v = 0) noexcept : value(v) {}
    [[nodiscard]] constexpr uint32_t get() const noexcept { return value; }
    constexpr explicit operator uint32_t() const noexcept { return value; }
    auto operator<=>(const StrongSimulatedPid&) const = default;
};

struct StrongAndroidPid {
    uint32_t value{0};
    constexpr explicit StrongAndroidPid(uint32_t v = 0) noexcept : value(v) {}
    [[nodiscard]] constexpr uint32_t get() const noexcept { return value; }
    constexpr explicit operator uint32_t() const noexcept { return value; }
    auto operator<=>(const StrongAndroidPid&) const = default;
};

static int test_pid_type_safety_audit_and_strong_type_proof() {
    std::cout << "Running test_pid_type_safety_audit_and_strong_type_proof...\n";

    // 1. Audit Current State: SimulatedPid is an alias of uint32_t
    static_assert(std::is_same_v<SimulatedPid, uint32_t>,
                  "SimulatedPid is currently an alias of uint32_t");

    // 2. Compile-Time Proof: Strong types guarantee non-interchangeability
    static_assert(!std::is_same_v<StrongSimulatedPid, StrongAndroidPid>,
                  "StrongSimulatedPid and StrongAndroidPid must be distinct types");
    static_assert(!std::is_convertible_v<StrongSimulatedPid, StrongAndroidPid>,
                  "StrongSimulatedPid must not implicitly convert to StrongAndroidPid");
    static_assert(!std::is_convertible_v<StrongAndroidPid, StrongSimulatedPid>,
                  "StrongAndroidPid must not implicitly convert to StrongSimulatedPid");
    static_assert(!std::is_convertible_v<uint32_t, StrongSimulatedPid>,
                  "uint32_t must not implicitly convert to StrongSimulatedPid");
    static_assert(!std::is_convertible_v<StrongSimulatedPid, uint32_t>,
                  "StrongSimulatedPid must not implicitly convert to uint32_t without explicit cast");

    StrongSimulatedPid sim_pid{1001};
    StrongAndroidPid adb_pid{5821};
    KERNOX_ASSERT_EQ(sim_pid.get(), 1001u);
    KERNOX_ASSERT_EQ(adb_pid.get(), 5821u);
    KERNOX_ASSERT_NE(sim_pid.get(), adb_pid.get());

    return 0;
}

int main() {
    std::cout << "=== KERNOX 2.0 Process Intelligence Engine Tests ===\n";

    if (test_process_creation_and_pid_allocation() != 0) return 1;
    if (test_parameter_validation_and_rejections() != 0) return 1;
    if (test_lifecycle_standard_happy_paths() != 0) return 1;
    if (test_lifecycle_invalid_transitions() != 0) return 1;
    if (test_termination_and_under_pressure_flag() != 0) return 1;
    if (test_memory_accounting_and_invariants() != 0) return 1;
    if (test_cpu_priority_and_importance_orthogonality() != 0) return 1;
    if (test_cpu_burst_consumption_and_task_projection() != 0) return 1;
    if (test_string_conversions_and_formatting() != 0) return 1;
    if (test_pid_type_safety_audit_and_strong_type_proof() != 0) return 1;

    std::cout << "=== All Process Intelligence Tests Passed Successfully ===\n";
    return 0;
}
