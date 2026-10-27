#pragma once

#include "kernox/core/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::sim {

enum class DiskDirection {
    Up,   // Moving towards higher cylinder numbers (towards cylinder_count - 1)
    Down  // Moving towards lower cylinder numbers (towards cylinder 0)
};

[[nodiscard]] std::string_view to_string(DiskDirection dir) noexcept;
[[nodiscard]] std::optional<DiskDirection> parse_direction(std::string_view str) noexcept;

enum class DiskAlgorithm {
    FCFS,
    SSTF,
    SCAN,
    CSCAN,
    LOOK,
    CLOOK
};

[[nodiscard]] std::string_view to_string(DiskAlgorithm algo) noexcept;
[[nodiscard]] std::optional<DiskAlgorithm> parse_disk_algorithm(std::string_view str) noexcept;

enum class MovementType {
    Service,   // Head moved to service a requested cylinder
    Boundary,  // Head moved to physical disk boundary (0 or cylinder_count - 1) in SCAN / C-SCAN
    Wrap       // Head wrapped around (in C-SCAN / C-LOOK)
};

[[nodiscard]] std::string_view to_string(MovementType type) noexcept;

struct DiskMovementStep {
    uint32_t from_cylinder{0};
    uint32_t to_cylinder{0};
    uint32_t distance{0};
    MovementType type{MovementType::Service};
    std::optional<uint32_t> serviced_request{std::nullopt};
};

struct DiskScheduleConfig {
    uint32_t cylinder_count{200}; // Range: [0, cylinder_count - 1]
    uint32_t initial_head{53};
    std::vector<uint32_t> requests;
    DiskDirection direction{DiskDirection::Up};
};

struct DiskScheduleResult {
    std::string algorithm_name;
    uint32_t cylinder_count{0};
    uint32_t initial_head{0};
    DiskDirection initial_direction{DiskDirection::Up};
    std::vector<uint32_t> service_order;
    std::vector<DiskMovementStep> movement_trace;
    uint64_t total_head_movement{0};
    size_t requests_serviced{0};
    uint32_t final_head_position{0};
    std::string policy_notes;
};

class DiskScheduler {
public:
    // Config validation
    static core::Result<void> validate_config(const DiskScheduleConfig& config);

    // Core Algorithms
    static core::Result<DiskScheduleResult> run_fcfs(const DiskScheduleConfig& config);
    static core::Result<DiskScheduleResult> run_sstf(const DiskScheduleConfig& config);
    static core::Result<DiskScheduleResult> run_scan(const DiskScheduleConfig& config);
    static core::Result<DiskScheduleResult> run_cscan(const DiskScheduleConfig& config);
    static core::Result<DiskScheduleResult> run_look(const DiskScheduleConfig& config);
    static core::Result<DiskScheduleResult> run_clook(const DiskScheduleConfig& config);

    // Generic Runner
    static core::Result<DiskScheduleResult> run(DiskAlgorithm algo, const DiskScheduleConfig& config);

    // Built-in Workloads
    static DiskScheduleConfig get_canonical_workload();
    static DiskScheduleConfig get_secondary_workload();

    // Formatters
    static std::string format_trace_table(const DiskScheduleResult& result);
    static std::string format_summary(const DiskScheduleResult& result);
};

} // namespace kernox::sim
