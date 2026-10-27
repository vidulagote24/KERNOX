#include "kernox/modules/sim/deadlock.hpp"
#include "test_harness.hpp"

#include <iostream>
#include <vector>

using namespace kernox::sim;
using namespace kernox::core;

static int test_banker_safety_canonical() {
    std::cout << "Running test_banker_safety_canonical...\n";
    auto state = DeadlockEngine::get_banker_safe_workload();

    auto safety_res = DeadlockEngine::run_banker_safety(state);
    KERNOX_ASSERT_TRUE(safety_res.is_ok());
    const auto& s = safety_res.value();

    KERNOX_ASSERT_TRUE(s.is_safe);
    KERNOX_ASSERT_EQ(s.safe_sequence.size(), 5u);

    // Expected deterministic sequence: P1, P3, P0, P2, P4
    std::vector<size_t> expected_seq = {1, 3, 0, 2, 4};
    KERNOX_ASSERT_TRUE(s.safe_sequence == expected_seq);

    // Final work vector must equal total resources: A=10, B=5, C=7
    ResourceVector expected_final_work = {10, 5, 7};
    KERNOX_ASSERT_TRUE(s.final_work == expected_final_work);
    KERNOX_ASSERT_TRUE(s.unfinished_processes.empty());

    return 0;
}

static int test_banker_safety_unsafe() {
    std::cout << "Running test_banker_safety_unsafe...\n";
    auto state = DeadlockEngine::get_banker_unsafe_workload();

    auto safety_res = DeadlockEngine::run_banker_safety(state);
    KERNOX_ASSERT_TRUE(safety_res.is_ok());
    const auto& s = safety_res.value();

    KERNOX_ASSERT_FALSE(s.is_safe);
    KERNOX_ASSERT_TRUE(s.safe_sequence.empty());
    KERNOX_ASSERT_EQ(s.unfinished_processes.size(), 5u);

    std::vector<size_t> expected_unfinished = {0, 1, 2, 3, 4};
    KERNOX_ASSERT_TRUE(s.unfinished_processes == expected_unfinished);
    ResourceVector expected_final_work = {0, 0, 0};
    KERNOX_ASSERT_TRUE(s.final_work == expected_final_work);

    return 0;
}

static int test_banker_resource_requests() {
    std::cout << "Running test_banker_resource_requests...\n";
    auto state = DeadlockEngine::get_banker_safe_workload();

    // 1. Valid grantable request: P1 requests (1, 0, 2)
    {
        ResourceVector req = {1, 0, 2};
        auto res = DeadlockEngine::process_resource_request(state, 1, req);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_TRUE(r.status == RequestStatus::Granted);
        KERNOX_ASSERT_TRUE(r.tentative_safety.has_value());
        KERNOX_ASSERT_TRUE(r.tentative_safety->is_safe);

        // Resulting state has been committed
        ResourceVector expected_avail = {2, 3, 0};
        KERNOX_ASSERT_TRUE(r.resulting_state.available == expected_avail);
        KERNOX_ASSERT_EQ(r.resulting_state.allocation[1][0], 3u);
        KERNOX_ASSERT_EQ(r.resulting_state.allocation[1][1], 0u);
        KERNOX_ASSERT_EQ(r.resulting_state.allocation[1][2], 2u);
        KERNOX_ASSERT_EQ(r.resulting_state.need[1][0], 0u);
        KERNOX_ASSERT_EQ(r.resulting_state.need[1][1], 2u);
        KERNOX_ASSERT_EQ(r.resulting_state.need[1][2], 0u);
    }

    // 2. Request exceeds claim: P0 requests (8, 0, 0) when need is (7, 4, 3)
    {
        ResourceVector req = {8, 0, 0};
        auto res = DeadlockEngine::process_resource_request(state, 0, req);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_TRUE(r.status == RequestStatus::ExceededClaim);
        // Original state must be preserved unchanged
        KERNOX_ASSERT_TRUE(r.resulting_state.available == state.available);
        KERNOX_ASSERT_TRUE(r.resulting_state.allocation == state.allocation);
    }

    // 3. Request exceeds available resources: P0 requests (0, 4, 0) when available is (3, 3, 2)
    {
        ResourceVector req = {0, 4, 0};
        auto res = DeadlockEngine::process_resource_request(state, 0, req);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_TRUE(r.status == RequestStatus::InsufficientAvailable);
        // Original state must be preserved unchanged
        KERNOX_ASSERT_TRUE(r.resulting_state.available == state.available);
        KERNOX_ASSERT_TRUE(r.resulting_state.allocation == state.allocation);
    }

    // 4. Request leaves state UNSAFE: P4 requests (3, 3, 0)
    // Available was (3, 3, 2). If granted, available becomes (0, 0, 2), causing all processes to stall!
    {
        ResourceVector req = {3, 3, 0};
        auto res = DeadlockEngine::process_resource_request(state, 4, req);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_TRUE(r.status == RequestStatus::UnsafeState);
        KERNOX_ASSERT_TRUE(r.tentative_safety.has_value());
        KERNOX_ASSERT_FALSE(r.tentative_safety->is_safe);

        // Transactional guarantee: original state must be completely unmodified
        KERNOX_ASSERT_TRUE(r.resulting_state.available == state.available);
        KERNOX_ASSERT_TRUE(r.resulting_state.allocation == state.allocation);
        KERNOX_ASSERT_TRUE(r.resulting_state.need == state.need);
    }

    // 5. Invalid arguments: process out of range or wrong vector dimension
    {
        ResourceVector req = {1, 0, 0};
        auto res_bad_pid = DeadlockEngine::process_resource_request(state, 99, req);
        KERNOX_ASSERT_TRUE(res_bad_pid.is_err());
        KERNOX_ASSERT_TRUE(res_bad_pid.error().code == ErrorCode::InvalidArgument);

        ResourceVector bad_dim_req = {1, 0};
        auto res_bad_dim = DeadlockEngine::process_resource_request(state, 0, bad_dim_req);
        KERNOX_ASSERT_TRUE(res_bad_dim.is_err());
        KERNOX_ASSERT_TRUE(res_bad_dim.error().code == ErrorCode::InvalidArgument);
    }

    return 0;
}

static int test_deadlock_detection() {
    std::cout << "Running test_deadlock_detection...\n";

    // 1. Detection with genuine deadlock
    {
        auto state = DeadlockEngine::get_detection_deadlock_workload();
        auto res = DeadlockEngine::run_deadlock_detection(state);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& d = res.value();

        KERNOX_ASSERT_TRUE(d.is_deadlocked);
        // P0 has no requests and finishes releasing (0, 1, 0) -> Work becomes (0, 1, 0)
        // P1, P2, P3, P4 cannot finish and are deadlocked
        KERNOX_ASSERT_EQ(d.deadlocked_processes.size(), 4u);
        std::vector<size_t> expected_deadlocked = {1, 2, 3, 4};
        KERNOX_ASSERT_TRUE(d.deadlocked_processes == expected_deadlocked);
        ResourceVector expected_work = {0, 1, 0};
        KERNOX_ASSERT_TRUE(d.final_work == expected_work);
    }

    // 2. Detection with NO deadlock
    {
        auto state = DeadlockEngine::get_detection_free_workload();
        auto res = DeadlockEngine::run_deadlock_detection(state);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& d = res.value();

        KERNOX_ASSERT_FALSE(d.is_deadlocked);
        KERNOX_ASSERT_TRUE(d.deadlocked_processes.empty());
        // All processes finish, releasing all allocated resources
        ResourceVector expected_work = {7, 2, 6};
        KERNOX_ASSERT_TRUE(d.final_work == expected_work);
    }

    return 0;
}

static int test_resource_allocation_graph() {
    std::cout << "Running test_resource_allocation_graph...\n";

    // 1. Single-instance graph with cycle: Definite Deadlock
    {
        auto rag = DeadlockEngine::get_rag_single_cycle_workload();
        KERNOX_ASSERT_TRUE(rag.is_single_instance_only());
        auto cycle = rag.find_cycle();
        KERNOX_ASSERT_TRUE(cycle.has_cycle);
        KERNOX_ASSERT_TRUE(cycle.is_single_instance_only);
        KERNOX_ASSERT_TRUE(cycle.interpretation.find("Definite Deadlock") != std::string::npos);

        // Verify cycle path starts and ends at same node
        KERNOX_ASSERT_TRUE(!cycle.cycle_path.empty());
        KERNOX_ASSERT_TRUE(cycle.cycle_path.front() == cycle.cycle_path.back());
    }

    // 2. Multi-instance graph with cycle: Warning only (NOT definite deadlock)
    {
        auto rag = DeadlockEngine::get_rag_multi_cycle_workload();
        KERNOX_ASSERT_FALSE(rag.is_single_instance_only());
        auto cycle = rag.find_cycle();
        KERNOX_ASSERT_TRUE(cycle.has_cycle);
        KERNOX_ASSERT_FALSE(cycle.is_single_instance_only);
        KERNOX_ASSERT_TRUE(cycle.interpretation.find("Deadlock Warning") != std::string::npos);
        KERNOX_ASSERT_TRUE(cycle.interpretation.find("NOT sufficient") != std::string::npos);
    }

    // 3. Graph with NO cycle
    {
        auto rag = DeadlockEngine::get_rag_no_cycle_workload();
        auto cycle = rag.find_cycle();
        KERNOX_ASSERT_FALSE(cycle.has_cycle);
        KERNOX_ASSERT_TRUE(cycle.cycle_path.empty());
        KERNOX_ASSERT_TRUE(cycle.interpretation.find("No Deadlock") != std::string::npos);
    }

    // 4. Edge removal breaks cycle
    {
        auto rag = DeadlockEngine::get_rag_single_cycle_workload();
        KERNOX_ASSERT_TRUE(rag.find_cycle().has_cycle);

        // Remove assignment edge R1 -> P1
        auto rem_res = rag.remove_assignment_edge(1, 1, 1);
        KERNOX_ASSERT_TRUE(rem_res.is_ok());
        KERNOX_ASSERT_FALSE(rag.find_cycle().has_cycle);

        // Re-adding edge recreates cycle
        auto add_res = rag.add_assignment_edge(1, 1, 1);
        KERNOX_ASSERT_TRUE(add_res.is_ok());
        KERNOX_ASSERT_TRUE(rag.find_cycle().has_cycle);
    }

    // 5. Edge removal bounds checking
    {
        auto rag = DeadlockEngine::get_rag_no_cycle_workload();
        auto err_res = rag.remove_request_edge(0, 0, 5); // Removing non-existent edges
        KERNOX_ASSERT_TRUE(err_res.is_err());
    }

    return 0;
}

static int test_validation_and_edge_cases() {
    std::cout << "Running test_validation_and_edge_cases...\n";

    // 1. Allocation exceeds max claim
    {
        BankerState s = DeadlockEngine::get_banker_safe_workload();
        s.allocation[0][0] = s.max_claim[0][0] + 1; // Violates allocation <= max
        auto val_res = s.validate();
        KERNOX_ASSERT_TRUE(val_res.is_err());
        KERNOX_ASSERT_TRUE(val_res.error().code == ErrorCode::InvalidArgument);
    }

    // 2. Inconsistent Need matrix
    {
        BankerState s = DeadlockEngine::get_banker_safe_workload();
        s.need[0][0] += 1;
        auto val_res = s.validate();
        KERNOX_ASSERT_TRUE(val_res.is_err());
        KERNOX_ASSERT_TRUE(val_res.error().code == ErrorCode::InvalidArgument);
    }

    // 3. Dimension mismatch
    {
        BankerState s = DeadlockEngine::get_banker_safe_workload();
        s.available.pop_back();
        auto val_res = s.validate();
        KERNOX_ASSERT_TRUE(val_res.is_err());
        KERNOX_ASSERT_TRUE(val_res.error().code == ErrorCode::InvalidArgument);
    }

    // 4. Formatters
    {
        auto s = DeadlockEngine::get_banker_safe_workload();
        std::string table = DeadlockEngine::format_banker_matrices(s);
        KERNOX_ASSERT_TRUE(!table.empty());
        KERNOX_ASSERT_TRUE(table.find("Allocation") != std::string::npos);

        auto safe_res = DeadlockEngine::run_banker_safety(s);
        std::string summary = DeadlockEngine::format_safety_summary(safe_res.value(), s.resource_count);
        KERNOX_ASSERT_TRUE(summary.find("SAFE") != std::string::npos);
        KERNOX_ASSERT_TRUE(summary.find("P1 -> P3") != std::string::npos);
    }

    return 0;
}

static int test_banker_zero_need_and_partial_stall() {
    std::cout << "Running test_banker_zero_need_and_partial_stall...\n";

    // 1. Process with zero need cascading when Available is initially zero
    {
        BankerState s{};
        s.process_count = 2;
        s.resource_count = 3;
        s.available = {0, 0, 0};
        s.allocation = {
            {2, 1, 1}, // P0
            {1, 1, 0}  // P1
        };
        s.max_claim = {
            {2, 1, 1}, // P0: Need is [0, 0, 0]
            {3, 1, 0}  // P1: Need is [2, 0, 0]
        };
        auto need_res = BankerState::calculate_need(s.max_claim, s.allocation);
        KERNOX_ASSERT_TRUE(need_res.is_ok());
        s.need = need_res.value();

        auto res = DeadlockEngine::run_banker_safety(s);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& safe = res.value();
        KERNOX_ASSERT_TRUE(safe.is_safe);
        KERNOX_ASSERT_EQ(safe.safe_sequence.size(), 2u);
        // P0 must execute first (need is 0 <= 0), releasing (2, 1, 1) -> enabling P1
        KERNOX_ASSERT_EQ(safe.safe_sequence[0], 0u);
        KERNOX_ASSERT_EQ(safe.safe_sequence[1], 1u);
        ResourceVector expected_work = {3, 2, 1};
        KERNOX_ASSERT_TRUE(safe.final_work == expected_work);
    }

    // 2. Unsafe state with partial execution stall: safe_sequence must be empty
    {
        BankerState s{};
        s.process_count = 3;
        s.resource_count = 2;
        s.available = {0, 0};
        s.allocation = {
            {1, 0}, // P0
            {1, 0}, // P1
            {1, 0}  // P2
        };
        s.max_claim = {
            {1, 0}, // P0: Need is [0, 0]
            {3, 2}, // P1: Need is [2, 2]
            {4, 3}  // P2: Need is [3, 3]
        };
        auto need_res = BankerState::calculate_need(s.max_claim, s.allocation);
        KERNOX_ASSERT_TRUE(need_res.is_ok());
        s.need = need_res.value();

        auto res = DeadlockEngine::run_banker_safety(s);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& unsafe = res.value();
        KERNOX_ASSERT_FALSE(unsafe.is_safe);
        // Verified defect fix: safe_sequence must be empty even though P0 completed before stall
        KERNOX_ASSERT_TRUE(unsafe.safe_sequence.empty());
        KERNOX_ASSERT_EQ(unsafe.unfinished_processes.size(), 2u);
        std::vector<size_t> expected_unfinished = {1, 2};
        KERNOX_ASSERT_TRUE(unsafe.unfinished_processes == expected_unfinished);
        ResourceVector expected_work = {1, 0}; // P0 released (1, 0)
        KERNOX_ASSERT_TRUE(unsafe.final_work == expected_work);
    }

    // 3. Process with 0 allocation and 0 max claim (fully inactive process)
    {
        BankerState s{};
        s.process_count = 1;
        s.resource_count = 1;
        s.available = {0};
        s.allocation = {{0}};
        s.max_claim = {{0}};
        s.need = {{0}};
        auto res = DeadlockEngine::run_banker_safety(s);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(res.value().is_safe);
        KERNOX_ASSERT_EQ(res.value().safe_sequence.size(), 1u);
        KERNOX_ASSERT_EQ(res.value().safe_sequence[0], 0u);
    }

    return 0;
}

static int test_detection_edge_cases_and_progress() {
    std::cout << "Running test_detection_edge_cases_and_progress...\n";

    // 1. Process holding NO resources but requesting resources is NOT deadlocked
    {
        DetectionState s{};
        s.process_count = 2;
        s.resource_count = 2;
        s.available = {0, 0};
        s.allocation = {
            {0, 0}, // P0 holds nothing
            {1, 1}  // P1 holds (1, 1)
        };
        s.request = {
            {1, 1}, // P0 requests (1, 1)
            {0, 0}  // P1 requests (0, 0)
        };
        auto res = DeadlockEngine::run_deadlock_detection(s);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& d = res.value();
        // Neither process is deadlocked: P0 holds nothing, P1 finishes and releases (1, 1)
        KERNOX_ASSERT_FALSE(d.is_deadlocked);
        KERNOX_ASSERT_TRUE(d.deadlocked_processes.empty());
        ResourceVector expected_work = {1, 1};
        KERNOX_ASSERT_TRUE(d.final_work == expected_work);
    }

    // 2. Unfinished process is not deadlocked because another process unblocks it
    {
        DetectionState s{};
        s.process_count = 2;
        s.resource_count = 2;
        s.available = {0, 0};
        s.allocation = {
            {1, 0}, // P0 holds R0
            {0, 1}  // P1 holds R1
        };
        s.request = {
            {0, 1}, // P0 requests R1 (must wait for P1)
            {0, 0}  // P1 has no requests, ready to finish!
        };
        auto res = DeadlockEngine::run_deadlock_detection(s);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& d = res.value();
        KERNOX_ASSERT_FALSE(d.is_deadlocked);
        KERNOX_ASSERT_TRUE(d.deadlocked_processes.empty());
        ResourceVector expected_work = {1, 1};
        KERNOX_ASSERT_TRUE(d.final_work == expected_work);
    }

    // 3. Overflow protection in DetectionState validation
    {
        DetectionState s{};
        s.process_count = 2;
        s.resource_count = 1;
        s.available = {UINT32_MAX};
        s.allocation = {{1}, {0}};
        s.request = {{0}, {0}};
        auto val_res = s.validate();
        KERNOX_ASSERT_TRUE(val_res.is_err());
        KERNOX_ASSERT_TRUE(val_res.error().code == ErrorCode::InvalidArgument);
    }

    return 0;
}

static int test_rag_advanced_topologies_and_cycles() {
    std::cout << "Running test_rag_advanced_topologies_and_cycles...\n";

    // 1. Self-loop cycle: P0 requests R0, R0 assigned to P0
    {
        ResourceAllocationGraph rag(1, 1);
        rag.set_resource_capacity(0, 1);
        KERNOX_ASSERT_TRUE(rag.add_assignment_edge(0, 0, 1).is_ok());
        KERNOX_ASSERT_TRUE(rag.add_request_edge(0, 0, 1).is_ok());

        auto cycle = rag.find_cycle();
        KERNOX_ASSERT_TRUE(cycle.has_cycle);
        KERNOX_ASSERT_TRUE(cycle.is_single_instance_only);
        KERNOX_ASSERT_TRUE(cycle.cycle_is_single_instance);
        KERNOX_ASSERT_TRUE(cycle.interpretation.find("Definite Deadlock") != std::string::npos);
        std::vector<std::string> expected_path = {"P0", "R0", "P0"};
        KERNOX_ASSERT_TRUE(cycle.cycle_path == expected_path);
    }

    // 2. Disconnected graph with cycle in second component
    {
        ResourceAllocationGraph rag(3, 3);
        rag.set_resource_capacity(0, 1);
        rag.set_resource_capacity(1, 1);
        rag.set_resource_capacity(2, 1);

        // Component 1: P0 and R0 (no cycle)
        KERNOX_ASSERT_TRUE(rag.add_assignment_edge(0, 0, 1).is_ok());

        // Component 2: P1, P2, R1, R2 (cycle P1 -> R1 -> P2 -> R2 -> P1)
        KERNOX_ASSERT_TRUE(rag.add_request_edge(1, 1, 1).is_ok());
        KERNOX_ASSERT_TRUE(rag.add_assignment_edge(1, 2, 1).is_ok());
        KERNOX_ASSERT_TRUE(rag.add_request_edge(2, 2, 1).is_ok());
        KERNOX_ASSERT_TRUE(rag.add_assignment_edge(2, 1, 1).is_ok());

        auto cycle = rag.find_cycle();
        KERNOX_ASSERT_TRUE(cycle.has_cycle);
        KERNOX_ASSERT_TRUE(cycle.is_single_instance_only);
        KERNOX_ASSERT_TRUE(cycle.cycle_is_single_instance);
        KERNOX_ASSERT_TRUE(cycle.interpretation.find("Definite Deadlock") != std::string::npos);
    }

    // 3. Multi-resource graph where cycle contains only single-instance resources
    {
        ResourceAllocationGraph rag(3, 3);
        rag.set_resource_capacity(0, 1);  // R0 is single-instance
        rag.set_resource_capacity(1, 1);  // R1 is single-instance
        rag.set_resource_capacity(2, 10); // R2 is multi-instance (unrelated)

        // Cycle involves only P0, R0, P1, R1
        KERNOX_ASSERT_TRUE(rag.add_assignment_edge(0, 0, 1).is_ok());
        KERNOX_ASSERT_TRUE(rag.add_request_edge(0, 1, 1).is_ok());
        KERNOX_ASSERT_TRUE(rag.add_assignment_edge(1, 1, 1).is_ok());
        KERNOX_ASSERT_TRUE(rag.add_request_edge(1, 0, 1).is_ok());

        // Entire graph is not single-instance (R2 has 10)
        KERNOX_ASSERT_FALSE(rag.is_single_instance_only());

        auto cycle = rag.find_cycle();
        KERNOX_ASSERT_TRUE(cycle.has_cycle);
        // Verified fix: the cycle itself contains only single-instance resources!
        KERNOX_ASSERT_TRUE(cycle.cycle_is_single_instance);
        KERNOX_ASSERT_TRUE(cycle.interpretation.find("Definite Deadlock") != std::string::npos);
    }

    // 4. Capacity bounds enforcement
    {
        ResourceAllocationGraph rag(2, 1);
        rag.set_resource_capacity(0, 1);
        KERNOX_ASSERT_TRUE(rag.add_assignment_edge(0, 0, 1).is_ok());
        // Allocating another instance when capacity is 1 must fail immediately
        auto err_res = rag.add_assignment_edge(0, 1, 1);
        KERNOX_ASSERT_TRUE(err_res.is_err());
        KERNOX_ASSERT_TRUE(err_res.error().code == ErrorCode::InvalidArgument);

        // Zero capacity validation
        rag.set_resource_capacity(0, 0);
        auto val_res = rag.validate();
        KERNOX_ASSERT_TRUE(val_res.is_err());
        KERNOX_ASSERT_TRUE(val_res.error().code == ErrorCode::InvalidArgument);
    }

    return 0;
}

int main() {
    std::cout << "=== KERNOX Deadlock Simulation Engine Tests ===\n";

    if (test_banker_safety_canonical() != 0) return 1;
    if (test_banker_safety_unsafe() != 0) return 1;
    if (test_banker_resource_requests() != 0) return 1;
    if (test_deadlock_detection() != 0) return 1;
    if (test_resource_allocation_graph() != 0) return 1;
    if (test_validation_and_edge_cases() != 0) return 1;
    if (test_banker_zero_need_and_partial_stall() != 0) return 1;
    if (test_detection_edge_cases_and_progress() != 0) return 1;
    if (test_rag_advanced_topologies_and_cycles() != 0) return 1;

    std::cout << "=== All Deadlock Simulation Tests Passed Successfully ===\n";
    return 0;
}
