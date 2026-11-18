#pragma once

#include "kernox/core/result.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace kernox::reclaim {

// Discrete memory pressure levels.
enum class MemoryPressureLevel : uint32_t {
    Normal = 0,   // Utilization below moderate threshold
    Moderate = 1, // Utilization at or above moderate threshold, below critical threshold
    Critical = 2  // Utilization at or above critical threshold
};

[[nodiscard]] std::string_view to_string(MemoryPressureLevel level) noexcept;
std::ostream& operator<<(std::ostream& os, MemoryPressureLevel level);

// Configurable utilization thresholds.
struct PressureThresholds {
    uint64_t total_memory_bytes{1024ULL * 1024ULL * 1024ULL}; // Default: 1 GiB
    double moderate_threshold_ratio{0.75};                     // 75% utilization
    double critical_threshold_ratio{0.90};                     // 90% utilization

    // Validates threshold consistency and non-zero memory capacity.
    [[nodiscard]] core::Result<void> validate() const;

    [[nodiscard]] uint64_t moderate_limit_bytes() const noexcept {
        return static_cast<uint64_t>(static_cast<double>(total_memory_bytes) * moderate_threshold_ratio);
    }

    [[nodiscard]] uint64_t critical_limit_bytes() const noexcept {
        return static_cast<uint64_t>(static_cast<double>(total_memory_bytes) * critical_threshold_ratio);
    }
};

// Evaluates simulated memory consumption against configurable threshold limits.
class MemoryPressureDetector {
public:
    static core::Result<MemoryPressureDetector> create(PressureThresholds thresholds = PressureThresholds{});
    static core::Result<MemoryPressureDetector> create(
        uint64_t total_memory_bytes,
        double moderate_ratio = 0.75,
        double critical_ratio = 0.90
    );

    // Replaces thresholds after validating consistency.
    core::Result<void> update_thresholds(PressureThresholds new_thresholds);

    [[nodiscard]] const PressureThresholds& thresholds() const noexcept { return thresholds_; }

    // Evaluates the discrete pressure level for a given memory consumption in bytes.
    [[nodiscard]] MemoryPressureLevel detect(uint64_t used_bytes) const noexcept;

    // Calculates floating-point utilization ratio [0.0, ...] (supports overcommit).
    [[nodiscard]] double utilization_ratio(uint64_t used_bytes) const noexcept;

    // Returns a comprehensive textual diagnostics string.
    [[nodiscard]] std::string describe_pressure(uint64_t used_bytes) const;

    // Calculates bytes that must be freed to drop strictly below the moderate threshold.
    [[nodiscard]] uint64_t bytes_to_reach_normal(uint64_t used_bytes) const noexcept;

    // Calculates bytes that must be freed to drop strictly below the critical threshold.
    [[nodiscard]] uint64_t bytes_to_reach_moderate(uint64_t used_bytes) const noexcept;

private:
    explicit MemoryPressureDetector(PressureThresholds thresholds) : thresholds_(thresholds) {}

    PressureThresholds thresholds_{};
};

} // namespace kernox::reclaim
