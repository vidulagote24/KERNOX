#include "kernox/modules/reclaim/reclamation_engine.hpp"

#include <iomanip>
#include <sstream>

namespace kernox::reclaim {

std::string_view to_string(ReclamationStopReason reason) noexcept {
    switch (reason) {
        case ReclamationStopReason::TargetAchieved:
            return "TargetAchieved";
        case ReclamationStopReason::NoEligibleVictim:
            return "NoEligibleVictim";
        case ReclamationStopReason::PoolExhausted:
            return "PoolExhausted";
        case ReclamationStopReason::InitialStateSatisfied:
            return "InitialStateSatisfied";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& os, ReclamationStopReason reason) {
    return os << to_string(reason);
}

std::string ReclamationReport::to_string() const {
    std::ostringstream oss;
    oss << "================================================================================\n"
        << "KERNOX MEMORY RECLAMATION REPORT\n"
        << "Policy: " << policy_name << "\n"
        << "Status: " << (target_achieved ? "SUCCESS (Target Achieved)" : "TERMINATED (Target Incomplete)") << "\n"
        << "Stop Reason: " << reclaim::to_string(stop_reason) << " (" << stop_explanation << ")\n"
        << "--------------------------------------------------------------------------------\n"
        << "Pressure Transition : " << reclaim::to_string(initial_pressure) << " -> "
        << reclaim::to_string(final_pressure) << "\n"
        << "Used Memory         : " << initial_used_bytes << " B -> " << final_used_bytes
        << " B (Total Capacity: " << total_capacity_bytes << " B)\n"
        << "Available Memory    : " << initial_available_bytes << " B -> " << final_available_bytes << " B\n"
        << "Reclaimed Memory    : " << actual_reclaim_bytes << " B (Requested Target: "
        << requested_reclaim_bytes << " B)\n"
        << "Processes Reclaimed : " << processes_terminated << " (Selection Attempts: "
        << selection_attempts << ")\n";

    if (!decisions.empty()) {
        oss << "--------------------------------------------------------------------------------\n"
            << "RECLAMATION DECISION LOG:\n";
        for (size_t i = 0; i < decisions.size(); ++i) {
            const auto& d = decisions[i];
            oss << "  [" << (i + 1) << "] PID " << d.victim_pid << " (\"" << d.victim_name << "\")"
                << " | Importance: " << intel::to_string(d.importance) << " ("
                << static_cast<uint32_t>(d.importance) << ")"
                << " | CPU Prio: " << d.cpu_priority
                << " | Freed: " << d.memory_reclaimed_bytes << " B\n"
                << "      Rationale: " << d.reason << "\n";
        }
    }
    oss << "================================================================================";
    return oss.str();
}

core::Result<MemoryReclamationEngine> MemoryReclamationEngine::create(
    uint64_t total_capacity_bytes,
    PressureThresholds thresholds
) {
    thresholds.total_memory_bytes = total_capacity_bytes;
    auto val_res = thresholds.validate();
    if (val_res.is_err()) {
        return core::Result<MemoryReclamationEngine>::err(val_res.error());
    }

    auto det_res = MemoryPressureDetector::create(thresholds);
    if (det_res.is_err()) {
        return core::Result<MemoryReclamationEngine>::err(det_res.error());
    }

    return core::Result<MemoryReclamationEngine>::ok(
        MemoryReclamationEngine(total_capacity_bytes, std::move(det_res.value()))
    );
}

MemoryReclamationEngine::MemoryReclamationEngine(
    uint64_t total_capacity_bytes,
    MemoryPressureDetector detector
)
    : total_capacity_bytes_(total_capacity_bytes),
      current_consumed_bytes_(0),
      detector_(detector) {}

core::Result<void> MemoryReclamationEngine::register_process(intel::SimulatedProcess process) {
    intel::SimulatedPid pid = process.pid();
    if (has_process(pid)) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Process PID " + std::to_string(pid) + " is already registered in memory engine"
        );
    }

    uint64_t resident = process.memory().memory_consumed_bytes;
    if (resident > available_bytes()) {
        return core::Result<void>::err(
            core::ErrorCode::ResourceExhausted,
            "Cannot register process PID " + std::to_string(pid) + ": resident memory (" +
            std::to_string(resident) + " B) exceeds available simulated capacity (" +
            std::to_string(available_bytes()) + " B)"
        );
    }

    current_consumed_bytes_ += resident;
    pid_to_index_[pid] = processes_.size();
    processes_.push_back(std::move(process));
    return core::Result<void>::ok();
}

core::Result<void> MemoryReclamationEngine::unregister_process(intel::SimulatedPid pid) {
    auto it = pid_to_index_.find(pid);
    if (it == pid_to_index_.end()) {
        return core::Result<void>::err(
            core::ErrorCode::NotFound,
            "Process PID " + std::to_string(pid) + " not found in memory engine"
        );
    }

    size_t idx = it->second;
    uint64_t resident = processes_[idx].memory().memory_consumed_bytes;
    if (resident > current_consumed_bytes_) {
        current_consumed_bytes_ = 0;
    } else {
        current_consumed_bytes_ -= resident;
    }

    processes_.erase(processes_.begin() + static_cast<std::ptrdiff_t>(idx));
    pid_to_index_.clear();
    for (size_t i = 0; i < processes_.size(); ++i) {
        pid_to_index_[processes_[i].pid()] = i;
    }

    return core::Result<void>::ok();
}

bool MemoryReclamationEngine::has_process(intel::SimulatedPid pid) const noexcept {
    return pid_to_index_.find(pid) != pid_to_index_.end();
}

const intel::SimulatedProcess* MemoryReclamationEngine::get_process(intel::SimulatedPid pid) const noexcept {
    auto it = pid_to_index_.find(pid);
    if (it == pid_to_index_.end()) {
        return nullptr;
    }
    return &processes_[it->second];
}

intel::SimulatedProcess* MemoryReclamationEngine::get_process_mut(intel::SimulatedPid pid) noexcept {
    auto it = pid_to_index_.find(pid);
    if (it == pid_to_index_.end()) {
        return nullptr;
    }
    return &processes_[it->second];
}

std::vector<const intel::SimulatedProcess*> MemoryReclamationEngine::processes() const {
    std::vector<const intel::SimulatedProcess*> result;
    result.reserve(processes_.size());
    for (const auto& p : processes_) {
        result.push_back(&p);
    }
    return result;
}

core::Result<void> MemoryReclamationEngine::allocate_process_memory(intel::SimulatedPid pid, uint64_t bytes) {
    auto* proc = get_process_mut(pid);
    if (!proc) {
        return core::Result<void>::err(
            core::ErrorCode::NotFound,
            "Process PID " + std::to_string(pid) + " not registered in memory engine"
        );
    }

    if (bytes == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Memory allocation bytes must be greater than zero"
        );
    }

    if (bytes > available_bytes()) {
        return core::Result<void>::err(
            core::ErrorCode::ResourceExhausted,
            "Requested allocation (" + std::to_string(bytes) + " B) exceeds available simulated memory (" +
            std::to_string(available_bytes()) + " B)"
        );
    }

    auto alloc_res = proc->allocate_memory(bytes);
    if (alloc_res.is_err()) {
        return alloc_res;
    }

    current_consumed_bytes_ += bytes;
    return core::Result<void>::ok();
}

core::Result<void> MemoryReclamationEngine::release_process_memory(intel::SimulatedPid pid, uint64_t bytes) {
    auto* proc = get_process_mut(pid);
    if (!proc) {
        return core::Result<void>::err(
            core::ErrorCode::NotFound,
            "Process PID " + std::to_string(pid) + " not registered in memory engine"
        );
    }

    if (bytes == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Memory release bytes must be greater than zero"
        );
    }

    auto rel_res = proc->release_memory(bytes);
    if (rel_res.is_err()) {
        return rel_res;
    }

    if (bytes > current_consumed_bytes_) {
        current_consumed_bytes_ = 0;
    } else {
        current_consumed_bytes_ -= bytes;
    }

    return core::Result<void>::ok();
}

core::Result<uint64_t> MemoryReclamationEngine::query_process_memory(intel::SimulatedPid pid) const {
    const auto* proc = get_process(pid);
    if (!proc) {
        return core::Result<uint64_t>::err(
            core::ErrorCode::NotFound,
            "Process PID " + std::to_string(pid) + " not registered in memory engine"
        );
    }
    return core::Result<uint64_t>::ok(proc->memory().memory_consumed_bytes);
}

ReclamationReport MemoryReclamationEngine::reclaim(
    const IReclamationPolicy& policy,
    const ReclamationRequest& request
) {
    ReclamationReport report;
    report.policy_name = std::string(policy.name());
    report.initial_pressure = current_pressure_level();
    report.initial_used_bytes = current_consumed_bytes_;
    report.total_capacity_bytes = total_capacity_bytes_;
    report.initial_available_bytes = available_bytes();
    report.requested_reclaim_bytes = request.target_bytes;

    // Check trivial or pre-satisfied goals
    if (request.goal_type == ReclamationGoalType::Bytes && request.target_bytes == 0) {
        report.target_achieved = true;
        report.stop_reason = ReclamationStopReason::InitialStateSatisfied;
        report.stop_explanation = "Requested 0 bytes to reclaim; goal satisfied trivially";
        report.final_pressure = current_pressure_level();
        report.final_used_bytes = current_consumed_bytes_;
        report.final_available_bytes = available_bytes();
        return report;
    }

    if (request.goal_type == ReclamationGoalType::TargetPressure &&
        static_cast<uint32_t>(report.initial_pressure) <= static_cast<uint32_t>(request.target_pressure)) {
        report.target_achieved = true;
        report.stop_reason = ReclamationStopReason::InitialStateSatisfied;
        report.stop_explanation = "System pressure (" + std::string(to_string(report.initial_pressure)) +
                                  ") is already at or below target level (" +
                                  std::string(to_string(request.target_pressure)) + ")";
        report.final_pressure = current_pressure_level();
        report.final_used_bytes = current_consumed_bytes_;
        report.final_available_bytes = available_bytes();
        return report;
    }

    if (request.goal_type == ReclamationGoalType::TargetPressure) {
        if (request.target_pressure == MemoryPressureLevel::Normal) {
            report.requested_reclaim_bytes = detector_.bytes_to_reach_normal(current_consumed_bytes_);
        } else if (request.target_pressure == MemoryPressureLevel::Moderate) {
            report.requested_reclaim_bytes = detector_.bytes_to_reach_moderate(current_consumed_bytes_);
        }
    }

    // Iterative reclamation loop
    while (true) {
        // Goal check
        if (request.goal_type == ReclamationGoalType::Bytes &&
            report.actual_reclaim_bytes >= report.requested_reclaim_bytes) {
            report.target_achieved = true;
            report.stop_reason = ReclamationStopReason::TargetAchieved;
            report.stop_explanation = "Reclaimed " + std::to_string(report.actual_reclaim_bytes) +
                                      " B (target: " + std::to_string(report.requested_reclaim_bytes) + " B)";
            break;
        }

        if (request.goal_type == ReclamationGoalType::TargetPressure &&
            static_cast<uint32_t>(current_pressure_level()) <= static_cast<uint32_t>(request.target_pressure)) {
            report.target_achieved = true;
            report.stop_reason = ReclamationStopReason::TargetAchieved;
            report.stop_explanation = "System pressure reduced to " +
                                      std::string(to_string(current_pressure_level())) +
                                      " (target: " + std::string(to_string(request.target_pressure)) + ")";
            break;
        }

        // Collect candidates: must be alive and have non-zero resident memory
        std::vector<const intel::SimulatedProcess*> candidates;
        candidates.reserve(processes_.size());
        for (const auto& proc : processes_) {
            if (proc.is_alive() && proc.memory().memory_consumed_bytes > 0) {
                candidates.push_back(&proc);
            }
        }

        if (candidates.empty()) {
            report.target_achieved = false;
            report.stop_reason = processes_.empty() ? ReclamationStopReason::PoolExhausted
                                                    : ReclamationStopReason::NoEligibleVictim;
            report.stop_explanation = "No eligible alive processes with reclaimable memory remain in pool";
            break;
        }

        report.selection_attempts++;
        auto victim_opt = policy.select_victim(candidates);
        if (!victim_opt.has_value()) {
            report.target_achieved = false;
            report.stop_reason = ReclamationStopReason::NoEligibleVictim;
            report.stop_explanation = "Policy " + std::string(policy.name()) +
                                      " selected no victim (all candidates protected or ineligible)";
            break;
        }

        intel::SimulatedPid victim_pid = victim_opt->pid;
        auto* victim_proc = get_process_mut(victim_pid);
        if (!victim_proc || !victim_proc->is_alive() || victim_proc->memory().memory_consumed_bytes == 0) {
            report.target_achieved = false;
            report.stop_reason = ReclamationStopReason::NoEligibleVictim;
            report.stop_explanation = "Selected victim PID " + std::to_string(victim_pid) +
                                      " is invalid or has no memory to reclaim";
            break;
        }

        auto pre_reclaim_pressure = current_pressure_level();
        uint64_t freed_bytes = victim_proc->reclaim_all_memory();
        if (freed_bytes > current_consumed_bytes_) {
            current_consumed_bytes_ = 0;
        } else {
            current_consumed_bytes_ -= freed_bytes;
        }
        report.actual_reclaim_bytes += freed_bytes;

        auto term_res = victim_proc->terminate(
            "Killed under memory pressure by " + std::string(policy.name()) + ": " + victim_opt->reason,
            request.current_time_ticks,
            /*killed_under_pressure=*/true
        );
        static_cast<void>(term_res);

        ReclamationDecision decision{
            victim_pid,
            victim_proc->name(),
            victim_proc->importance(),
            victim_proc->cpu_priority(),
            freed_bytes,
            std::string(policy.name()),
            victim_opt->reason,
            request.current_time_ticks,
            pre_reclaim_pressure
        };
        report.decisions.push_back(decision);
        report.processes_terminated++;
    }

    report.final_pressure = current_pressure_level();
    report.final_used_bytes = current_consumed_bytes_;
    report.final_available_bytes = available_bytes();
    return report;
}

ReclamationReport MemoryReclamationEngine::reclaim_bytes(
    const IReclamationPolicy& policy,
    uint64_t target_bytes,
    uint64_t current_time_ticks
) {
    ReclamationRequest req{
        ReclamationGoalType::Bytes,
        target_bytes,
        MemoryPressureLevel::Normal,
        current_time_ticks,
        "Reclaim target bytes"
    };
    return reclaim(policy, req);
}

ReclamationReport MemoryReclamationEngine::reclaim_to_pressure(
    const IReclamationPolicy& policy,
    MemoryPressureLevel target_level,
    uint64_t current_time_ticks
) {
    ReclamationRequest req{
        ReclamationGoalType::TargetPressure,
        0,
        target_level,
        current_time_ticks,
        "Reclaim to target pressure level"
    };
    return reclaim(policy, req);
}

MemoryReclamationEngine MemoryReclamationEngine::clone() const {
    return *this;
}

} // namespace kernox::reclaim
