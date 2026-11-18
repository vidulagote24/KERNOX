#pragma once

#include "kernox/modules/intel/process_model.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::reclaim {

// Output descriptor indicating a chosen victim process and selection rationale.
struct VictimSelection {
    intel::SimulatedPid pid{0};
    std::string reason{};
};

// Abstract Strategy interface for memory reclamation victim selection policies.
// Policies evaluate eligible simulated processes and select the next termination victim.
// Policies NEVER terminate processes themselves.
class IReclamationPolicy {
public:
    virtual ~IReclamationPolicy() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Evaluates a collection of candidate processes and selects the single most suitable victim.
    // Returns std::nullopt if no candidate is eligible or if all remaining candidates are protected.
    [[nodiscard]] virtual std::optional<VictimSelection> select_victim(
        const std::vector<const intel::SimulatedProcess*>& candidates
    ) const = 0;
};

// FIFO Reclamation Policy:
// Selects the candidate that has been resident in simulated memory the longest (earliest creation timestamp).
// Ties broken deterministically by smaller SimulatedPid.
class FifoReclamationPolicy final : public IReclamationPolicy {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "FIFO";
    }

    [[nodiscard]] std::optional<VictimSelection> select_victim(
        const std::vector<const intel::SimulatedProcess*>& candidates
    ) const override;
};

// LRU Reclamation Policy:
// Selects the least recently active candidate (earliest last_active_time_ticks).
// Ties broken deterministically by earlier creation time, then smaller SimulatedPid.
class LruReclamationPolicy final : public IReclamationPolicy {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "LRU";
    }

    [[nodiscard]] std::optional<VictimSelection> select_victim(
        const std::vector<const intel::SimulatedProcess*>& candidates
    ) const override;
};

// Priority-Based Reclamation Policy:
// Selects the candidate with the highest numeric cpu_priority (lowest CPU scheduling urgency, range 0-99).
// Note: This is an OS simulation policy, not a model of Android LMKD.
// Ties broken deterministically by earlier creation time, then smaller SimulatedPid.
class PriorityReclamationPolicy final : public IReclamationPolicy {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Priority";
    }

    [[nodiscard]] std::optional<VictimSelection> select_victim(
        const std::vector<const intel::SimulatedProcess*>& candidates
    ) const override;
};

// Android-Inspired Importance-Aware Reclamation Policy:
// Selects candidates according to mobile lifecycle tiers:
//   Cached (500) -> Background (400) -> Service (300) -> Visible (200) -> Foreground (100)
// Higher numeric score = higher eligibility for termination.
// Foreground processes are protected by default (min_protected_importance = Foreground).
// Ties within the same tier broken by largest memory consumed, then earliest last active, then smaller PID.
class ImportanceAwareReclamationPolicy final : public IReclamationPolicy {
public:
    explicit ImportanceAwareReclamationPolicy(
        intel::ProcessImportance min_protected = intel::ProcessImportance::Foreground
    ) noexcept
        : min_protected_importance_(min_protected) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "ImportanceAware";
    }

    [[nodiscard]] intel::ProcessImportance min_protected_importance() const noexcept {
        return min_protected_importance_;
    }

    void set_min_protected_importance(intel::ProcessImportance min_protected) noexcept {
        min_protected_importance_ = min_protected;
    }

    [[nodiscard]] std::optional<VictimSelection> select_victim(
        const std::vector<const intel::SimulatedProcess*>& candidates
    ) const override;

private:
    intel::ProcessImportance min_protected_importance_{intel::ProcessImportance::Foreground};
};

} // namespace kernox::reclaim
