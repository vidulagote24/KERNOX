#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/intel/process_model.hpp"
#include "kernox/modules/reclaim/pressure_detector.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::simulator {

// Dynamic memory demand/release event occurring during custom simulation (in MB).
struct CustomMemoryEvent {
    uint64_t tick{0};       // Simulation time tick at which event triggers
    int64_t delta_mb{0};    // Positive: allocation demand; Negative: voluntary release
    std::string reason{};   // Diagnostic description
};

// Simulated device RAM and threshold configuration (in MB).
struct CustomDeviceConfiguration {
    uint64_t total_ram_mb{2048};              // Default: 2048 MB (2 GiB)
    double moderate_threshold_ratio{0.75};    // Default: 75%
    double critical_threshold_ratio{0.90};    // Default: 90%

    [[nodiscard]] reclaim::PressureThresholds to_pressure_thresholds() const noexcept {
        return reclaim::PressureThresholds{
            .total_memory_bytes = total_ram_mb * 1024ULL * 1024ULL,
            .moderate_threshold_ratio = moderate_threshold_ratio,
            .critical_threshold_ratio = critical_threshold_ratio
        };
    }
};

// User-specified application process input (in MB).
struct CustomProcessInput {
    std::string name{};
    uint32_t cpu_priority{0};                             // 0 (highest) to 99 (lowest)
    intel::ProcessImportance importance{intel::ProcessImportance::Background};
    uint64_t initial_memory_mb{0};                        // Initial resident memory in MB
    uint64_t requested_memory_mb{0};                      // Total declared memory requirement in MB (>= initial)
    uint64_t cpu_burst_ticks{50};                         // Total execution ticks required (> 0)
    uint64_t arrival_tick{0};                             // Tick when process enters system
    std::vector<CustomMemoryEvent> dynamic_events{};      // Sequence of dynamic memory allocations/releases
};

// Full custom simulation configuration container.
struct CustomSimulationConfiguration {
    CustomDeviceConfiguration device_config{};
    std::vector<CustomProcessInput> processes{};
    uint64_t seed{42};
    std::string scenario_name{"CustomInteractive"};
};

// Input validation functions
[[nodiscard]] core::Result<void> validate_device_config(const CustomDeviceConfiguration& config);
[[nodiscard]] core::Result<void> validate_process_input(const CustomProcessInput& input);
[[nodiscard]] core::Result<void> validate_process_collection(
    const std::vector<CustomProcessInput>& processes,
    const CustomDeviceConfiguration& device_config
);
[[nodiscard]] core::Result<void> validate_simulation_config(const CustomSimulationConfiguration& config);

} // namespace kernox::simulator
