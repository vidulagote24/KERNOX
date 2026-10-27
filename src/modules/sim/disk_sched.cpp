#include "kernox/modules/sim/disk_sched.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace kernox::sim {

std::string_view to_string(DiskDirection dir) noexcept {
    switch (dir) {
        case DiskDirection::Up:
            return "UP (Increasing / Outward)";
        case DiskDirection::Down:
            return "DOWN (Decreasing / Inward)";
    }
    return "Unknown";
}

std::optional<DiskDirection> parse_direction(std::string_view str) noexcept {
    if (str == "up" || str == "UP" || str == "Up") return DiskDirection::Up;
    if (str == "down" || str == "DOWN" || str == "Down") return DiskDirection::Down;
    return std::nullopt;
}

std::string_view to_string(DiskAlgorithm algo) noexcept {
    switch (algo) {
        case DiskAlgorithm::FCFS:
            return "First-Come First-Served (FCFS)";
        case DiskAlgorithm::SSTF:
            return "Shortest Seek Time First (SSTF)";
        case DiskAlgorithm::SCAN:
            return "SCAN (Elevator Algorithm)";
        case DiskAlgorithm::CSCAN:
            return "C-SCAN (Circular SCAN)";
        case DiskAlgorithm::LOOK:
            return "LOOK";
        case DiskAlgorithm::CLOOK:
            return "C-LOOK (Circular LOOK)";
    }
    return "Unknown";
}

std::optional<DiskAlgorithm> parse_disk_algorithm(std::string_view str) noexcept {
    if (str == "fcfs" || str == "FCFS") return DiskAlgorithm::FCFS;
    if (str == "sstf" || str == "SSTF") return DiskAlgorithm::SSTF;
    if (str == "scan" || str == "SCAN") return DiskAlgorithm::SCAN;
    if (str == "cscan" || str == "CSCAN" || str == "c-scan" || str == "C-SCAN") return DiskAlgorithm::CSCAN;
    if (str == "look" || str == "LOOK") return DiskAlgorithm::LOOK;
    if (str == "clook" || str == "CLOOK" || str == "c-look" || str == "C-LOOK") return DiskAlgorithm::CLOOK;
    return std::nullopt;
}

std::string_view to_string(MovementType type) noexcept {
    switch (type) {
        case MovementType::Service:
            return "SERVICE";
        case MovementType::Boundary:
            return "BOUNDARY";
        case MovementType::Wrap:
            return "WRAP";
    }
    return "UNKNOWN";
}

DiskScheduleConfig DiskScheduler::get_canonical_workload() {
    return DiskScheduleConfig{
        .cylinder_count = 200,
        .initial_head = 53,
        .requests = {98, 183, 37, 122, 14, 124, 65, 67},
        .direction = DiskDirection::Up
    };
}

DiskScheduleConfig DiskScheduler::get_secondary_workload() {
    return DiskScheduleConfig{
        .cylinder_count = 200,
        .initial_head = 100,
        .requests = {55, 58, 39, 18, 90, 160, 150, 38, 184},
        .direction = DiskDirection::Down
    };
}

core::Result<void> DiskScheduler::validate_config(const DiskScheduleConfig& config) {
    if (config.cylinder_count == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Disk cylinder count must be strictly positive (> 0)");
    }
    if (config.initial_head >= config.cylinder_count) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Initial head position (" + std::to_string(config.initial_head) +
            ") exceeds maximum cylinder index (" + std::to_string(config.cylinder_count - 1) + ")");
    }
    if (config.requests.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Disk request queue cannot be empty");
    }
    for (size_t i = 0; i < config.requests.size(); ++i) {
        uint32_t req = config.requests[i];
        if (req >= config.cylinder_count) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Request at index " + std::to_string(i) + " (" + std::to_string(req) +
                ") exceeds maximum cylinder index (" + std::to_string(config.cylinder_count - 1) + ")");
        }
    }
    return core::Result<void>::ok();
}

static void add_step(
    std::vector<DiskMovementStep>& trace,
    uint64_t& total_movement,
    uint32_t from,
    uint32_t to,
    MovementType type,
    std::optional<uint32_t> serviced) {
    uint32_t dist = (to >= from) ? (to - from) : (from - to);
    total_movement += dist;
    trace.push_back(DiskMovementStep{
        .from_cylinder = from,
        .to_cylinder = to,
        .distance = dist,
        .type = type,
        .serviced_request = serviced
    });
}

struct PendingRequest {
    size_t index{0};
    uint32_t cylinder{0};
    bool serviced{false};
};

// ----------------------------------------------------------------------------
// A. First-Come First-Served (FCFS)
// ----------------------------------------------------------------------------
core::Result<DiskScheduleResult> DiskScheduler::run_fcfs(const DiskScheduleConfig& config) {
    auto val_res = validate_config(config);
    if (val_res.is_err()) return core::Result<DiskScheduleResult>::err(val_res.error());

    DiskScheduleResult res{};
    res.algorithm_name = "First-Come First-Served (FCFS)";
    res.cylinder_count = config.cylinder_count;
    res.initial_head = config.initial_head;
    res.initial_direction = config.direction;
    res.policy_notes = "Services cylinder requests strictly in arrival order.";

    uint32_t current_head = config.initial_head;
    for (uint32_t req : config.requests) {
        add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
        res.service_order.push_back(req);
        current_head = req;
    }

    res.requests_serviced = res.service_order.size();
    res.final_head_position = current_head;
    return core::Result<DiskScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// B. Shortest Seek Time First (SSTF)
// ----------------------------------------------------------------------------
core::Result<DiskScheduleResult> DiskScheduler::run_sstf(const DiskScheduleConfig& config) {
    auto val_res = validate_config(config);
    if (val_res.is_err()) return core::Result<DiskScheduleResult>::err(val_res.error());

    DiskScheduleResult res{};
    res.algorithm_name = "Shortest Seek Time First (SSTF)";
    res.cylinder_count = config.cylinder_count;
    res.initial_head = config.initial_head;
    res.initial_direction = config.direction;
    res.policy_notes = "Greedily chooses request with smallest seek distance; tie-break by earlier original index.";

    std::vector<PendingRequest> pending;
    pending.reserve(config.requests.size());
    for (size_t i = 0; i < config.requests.size(); ++i) {
        pending.push_back(PendingRequest{.index = i, .cylinder = config.requests[i], .serviced = false});
    }

    uint32_t current_head = config.initial_head;
    size_t remaining = pending.size();

    while (remaining > 0) {
        size_t best_idx = pending.size();
        uint32_t best_dist = UINT32_MAX;

        for (size_t i = 0; i < pending.size(); ++i) {
            if (!pending[i].serviced) {
                uint32_t dist = (pending[i].cylinder >= current_head)
                                    ? (pending[i].cylinder - current_head)
                                    : (current_head - pending[i].cylinder);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = i;
                }
            }
        }

        auto& target = pending[best_idx];
        add_step(res.movement_trace, res.total_head_movement, current_head, target.cylinder, MovementType::Service, target.cylinder);
        res.service_order.push_back(target.cylinder);
        current_head = target.cylinder;
        target.serviced = true;
        remaining--;
    }

    res.requests_serviced = res.service_order.size();
    res.final_head_position = current_head;
    return core::Result<DiskScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// C. SCAN (Elevator Algorithm)
// ----------------------------------------------------------------------------
core::Result<DiskScheduleResult> DiskScheduler::run_scan(const DiskScheduleConfig& config) {
    auto val_res = validate_config(config);
    if (val_res.is_err()) return core::Result<DiskScheduleResult>::err(val_res.error());

    DiskScheduleResult res{};
    res.algorithm_name = "SCAN (Elevator Algorithm)";
    res.cylinder_count = config.cylinder_count;
    res.initial_head = config.initial_head;
    res.initial_direction = config.direction;
    res.policy_notes = "Travels in initial direction servicing requests; hits physical boundary before reversing.";

    std::vector<uint32_t> upper;
    std::vector<uint32_t> lower;
    for (uint32_t req : config.requests) {
        if (req >= config.initial_head) {
            upper.push_back(req);
        } else {
            lower.push_back(req);
        }
    }

    std::sort(upper.begin(), upper.end()); // Ascending
    std::sort(lower.begin(), lower.end()); // Ascending

    uint32_t current_head = config.initial_head;
    uint32_t max_cylinder = config.cylinder_count - 1;

    if (config.direction == DiskDirection::Up) {
        // Move UP servicing upper requests
        for (uint32_t req : upper) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        if (!lower.empty()) {
            // Must hit upper physical boundary before reversing
            if (current_head != max_cylinder) {
                add_step(res.movement_trace, res.total_head_movement, current_head, max_cylinder, MovementType::Boundary, std::nullopt);
                current_head = max_cylinder;
            }
            // Move DOWN servicing lower requests in descending order
            for (auto it = lower.rbegin(); it != lower.rend(); ++it) {
                add_step(res.movement_trace, res.total_head_movement, current_head, *it, MovementType::Service, *it);
                res.service_order.push_back(*it);
                current_head = *it;
            }
        }
    } else {
        // Move DOWN servicing lower requests in descending order
        std::vector<uint32_t> lower_at_head;
        std::vector<uint32_t> strictly_lower;
        for (uint32_t req : config.requests) {
            if (req <= config.initial_head) {
                strictly_lower.push_back(req);
            } else {
                lower_at_head.push_back(req); // strictly upper
            }
        }
        std::sort(strictly_lower.begin(), strictly_lower.end(), std::greater<uint32_t>());
        std::sort(lower_at_head.begin(), lower_at_head.end());

        for (uint32_t req : strictly_lower) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        if (!lower_at_head.empty()) {
            if (current_head != 0) {
                add_step(res.movement_trace, res.total_head_movement, current_head, 0, MovementType::Boundary, std::nullopt);
                current_head = 0;
            }
            for (uint32_t req : lower_at_head) {
                add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
                res.service_order.push_back(req);
                current_head = req;
            }
        }
    }

    res.requests_serviced = res.service_order.size();
    res.final_head_position = current_head;
    return core::Result<DiskScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// D. C-SCAN (Circular SCAN)
// ----------------------------------------------------------------------------
core::Result<DiskScheduleResult> DiskScheduler::run_cscan(const DiskScheduleConfig& config) {
    auto val_res = validate_config(config);
    if (val_res.is_err()) return core::Result<DiskScheduleResult>::err(val_res.error());

    DiskScheduleResult res{};
    res.algorithm_name = "C-SCAN (Circular SCAN)";
    res.cylinder_count = config.cylinder_count;
    res.initial_head = config.initial_head;
    res.initial_direction = config.direction;
    res.policy_notes = "Unidirectional servicing; traverses to boundary, wraps to opposite boundary with movement counted.";

    uint32_t current_head = config.initial_head;
    uint32_t max_cylinder = config.cylinder_count - 1;

    if (config.direction == DiskDirection::Up) {
        std::vector<uint32_t> upper;
        std::vector<uint32_t> lower;
        for (uint32_t req : config.requests) {
            if (req >= config.initial_head) {
                upper.push_back(req);
            } else {
                lower.push_back(req);
            }
        }
        std::sort(upper.begin(), upper.end());
        std::sort(lower.begin(), lower.end());

        for (uint32_t req : upper) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        if (!lower.empty()) {
            if (current_head != max_cylinder) {
                add_step(res.movement_trace, res.total_head_movement, current_head, max_cylinder, MovementType::Boundary, std::nullopt);
                current_head = max_cylinder;
            }
            // Wraparound from max_cylinder to 0
            add_step(res.movement_trace, res.total_head_movement, current_head, 0, MovementType::Wrap, std::nullopt);
            current_head = 0;

            for (uint32_t req : lower) {
                add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
                res.service_order.push_back(req);
                current_head = req;
            }
        }
    } else {
        std::vector<uint32_t> lower;
        std::vector<uint32_t> upper;
        for (uint32_t req : config.requests) {
            if (req <= config.initial_head) {
                lower.push_back(req);
            } else {
                upper.push_back(req);
            }
        }
        std::sort(lower.begin(), lower.end(), std::greater<uint32_t>());
        std::sort(upper.begin(), upper.end(), std::greater<uint32_t>());

        for (uint32_t req : lower) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        if (!upper.empty()) {
            if (current_head != 0) {
                add_step(res.movement_trace, res.total_head_movement, current_head, 0, MovementType::Boundary, std::nullopt);
                current_head = 0;
            }
            // Wraparound from 0 to max_cylinder
            add_step(res.movement_trace, res.total_head_movement, current_head, max_cylinder, MovementType::Wrap, std::nullopt);
            current_head = max_cylinder;

            for (uint32_t req : upper) {
                add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
                res.service_order.push_back(req);
                current_head = req;
            }
        }
    }

    res.requests_serviced = res.service_order.size();
    res.final_head_position = current_head;
    return core::Result<DiskScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// E. LOOK
// ----------------------------------------------------------------------------
core::Result<DiskScheduleResult> DiskScheduler::run_look(const DiskScheduleConfig& config) {
    auto val_res = validate_config(config);
    if (val_res.is_err()) return core::Result<DiskScheduleResult>::err(val_res.error());

    DiskScheduleResult res{};
    res.algorithm_name = "LOOK";
    res.cylinder_count = config.cylinder_count;
    res.initial_head = config.initial_head;
    res.initial_direction = config.direction;
    res.policy_notes = "Travels in initial direction servicing requests; reverses at furthest pending request without hitting boundary.";

    uint32_t current_head = config.initial_head;

    if (config.direction == DiskDirection::Up) {
        std::vector<uint32_t> upper;
        std::vector<uint32_t> lower;
        for (uint32_t req : config.requests) {
            if (req >= config.initial_head) {
                upper.push_back(req);
            } else {
                lower.push_back(req);
            }
        }
        std::sort(upper.begin(), upper.end());
        std::sort(lower.begin(), lower.end(), std::greater<uint32_t>());

        for (uint32_t req : upper) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        for (uint32_t req : lower) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }
    } else {
        std::vector<uint32_t> lower;
        std::vector<uint32_t> upper;
        for (uint32_t req : config.requests) {
            if (req <= config.initial_head) {
                lower.push_back(req);
            } else {
                upper.push_back(req);
            }
        }
        std::sort(lower.begin(), lower.end(), std::greater<uint32_t>());
        std::sort(upper.begin(), upper.end());

        for (uint32_t req : lower) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        for (uint32_t req : upper) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }
    }

    res.requests_serviced = res.service_order.size();
    res.final_head_position = current_head;
    return core::Result<DiskScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// F. C-LOOK (Circular LOOK)
// ----------------------------------------------------------------------------
core::Result<DiskScheduleResult> DiskScheduler::run_clook(const DiskScheduleConfig& config) {
    auto val_res = validate_config(config);
    if (val_res.is_err()) return core::Result<DiskScheduleResult>::err(val_res.error());

    DiskScheduleResult res{};
    res.algorithm_name = "C-LOOK (Circular LOOK)";
    res.cylinder_count = config.cylinder_count;
    res.initial_head = config.initial_head;
    res.initial_direction = config.direction;
    res.policy_notes = "Unidirectional servicing; wraps directly to lowest/highest requested cylinder with wraparound counted.";

    uint32_t current_head = config.initial_head;

    if (config.direction == DiskDirection::Up) {
        std::vector<uint32_t> upper;
        std::vector<uint32_t> lower;
        for (uint32_t req : config.requests) {
            if (req >= config.initial_head) {
                upper.push_back(req);
            } else {
                lower.push_back(req);
            }
        }
        std::sort(upper.begin(), upper.end());
        std::sort(lower.begin(), lower.end());

        for (uint32_t req : upper) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        if (!lower.empty()) {
            // Wrap directly to lowest pending cylinder in lower
            uint32_t lowest_req = lower.front();
            add_step(res.movement_trace, res.total_head_movement, current_head, lowest_req, MovementType::Wrap, lowest_req);
            res.service_order.push_back(lowest_req);
            current_head = lowest_req;

            // Service remaining lower requests
            for (size_t i = 1; i < lower.size(); ++i) {
                add_step(res.movement_trace, res.total_head_movement, current_head, lower[i], MovementType::Service, lower[i]);
                res.service_order.push_back(lower[i]);
                current_head = lower[i];
            }
        }
    } else {
        std::vector<uint32_t> lower;
        std::vector<uint32_t> upper;
        for (uint32_t req : config.requests) {
            if (req <= config.initial_head) {
                lower.push_back(req);
            } else {
                upper.push_back(req);
            }
        }
        std::sort(lower.begin(), lower.end(), std::greater<uint32_t>());
        std::sort(upper.begin(), upper.end(), std::greater<uint32_t>());

        for (uint32_t req : lower) {
            add_step(res.movement_trace, res.total_head_movement, current_head, req, MovementType::Service, req);
            res.service_order.push_back(req);
            current_head = req;
        }

        if (!upper.empty()) {
            // Wrap directly to highest pending cylinder in upper
            uint32_t highest_req = upper.front();
            add_step(res.movement_trace, res.total_head_movement, current_head, highest_req, MovementType::Wrap, highest_req);
            res.service_order.push_back(highest_req);
            current_head = highest_req;

            for (size_t i = 1; i < upper.size(); ++i) {
                add_step(res.movement_trace, res.total_head_movement, current_head, upper[i], MovementType::Service, upper[i]);
                res.service_order.push_back(upper[i]);
                current_head = upper[i];
            }
        }
    }

    res.requests_serviced = res.service_order.size();
    res.final_head_position = current_head;
    return core::Result<DiskScheduleResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// Generic Runner
// ----------------------------------------------------------------------------
core::Result<DiskScheduleResult> DiskScheduler::run(DiskAlgorithm algo, const DiskScheduleConfig& config) {
    switch (algo) {
        case DiskAlgorithm::FCFS:  return run_fcfs(config);
        case DiskAlgorithm::SSTF:  return run_sstf(config);
        case DiskAlgorithm::SCAN:  return run_scan(config);
        case DiskAlgorithm::CSCAN: return run_cscan(config);
        case DiskAlgorithm::LOOK:  return run_look(config);
        case DiskAlgorithm::CLOOK: return run_clook(config);
    }
    return core::Result<DiskScheduleResult>::err(
        core::ErrorCode::InvalidArgument, "Unsupported disk scheduling algorithm");
}

// ----------------------------------------------------------------------------
// Formatters
// ----------------------------------------------------------------------------
std::string DiskScheduler::format_trace_table(const DiskScheduleResult& result) {
    std::ostringstream oss;
    oss << "+------+------+----+----------+----------+----------------+\n"
        << "| Step | From | To | Distance | Type     | Serviced Req   |\n"
        << "+------+------+----+----------+----------+----------------+\n";

    for (size_t i = 0; i < result.movement_trace.size(); ++i) {
        const auto& step = result.movement_trace[i];
        std::string serv = step.serviced_request.has_value() ? std::to_string(*step.serviced_request) : "-";
        oss << "| " << std::setw(4) << (i + 1) << " | "
            << std::setw(4) << step.from_cylinder << " | "
            << std::setw(2) << step.to_cylinder << " | "
            << std::setw(8) << step.distance << " | "
            << std::setw(8) << to_string(step.type) << " | "
            << std::setw(14) << serv << " |\n";
    }
    oss << "+------+------+----+----------+----------+----------------+\n";
    return oss.str();
}

std::string DiskScheduler::format_summary(const DiskScheduleResult& result) {
    std::ostringstream oss;
    oss << "Disk Scheduling Performance Summary:\n"
        << "  Algorithm                  : " << result.algorithm_name << "\n"
        << "  Total Disk Cylinders       : " << result.cylinder_count << " (range 0-" << (result.cylinder_count - 1) << ")\n"
        << "  Initial Head Position      : " << result.initial_head << "\n"
        << "  Initial Head Direction     : " << to_string(result.initial_direction) << "\n"
        << "  Total Head Movement (THM)  : " << result.total_head_movement << " cylinders\n"
        << "  Requests Serviced          : " << result.requests_serviced << "\n"
        << "  Final Head Position        : " << result.final_head_position << "\n";

    if (!result.service_order.empty()) {
        oss << "  Service Order Sequence     : ";
        for (size_t i = 0; i < result.service_order.size(); ++i) {
            oss << result.service_order[i] << (i + 1 < result.service_order.size() ? " -> " : "");
        }
        oss << "\n";
    }

    if (result.requests_serviced > 0) {
        double avg_seek = static_cast<double>(result.total_head_movement) / static_cast<double>(result.requests_serviced);
        oss << std::fixed << std::setprecision(2);
        oss << "  Average Seek per Request   : " << avg_seek << " cylinders\n";
    }
    return oss.str();
}

} // namespace kernox::sim
