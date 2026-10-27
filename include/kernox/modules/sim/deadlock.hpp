#pragma once

#include "kernox/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kernox::sim {

using ResourceVector = std::vector<uint32_t>;
using ResourceMatrix = std::vector<std::vector<uint32_t>>;

// ----------------------------------------------------------------------------
// Banker's Algorithm State
// ----------------------------------------------------------------------------
struct BankerState {
    size_t process_count{0};
    size_t resource_count{0};
    ResourceVector available;   // [m]
    ResourceMatrix allocation;  // [n][m]
    ResourceMatrix max_claim;   // [n][m]
    ResourceMatrix need;        // [n][m]: Need[i][j] = Max[i][j] - Allocation[i][j]

    [[nodiscard]] core::Result<void> validate() const;

    // Helper to calculate Need matrix from Max and Allocation
    static core::Result<ResourceMatrix> calculate_need(
        const ResourceMatrix& max_claim, const ResourceMatrix& allocation);
};

// ----------------------------------------------------------------------------
// Banker's Safety Result
// ----------------------------------------------------------------------------
struct SafetyResult {
    bool is_safe{false};
    std::vector<size_t> safe_sequence; // Deterministic execution sequence of process indices
    ResourceVector final_work;
    std::vector<size_t> unfinished_processes;
    std::string diagnostic;
};

// ----------------------------------------------------------------------------
// Resource Request Result
// ----------------------------------------------------------------------------
enum class RequestStatus {
    Granted,
    ExceededClaim,          // Request exceeds remaining declared need
    InsufficientAvailable,  // Request exceeds currently available resources (must wait)
    UnsafeState             // Granting request would result in an unsafe state
};

[[nodiscard]] std::string_view to_string(RequestStatus status) noexcept;

struct RequestResult {
    RequestStatus status{RequestStatus::UnsafeState};
    std::string message;
    BankerState resulting_state; // Committed state if Granted; unchanged original state otherwise
    std::optional<SafetyResult> tentative_safety;
};

// ----------------------------------------------------------------------------
// Deadlock Detection Model (Multiple Instances)
// ----------------------------------------------------------------------------
struct DetectionState {
    size_t process_count{0};
    size_t resource_count{0};
    ResourceVector available;   // [m]
    ResourceMatrix allocation;  // [n][m]
    ResourceMatrix request;     // [n][m] Current outstanding requests

    [[nodiscard]] core::Result<void> validate() const;
};

struct DetectionResult {
    bool is_deadlocked{false};
    std::vector<size_t> deadlocked_processes;
    ResourceVector final_work;
    std::string explanation;
};

// ----------------------------------------------------------------------------
// Resource Allocation Graph (RAG)
// ----------------------------------------------------------------------------
struct RAGCycleResult {
    bool has_cycle{false};
    std::vector<std::string> cycle_path; // e.g., ["P0", "R0", "P1", "R1", "P0"]
    bool is_single_instance_only{true};  // Entire graph contains only single-instance resources
    bool cycle_is_single_instance{true}; // All resources participating in the cycle have capacity == 1
    std::string interpretation;
};

class ResourceAllocationGraph {
public:
    explicit ResourceAllocationGraph(size_t process_count = 0, size_t resource_count = 0);

    void set_process_count(size_t n);
    void set_resource_count(size_t m);
    void set_resource_capacity(size_t res_idx, uint32_t capacity);

    [[nodiscard]] size_t process_count() const noexcept { return process_count_; }
    [[nodiscard]] size_t resource_count() const noexcept { return resource_count_; }
    [[nodiscard]] uint32_t resource_capacity(size_t res_idx) const;

    // Edge management:
    // Request edge: Process P_i -> Resource R_j (process requests resource)
    // Assignment edge: Resource R_j -> Process P_i (resource allocated to process)
    core::Result<void> add_request_edge(size_t proc_idx, size_t res_idx, uint32_t count = 1);
    core::Result<void> remove_request_edge(size_t proc_idx, size_t res_idx, uint32_t count = 1);

    core::Result<void> add_assignment_edge(size_t res_idx, size_t proc_idx, uint32_t count = 1);
    core::Result<void> remove_assignment_edge(size_t res_idx, size_t proc_idx, uint32_t count = 1);

    [[nodiscard]] uint32_t request_edge_count(size_t proc_idx, size_t res_idx) const;
    [[nodiscard]] uint32_t assignment_edge_count(size_t res_idx, size_t proc_idx) const;

    [[nodiscard]] core::Result<void> validate() const;
    [[nodiscard]] bool is_single_instance_only() const;

    [[nodiscard]] RAGCycleResult find_cycle() const;

private:
    size_t process_count_{0};
    size_t resource_count_{0};
    ResourceVector resource_capacities_; // Capacity of each resource type
    ResourceMatrix request_edges_;       // [n][m]: P_i -> R_j
    ResourceMatrix assignment_edges_;    // [m][n]: R_j -> P_i
};

// ----------------------------------------------------------------------------
// Deadlock Simulation Engine API
// ----------------------------------------------------------------------------
class DeadlockEngine {
public:
    // Banker's Algorithm
    static core::Result<SafetyResult> run_banker_safety(const BankerState& state);
    static core::Result<RequestResult> process_resource_request(
        const BankerState& state, size_t process_id, const ResourceVector& request);

    // Deadlock Detection
    static core::Result<DetectionResult> run_deadlock_detection(const DetectionState& state);

    // Resource Allocation Graph Analysis
    static core::Result<RAGCycleResult> analyze_rag(const ResourceAllocationGraph& rag);

    // Built-in Deterministic Workloads
    static BankerState get_banker_safe_workload();
    static BankerState get_banker_unsafe_workload();
    static DetectionState get_detection_deadlock_workload();
    static DetectionState get_detection_free_workload();
    static ResourceAllocationGraph get_rag_single_cycle_workload();
    static ResourceAllocationGraph get_rag_multi_cycle_workload();
    static ResourceAllocationGraph get_rag_no_cycle_workload();

    // ASCII Formatters
    static std::string format_banker_matrices(const BankerState& state);
    static std::string format_safety_summary(const SafetyResult& result, size_t resource_count);
    static std::string format_detection_matrices(const DetectionState& state);
    static std::string format_detection_summary(const DetectionResult& result, size_t resource_count);
    static std::string format_rag_summary(const ResourceAllocationGraph& rag, const RAGCycleResult& cycle);
};

} // namespace kernox::sim
