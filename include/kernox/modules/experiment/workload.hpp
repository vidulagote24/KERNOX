#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/intel/process_model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::experiment {

// Built-in workload scenario types with distinct operational profiles.
enum class WorkloadScenarioType : uint32_t {
    Normal = 0,               // Standard desktop/mobile usage; low to moderate memory pressure
    HighMemoryPressure = 1,   // Aggressive allocation driving system into Critical pressure
    BackgroundHeavy = 2,      // Dominant Cached and Background applications
    ForegroundProtection = 3, // Strong Foreground presence under pressure; tests protection
    Mixed = 4,                // Heterogeneous priorities, memory sizes, and arrival times
    Custom = 5                // User-configured workload parameters
};

[[nodiscard]] std::string_view to_string(WorkloadScenarioType type) noexcept;
[[nodiscard]] std::optional<WorkloadScenarioType> parse_scenario_type(std::string_view str) noexcept;

// Dynamic memory demand/release event occurring during simulated execution.
struct MemoryDemandEvent {
    uint64_t tick{0};       // Simulation time tick at which event triggers
    int64_t delta_bytes{0}; // Positive: allocation demand; Negative: voluntary release
    std::string reason{};   // Diagnostic description (e.g., "Image buffer decode", "Tab closed")
};

// Immutable logical specification of a simulated application process in a workload.
struct ProcessWorkloadSpec {
    std::string name{};
    uint32_t cpu_priority{0};                             // 0 (highest) to 99 (lowest)
    intel::ProcessImportance importance{intel::ProcessImportance::Background};
    uint64_t requested_memory_bytes{0};                   // Total declared memory requirement
    uint64_t initial_memory_bytes{0};                     // Resident memory allocated upon admission
    uint64_t cpu_burst_ticks{50};                         // Total execution ticks required
    uint64_t arrival_tick{0};                             // Monotonic tick when process enters system
    std::vector<MemoryDemandEvent> dynamic_events{};      // Sequence of dynamic memory allocations/releases
};

// Complete logical definition of a workload, independent of simulation runs.
struct WorkloadDefinition {
    std::string scenario_name{};
    WorkloadScenarioType scenario_type{WorkloadScenarioType::Normal};
    uint64_t seed{0};
    uint64_t total_memory_bytes{1024ULL * 1024ULL * 1024ULL}; // Default: 1 GiB capacity
    std::vector<ProcessWorkloadSpec> process_specs{};
    std::string fingerprint{};                                // Deterministic workload integrity hash

    // Computes a 64-bit FNV-1a hex digest uniquely identifying this workload configuration.
    [[nodiscard]] std::string compute_fingerprint() const;
};

// Configuration driving deterministic workload generation.
struct WorkloadGeneratorConfig {
    WorkloadScenarioType scenario{WorkloadScenarioType::Normal};
    uint64_t seed{42};
    size_t process_count{10};
    uint64_t total_memory_bytes{1024ULL * 1024ULL * 1024ULL}; // 1 GiB

    [[nodiscard]] core::Result<void> validate() const;
};

// Deterministic workload generator backed by std::mt19937_64.
// Guarantees identical logical workloads for identical configurations and seeds.
class WorkloadGenerator {
public:
    // Generates a complete workload definition from configuration.
    static core::Result<WorkloadDefinition> generate(const WorkloadGeneratorConfig& config);

    // Convenience generator for built-in scenarios.
    static core::Result<WorkloadDefinition> generate_scenario(
        WorkloadScenarioType scenario,
        uint64_t seed = 42,
        size_t process_count = 10,
        uint64_t total_memory_bytes = 1024ULL * 1024ULL * 1024ULL
    );
};

} // namespace kernox::experiment
