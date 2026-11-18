#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/experiment/workload.hpp"
#include "kernox/modules/reclaim/pressure_detector.hpp"
#include "kernox/modules/reclaim/reclamation_engine.hpp"
#include "kernox/modules/reclaim/reclamation_policy.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::experiment {

// Comprehensive metrics computed over an experiment execution run.
struct ExperimentMetrics {
    // Memory Metrics (bytes)
    uint64_t total_capacity_bytes{0};
    uint64_t initial_used_bytes{0};
    uint64_t peak_used_bytes{0};
    uint64_t final_used_bytes{0};
    double average_utilization_ratio{0.0};
    uint64_t total_reclaimed_bytes{0};
    size_t pressure_events_moderate{0};
    size_t pressure_events_critical{0};

    // Process Metrics
    size_t total_processes{0};
    size_t processes_completed{0};
    size_t processes_terminated_pressure{0};
    size_t processes_survived{0}; // Completed + still alive at experiment conclusion
    double survival_rate{0.0};    // processes_survived / total_processes

    // Importance-Tier Survival Rates (Tier survived / Tier total)
    double foreground_survival_rate{1.0};
    double visible_survival_rate{1.0};
    double service_survival_rate{1.0};
    double background_survival_rate{1.0};
    double cached_survival_rate{1.0};

    // Reclamation Operational Metrics
    size_t reclamation_invocations{0};
    size_t reclamation_successes{0};
    size_t processes_reclaimed{0};
    double average_reclaimed_per_victim_bytes{0.0};
    size_t failed_allocations{0};
    size_t no_eligible_victim_events{0};
    double target_achievement_rate{0.0};

    // Timing Metrics
    uint64_t simulated_duration_ticks{0};       // Logical simulation ticks
    uint64_t wall_clock_elapsed_microseconds{0}; // Real host execution time
};

// Snapshot of memory state at a specific simulation tick.
struct MemoryTimelinePoint {
    uint64_t tick{0};
    uint64_t consumed_bytes{0};
    reclaim::MemoryPressureLevel pressure_level{reclaim::MemoryPressureLevel::Normal};
    size_t active_processes{0};
};

// Process execution summary captured at experiment conclusion.
struct ProcessExecutionRecord {
    intel::SimulatedPid pid{0};
    std::string name{};
    intel::ProcessImportance importance{intel::ProcessImportance::Background};
    uint32_t cpu_priority{0};
    uint64_t initial_memory_bytes{0};
    uint64_t peak_memory_bytes{0};
    uint64_t final_memory_bytes{0};
    intel::ProcessLifecycleState final_state{intel::ProcessLifecycleState::Created};
    bool killed_under_pressure{false};
    std::string termination_reason{};
};

// Result of executing an experiment run for a single policy against a workload.
struct ExperimentResult {
    std::string policy_name{};
    std::string scenario_name{};
    uint64_t seed{0};
    std::string workload_fingerprint{};
    ExperimentMetrics metrics{};
    std::vector<reclaim::ReclamationDecision> reclamation_log{};
    std::vector<MemoryTimelinePoint> memory_timeline{};
    std::vector<ProcessExecutionRecord> process_records{};

    [[nodiscard]] std::string to_string() const;
};

// Structured comparison container aggregating results for all four policies on the exact same workload.
struct ExperimentComparison {
    std::string scenario_name{};
    uint64_t seed{0};
    std::string workload_fingerprint{};
    uint64_t total_capacity_bytes{0};
    std::vector<ExperimentResult> policy_results{};

    [[nodiscard]] const ExperimentResult* get_result(std::string_view policy_name) const noexcept;
    [[nodiscard]] std::string to_table_string() const;
    [[nodiscard]] std::string to_summary_string() const;
};

// Configuration parameters for experiment simulation execution.
struct ExperimentConfiguration {
    reclaim::PressureThresholds thresholds{};
    reclaim::MemoryPressureLevel target_reclaim_pressure{reclaim::MemoryPressureLevel::Normal};
    bool verbose_logging{false};
};

// Orchestrates deterministic simulation runs and multi-policy comparisons.
class ExperimentRunner {
public:
    // Executes a simulation run for a single policy against a workload definition.
    static core::Result<ExperimentResult> run_policy(
        const WorkloadDefinition& workload,
        const reclaim::IReclamationPolicy& policy,
        const ExperimentConfiguration& config = ExperimentConfiguration{}
    );

    // Executes a fair comparative benchmark running all 4 policies on independent fresh states
    // reconstructed from the exact same workload definition.
    static core::Result<ExperimentComparison> compare_all_policies(
        const WorkloadDefinition& workload,
        const ExperimentConfiguration& config = ExperimentConfiguration{}
    );

    // Helper: Runs a scenario by type with all 4 policies.
    static core::Result<ExperimentComparison> compare_scenario(
        WorkloadScenarioType scenario,
        uint64_t seed = 42,
        size_t process_count = 10,
        uint64_t total_memory_bytes = 1024ULL * 1024ULL * 1024ULL,
        const ExperimentConfiguration& config = ExperimentConfiguration{}
    );
};

} // namespace kernox::experiment
