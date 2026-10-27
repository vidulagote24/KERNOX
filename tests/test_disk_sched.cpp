#include "kernox/modules/sim/disk_sched.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

using namespace kernox::sim;
using namespace kernox::core;

// Helper to verify invariants on any valid DiskScheduleResult
static int verify_disk_invariants(const DiskScheduleConfig& config, const DiskScheduleResult& res) {
    // 1. Total requests serviced equals input request count
    KERNOX_ASSERT_EQ(res.requests_serviced, config.requests.size());
    KERNOX_ASSERT_EQ(res.service_order.size(), config.requests.size());

    // 2. Multiset of serviced requests matches multiset of config requests
    std::vector<uint32_t> expected_reqs = config.requests;
    std::vector<uint32_t> actual_reqs = res.service_order;
    std::sort(expected_reqs.begin(), expected_reqs.end());
    std::sort(actual_reqs.begin(), actual_reqs.end());
    KERNOX_ASSERT_EQ(expected_reqs.size(), actual_reqs.size());
    for (size_t i = 0; i < expected_reqs.size(); ++i) {
        KERNOX_ASSERT_EQ(expected_reqs[i], actual_reqs[i]);
    }

    // 3. Movement trace distance consistency & sum equals total_head_movement
    uint64_t calculated_total_movement = 0;
    uint32_t current_pos = config.initial_head;
    for (const auto& step : res.movement_trace) {
        KERNOX_ASSERT_EQ(step.from_cylinder, current_pos);
        uint32_t expected_dist = (step.to_cylinder >= step.from_cylinder)
                                     ? (step.to_cylinder - step.from_cylinder)
                                     : (step.from_cylinder - step.to_cylinder);
        KERNOX_ASSERT_EQ(step.distance, expected_dist);
        calculated_total_movement += step.distance;
        current_pos = step.to_cylinder;
    }
    KERNOX_ASSERT_EQ(calculated_total_movement, res.total_head_movement);
    KERNOX_ASSERT_EQ(current_pos, res.final_head_position);

    // 4. Initial head and cylinder count preserved
    KERNOX_ASSERT_EQ(res.cylinder_count, config.cylinder_count);
    KERNOX_ASSERT_EQ(res.initial_head, config.initial_head);
    KERNOX_ASSERT_TRUE(res.initial_direction == config.direction);

    return 0;
}

static int test_canonical_workloads() {
    std::cout << "Running test_canonical_workloads...\n";
    auto config = DiskScheduler::get_canonical_workload();
    KERNOX_ASSERT_EQ(config.cylinder_count, 200u);
    KERNOX_ASSERT_EQ(config.initial_head, 53u);
    KERNOX_ASSERT_TRUE(config.direction == DiskDirection::Up);

    // A. FCFS
    {
        auto res = DiskScheduler::run_fcfs(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 640ULL);
        std::vector<uint32_t> expected_order = {98, 183, 37, 122, 14, 124, 65, 67};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 67u);
    }

    // B. SSTF
    {
        auto res = DiskScheduler::run_sstf(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 236ULL);
        std::vector<uint32_t> expected_order = {65, 67, 37, 14, 98, 122, 124, 183};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 183u);
    }

    // C. SCAN UP
    {
        auto res = DiskScheduler::run_scan(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 331ULL);
        std::vector<uint32_t> expected_order = {65, 67, 98, 122, 124, 183, 37, 14};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 14u);
    }

    // D. C-SCAN UP
    {
        auto res = DiskScheduler::run_cscan(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 382ULL);
        std::vector<uint32_t> expected_order = {65, 67, 98, 122, 124, 183, 14, 37};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 37u);
    }

    // E. LOOK UP
    {
        auto res = DiskScheduler::run_look(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 299ULL);
        std::vector<uint32_t> expected_order = {65, 67, 98, 122, 124, 183, 37, 14};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 14u);
    }

    // F. C-LOOK UP
    {
        auto res = DiskScheduler::run_clook(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 322ULL);
        std::vector<uint32_t> expected_order = {65, 67, 98, 122, 124, 183, 14, 37};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 37u);
    }

    return 0;
}

static int test_downward_direction() {
    std::cout << "Running test_downward_direction...\n";
    // Canonical requests but initial direction DOWN
    DiskScheduleConfig config{
        .cylinder_count = 200,
        .initial_head = 53,
        .requests = {98, 183, 37, 122, 14, 124, 65, 67},
        .direction = DiskDirection::Down
    };

    // SCAN DOWN: 53 -> 37(16) -> 14(23) -> 0(14) [boundary] -> 65(65) -> 67(2) -> 98(31) -> 122(24) -> 124(2) -> 183(59)
    // Total: 53 -> 0 = 53; 0 -> 183 = 183. Total = 236 cylinders.
    {
        auto res = DiskScheduler::run_scan(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 236ULL);
        std::vector<uint32_t> expected_order = {37, 14, 65, 67, 98, 122, 124, 183};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 183u);
    }

    // C-SCAN DOWN: 53 -> 37(16) -> 14(23) -> 0(14) [boundary] -> 199(199) [wrap] -> 183(16) -> 124(59) -> 122(2) -> 98(24) -> 67(31) -> 65(2)
    // 53 -> 0 = 53; 0 -> 199 = 199; 199 -> 65 = 134. Total = 53 + 199 + 134 = 386 cylinders.
    {
        auto res = DiskScheduler::run_cscan(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 386ULL);
        std::vector<uint32_t> expected_order = {37, 14, 183, 124, 122, 98, 67, 65};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 65u);
    }

    // LOOK DOWN: 53 -> 37(16) -> 14(23) -> 65(51) -> 67(2) -> 98(31) -> 122(24) -> 124(2) -> 183(59)
    // 53 -> 14 = 39; 14 -> 183 = 169. Total = 208 cylinders.
    {
        auto res = DiskScheduler::run_look(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 208ULL);
        std::vector<uint32_t> expected_order = {37, 14, 65, 67, 98, 122, 124, 183};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 183u);
    }

    // C-LOOK DOWN: 53 -> 37(16) -> 14(23) -> 183(169) [wrap to highest] -> 124(59) -> 122(2) -> 98(24) -> 67(31) -> 65(2)
    // 53 -> 14 = 39; 14 -> 183 = 169; 183 -> 65 = 118. Total = 39 + 169 + 118 = 326 cylinders.
    {
        auto res = DiskScheduler::run_clook(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
        KERNOX_ASSERT_EQ(r.total_head_movement, 326ULL);
        std::vector<uint32_t> expected_order = {37, 14, 183, 124, 122, 98, 67, 65};
        KERNOX_ASSERT_TRUE(r.service_order == expected_order);
        KERNOX_ASSERT_EQ(r.final_head_position, 65u);
    }

    return 0;
}

static int test_sstf_tie_breaking() {
    std::cout << "Running test_sstf_tie_breaking...\n";
    // Head at 50. Requests: 40 (index 0, dist 10), 60 (index 1, dist 10).
    // Earliest index 0 (cylinder 40) must be chosen first.
    DiskScheduleConfig config{
        .cylinder_count = 100,
        .initial_head = 50,
        .requests = {40, 60},
        .direction = DiskDirection::Up
    };

    auto res = DiskScheduler::run_sstf(config);
    KERNOX_ASSERT_TRUE(res.is_ok());
    const auto& r = res.value();
    KERNOX_ASSERT_EQ(verify_disk_invariants(config, r), 0);
    std::vector<uint32_t> expected_order = {40, 60};
    KERNOX_ASSERT_TRUE(r.service_order == expected_order);
    // 50 -> 40 = 10; 40 -> 60 = 20. Total = 30
    KERNOX_ASSERT_EQ(r.total_head_movement, 30ULL);

    // Reversed input order: 60 (index 0), 40 (index 1)
    config.requests = {60, 40};
    auto res2 = DiskScheduler::run_sstf(config);
    KERNOX_ASSERT_TRUE(res2.is_ok());
    const auto& r2 = res2.value();
    KERNOX_ASSERT_EQ(verify_disk_invariants(config, r2), 0);
    std::vector<uint32_t> expected_order2 = {60, 40};
    KERNOX_ASSERT_TRUE(r2.service_order == expected_order2);
    KERNOX_ASSERT_EQ(r2.total_head_movement, 30ULL);

    return 0;
}

static int test_edge_cases() {
    std::cout << "Running test_edge_cases...\n";

    // 1. Head already at requested cylinder
    {
        DiskScheduleConfig config{
            .cylinder_count = 200,
            .initial_head = 50,
            .requests = {50, 60, 40},
            .direction = DiskDirection::Up
        };
        auto res = DiskScheduler::run_fcfs(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, res.value()), 0);
        // 50 -> 50 (dist 0), 50 -> 60 (dist 10), 60 -> 40 (dist 20) = 30
        KERNOX_ASSERT_EQ(res.value().total_head_movement, 30ULL);
    }

    // 2. Duplicate requests
    {
        DiskScheduleConfig config{
            .cylinder_count = 100,
            .initial_head = 20,
            .requests = {30, 30, 10, 30},
            .direction = DiskDirection::Up
        };
        auto res = DiskScheduler::run_sstf(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, res.value()), 0);
        KERNOX_ASSERT_EQ(res.value().requests_serviced, 4u);
    }

    // 3. All requests above initial head
    {
        DiskScheduleConfig config{
            .cylinder_count = 200,
            .initial_head = 10,
            .requests = {30, 50, 70},
            .direction = DiskDirection::Up
        };
        // SCAN UP does not need to hit 199 if no lower requests exist
        auto res = DiskScheduler::run_scan(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, res.value()), 0);
        // 10 -> 30(20) -> 50(20) -> 70(20) = 60
        KERNOX_ASSERT_EQ(res.value().total_head_movement, 60ULL);
        KERNOX_ASSERT_EQ(res.value().final_head_position, 70u);
    }

    // 4. All requests below initial head
    {
        DiskScheduleConfig config{
            .cylinder_count = 200,
            .initial_head = 90,
            .requests = {70, 50, 30},
            .direction = DiskDirection::Down
        };
        auto res = DiskScheduler::run_scan(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, res.value()), 0);
        // 90 -> 70(20) -> 50(20) -> 30(20) = 60
        KERNOX_ASSERT_EQ(res.value().total_head_movement, 60ULL);
        KERNOX_ASSERT_EQ(res.value().final_head_position, 30u);
    }

    // 5. Requests at cylinders 0 and 199 (boundaries)
    {
        DiskScheduleConfig config{
            .cylinder_count = 200,
            .initial_head = 100,
            .requests = {0, 199},
            .direction = DiskDirection::Up
        };
        auto res = DiskScheduler::run_scan(config);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(verify_disk_invariants(config, res.value()), 0);
        // 100 -> 199 (99) -> 0 (199) = 298
        KERNOX_ASSERT_EQ(res.value().total_head_movement, 298ULL);
    }

    // 6. Single request
    {
        DiskScheduleConfig config{
            .cylinder_count = 100,
            .initial_head = 25,
            .requests = {80},
            .direction = DiskDirection::Up
        };
        for (auto algo : {DiskAlgorithm::FCFS, DiskAlgorithm::SSTF, DiskAlgorithm::SCAN,
                          DiskAlgorithm::CSCAN, DiskAlgorithm::LOOK, DiskAlgorithm::CLOOK}) {
            auto res = DiskScheduler::run(algo, config);
            KERNOX_ASSERT_TRUE(res.is_ok());
            KERNOX_ASSERT_EQ(verify_disk_invariants(config, res.value()), 0);
            KERNOX_ASSERT_EQ(res.value().total_head_movement, 55ULL);
            KERNOX_ASSERT_EQ(res.value().final_head_position, 80u);
        }
    }

    return 0;
}

static int test_invalid_configurations() {
    std::cout << "Running test_invalid_configurations...\n";

    // 1. Cylinder count = 0
    {
        DiskScheduleConfig cfg{
            .cylinder_count = 0,
            .initial_head = 0,
            .requests = {10},
            .direction = DiskDirection::Up
        };
        auto res = DiskScheduler::validate_config(cfg);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_TRUE(res.error().code == ErrorCode::InvalidArgument);
    }

    // 2. Initial head >= cylinder_count
    {
        DiskScheduleConfig cfg{
            .cylinder_count = 100,
            .initial_head = 100,
            .requests = {10},
            .direction = DiskDirection::Up
        };
        auto res = DiskScheduler::validate_config(cfg);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_TRUE(res.error().code == ErrorCode::InvalidArgument);
    }

    // 3. Empty requests
    {
        DiskScheduleConfig cfg{
            .cylinder_count = 100,
            .initial_head = 50,
            .requests = {},
            .direction = DiskDirection::Up
        };
        auto res = DiskScheduler::validate_config(cfg);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_TRUE(res.error().code == ErrorCode::InvalidArgument);
    }

    // 4. Request >= cylinder_count
    {
        DiskScheduleConfig cfg{
            .cylinder_count = 200,
            .initial_head = 50,
            .requests = {10, 200, 30},
            .direction = DiskDirection::Up
        };
        auto res = DiskScheduler::validate_config(cfg);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_TRUE(res.error().code == ErrorCode::InvalidArgument);
    }

    // 5. Enum string parsers
    {
        KERNOX_ASSERT_TRUE(parse_direction("up").has_value());
        KERNOX_ASSERT_TRUE(parse_direction("DOWN").has_value());
        KERNOX_ASSERT_FALSE(parse_direction("sideways").has_value());

        KERNOX_ASSERT_TRUE(parse_disk_algorithm("fcfs").has_value());
        KERNOX_ASSERT_TRUE(parse_disk_algorithm("sstf").has_value());
        KERNOX_ASSERT_TRUE(parse_disk_algorithm("scan").has_value());
        KERNOX_ASSERT_TRUE(parse_disk_algorithm("cscan").has_value());
        KERNOX_ASSERT_TRUE(parse_disk_algorithm("look").has_value());
        KERNOX_ASSERT_TRUE(parse_disk_algorithm("clook").has_value());
        KERNOX_ASSERT_FALSE(parse_disk_algorithm("magic").has_value());
    }

    return 0;
}

static int test_formatters() {
    std::cout << "Running test_formatters...\n";
    auto config = DiskScheduler::get_canonical_workload();
    auto res = DiskScheduler::run_sstf(config);
    KERNOX_ASSERT_TRUE(res.is_ok());

    std::string table = DiskScheduler::format_trace_table(res.value());
    KERNOX_ASSERT_TRUE(!table.empty());
    KERNOX_ASSERT_TRUE(table.find("Step") != std::string::npos);
    KERNOX_ASSERT_TRUE(table.find("Distance") != std::string::npos);

    std::string summary = DiskScheduler::format_summary(res.value());
    KERNOX_ASSERT_TRUE(!summary.empty());
    KERNOX_ASSERT_TRUE(summary.find("Total Head Movement") != std::string::npos);
    KERNOX_ASSERT_TRUE(summary.find("236 cylinders") != std::string::npos);

    return 0;
}

int main() {
    std::cout << "=== KERNOX Disk Scheduling Simulation Engine Tests ===\n";

    if (test_canonical_workloads() != 0) return 1;
    if (test_downward_direction() != 0) return 1;
    if (test_sstf_tie_breaking() != 0) return 1;
    if (test_edge_cases() != 0) return 1;
    if (test_invalid_configurations() != 0) return 1;
    if (test_formatters() != 0) return 1;

    std::cout << "=== All Disk Scheduling Tests Passed Successfully ===\n";
    return 0;
}
