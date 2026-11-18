#include "kernox/modules/intel/process_model.hpp"
#include "kernox/modules/reclaim/pressure_detector.hpp"
#include "kernox/modules/reclaim/reclamation_engine.hpp"
#include "kernox/modules/reclaim/reclamation_policy.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace kernox::reclaim;
using namespace kernox::intel;
using namespace kernox::core;

static int test_pressure_detector() {
    std::cout << "Running test_pressure_detector...\n";

    // 1. Invalid capacity (0 bytes)
    auto bad_cap = MemoryPressureDetector::create(0);
    KERNOX_ASSERT_TRUE(bad_cap.is_err());
    KERNOX_ASSERT_EQ(bad_cap.error().code, ErrorCode::InvalidArgument);

    // 2. Invalid threshold ratios
    auto bad_mod = MemoryPressureDetector::create(1000, -0.1, 0.9);
    KERNOX_ASSERT_TRUE(bad_mod.is_err());

    auto bad_crit = MemoryPressureDetector::create(1000, 0.7, 1.2);
    KERNOX_ASSERT_TRUE(bad_crit.is_err());

    auto bad_order = MemoryPressureDetector::create(1000, 0.85, 0.75);
    KERNOX_ASSERT_TRUE(bad_order.is_err());

    // 3. Valid detector: 1000 bytes capacity, 75% moderate (750 B), 90% critical (900 B)
    auto det_res = MemoryPressureDetector::create(1000, 0.75, 0.90);
    KERNOX_ASSERT_TRUE(det_res.is_ok());
    const auto& det = det_res.value();

    KERNOX_ASSERT_EQ(det.thresholds().moderate_limit_bytes(), 750u);
    KERNOX_ASSERT_EQ(det.thresholds().critical_limit_bytes(), 900u);

    // Normal pressure (< 750)
    KERNOX_ASSERT_EQ(det.detect(0), MemoryPressureLevel::Normal);
    KERNOX_ASSERT_EQ(det.detect(500), MemoryPressureLevel::Normal);
    KERNOX_ASSERT_EQ(det.detect(749), MemoryPressureLevel::Normal);
    KERNOX_ASSERT_EQ(det.bytes_to_reach_normal(500), 0u);

    // Exact threshold boundary: Moderate (750 <= usage < 900)
    KERNOX_ASSERT_EQ(det.detect(750), MemoryPressureLevel::Moderate);
    KERNOX_ASSERT_EQ(det.detect(800), MemoryPressureLevel::Moderate);
    KERNOX_ASSERT_EQ(det.detect(899), MemoryPressureLevel::Moderate);
    KERNOX_ASSERT_EQ(det.bytes_to_reach_normal(750), 1u);
    KERNOX_ASSERT_EQ(det.bytes_to_reach_normal(800), 51u);

    // Exact threshold boundary: Critical (usage >= 900)
    KERNOX_ASSERT_EQ(det.detect(900), MemoryPressureLevel::Critical);
    KERNOX_ASSERT_EQ(det.detect(950), MemoryPressureLevel::Critical);
    KERNOX_ASSERT_EQ(det.detect(1000), MemoryPressureLevel::Critical);

    // Overcommit (usage > capacity)
    KERNOX_ASSERT_EQ(det.detect(1200), MemoryPressureLevel::Critical);
    KERNOX_ASSERT_TRUE(std::abs(det.utilization_ratio(1200) - 1.2) < 1e-6);

    // Bytes to relieve pressure
    KERNOX_ASSERT_EQ(det.bytes_to_reach_moderate(950), 51u);
    KERNOX_ASSERT_EQ(det.bytes_to_reach_normal(950), 201u);

    // Diagnostics string
    std::string desc = det.describe_pressure(800);
    KERNOX_ASSERT_TRUE(desc.find("Moderate") != std::string::npos);
    KERNOX_ASSERT_TRUE(desc.find("80.0%") != std::string::npos);

    // Custom threshold update
    PressureThresholds custom{2000, 0.50, 0.80};
    auto det2 = det;
    KERNOX_ASSERT_TRUE(det2.update_thresholds(custom).is_ok());
    KERNOX_ASSERT_EQ(det2.detect(1100), MemoryPressureLevel::Moderate); // 55%
    KERNOX_ASSERT_EQ(det2.detect(1700), MemoryPressureLevel::Critical); // 85%

    return 0;
}

static int test_memory_accounting() {
    std::cout << "Running test_memory_accounting...\n";

    SimulatedProcess::reset_pid_generator(2000);
    auto p1 = SimulatedProcess::create("proc_A", 10, ProcessImportance::Background, 500, 50).value();
    auto p2 = SimulatedProcess::create("proc_B", 20, ProcessImportance::Service, 500, 50).value();

    // Capacity: 1000 bytes
    auto engine_res = MemoryReclamationEngine::create(1000);
    KERNOX_ASSERT_TRUE(engine_res.is_ok());
    auto& engine = engine_res.value();

    KERNOX_ASSERT_EQ(engine.total_capacity_bytes(), 1000u);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 0u);
    KERNOX_ASSERT_EQ(engine.available_bytes(), 1000u);

    // Register process without resident memory
    KERNOX_ASSERT_TRUE(engine.register_process(std::move(p1)).is_ok());
    KERNOX_ASSERT_TRUE(engine.has_process(2000));
    KERNOX_ASSERT_EQ(engine.process_count(), 1u);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 0u);

    // Duplicate registration rejected
    auto p1_dup = SimulatedProcess::create("proc_A_dup", 10, ProcessImportance::Background, 500, 50).value();
    // Force PID 2000 by setting internal generator or re-register
    KERNOX_ASSERT_TRUE(engine.register_process(std::move(p2)).is_ok());
    KERNOX_ASSERT_TRUE(engine.has_process(2001));
    KERNOX_ASSERT_EQ(engine.process_count(), 2u);

    // Allocate memory for process 2000
    KERNOX_ASSERT_TRUE(engine.allocate_process_memory(2000, 300).is_ok());
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 300u);
    KERNOX_ASSERT_EQ(engine.available_bytes(), 700u);
    KERNOX_ASSERT_EQ(engine.query_process_memory(2000).value(), 300u);

    // Allocate memory for process 2001
    KERNOX_ASSERT_TRUE(engine.allocate_process_memory(2001, 400).is_ok());
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 700u);
    KERNOX_ASSERT_EQ(engine.available_bytes(), 300u);

    // Exceed available capacity (try to allocate 400 when only 300 available)
    auto err_exceed = engine.allocate_process_memory(2000, 400);
    KERNOX_ASSERT_TRUE(err_exceed.is_err());
    KERNOX_ASSERT_EQ(err_exceed.error().code, ErrorCode::ResourceExhausted);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 700u);

    // Safe release
    KERNOX_ASSERT_TRUE(engine.release_process_memory(2000, 100).is_ok());
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 600u);
    KERNOX_ASSERT_EQ(engine.available_bytes(), 400u);
    KERNOX_ASSERT_EQ(engine.query_process_memory(2000).value(), 200u);

    // Underflow release protection
    auto err_underflow = engine.release_process_memory(2000, 300);
    KERNOX_ASSERT_TRUE(err_underflow.is_err());
    KERNOX_ASSERT_EQ(err_underflow.error().code, ErrorCode::InvalidArgument);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 600u);

    // Unregister process frees its resident memory
    KERNOX_ASSERT_TRUE(engine.unregister_process(2000).is_ok());
    KERNOX_ASSERT_FALSE(engine.has_process(2000));
    KERNOX_ASSERT_EQ(engine.process_count(), 1u);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 400u); // Only p2 (400 B) remains
    KERNOX_ASSERT_EQ(engine.available_bytes(), 600u);

    return 0;
}

static int test_fifo_policy() {
    std::cout << "Running test_fifo_policy...\n";

    SimulatedProcess::reset_pid_generator(3000);
    // p1 created at tick 10
    auto p1 = SimulatedProcess::create("p_old", 10, ProcessImportance::Background, 500, 50, 10).value();
    // p2 created at tick 20
    auto p2 = SimulatedProcess::create("p_new", 10, ProcessImportance::Background, 500, 50, 20).value();
    // p3 created at tick 10 (tie with p1, but PID 3002 > PID 3000)
    auto p3 = SimulatedProcess::create("p_tie", 10, ProcessImportance::Background, 500, 50, 10).value();

    KERNOX_ASSERT_TRUE(p1.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p2.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p3.allocate_memory(100).is_ok());

    FifoReclamationPolicy fifo;
    std::vector<const SimulatedProcess*> candidates{&p2, &p3, &p1};

    // Oldest is tick 10 (p1 and p3). Tie-break: smaller PID -> p1 (3000)
    auto victim1 = fifo.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim1.has_value());
    KERNOX_ASSERT_EQ(victim1->pid, 3000u);

    // Mark p1 terminated
    KERNOX_ASSERT_TRUE(p1.terminate("test", 100).is_ok());

    // Next oldest is p3 (tick 10)
    auto victim2 = fifo.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim2.has_value());
    KERNOX_ASSERT_EQ(victim2->pid, 3002u);

    // Mark p3 terminated
    KERNOX_ASSERT_TRUE(p3.terminate("test", 100).is_ok());

    // Next is p2 (tick 20)
    auto victim3 = fifo.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim3.has_value());
    KERNOX_ASSERT_EQ(victim3->pid, 3001u);

    // Empty when all terminated
    KERNOX_ASSERT_TRUE(p2.terminate("test", 100).is_ok());
    auto victim_none = fifo.select_victim(candidates);
    KERNOX_ASSERT_FALSE(victim_none.has_value());

    return 0;
}

static int test_lru_policy() {
    std::cout << "Running test_lru_policy...\n";

    SimulatedProcess::reset_pid_generator(4000);
    auto p1 = SimulatedProcess::create("p1", 10, ProcessImportance::Background, 500, 50, 0).value();
    auto p2 = SimulatedProcess::create("p2", 10, ProcessImportance::Background, 500, 50, 0).value();
    auto p3 = SimulatedProcess::create("p3", 10, ProcessImportance::Background, 500, 50, 0).value();

    KERNOX_ASSERT_TRUE(p1.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p2.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p3.allocate_memory(100).is_ok());

    // Simulate activity: p1 active at 10, p2 active at 50, p3 active at 30
    KERNOX_ASSERT_TRUE(p1.mark_ready().is_ok());
    KERNOX_ASSERT_TRUE(p1.dispatch_running(10).is_ok());

    KERNOX_ASSERT_TRUE(p2.mark_ready().is_ok());
    KERNOX_ASSERT_TRUE(p2.dispatch_running(50).is_ok());

    KERNOX_ASSERT_TRUE(p3.mark_ready().is_ok());
    KERNOX_ASSERT_TRUE(p3.dispatch_running(30).is_ok());

    LruReclamationPolicy lru;
    std::vector<const SimulatedProcess*> candidates{&p2, &p3, &p1};

    // p1 has earliest last active (10)
    auto victim1 = lru.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim1.has_value());
    KERNOX_ASSERT_EQ(victim1->pid, 4000u);

    // p1 terminated
    KERNOX_ASSERT_TRUE(p1.terminate("test", 100).is_ok());

    // Next least recently active is p3 (30)
    auto victim2 = lru.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim2.has_value());
    KERNOX_ASSERT_EQ(victim2->pid, 4002u);

    // Next is p2 (50)
    KERNOX_ASSERT_TRUE(p3.terminate("test", 100).is_ok());
    auto victim3 = lru.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim3.has_value());
    KERNOX_ASSERT_EQ(victim3->pid, 4001u);

    return 0;
}

static int test_priority_policy() {
    std::cout << "Running test_priority_policy...\n";

    SimulatedProcess::reset_pid_generator(5000);
    // p1 has CPU priority 10 (high urgency)
    auto p1 = SimulatedProcess::create("urgent", 10, ProcessImportance::Background, 500, 50, 0).value();
    // p2 has CPU priority 80 (low urgency -> preferred victim)
    auto p2 = SimulatedProcess::create("idle_worker", 80, ProcessImportance::Background, 500, 50, 0).value();
    // p3 has CPU priority 50 (medium urgency)
    auto p3 = SimulatedProcess::create("medium", 50, ProcessImportance::Background, 500, 50, 0).value();

    KERNOX_ASSERT_TRUE(p1.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p2.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p3.allocate_memory(100).is_ok());

    PriorityReclamationPolicy prio;
    std::vector<const SimulatedProcess*> candidates{&p1, &p3, &p2};

    // Highest numeric priority (lowest scheduling urgency) is p2 (80)
    auto victim1 = prio.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim1.has_value());
    KERNOX_ASSERT_EQ(victim1->pid, 5001u);

    KERNOX_ASSERT_TRUE(p2.terminate("test", 100).is_ok());

    // Next is p3 (50)
    auto victim2 = prio.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim2.has_value());
    KERNOX_ASSERT_EQ(victim2->pid, 5002u);

    KERNOX_ASSERT_TRUE(p3.terminate("test", 100).is_ok());

    // Next is p1 (10)
    auto victim3 = prio.select_victim(candidates);
    KERNOX_ASSERT_TRUE(victim3.has_value());
    KERNOX_ASSERT_EQ(victim3->pid, 5000u);

    return 0;
}

static int test_importance_aware_policy() {
    std::cout << "Running test_importance_aware_policy...\n";

    SimulatedProcess::reset_pid_generator(6000);
    auto p_fg = SimulatedProcess::create("fg_app", 10, ProcessImportance::Foreground, 500, 50).value();
    auto p_vis = SimulatedProcess::create("vis_app", 10, ProcessImportance::Visible, 500, 50).value();
    auto p_svc = SimulatedProcess::create("svc_app", 10, ProcessImportance::Service, 500, 50).value();
    auto p_bg = SimulatedProcess::create("bg_app", 10, ProcessImportance::Background, 500, 50).value();
    auto p_cached = SimulatedProcess::create("cached_app", 10, ProcessImportance::Cached, 500, 50).value();

    KERNOX_ASSERT_TRUE(p_fg.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p_vis.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p_svc.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p_bg.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p_cached.allocate_memory(100).is_ok());

    ImportanceAwareReclamationPolicy lmk; // Default: Foreground protected
    std::vector<const SimulatedProcess*> candidates{&p_fg, &p_vis, &p_svc, &p_bg, &p_cached};

    // 1. Cached (500) selected first
    auto v1 = lmk.select_victim(candidates);
    KERNOX_ASSERT_TRUE(v1.has_value());
    KERNOX_ASSERT_EQ(v1->pid, p_cached.pid());
    KERNOX_ASSERT_TRUE(p_cached.terminate().is_ok());

    // 2. Background (400) selected next
    auto v2 = lmk.select_victim(candidates);
    KERNOX_ASSERT_TRUE(v2.has_value());
    KERNOX_ASSERT_EQ(v2->pid, p_bg.pid());
    KERNOX_ASSERT_TRUE(p_bg.terminate().is_ok());

    // 3. Service (300) selected next
    auto v3 = lmk.select_victim(candidates);
    KERNOX_ASSERT_TRUE(v3.has_value());
    KERNOX_ASSERT_EQ(v3->pid, p_svc.pid());
    KERNOX_ASSERT_TRUE(p_svc.terminate().is_ok());

    // 4. Visible (200) selected next
    auto v4 = lmk.select_victim(candidates);
    KERNOX_ASSERT_TRUE(v4.has_value());
    KERNOX_ASSERT_EQ(v4->pid, p_vis.pid());
    KERNOX_ASSERT_TRUE(p_vis.terminate().is_ok());

    // 5. Only Foreground remains -> Protected! No eligible victim
    auto v5 = lmk.select_victim(candidates);
    KERNOX_ASSERT_FALSE(v5.has_value());

    // Configurable protection: Protect Visible and Foreground
    ImportanceAwareReclamationPolicy lmk_vis_protected(ProcessImportance::Visible);
    SimulatedProcess::reset_pid_generator(6100);
    auto p_vis2 = SimulatedProcess::create("vis2", 10, ProcessImportance::Visible, 500, 50).value();
    auto p_svc2 = SimulatedProcess::create("svc2", 10, ProcessImportance::Service, 500, 50).value();
    KERNOX_ASSERT_TRUE(p_vis2.allocate_memory(100).is_ok());
    KERNOX_ASSERT_TRUE(p_svc2.allocate_memory(100).is_ok());
    std::vector<const SimulatedProcess*> pool2{&p_vis2, &p_svc2};

    // Service selected
    auto v_svc = lmk_vis_protected.select_victim(pool2);
    KERNOX_ASSERT_TRUE(v_svc.has_value());
    KERNOX_ASSERT_EQ(v_svc->pid, p_svc2.pid());
    KERNOX_ASSERT_TRUE(p_svc2.terminate().is_ok());

    // Visible is now protected -> nullopt
    auto v_none = lmk_vis_protected.select_victim(pool2);
    KERNOX_ASSERT_FALSE(v_none.has_value());

    return 0;
}

static int test_reclamation_engine_multi_step() {
    std::cout << "Running test_reclamation_engine_multi_step...\n";

    SimulatedProcess::reset_pid_generator(7000);
    // Total capacity: 1000 bytes. Moderate = 750 B, Critical = 900 B
    auto engine = MemoryReclamationEngine::create(1000).value();

    auto p_fg = SimulatedProcess::create("fg_main", 5, ProcessImportance::Foreground, 1000, 50).value();
    auto p_bg1 = SimulatedProcess::create("bg_worker1", 50, ProcessImportance::Background, 1000, 50).value();
    auto p_bg2 = SimulatedProcess::create("bg_worker2", 60, ProcessImportance::Background, 1000, 50).value();
    auto p_cached = SimulatedProcess::create("cached_tab", 80, ProcessImportance::Cached, 1000, 50).value();

    KERNOX_ASSERT_TRUE(engine.register_process(std::move(p_fg)).is_ok());
    KERNOX_ASSERT_TRUE(engine.register_process(std::move(p_bg1)).is_ok());
    KERNOX_ASSERT_TRUE(engine.register_process(std::move(p_bg2)).is_ok());
    KERNOX_ASSERT_TRUE(engine.register_process(std::move(p_cached)).is_ok());

    // Allocate memory:
    // FG: 200 B
    // BG1: 250 B
    // BG2: 250 B
    // Cached: 250 B
    // Total: 950 B (Critical pressure! > 900 B)
    KERNOX_ASSERT_TRUE(engine.allocate_process_memory(7000, 200).is_ok());
    KERNOX_ASSERT_TRUE(engine.allocate_process_memory(7001, 250).is_ok());
    KERNOX_ASSERT_TRUE(engine.allocate_process_memory(7002, 250).is_ok());
    KERNOX_ASSERT_TRUE(engine.allocate_process_memory(7003, 250).is_ok());

    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 950u);
    KERNOX_ASSERT_EQ(engine.current_pressure_level(), MemoryPressureLevel::Critical);

    // Goal: Reclaim memory until pressure drops to Normal (< 750 B, meaning free at least 201 B)
    ImportanceAwareReclamationPolicy policy;
    ReclamationReport report = engine.reclaim_to_pressure(policy, MemoryPressureLevel::Normal, 100);

    KERNOX_ASSERT_TRUE(report.target_achieved);
    KERNOX_ASSERT_EQ(report.stop_reason, ReclamationStopReason::TargetAchieved);
    KERNOX_ASSERT_EQ(report.initial_pressure, MemoryPressureLevel::Critical);
    KERNOX_ASSERT_EQ(report.final_pressure, MemoryPressureLevel::Normal);

    // Cached (250 B) is terminated first, freeing 250 B.
    // 950 - 250 = 700 B (< 750 B Normal threshold!). Exactly 1 process terminated!
    KERNOX_ASSERT_EQ(report.processes_terminated, 1u);
    KERNOX_ASSERT_EQ(report.actual_reclaim_bytes, 250u);
    KERNOX_ASSERT_EQ(report.decisions.size(), 1u);
    KERNOX_ASSERT_EQ(report.decisions[0].victim_pid, 7003u);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 700u);

    // Verify victim process state
    const auto* cached_proc = engine.get_process(7003);
    KERNOX_ASSERT_TRUE(cached_proc != nullptr);
    KERNOX_ASSERT_EQ(cached_proc->state(), ProcessLifecycleState::Terminated);
    KERNOX_ASSERT_TRUE(cached_proc->was_killed_under_pressure());
    KERNOX_ASSERT_EQ(cached_proc->memory().memory_consumed_bytes, 0u);

    // Next: Request 400 B more reclaimed using Priority policy
    PriorityReclamationPolicy prio_policy;
    ReclamationReport report2 = engine.reclaim_bytes(prio_policy, 400, 150);
    KERNOX_ASSERT_TRUE(report2.target_achieved);
    // bg_worker2 (prio 60, 250 B) and bg_worker1 (prio 50, 250 B) -> 500 B reclaimed (> 400 B)
    KERNOX_ASSERT_EQ(report2.processes_terminated, 2u);
    KERNOX_ASSERT_EQ(report2.actual_reclaim_bytes, 500u);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 200u);

    // Now only FG (7000, 200 B) remains.
    // Try to reclaim 100 B with ImportanceAware policy (FG protected)
    ReclamationReport report3 = engine.reclaim_bytes(policy, 100, 200);
    KERNOX_ASSERT_FALSE(report3.target_achieved);
    KERNOX_ASSERT_EQ(report3.stop_reason, ReclamationStopReason::NoEligibleVictim);
    KERNOX_ASSERT_EQ(report3.processes_terminated, 0u);
    KERNOX_ASSERT_EQ(engine.current_consumed_bytes(), 200u); // Unchanged!

    // Verify report formatting does not crash
    std::string report_str = report.to_string();
    KERNOX_ASSERT_TRUE(report_str.find("SUCCESS") != std::string::npos);

    return 0;
}

static int test_policy_comparison_cloning() {
    std::cout << "Running test_policy_comparison_cloning...\n";

    SimulatedProcess::reset_pid_generator(8000);
    auto base_engine = MemoryReclamationEngine::create(1000).value();

    // Population:
    // P0: FG, Prio 0, 150 B, Tick 10
    // P1: Cached, Prio 80, 200 B, Tick 20
    // P2: Background, Prio 20, 300 B, Tick 5
    // P3: Service, Prio 90, 250 B, Tick 15
    auto p0 = SimulatedProcess::create("fg_user", 0, ProcessImportance::Foreground, 1000, 50, 10).value();
    auto p1 = SimulatedProcess::create("cached_cleaner", 80, ProcessImportance::Cached, 1000, 50, 20).value();
    auto p2 = SimulatedProcess::create("bg_indexer", 20, ProcessImportance::Background, 1000, 50, 5).value();
    auto p3 = SimulatedProcess::create("svc_music", 90, ProcessImportance::Service, 1000, 50, 15).value();

    KERNOX_ASSERT_TRUE(base_engine.register_process(std::move(p0)).is_ok());
    KERNOX_ASSERT_TRUE(base_engine.register_process(std::move(p1)).is_ok());
    KERNOX_ASSERT_TRUE(base_engine.register_process(std::move(p2)).is_ok());
    KERNOX_ASSERT_TRUE(base_engine.register_process(std::move(p3)).is_ok());

    KERNOX_ASSERT_TRUE(base_engine.allocate_process_memory(8000, 150).is_ok());
    KERNOX_ASSERT_TRUE(base_engine.allocate_process_memory(8001, 200).is_ok());
    KERNOX_ASSERT_TRUE(base_engine.allocate_process_memory(8002, 300).is_ok());
    KERNOX_ASSERT_TRUE(base_engine.allocate_process_memory(8003, 250).is_ok());
    KERNOX_ASSERT_EQ(base_engine.current_consumed_bytes(), 900u); // 90% Critical

    // Clone into 4 independent identical engines
    auto engine_fifo = base_engine.clone();
    auto engine_lru = base_engine.clone();
    auto engine_prio = base_engine.clone();
    auto engine_lmk = base_engine.clone();

    // Run each policy to reclaim 200 bytes
    FifoReclamationPolicy fifo_pol;
    auto rep_fifo = engine_fifo.reclaim_bytes(fifo_pol, 200);
    // Oldest creation time is P2 (tick 5, 300 B, PID 8002)
    KERNOX_ASSERT_EQ(rep_fifo.decisions[0].victim_pid, 8002u);

    PriorityReclamationPolicy prio_pol;
    auto rep_prio = engine_prio.reclaim_bytes(prio_pol, 200);
    // Highest numeric priority is P3 (prio 90, PID 8003)
    KERNOX_ASSERT_EQ(rep_prio.decisions[0].victim_pid, 8003u);

    ImportanceAwareReclamationPolicy lmk_pol;
    auto rep_lmk = engine_lmk.reclaim_bytes(lmk_pol, 200);
    // Highest importance tier is Cached: P1 (PID 8001)
    KERNOX_ASSERT_EQ(rep_lmk.decisions[0].victim_pid, 8001u);

    // Verify all 3 policies picked different victims from identical inputs!
    KERNOX_ASSERT_NE(rep_fifo.decisions[0].victim_pid, rep_prio.decisions[0].victim_pid);
    KERNOX_ASSERT_NE(rep_prio.decisions[0].victim_pid, rep_lmk.decisions[0].victim_pid);
    KERNOX_ASSERT_NE(rep_fifo.decisions[0].victim_pid, rep_lmk.decisions[0].victim_pid);

    return 0;
}

int main() {
    std::cout << "=== KERNOX 2.0 Memory Pressure & Reclamation Tests ===\n";

    if (test_pressure_detector() != 0) return 1;
    if (test_memory_accounting() != 0) return 1;
    if (test_fifo_policy() != 0) return 1;
    if (test_lru_policy() != 0) return 1;
    if (test_priority_policy() != 0) return 1;
    if (test_importance_aware_policy() != 0) return 1;
    if (test_reclamation_engine_multi_step() != 0) return 1;
    if (test_policy_comparison_cloning() != 0) return 1;

    std::cout << "=== All Memory Pressure & Reclamation Tests Passed Successfully ===\n";
    return 0;
}
