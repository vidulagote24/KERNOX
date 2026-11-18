#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/intel/process_model.hpp"
#include "kernox/modules/reclaim/pressure_detector.hpp"
#include "kernox/modules/reclaim/reclamation_policy.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kernox::reclaim {

// Goal specifying what condition will satisfy a reclamation run.
enum class ReclamationGoalType : uint32_t {
    Bytes = 0,         // Reclaim at least a specified number of bytes
    TargetPressure = 1 // Reclaim until pressure drops to or below a specified level
};

// Termination rationale for a reclamation operation.
enum class ReclamationStopReason : uint32_t {
    TargetAchieved = 0,       // Requested bytes or target pressure level satisfied
    NoEligibleVictim = 1,     // No candidates eligible or all remaining candidates protected
    PoolExhausted = 2,        // All registered processes have terminated or have zero memory
    InitialStateSatisfied = 3 // System already satisfied the goal before any reclamation
};

[[nodiscard]] std::string_view to_string(ReclamationStopReason reason) noexcept;
std::ostream& operator<<(std::ostream& os, ReclamationStopReason reason);

// Input specification for a memory reclamation request.
struct ReclamationRequest {
    ReclamationGoalType goal_type{ReclamationGoalType::Bytes};
    uint64_t target_bytes{0};                                           // Used when goal_type == Bytes
    MemoryPressureLevel target_pressure{MemoryPressureLevel::Normal};  // Used when goal_type == TargetPressure
    uint64_t current_time_ticks{0};                                     // Monotonic tick for termination records
    std::string custom_reason{"Memory pressure reclamation"};
};

// Record of an individual victim process termination and memory reclaim.
struct ReclamationDecision {
    intel::SimulatedPid victim_pid{0};
    std::string victim_name{};
    intel::ProcessImportance importance{intel::ProcessImportance::Cached};
    uint32_t cpu_priority{0};
    uint64_t memory_reclaimed_bytes{0};
    std::string policy_name{};
    std::string reason{};
    uint64_t timestamp_ticks{0};
    MemoryPressureLevel pressure_level{MemoryPressureLevel::Moderate};
};

// Structured metrics and audit trace returned upon completing a reclamation run.
struct ReclamationReport {
    std::string policy_name{};
    MemoryPressureLevel initial_pressure{MemoryPressureLevel::Normal};
    MemoryPressureLevel final_pressure{MemoryPressureLevel::Normal};
    uint64_t total_capacity_bytes{0};
    uint64_t initial_used_bytes{0};
    uint64_t final_used_bytes{0};
    uint64_t initial_available_bytes{0};
    uint64_t final_available_bytes{0};
    uint64_t requested_reclaim_bytes{0};
    uint64_t actual_reclaim_bytes{0};
    size_t processes_terminated{0};
    size_t selection_attempts{0};
    bool target_achieved{false};
    ReclamationStopReason stop_reason{ReclamationStopReason::TargetAchieved};
    std::string stop_explanation{};
    std::vector<ReclamationDecision> decisions{};

    [[nodiscard]] std::string to_string() const;
};

// Core engine managing aggregate system capacity, registered processes, and pressure-driven reclamation.
class MemoryReclamationEngine {
public:
    static core::Result<MemoryReclamationEngine> create(
        uint64_t total_capacity_bytes,
        PressureThresholds thresholds = PressureThresholds{}
    );

    // --- Process Registration & Accounting ---

    core::Result<void> register_process(intel::SimulatedProcess process);
    core::Result<void> unregister_process(intel::SimulatedPid pid);

    [[nodiscard]] bool has_process(intel::SimulatedPid pid) const noexcept;
    [[nodiscard]] const intel::SimulatedProcess* get_process(intel::SimulatedPid pid) const noexcept;
    [[nodiscard]] intel::SimulatedProcess* get_process_mut(intel::SimulatedPid pid) noexcept;
    [[nodiscard]] std::vector<const intel::SimulatedProcess*> processes() const;
    [[nodiscard]] size_t process_count() const noexcept { return processes_.size(); }

    // --- Memory Allocation & Release Operations ---

    core::Result<void> allocate_process_memory(intel::SimulatedPid pid, uint64_t bytes);
    core::Result<void> release_process_memory(intel::SimulatedPid pid, uint64_t bytes);
    [[nodiscard]] core::Result<uint64_t> query_process_memory(intel::SimulatedPid pid) const;

    // --- System Memory Capacity & Pressure Queries ---

    [[nodiscard]] uint64_t total_capacity_bytes() const noexcept { return total_capacity_bytes_; }
    [[nodiscard]] uint64_t current_consumed_bytes() const noexcept { return current_consumed_bytes_; }
    [[nodiscard]] uint64_t available_bytes() const noexcept {
        return total_capacity_bytes_ >= current_consumed_bytes_
                   ? total_capacity_bytes_ - current_consumed_bytes_
                   : 0;
    }
    [[nodiscard]] double memory_utilization_ratio() const noexcept {
        return detector_.utilization_ratio(current_consumed_bytes_);
    }
    [[nodiscard]] MemoryPressureLevel current_pressure_level() const noexcept {
        return detector_.detect(current_consumed_bytes_);
    }
    [[nodiscard]] const MemoryPressureDetector& pressure_detector() const noexcept { return detector_; }
    [[nodiscard]] MemoryPressureDetector& pressure_detector() noexcept { return detector_; }

    // --- Memory Reclamation Coordination ---

    // Executes reclamation according to the provided request parameters.
    ReclamationReport reclaim(const IReclamationPolicy& policy, const ReclamationRequest& request);

    // Convenience method: reclaims at least target_bytes.
    ReclamationReport reclaim_bytes(
        const IReclamationPolicy& policy,
        uint64_t target_bytes,
        uint64_t current_time_ticks = 0
    );

    // Convenience method: reclaims memory until system pressure drops to or below target_level.
    ReclamationReport reclaim_to_pressure(
        const IReclamationPolicy& policy,
        MemoryPressureLevel target_level = MemoryPressureLevel::Normal,
        uint64_t current_time_ticks = 0
    );

    // Deep clone supporting Phase D comparative benchmarking across multiple policies.
    [[nodiscard]] MemoryReclamationEngine clone() const;

private:
    MemoryReclamationEngine(uint64_t total_capacity_bytes, MemoryPressureDetector detector);

    uint64_t total_capacity_bytes_{0};
    uint64_t current_consumed_bytes_{0};
    MemoryPressureDetector detector_;

    std::vector<intel::SimulatedProcess> processes_{};
    std::unordered_map<intel::SimulatedPid, size_t> pid_to_index_{};
};

} // namespace kernox::reclaim
