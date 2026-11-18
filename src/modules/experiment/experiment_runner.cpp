#include "kernox/modules/experiment/experiment_runner.hpp"
#include "kernox/core/timer.hpp"

#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace kernox::experiment {

std::string ExperimentResult::to_string() const {
    std::ostringstream oss;
    double peak_mb = static_cast<double>(metrics.peak_used_bytes) / (1024.0 * 1024.0);
    double cap_mb = static_cast<double>(metrics.total_capacity_bytes) / (1024.0 * 1024.0);
    double rec_mb = static_cast<double>(metrics.total_reclaimed_bytes) / (1024.0 * 1024.0);

    oss << "================================================================================\n"
        << "EXPERIMENT RESULT: " << policy_name << " (" << scenario_name << ")\n"
        << "Workload Fingerprint: " << workload_fingerprint << " | Seed: " << seed << "\n"
        << "--------------------------------------------------------------------------------\n"
        << "Process Population  : " << metrics.total_processes << " generated, "
        << metrics.processes_completed << " completed, "
        << metrics.processes_terminated_pressure << " killed under pressure\n"
        << "Survival Rate       : " << std::fixed << std::setprecision(1) << (metrics.survival_rate * 100.0) << "% "
        << "(" << metrics.processes_survived << " / " << metrics.total_processes << " survived)\n"
        << "Tier Survival Rates : FG " << (metrics.foreground_survival_rate * 100.0) << "% | "
        << "Vis " << (metrics.visible_survival_rate * 100.0) << "% | "
        << "Svc " << (metrics.service_survival_rate * 100.0) << "% | "
        << "BG " << (metrics.background_survival_rate * 100.0) << "% | "
        << "Cached " << (metrics.cached_survival_rate * 100.0) << "%\n"
        << "Memory Capacity     : " << cap_mb << " MiB | Peak Resident: " << peak_mb << " MiB ("
        << (metrics.average_utilization_ratio * 100.0) << "% avg util)\n"
        << "Reclaimed Memory    : " << rec_mb << " MiB (" << metrics.processes_reclaimed << " victims, "
        << metrics.reclamation_invocations << " invocations)\n"
        << "Pressure Events     : " << metrics.pressure_events_moderate << " moderate, "
        << metrics.pressure_events_critical << " critical\n"
        << "Failed Allocations  : " << metrics.failed_allocations << "\n"
        << "Execution Time      : " << metrics.simulated_duration_ticks << " ticks logical, "
        << (static_cast<double>(metrics.wall_clock_elapsed_microseconds) / 1000.0) << " ms wall-clock\n"
        << "================================================================================";
    return oss.str();
}

const ExperimentResult* ExperimentComparison::get_result(std::string_view policy_name) const noexcept {
    for (const auto& r : policy_results) {
        if (r.policy_name == policy_name) {
            return &r;
        }
    }
    return nullptr;
}

std::string ExperimentComparison::to_table_string() const {
    std::ostringstream oss;
    oss << "========================================================================================================\n"
        << "KERNOX 2.0 EXPERIMENT POLICY COMPARISON BENCHMARK\n"
        << "Scenario: " << scenario_name << " | Seed: " << seed
        << " | Fingerprint: " << workload_fingerprint << " | Capacity: "
        << (static_cast<double>(total_capacity_bytes) / (1024.0 * 1024.0)) << " MiB\n"
        << "--------------------------------------------------------------------------------------------------------\n"
        << std::left << std::setw(16) << "Policy"
        << std::right << std::setw(12) << "Reclaimed"
        << std::setw(10) << "Victims"
        << std::setw(10) << "Survived"
        << std::setw(14) << "Survival %"
        << std::setw(12) << "FG Surv %"
        << std::setw(14) << "Peak Util %"
        << std::setw(12) << "Fail Allocs"
        << "\n"
        << "--------------------------------------------------------------------------------------------------------\n";

    for (const auto& r : policy_results) {
        double rec_mb = static_cast<double>(r.metrics.total_reclaimed_bytes) / (1024.0 * 1024.0);
        double peak_ratio = r.metrics.total_capacity_bytes > 0
            ? (static_cast<double>(r.metrics.peak_used_bytes) / static_cast<double>(r.metrics.total_capacity_bytes)) * 100.0
            : 0.0;

        oss << std::left << std::setw(16) << r.policy_name
            << std::right << std::fixed << std::setprecision(1)
            << std::setw(9) << rec_mb << " MB"
            << std::setw(10) << r.metrics.processes_reclaimed
            << std::setw(10) << r.metrics.processes_survived
            << std::setw(13) << (r.metrics.survival_rate * 100.0) << "%"
            << std::setw(11) << (r.metrics.foreground_survival_rate * 100.0) << "%"
            << std::setw(13) << peak_ratio << "%"
            << std::setw(12) << r.metrics.failed_allocations
            << "\n";
    }

    oss << "========================================================================================================";
    return oss.str();
}

std::string ExperimentComparison::to_summary_string() const {
    std::ostringstream oss;
    oss << to_table_string() << "\n\n"
        << "POLICY TRADE-OFF ANALYSIS:\n";

    const auto* lmk = get_result("ImportanceAware");
    const auto* fifo = get_result("FIFO");
    const auto* lru = get_result("LRU");
    const auto* prio = get_result("Priority");

    if (lmk) {
        oss << "  * Importance-Aware: Foreground survival rate = "
            << std::fixed << std::setprecision(1) << (lmk->metrics.foreground_survival_rate * 100.0) << "%. "
            << (lmk->metrics.foreground_survival_rate >= 1.0
                    ? "[PROTECTION VERIFIED: All foreground tasks survived]"
                    : "[PROTECTION ENGAGED: Partial survival under severe shortfall]")
            << "\n";
    }
    if (fifo) {
        oss << "  * FIFO: Reclaimed " << (static_cast<double>(fifo->metrics.total_reclaimed_bytes) / (1024.0 * 1024.0))
            << " MB across " << fifo->metrics.processes_reclaimed << " oldest resident victims.\n";
    }
    if (lru) {
        oss << "  * LRU: Reclaimed " << (static_cast<double>(lru->metrics.total_reclaimed_bytes) / (1024.0 * 1024.0))
            << " MB across " << lru->metrics.processes_reclaimed << " least recently active victims.\n";
    }
    if (prio) {
        oss << "  * Priority: Reclaimed " << (static_cast<double>(prio->metrics.total_reclaimed_bytes) / (1024.0 * 1024.0))
            << " MB across " << prio->metrics.processes_reclaimed << " low-scheduling-urgency victims.\n";
    }

    return oss.str();
}

core::Result<ExperimentResult> ExperimentRunner::run_policy(
    const WorkloadDefinition& workload,
    const reclaim::IReclamationPolicy& policy,
    const ExperimentConfiguration& config
) {
    core::Timer timer;

    reclaim::PressureThresholds thresh = config.thresholds;
    thresh.total_memory_bytes = workload.total_memory_bytes;
    auto engine_res = reclaim::MemoryReclamationEngine::create(workload.total_memory_bytes, thresh);
    if (engine_res.is_err()) {
        return core::Result<ExperimentResult>::err(engine_res.error());
    }
    auto engine = std::move(engine_res.value());

    // Deterministic PID allocation reset to ensure identical PIDs across all policy runs
    intel::SimulatedProcess::reset_pid_generator(1000);

    ExperimentResult result;
    result.policy_name = std::string(policy.name());
    result.scenario_name = workload.scenario_name;
    result.seed = workload.seed;
    result.workload_fingerprint = workload.fingerprint;

    ExperimentMetrics& metrics = result.metrics;
    metrics.total_capacity_bytes = workload.total_memory_bytes;
    metrics.total_processes = workload.process_specs.size();

    struct PendingItem {
        intel::SimulatedProcess proc;
        uint64_t arrival_tick{0};
        uint64_t initial_memory{0};
        std::vector<MemoryDemandEvent> events{};
    };

    std::vector<PendingItem> pending;
    pending.reserve(workload.process_specs.size());

    for (const auto& spec : workload.process_specs) {
        auto proc_res = intel::SimulatedProcess::create(
            spec.name,
            spec.cpu_priority,
            spec.importance,
            spec.requested_memory_bytes,
            spec.cpu_burst_ticks,
            spec.arrival_tick
        );
        if (proc_res.is_err()) {
            return core::Result<ExperimentResult>::err(proc_res.error());
        }

        pending.push_back(PendingItem{
            std::move(proc_res.value()),
            spec.arrival_tick,
            spec.initial_memory_bytes,
            spec.dynamic_events
        });
    }

    uint64_t max_tick = 50;
    for (const auto& p : pending) {
        if (p.arrival_tick > max_tick) max_tick = p.arrival_tick;
        for (const auto& ev : p.events) {
            if (ev.tick > max_tick) max_tick = ev.tick;
        }
    }
    max_tick += 50;

    double sum_utilization = 0.0;
    size_t sample_count = 0;

    auto sample_metrics = [&](uint64_t tick) {
        uint64_t used = engine.current_consumed_bytes();
        if (used > metrics.peak_used_bytes) {
            metrics.peak_used_bytes = used;
        }
        sum_utilization += engine.memory_utilization_ratio();
        sample_count++;

        auto p_level = engine.current_pressure_level();
        if (p_level == reclaim::MemoryPressureLevel::Moderate) {
            metrics.pressure_events_moderate++;
        } else if (p_level == reclaim::MemoryPressureLevel::Critical) {
            metrics.pressure_events_critical++;
        }

        size_t active_count = 0;
        for (const auto* p : engine.processes()) {
            if (p && p->is_alive()) {
                active_count++;
            }
        }

        result.memory_timeline.push_back(MemoryTimelinePoint{
            .tick = tick,
            .consumed_bytes = used,
            .pressure_level = p_level,
            .active_processes = active_count
        });
    };

    std::vector<bool> admitted(pending.size(), false);
    std::vector<size_t> event_indices(pending.size(), 0);

    for (uint64_t current_tick = 0; current_tick <= max_tick; ++current_tick) {
        // 1. Process Admissions
        for (size_t i = 0; i < pending.size(); ++i) {
            if (!admitted[i] && pending[i].arrival_tick <= current_tick) {
                auto& item = pending[i];
                auto pid = item.proc.pid();
                auto mark_res = item.proc.mark_ready();
                static_cast<void>(mark_res);

                auto reg_res = engine.register_process(std::move(item.proc));
                if (reg_res.is_ok()) {
                    admitted[i] = true;
                    if (item.initial_memory > 0) {
                        if (item.initial_memory > engine.available_bytes()) {
                            metrics.reclamation_invocations++;
                            auto rep = engine.reclaim_to_pressure(
                                policy,
                                config.target_reclaim_pressure,
                                current_tick
                            );
                            for (const auto& dec : rep.decisions) {
                                result.reclamation_log.push_back(dec);
                            }
                            metrics.total_reclaimed_bytes += rep.actual_reclaim_bytes;
                            metrics.processes_reclaimed += rep.processes_terminated;
                            if (rep.target_achieved) {
                                metrics.reclamation_successes++;
                            }
                            if (rep.stop_reason == reclaim::ReclamationStopReason::NoEligibleVictim) {
                                metrics.no_eligible_victim_events++;
                            }
                        }

                        auto alloc_res = engine.allocate_process_memory(pid, item.initial_memory);
                        if (alloc_res.is_err()) {
                            metrics.failed_allocations++;
                        }
                    }
                }
            }
        }

        // 2. Process Dynamic Memory Demands
        for (size_t i = 0; i < pending.size(); ++i) {
            if (admitted[i]) {
                auto& item = pending[i];
                auto pid = item.proc.pid();
                while (event_indices[i] < item.events.size() &&
                       item.events[event_indices[i]].tick <= current_tick) {
                    const auto& ev = item.events[event_indices[i]];
                    event_indices[i]++;

                    if (!engine.has_process(pid)) {
                        continue;
                    }
                    const auto* proc = engine.get_process(pid);
                    if (!proc || !proc->is_alive()) {
                        continue;
                    }

                    if (ev.delta_bytes > 0) {
                        auto needed = static_cast<uint64_t>(ev.delta_bytes);
                        if (needed > engine.available_bytes()) {
                            metrics.reclamation_invocations++;
                            auto rep = engine.reclaim_to_pressure(
                                policy,
                                config.target_reclaim_pressure,
                                current_tick
                            );
                            for (const auto& dec : rep.decisions) {
                                result.reclamation_log.push_back(dec);
                            }
                            metrics.total_reclaimed_bytes += rep.actual_reclaim_bytes;
                            metrics.processes_reclaimed += rep.processes_terminated;
                            if (rep.target_achieved) {
                                metrics.reclamation_successes++;
                            }
                            if (rep.stop_reason == reclaim::ReclamationStopReason::NoEligibleVictim) {
                                metrics.no_eligible_victim_events++;
                            }
                        }

                        auto a_res = engine.allocate_process_memory(pid, needed);
                        if (a_res.is_err()) {
                            metrics.failed_allocations++;
                        }
                    } else if (ev.delta_bytes < 0) {
                        auto to_rel = static_cast<uint64_t>(-ev.delta_bytes);
                        auto rel_res = engine.release_process_memory(pid, to_rel);
                        static_cast<void>(rel_res);
                    }
                }
            }
        }

        // 3. Process CPU Execution & Completion
        for (size_t i = 0; i < pending.size(); ++i) {
            if (admitted[i]) {
                auto pid = pending[i].proc.pid();
                auto* proc = engine.get_process_mut(pid);
                if (proc && proc->is_alive()) {
                    if (proc->state() == intel::ProcessLifecycleState::Ready) {
                        auto disp_res = proc->dispatch_running(current_tick);
                        static_cast<void>(disp_res);
                    }
                    if (proc->state() == intel::ProcessLifecycleState::Running) {
                        auto cpu_res = proc->consume_cpu_time(1, current_tick);
                        static_cast<void>(cpu_res);
                        if (proc->cpu_burst_remaining() == 0) {
                            auto comp_res = proc->complete(current_tick);
                            static_cast<void>(comp_res);
                            uint64_t freed = proc->reclaim_all_memory();
                            auto rel_res = engine.release_process_memory(pid, freed);
                            static_cast<void>(rel_res);
                            metrics.processes_completed++;
                        }
                    }
                }
            }
        }

        sample_metrics(current_tick);
    }

    metrics.simulated_duration_ticks = max_tick;
    metrics.wall_clock_elapsed_microseconds = static_cast<uint64_t>(timer.elapsed_microseconds());
    metrics.final_used_bytes = engine.current_consumed_bytes();
    metrics.average_utilization_ratio = sample_count > 0 ? (sum_utilization / static_cast<double>(sample_count)) : 0.0;

    // Process Survival Calculations
    std::unordered_map<uint32_t, size_t> tier_total;
    std::unordered_map<uint32_t, size_t> tier_survived;

    for (const auto& spec : workload.process_specs) {
        tier_total[static_cast<uint32_t>(spec.importance)]++;
    }

    size_t survived_count = 0;
    size_t killed_pressure_count = 0;

    for (const auto* proc : engine.processes()) {
        auto score = static_cast<uint32_t>(proc->importance());
        if (proc->state() == intel::ProcessLifecycleState::Completed || proc->is_alive()) {
            survived_count++;
            tier_survived[score]++;
        } else if (proc->was_killed_under_pressure()) {
            killed_pressure_count++;
        }
    }

    metrics.processes_survived = survived_count;
    metrics.processes_terminated_pressure = killed_pressure_count;
    metrics.survival_rate = metrics.total_processes > 0
        ? (static_cast<double>(metrics.processes_survived) / static_cast<double>(metrics.total_processes))
        : 0.0;

    auto calc_tier_rate = [&](intel::ProcessImportance imp) {
        auto score = static_cast<uint32_t>(imp);
        size_t tot = tier_total[score];
        if (tot == 0) return 1.0;
        return static_cast<double>(tier_survived[score]) / static_cast<double>(tot);
    };

    metrics.foreground_survival_rate = calc_tier_rate(intel::ProcessImportance::Foreground);
    metrics.visible_survival_rate = calc_tier_rate(intel::ProcessImportance::Visible);
    metrics.service_survival_rate = calc_tier_rate(intel::ProcessImportance::Service);
    metrics.background_survival_rate = calc_tier_rate(intel::ProcessImportance::Background);
    metrics.cached_survival_rate = calc_tier_rate(intel::ProcessImportance::Cached);

    metrics.average_reclaimed_per_victim_bytes = metrics.processes_reclaimed > 0
        ? (static_cast<double>(metrics.total_reclaimed_bytes) / static_cast<double>(metrics.processes_reclaimed))
        : 0.0;

    metrics.target_achievement_rate = metrics.reclamation_invocations > 0
        ? (static_cast<double>(metrics.reclamation_successes) / static_cast<double>(metrics.reclamation_invocations))
        : 1.0;

    result.process_records.reserve(pending.size());
    for (size_t i = 0; i < pending.size(); ++i) {
        auto pid = pending[i].proc.pid();
        const auto* proc = engine.get_process(pid);
        if (proc) {
            result.process_records.push_back(ProcessExecutionRecord{
                .pid = pid,
                .name = proc->name(),
                .importance = proc->importance(),
                .cpu_priority = proc->cpu_priority(),
                .initial_memory_bytes = pending[i].initial_memory,
                .peak_memory_bytes = proc->memory().peak_memory_bytes,
                .final_memory_bytes = proc->memory().memory_consumed_bytes,
                .final_state = proc->state(),
                .killed_under_pressure = proc->was_killed_under_pressure(),
                .termination_reason = proc->termination_reason()
            });
        }
    }

    return core::Result<ExperimentResult>::ok(std::move(result));
}

core::Result<ExperimentComparison> ExperimentRunner::compare_all_policies(
    const WorkloadDefinition& workload,
    const ExperimentConfiguration& config
) {
    ExperimentComparison comparison;
    comparison.scenario_name = workload.scenario_name;
    comparison.seed = workload.seed;
    comparison.workload_fingerprint = workload.fingerprint;
    comparison.total_capacity_bytes = workload.total_memory_bytes;

    reclaim::FifoReclamationPolicy fifo_pol;
    reclaim::LruReclamationPolicy lru_pol;
    reclaim::PriorityReclamationPolicy prio_pol;
    reclaim::ImportanceAwareReclamationPolicy lmk_pol;

    // Run FIFO
    auto fifo_res = run_policy(workload, fifo_pol, config);
    if (fifo_res.is_err()) return core::Result<ExperimentComparison>::err(fifo_res.error());
    comparison.policy_results.push_back(std::move(fifo_res.value()));

    // Run LRU
    auto lru_res = run_policy(workload, lru_pol, config);
    if (lru_res.is_err()) return core::Result<ExperimentComparison>::err(lru_res.error());
    comparison.policy_results.push_back(std::move(lru_res.value()));

    // Run Priority
    auto prio_res = run_policy(workload, prio_pol, config);
    if (prio_res.is_err()) return core::Result<ExperimentComparison>::err(prio_res.error());
    comparison.policy_results.push_back(std::move(prio_res.value()));

    // Run Importance-Aware
    auto lmk_res = run_policy(workload, lmk_pol, config);
    if (lmk_res.is_err()) return core::Result<ExperimentComparison>::err(lmk_res.error());
    comparison.policy_results.push_back(std::move(lmk_res.value()));

    return core::Result<ExperimentComparison>::ok(std::move(comparison));
}

core::Result<ExperimentComparison> ExperimentRunner::compare_scenario(
    WorkloadScenarioType scenario,
    uint64_t seed,
    size_t process_count,
    uint64_t total_memory_bytes,
    const ExperimentConfiguration& config
) {
    auto wkl_res = WorkloadGenerator::generate_scenario(scenario, seed, process_count, total_memory_bytes);
    if (wkl_res.is_err()) {
        return core::Result<ExperimentComparison>::err(wkl_res.error());
    }
    return compare_all_policies(wkl_res.value(), config);
}

} // namespace kernox::experiment
