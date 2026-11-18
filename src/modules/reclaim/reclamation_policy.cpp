#include "kernox/modules/reclaim/reclamation_policy.hpp"

#include <algorithm>

namespace kernox::reclaim {

std::optional<VictimSelection> FifoReclamationPolicy::select_victim(
    const std::vector<const intel::SimulatedProcess*>& candidates
) const {
    const intel::SimulatedProcess* best = nullptr;

    for (const auto* p : candidates) {
        if (!p || !p->is_alive() || p->memory().memory_consumed_bytes == 0) {
            continue;
        }

        if (!best) {
            best = p;
            continue;
        }

        if (p->creation_time_ticks() < best->creation_time_ticks()) {
            best = p;
        } else if (p->creation_time_ticks() == best->creation_time_ticks() && p->pid() < best->pid()) {
            best = p;
        }
    }

    if (!best) {
        return std::nullopt;
    }

    VictimSelection selection{
        best->pid(),
        "FIFO: oldest resident process (created at tick " +
        std::to_string(best->creation_time_ticks()) + ", PID " + std::to_string(best->pid()) + ")"
    };
    return selection;
}

std::optional<VictimSelection> LruReclamationPolicy::select_victim(
    const std::vector<const intel::SimulatedProcess*>& candidates
) const {
    const intel::SimulatedProcess* best = nullptr;

    for (const auto* p : candidates) {
        if (!p || !p->is_alive() || p->memory().memory_consumed_bytes == 0) {
            continue;
        }

        if (!best) {
            best = p;
            continue;
        }

        if (p->last_active_time_ticks() < best->last_active_time_ticks()) {
            best = p;
        } else if (p->last_active_time_ticks() == best->last_active_time_ticks()) {
            if (p->creation_time_ticks() < best->creation_time_ticks()) {
                best = p;
            } else if (p->creation_time_ticks() == best->creation_time_ticks() && p->pid() < best->pid()) {
                best = p;
            }
        }
    }

    if (!best) {
        return std::nullopt;
    }

    VictimSelection selection{
        best->pid(),
        "LRU: least recently active process (last active at tick " +
        std::to_string(best->last_active_time_ticks()) + ", PID " + std::to_string(best->pid()) + ")"
    };
    return selection;
}

std::optional<VictimSelection> PriorityReclamationPolicy::select_victim(
    const std::vector<const intel::SimulatedProcess*>& candidates
) const {
    const intel::SimulatedProcess* best = nullptr;

    for (const auto* p : candidates) {
        if (!p || !p->is_alive() || p->memory().memory_consumed_bytes == 0) {
            continue;
        }

        if (!best) {
            best = p;
            continue;
        }

        // Higher numeric cpu_priority = lower scheduling urgency -> preferred victim
        if (p->cpu_priority() > best->cpu_priority()) {
            best = p;
        } else if (p->cpu_priority() == best->cpu_priority()) {
            if (p->creation_time_ticks() < best->creation_time_ticks()) {
                best = p;
            } else if (p->creation_time_ticks() == best->creation_time_ticks() && p->pid() < best->pid()) {
                best = p;
            }
        }
    }

    if (!best) {
        return std::nullopt;
    }

    VictimSelection selection{
        best->pid(),
        "Priority: lowest CPU scheduling priority (" +
        std::to_string(best->cpu_priority()) + ", PID " + std::to_string(best->pid()) + ")"
    };
    return selection;
}

std::optional<VictimSelection> ImportanceAwareReclamationPolicy::select_victim(
    const std::vector<const intel::SimulatedProcess*>& candidates
) const {
    const intel::SimulatedProcess* best = nullptr;

    for (const auto* p : candidates) {
        if (!p || !p->is_alive() || p->memory().memory_consumed_bytes == 0) {
            continue;
        }

        // Check if process is protected under this policy's configuration
        // Lower numeric score = higher importance/protection.
        if (static_cast<uint32_t>(p->importance()) <= static_cast<uint32_t>(min_protected_importance_)) {
            continue;
        }

        if (!best) {
            best = p;
            continue;
        }

        auto p_score = static_cast<uint32_t>(p->importance());
        auto best_score = static_cast<uint32_t>(best->importance());

        // Higher importance score = higher eligibility for termination (Cached 500 > Background 400 ...)
        if (p_score > best_score) {
            best = p;
        } else if (p_score == best_score) {
            // Tie-break 1: Largest resident memory footprint
            if (p->memory().memory_consumed_bytes > best->memory().memory_consumed_bytes) {
                best = p;
            } else if (p->memory().memory_consumed_bytes == best->memory().memory_consumed_bytes) {
                // Tie-break 2: Least recently active
                if (p->last_active_time_ticks() < best->last_active_time_ticks()) {
                    best = p;
                } else if (p->last_active_time_ticks() == best->last_active_time_ticks()) {
                    // Tie-break 3: Deterministic PID
                    if (p->pid() < best->pid()) {
                        best = p;
                    }
                }
            }
        }
    }

    if (!best) {
        return std::nullopt;
    }

    VictimSelection selection{
        best->pid(),
        "Importance: tier " + std::string(intel::to_string(best->importance())) +
        " (score " + std::to_string(static_cast<uint32_t>(best->importance())) +
        "), resident " + std::to_string(best->memory().memory_consumed_bytes) +
        " B, PID " + std::to_string(best->pid())
    };
    return selection;
}

} // namespace kernox::reclaim
