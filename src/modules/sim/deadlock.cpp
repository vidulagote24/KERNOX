#include "kernox/modules/sim/deadlock.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace kernox::sim {

std::string_view to_string(RequestStatus status) noexcept {
    switch (status) {
        case RequestStatus::Granted:
            return "GRANTED";
        case RequestStatus::ExceededClaim:
            return "REJECTED (Exceeded Declared Claim)";
        case RequestStatus::InsufficientAvailable:
            return "REJECTED (Insufficient Available Resources)";
        case RequestStatus::UnsafeState:
            return "REJECTED (Would Result in Unsafe State)";
    }
    return "UNKNOWN";
}

// ----------------------------------------------------------------------------
// BankerState Validation
// ----------------------------------------------------------------------------
core::Result<void> BankerState::validate() const {
    if (process_count == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Banker state process count must be positive (> 0)");
    }
    if (resource_count == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Banker state resource count must be positive (> 0)");
    }
    if (available.size() != resource_count) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Available vector size (" + std::to_string(available.size()) +
            ") does not match resource count (" + std::to_string(resource_count) + ")");
    }
    if (allocation.size() != process_count) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Allocation matrix row count (" + std::to_string(allocation.size()) +
            ") does not match process count (" + std::to_string(process_count) + ")");
    }
    if (max_claim.size() != process_count) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Max claim matrix row count (" + std::to_string(max_claim.size()) +
            ") does not match process count (" + std::to_string(process_count) + ")");
    }
    if (need.size() != process_count) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Need matrix row count (" + std::to_string(need.size()) +
            ") does not match process count (" + std::to_string(process_count) + ")");
    }

    for (size_t i = 0; i < process_count; ++i) {
        if (allocation[i].size() != resource_count) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Allocation matrix row " + std::to_string(i) + " size mismatch");
        }
        if (max_claim[i].size() != resource_count) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Max claim matrix row " + std::to_string(i) + " size mismatch");
        }
        if (need[i].size() != resource_count) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Need matrix row " + std::to_string(i) + " size mismatch");
        }

        for (size_t j = 0; j < resource_count; ++j) {
            if (allocation[i][j] > max_claim[i][j]) {
                return core::Result<void>::err(
                    core::ErrorCode::InvalidArgument,
                    "Invalid state: Allocation exceeds Maximum claim for Process P" +
                    std::to_string(i) + " on Resource R" + std::to_string(j) +
                    " (" + std::to_string(allocation[i][j]) + " > " +
                    std::to_string(max_claim[i][j]) + ")");
            }
            uint32_t expected_need = max_claim[i][j] - allocation[i][j];
            if (need[i][j] != expected_need) {
                return core::Result<void>::err(
                    core::ErrorCode::InvalidArgument,
                    "Inconsistent Need matrix for Process P" + std::to_string(i) +
                    " Resource R" + std::to_string(j) + ": specified " +
                    std::to_string(need[i][j]) + " but expected " +
                    std::to_string(expected_need));
            }
        }
    }

    // Check for resource totals overflow
    for (size_t j = 0; j < resource_count; ++j) {
        uint64_t sum = available[j];
        for (size_t i = 0; i < process_count; ++i) {
            sum += allocation[i][j];
            if (sum > UINT32_MAX) {
                return core::Result<void>::err(
                    core::ErrorCode::InvalidArgument,
                    "Arithmetic overflow in total resource calculation for Resource R" +
                    std::to_string(j));
            }
        }
    }

    return core::Result<void>::ok();
}

core::Result<ResourceMatrix> BankerState::calculate_need(
    const ResourceMatrix& max_claim, const ResourceMatrix& allocation) {
    if (max_claim.empty() || allocation.empty()) {
        return core::Result<ResourceMatrix>::err(
            core::ErrorCode::InvalidArgument, "Cannot calculate need for empty matrices");
    }
    if (max_claim.size() != allocation.size()) {
        return core::Result<ResourceMatrix>::err(
            core::ErrorCode::InvalidArgument, "Matrix row count mismatch");
    }

    size_t n = max_claim.size();
    size_t m = max_claim[0].size();
    ResourceMatrix need(n, ResourceVector(m, 0));

    for (size_t i = 0; i < n; ++i) {
        if (max_claim[i].size() != m || allocation[i].size() != m) {
            return core::Result<ResourceMatrix>::err(
                core::ErrorCode::InvalidArgument, "Matrix column count mismatch");
        }
        for (size_t j = 0; j < m; ++j) {
            if (allocation[i][j] > max_claim[i][j]) {
                return core::Result<ResourceMatrix>::err(
                    core::ErrorCode::InvalidArgument,
                    "Allocation exceeds Max claim at [" + std::to_string(i) + "][" +
                    std::to_string(j) + "]");
            }
            need[i][j] = max_claim[i][j] - allocation[i][j];
        }
    }
    return core::Result<ResourceMatrix>::ok(std::move(need));
}

// ----------------------------------------------------------------------------
// DetectionState Validation
// ----------------------------------------------------------------------------
core::Result<void> DetectionState::validate() const {
    if (process_count == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Detection state process count must be positive (> 0)");
    }
    if (resource_count == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Detection state resource count must be positive (> 0)");
    }
    if (available.size() != resource_count) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Available vector size mismatch");
    }
    if (allocation.size() != process_count || request.size() != process_count) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Allocation or request matrix row count mismatch");
    }

    for (size_t i = 0; i < process_count; ++i) {
        if (allocation[i].size() != resource_count || request[i].size() != resource_count) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument, "Row size mismatch at process index " + std::to_string(i));
        }
    }

    // Check for resource totals overflow
    for (size_t j = 0; j < resource_count; ++j) {
        uint64_t sum = available[j];
        for (size_t i = 0; i < process_count; ++i) {
            sum += allocation[i][j];
            if (sum > UINT32_MAX) {
                return core::Result<void>::err(
                    core::ErrorCode::InvalidArgument,
                    "Arithmetic overflow in total resource calculation for Resource R" +
                    std::to_string(j));
            }
        }
    }

    return core::Result<void>::ok();
}

// ----------------------------------------------------------------------------
// ResourceAllocationGraph Implementation
// ----------------------------------------------------------------------------
ResourceAllocationGraph::ResourceAllocationGraph(size_t process_count, size_t resource_count)
    : process_count_(process_count),
      resource_count_(resource_count),
      resource_capacities_(resource_count, 1),
      request_edges_(process_count, ResourceVector(resource_count, 0)),
      assignment_edges_(resource_count, ResourceVector(process_count, 0)) {}

void ResourceAllocationGraph::set_process_count(size_t n) {
    process_count_ = n;
    request_edges_.assign(n, ResourceVector(resource_count_, 0));
    for (auto& row : assignment_edges_) {
        row.assign(n, 0);
    }
}

void ResourceAllocationGraph::set_resource_count(size_t m) {
    resource_count_ = m;
    resource_capacities_.assign(m, 1);
    for (auto& row : request_edges_) {
        row.assign(m, 0);
    }
    assignment_edges_.assign(m, ResourceVector(process_count_, 0));
}

void ResourceAllocationGraph::set_resource_capacity(size_t res_idx, uint32_t capacity) {
    if (res_idx < resource_capacities_.size()) {
        resource_capacities_[res_idx] = capacity;
    }
}

uint32_t ResourceAllocationGraph::resource_capacity(size_t res_idx) const {
    if (res_idx < resource_capacities_.size()) {
        return resource_capacities_[res_idx];
    }
    return 0;
}

core::Result<void> ResourceAllocationGraph::add_request_edge(size_t proc_idx, size_t res_idx, uint32_t count) {
    if (proc_idx >= process_count_) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Process index out of range");
    }
    if (res_idx >= resource_count_) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Resource index out of range");
    }
    if (static_cast<uint64_t>(request_edges_[proc_idx][res_idx]) + count > UINT32_MAX) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Arithmetic overflow in request edge count");
    }
    request_edges_[proc_idx][res_idx] += count;
    return core::Result<void>::ok();
}

core::Result<void> ResourceAllocationGraph::remove_request_edge(size_t proc_idx, size_t res_idx, uint32_t count) {
    if (proc_idx >= process_count_ || res_idx >= resource_count_) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Index out of range");
    }
    if (request_edges_[proc_idx][res_idx] < count) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Cannot remove more edges than exist");
    }
    request_edges_[proc_idx][res_idx] -= count;
    return core::Result<void>::ok();
}

core::Result<void> ResourceAllocationGraph::add_assignment_edge(size_t res_idx, size_t proc_idx, uint32_t count) {
    if (res_idx >= resource_count_ || proc_idx >= process_count_) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Index out of range");
    }
    uint64_t total_assigned = count;
    for (size_t i = 0; i < process_count_; ++i) {
        total_assigned += assignment_edges_[res_idx][i];
    }
    if (total_assigned > resource_capacities_[res_idx]) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot allocate more instances than total resource capacity (" +
            std::to_string(resource_capacities_[res_idx]) + ")");
    }
    assignment_edges_[res_idx][proc_idx] += count;
    return core::Result<void>::ok();
}

core::Result<void> ResourceAllocationGraph::remove_assignment_edge(size_t res_idx, size_t proc_idx, uint32_t count) {
    if (res_idx >= resource_count_ || proc_idx >= process_count_) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Index out of range");
    }
    if (assignment_edges_[res_idx][proc_idx] < count) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Cannot remove more edges than exist");
    }
    assignment_edges_[res_idx][proc_idx] -= count;
    return core::Result<void>::ok();
}

uint32_t ResourceAllocationGraph::request_edge_count(size_t proc_idx, size_t res_idx) const {
    if (proc_idx < process_count_ && res_idx < resource_count_) {
        return request_edges_[proc_idx][res_idx];
    }
    return 0;
}

uint32_t ResourceAllocationGraph::assignment_edge_count(size_t res_idx, size_t proc_idx) const {
    if (res_idx < resource_count_ && proc_idx < process_count_) {
        return assignment_edges_[res_idx][proc_idx];
    }
    return 0;
}

core::Result<void> ResourceAllocationGraph::validate() const {
    if (process_count_ == 0 || resource_count_ == 0) {
        return core::Result<void>::err(core::ErrorCode::InvalidArgument, "Graph cannot have zero processes or resources");
    }
    for (size_t j = 0; j < resource_count_; ++j) {
        if (resource_capacities_[j] == 0) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Resource capacity must be positive (> 0) for Resource R" + std::to_string(j));
        }
        uint64_t total_assigned = 0;
        for (size_t i = 0; i < process_count_; ++i) {
            total_assigned += assignment_edges_[j][i];
        }
        if (total_assigned > resource_capacities_[j]) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Total assigned instances for Resource R" + std::to_string(j) +
                " (" + std::to_string(total_assigned) + ") exceeds capacity (" +
                std::to_string(resource_capacities_[j]) + ")");
        }
    }
    return core::Result<void>::ok();
}

bool ResourceAllocationGraph::is_single_instance_only() const {
    for (uint32_t cap : resource_capacities_) {
        if (cap != 1) return false;
    }
    return true;
}

RAGCycleResult ResourceAllocationGraph::find_cycle() const {
    RAGCycleResult res{};
    res.is_single_instance_only = is_single_instance_only();

    // Node representation:
    // 0 to process_count_ - 1: Process nodes (P0, P1, ...)
    // process_count_ to process_count_ + resource_count_ - 1: Resource nodes (R0, R1, ...)
    size_t total_nodes = process_count_ + resource_count_;
    std::vector<std::vector<size_t>> adj(total_nodes);

    for (size_t i = 0; i < process_count_; ++i) {
        for (size_t j = 0; j < resource_count_; ++j) {
            if (request_edges_[i][j] > 0) {
                adj[i].push_back(process_count_ + j); // P_i -> R_j
            }
        }
    }

    for (size_t j = 0; j < resource_count_; ++j) {
        for (size_t i = 0; i < process_count_; ++i) {
            if (assignment_edges_[j][i] > 0) {
                adj[process_count_ + j].push_back(i); // R_j -> P_i
            }
        }
    }

    // 3-color DFS cycle detection: 0 = unvisited, 1 = visiting, 2 = visited
    std::vector<int> color(total_nodes, 0);
    std::vector<size_t> parent(total_nodes, SIZE_MAX);
    size_t cycle_start = SIZE_MAX;
    size_t cycle_end = SIZE_MAX;

    auto dfs = [&](auto& self, size_t u) -> bool {
        color[u] = 1;
        for (size_t v : adj[u]) {
            if (color[v] == 1) {
                // Back-edge found
                cycle_start = v;
                cycle_end = u;
                return true;
            }
            if (color[v] == 0) {
                parent[v] = u;
                if (self(self, v)) return true;
            }
        }
        color[u] = 2;
        return false;
    };

    for (size_t node = 0; node < total_nodes; ++node) {
        if (color[node] == 0) {
            if (dfs(dfs, node)) {
                res.has_cycle = true;
                break;
            }
        }
    }

    if (res.has_cycle) {
        std::vector<size_t> path_indices;
        path_indices.push_back(cycle_start);
        for (size_t curr = cycle_end; curr != cycle_start && curr != SIZE_MAX; curr = parent[curr]) {
            path_indices.push_back(curr);
        }
        path_indices.push_back(cycle_start);
        std::reverse(path_indices.begin(), path_indices.end());

        bool cycle_single_inst = true;
        for (size_t idx : path_indices) {
            if (idx < process_count_) {
                res.cycle_path.push_back("P" + std::to_string(idx));
            } else {
                size_t res_idx = idx - process_count_;
                res.cycle_path.push_back("R" + std::to_string(res_idx));
                if (resource_capacities_[res_idx] > 1) {
                    cycle_single_inst = false;
                }
            }
        }
        res.cycle_is_single_instance = cycle_single_inst;

        if (cycle_single_inst) {
            res.interpretation =
                "Definite Deadlock: In a single-instance resource system, a directed cycle in the "
                "Resource Allocation Graph is both a necessary and sufficient condition for deadlock.";
        } else {
            res.interpretation =
                "Deadlock Warning: Directed cycle detected in a multi-instance resource system. "
                "A cycle is a necessary condition for deadlock, but NOT sufficient. Other processes "
                "holding resource instances may finish and break the cycle. Run Deadlock Detection "
                "to confirm whether processes are truly deadlocked.";
        }
    } else {
        res.has_cycle = false;
        res.cycle_is_single_instance = true;
        res.interpretation =
            "No Deadlock: No directed cycle exists in the Resource Allocation Graph. "
            "Under the standard model, the absence of a cycle guarantees that the system is deadlock-free.";
    }

    return res;
}

// ----------------------------------------------------------------------------
// Step 3: Banker's Safety Algorithm
// ----------------------------------------------------------------------------
core::Result<SafetyResult> DeadlockEngine::run_banker_safety(const BankerState& state) {
    auto val_res = state.validate();
    if (val_res.is_err()) {
        return core::Result<SafetyResult>::err(val_res.error());
    }

    SafetyResult res{};
    ResourceVector work = state.available;
    std::vector<bool> finish(state.process_count, false);
    res.safe_sequence.reserve(state.process_count);

    size_t finished_count = 0;
    while (finished_count < state.process_count) {
        bool progress = false;

        // Deterministic tie-breaking: iterate in ascending process index order
        for (size_t i = 0; i < state.process_count; ++i) {
            if (!finish[i]) {
                bool can_finish = true;
                for (size_t j = 0; j < state.resource_count; ++j) {
                    if (state.need[i][j] > work[j]) {
                        can_finish = false;
                        break;
                    }
                }

                if (can_finish) {
                    for (size_t j = 0; j < state.resource_count; ++j) {
                        work[j] += state.allocation[i][j];
                    }
                    finish[i] = true;
                    res.safe_sequence.push_back(i);
                    finished_count++;
                    progress = true;
                    break; // Restart from lowest index to guarantee deterministic order
                }
            }
        }

        if (!progress) {
            break;
        }
    }

    res.final_work = std::move(work);

    if (finished_count == state.process_count) {
        res.is_safe = true;
        std::ostringstream oss;
        oss << "State is SAFE. Deterministic safe sequence found: <";
        for (size_t k = 0; k < res.safe_sequence.size(); ++k) {
            oss << "P" << res.safe_sequence[k] << (k + 1 < res.safe_sequence.size() ? ", " : ">");
        }
        res.diagnostic = oss.str();
    } else {
        res.is_safe = false;
        res.safe_sequence.clear();
        std::ostringstream oss;
        oss << "State is UNSAFE. Execution stalled after servicing " << finished_count
            << " of " << state.process_count << " processes. Unfinished processes: [";
        bool first = true;
        for (size_t i = 0; i < state.process_count; ++i) {
            if (!finish[i]) {
                res.unfinished_processes.push_back(i);
                if (!first) oss << ", ";
                oss << "P" << i;
                first = false;
            }
        }
        oss << "]. Notice: An unsafe state is not necessarily deadlocked; it indicates that the system "
            << "cannot guarantee completion without preemption if all processes request their maximum declared claims.";
        res.diagnostic = oss.str();
    }

    return core::Result<SafetyResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// Step 4: Banker's Resource Request Handling
// ----------------------------------------------------------------------------
core::Result<RequestResult> DeadlockEngine::process_resource_request(
    const BankerState& state, size_t process_id, const ResourceVector& request) {
    auto val_res = state.validate();
    if (val_res.is_err()) {
        return core::Result<RequestResult>::err(val_res.error());
    }

    if (process_id >= state.process_count) {
        return core::Result<RequestResult>::err(
            core::ErrorCode::InvalidArgument,
            "Process ID " + std::to_string(process_id) + " is out of range [0, " +
            std::to_string(state.process_count - 1) + "]");
    }
    if (request.size() != state.resource_count) {
        return core::Result<RequestResult>::err(
            core::ErrorCode::InvalidArgument,
            "Request vector size (" + std::to_string(request.size()) +
            ") does not match resource count (" + std::to_string(state.resource_count) + ")");
    }

    RequestResult res{};
    res.resulting_state = state; // Default to unmodified original state

    // 1. Check if Request <= Need[process_id]
    for (size_t j = 0; j < state.resource_count; ++j) {
        if (request[j] > state.need[process_id][j]) {
            res.status = RequestStatus::ExceededClaim;
            res.message = "Process P" + std::to_string(process_id) +
                          " exceeded its maximum declared claim on Resource R" + std::to_string(j) +
                          " (requested " + std::to_string(request[j]) + ", need is " +
                          std::to_string(state.need[process_id][j]) + ")";
            return core::Result<RequestResult>::ok(std::move(res));
        }
    }

    // 2. Check if Request <= Available
    for (size_t j = 0; j < state.resource_count; ++j) {
        if (request[j] > state.available[j]) {
            res.status = RequestStatus::InsufficientAvailable;
            res.message = "Insufficient resources available for Process P" + std::to_string(process_id) +
                          " on Resource R" + std::to_string(j) + " (requested " +
                          std::to_string(request[j]) + ", available is " +
                          std::to_string(state.available[j]) + "); process must wait.";
            return core::Result<RequestResult>::ok(std::move(res));
        }
    }

    // 3. Tentative allocation on deep copy
    BankerState tentative = state;
    for (size_t j = 0; j < state.resource_count; ++j) {
        tentative.available[j] -= request[j];
        tentative.allocation[process_id][j] += request[j];
        tentative.need[process_id][j] -= request[j];
    }

    auto safety_res = run_banker_safety(tentative);
    if (safety_res.is_err()) {
        return core::Result<RequestResult>::err(safety_res.error());
    }

    res.tentative_safety = safety_res.value();

    if (safety_res.value().is_safe) {
        // Safe: commit tentative state
        res.status = RequestStatus::Granted;
        res.resulting_state = std::move(tentative);
        res.message = "Resource request granted successfully. System state remains SAFE.";
    } else {
        // Unsafe: reject request, leave original state unchanged
        res.status = RequestStatus::UnsafeState;
        res.resulting_state = state; // Unchanged
        res.message = "Resource request REJECTED. Granting this request would leave the system in an UNSAFE state.";
    }

    return core::Result<RequestResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// Step 5: Deadlock Detection (Multiple Instances)
// ----------------------------------------------------------------------------
core::Result<DetectionResult> DeadlockEngine::run_deadlock_detection(const DetectionState& state) {
    auto val_res = state.validate();
    if (val_res.is_err()) {
        return core::Result<DetectionResult>::err(val_res.error());
    }

    DetectionResult res{};
    ResourceVector work = state.available;
    std::vector<bool> finish(state.process_count, false);

    // Initialize Finish: if Allocation[i] is 0 for all resources, Finish[i] = true; else false
    for (size_t i = 0; i < state.process_count; ++i) {
        bool has_allocation = false;
        for (size_t j = 0; j < state.resource_count; ++j) {
            if (state.allocation[i][j] > 0) {
                has_allocation = true;
                break;
            }
        }
        if (!has_allocation) {
            finish[i] = true;
        }
    }

    // Iterate finding unfinished processes whose request <= Work
    while (true) {
        bool progress = false;
        for (size_t i = 0; i < state.process_count; ++i) {
            if (!finish[i]) {
                bool can_satisfy = true;
                for (size_t j = 0; j < state.resource_count; ++j) {
                    if (state.request[i][j] > work[j]) {
                        can_satisfy = false;
                        break;
                    }
                }

                if (can_satisfy) {
                    for (size_t j = 0; j < state.resource_count; ++j) {
                        work[j] += state.allocation[i][j];
                    }
                    finish[i] = true;
                    progress = true;
                    break; // Restart loop to maintain deterministic order
                }
            }
        }
        if (!progress) break;
    }

    res.final_work = std::move(work);

    for (size_t i = 0; i < state.process_count; ++i) {
        if (!finish[i]) {
            res.deadlocked_processes.push_back(i);
        }
    }

    if (res.deadlocked_processes.empty()) {
        res.is_deadlocked = false;
        res.explanation = "No deadlock detected: all processes can acquire their outstanding requested resources and run to completion.";
    } else {
        res.is_deadlocked = true;
        std::ostringstream oss;
        oss << "DEADLOCK DETECTED! The following processes are deadlocked: [";
        for (size_t k = 0; k < res.deadlocked_processes.size(); ++k) {
            oss << "P" << res.deadlocked_processes[k] << (k + 1 < res.deadlocked_processes.size() ? ", " : "]");
        }
        oss << ". Their outstanding requests exceed available resources, and no process can make progress.";
        res.explanation = oss.str();
    }

    return core::Result<DetectionResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// Step 6: RAG Analysis API
// ----------------------------------------------------------------------------
core::Result<RAGCycleResult> DeadlockEngine::analyze_rag(const ResourceAllocationGraph& rag) {
    auto val_res = rag.validate();
    if (val_res.is_err()) {
        return core::Result<RAGCycleResult>::err(val_res.error());
    }
    return core::Result<RAGCycleResult>::ok(rag.find_cycle());
}

// ----------------------------------------------------------------------------
// Built-in Deterministic Workloads
// ----------------------------------------------------------------------------
BankerState DeadlockEngine::get_banker_safe_workload() {
    // Canonical 5 processes, 3 resource types (A=10, B=5, C=7)
    BankerState s{};
    s.process_count = 5;
    s.resource_count = 3;
    s.available = {3, 3, 2};
    s.allocation = {
        {0, 1, 0}, // P0
        {2, 0, 0}, // P1
        {3, 0, 2}, // P2
        {2, 1, 1}, // P3
        {0, 0, 2}  // P4
    };
    s.max_claim = {
        {7, 5, 3}, // P0
        {3, 2, 2}, // P1
        {9, 0, 2}, // P2
        {2, 2, 2}, // P3
        {4, 3, 3}  // P4
    };
    auto need_res = BankerState::calculate_need(s.max_claim, s.allocation);
    s.need = need_res.value();
    return s;
}

BankerState DeadlockEngine::get_banker_unsafe_workload() {
    // Unsafe state: available resources are zeroed out while processes have non-zero needs
    BankerState s = get_banker_safe_workload();
    s.available = {0, 0, 0};
    return s;
}

DetectionState DeadlockEngine::get_detection_deadlock_workload() {
    // Canonical deadlock detection workload (Silberschatz Section 5.7)
    DetectionState s{};
    s.process_count = 5;
    s.resource_count = 3;
    s.available = {0, 0, 0};
    s.allocation = {
        {0, 1, 0}, // P0
        {2, 0, 0}, // P1
        {3, 0, 3}, // P2
        {2, 1, 1}, // P3
        {0, 0, 2}  // P4
    };
    s.request = {
        {0, 0, 0}, // P0
        {2, 0, 2}, // P1
        {0, 0, 1}, // P2
        {1, 0, 0}, // P3
        {0, 0, 2}  // P4
    };
    return s;
}

DetectionState DeadlockEngine::get_detection_free_workload() {
    // Detection state without deadlock: P2 has 0 outstanding requests, letting it finish and cascade
    DetectionState s = get_detection_deadlock_workload();
    s.request[2] = {0, 0, 0};
    return s;
}

ResourceAllocationGraph DeadlockEngine::get_rag_single_cycle_workload() {
    // Single-instance system with a directed cycle: P0 -> R0 -> P1 -> R1 -> P0
    ResourceAllocationGraph rag(2, 2);
    rag.set_resource_capacity(0, 1);
    rag.set_resource_capacity(1, 1);

    static_cast<void>(rag.add_assignment_edge(0, 0, 1)); // R0 assigned to P0
    static_cast<void>(rag.add_request_edge(0, 1, 1));    // P0 requests R1
    static_cast<void>(rag.add_assignment_edge(1, 1, 1)); // R1 assigned to P1
    static_cast<void>(rag.add_request_edge(1, 0, 1));    // P1 requests R0
    return rag;
}

ResourceAllocationGraph DeadlockEngine::get_rag_multi_cycle_workload() {
    // Multi-instance system with cycle but NO deadlock (Silberschatz Figure 5.3)
    // 4 processes (P0..P3), 2 resource types (R0 has 2 instances, R1 has 2 instances)
    ResourceAllocationGraph rag(4, 2);
    rag.set_resource_capacity(0, 2); // R0: 2 instances
    rag.set_resource_capacity(1, 2); // R1: 2 instances

    // P0 requests R0; R0 assigned to P1 and P2
    static_cast<void>(rag.add_request_edge(0, 0, 1));
    static_cast<void>(rag.add_assignment_edge(0, 1, 1));
    static_cast<void>(rag.add_assignment_edge(0, 2, 1));

    // P1 requests R1; R1 assigned to P0 and P3
    static_cast<void>(rag.add_request_edge(1, 1, 1));
    static_cast<void>(rag.add_assignment_edge(1, 0, 1));
    static_cast<void>(rag.add_assignment_edge(1, 3, 1));

    // Cycle exists: P0 -> R0 -> P1 -> R1 -> P0
    // But P2 and P3 can finish and release instances of R0 and R1!
    return rag;
}

ResourceAllocationGraph DeadlockEngine::get_rag_no_cycle_workload() {
    ResourceAllocationGraph rag(2, 2);
    rag.set_resource_capacity(0, 1);
    rag.set_resource_capacity(1, 1);
    static_cast<void>(rag.add_assignment_edge(0, 0, 1)); // R0 assigned to P0
    static_cast<void>(rag.add_request_edge(1, 0, 1));    // P1 requests R0
    static_cast<void>(rag.add_assignment_edge(1, 1, 1)); // R1 assigned to P1
    return rag;
}

// ----------------------------------------------------------------------------
// ASCII Formatters
// ----------------------------------------------------------------------------
std::string DeadlockEngine::format_banker_matrices(const BankerState& state) {
    std::ostringstream oss;
    oss << "Available Resources: [";
    for (size_t j = 0; j < state.resource_count; ++j) {
        oss << "R" << j << "=" << state.available[j] << (j + 1 < state.resource_count ? ", " : "]\n\n");
    }

    oss << "+---------+--------------------+--------------------+--------------------+\n"
        << "| Process |     Allocation     |     Max Claim      |   Remaining Need   |\n"
        << "+---------+--------------------+--------------------+--------------------+\n";

    for (size_t i = 0; i < state.process_count; ++i) {
        std::ostringstream alloc_ss, max_ss, need_ss;
        for (size_t j = 0; j < state.resource_count; ++j) {
            alloc_ss << state.allocation[i][j] << (j + 1 < state.resource_count ? " " : "");
            max_ss << state.max_claim[i][j] << (j + 1 < state.resource_count ? " " : "");
            need_ss << state.need[i][j] << (j + 1 < state.resource_count ? " " : "");
        }

        oss << "| P" << std::left << std::setw(6) << i
            << " | " << std::right << std::setw(18) << alloc_ss.str()
            << " | " << std::setw(18) << max_ss.str()
            << " | " << std::setw(18) << need_ss.str()
            << " |\n";
    }
    oss << "+---------+--------------------+--------------------+--------------------+\n";
    return oss.str();
}

std::string DeadlockEngine::format_safety_summary(const SafetyResult& result, size_t resource_count) {
    std::ostringstream oss;
    oss << "Banker's Safety Analysis:\n"
        << "  Status               : " << (result.is_safe ? "SAFE" : "UNSAFE") << "\n";
    if (result.is_safe) {
        oss << "  Safe Sequence        : <";
        for (size_t k = 0; k < result.safe_sequence.size(); ++k) {
            oss << "P" << result.safe_sequence[k] << (k + 1 < result.safe_sequence.size() ? " -> " : ">\n");
        }
    } else {
        oss << "  Unfinished Processes : [";
        for (size_t k = 0; k < result.unfinished_processes.size(); ++k) {
            oss << "P" << result.unfinished_processes[k] << (k + 1 < result.unfinished_processes.size() ? ", " : "]\n");
        }
    }

    oss << "  Final Work Vector    : [";
    for (size_t j = 0; j < resource_count; ++j) {
        oss << "R" << j << "=" << result.final_work[j] << (j + 1 < resource_count ? ", " : "]\n");
    }
    oss << "  Diagnostic           : " << result.diagnostic << "\n";
    return oss.str();
}

std::string DeadlockEngine::format_detection_matrices(const DetectionState& state) {
    std::ostringstream oss;
    oss << "Available Resources: [";
    for (size_t j = 0; j < state.resource_count; ++j) {
        oss << "R" << j << "=" << state.available[j] << (j + 1 < state.resource_count ? ", " : "]\n\n");
    }

    oss << "+---------+--------------------+--------------------+\n"
        << "| Process |     Allocation     | Current Request    |\n"
        << "+---------+--------------------+--------------------+\n";

    for (size_t i = 0; i < state.process_count; ++i) {
        std::ostringstream alloc_ss, req_ss;
        for (size_t j = 0; j < state.resource_count; ++j) {
            alloc_ss << state.allocation[i][j] << (j + 1 < state.resource_count ? " " : "");
            req_ss << state.request[i][j] << (j + 1 < state.resource_count ? " " : "");
        }

        oss << "| P" << std::left << std::setw(6) << i
            << " | " << std::right << std::setw(18) << alloc_ss.str()
            << " | " << std::setw(18) << req_ss.str()
            << " |\n";
    }
    oss << "+---------+--------------------+--------------------+\n";
    return oss.str();
}

std::string DeadlockEngine::format_detection_summary(const DetectionResult& result, size_t resource_count) {
    std::ostringstream oss;
    oss << "Deadlock Detection Summary:\n"
        << "  Deadlock Detected    : " << (result.is_deadlocked ? "YES" : "NO") << "\n";
    if (result.is_deadlocked) {
        oss << "  Deadlocked Processes : [";
        for (size_t k = 0; k < result.deadlocked_processes.size(); ++k) {
            oss << "P" << result.deadlocked_processes[k] << (k + 1 < result.deadlocked_processes.size() ? ", " : "]\n");
        }
    }
    oss << "  Final Work Vector    : [";
    for (size_t j = 0; j < resource_count; ++j) {
        oss << "R" << j << "=" << result.final_work[j] << (j + 1 < resource_count ? ", " : "]\n");
    }
    oss << "  Explanation          : " << result.explanation << "\n";
    return oss.str();
}

std::string DeadlockEngine::format_rag_summary(const ResourceAllocationGraph& rag, const RAGCycleResult& cycle) {
    std::ostringstream oss;
    oss << "Resource Allocation Graph Topology:\n"
        << "  Processes            : " << rag.process_count() << "\n"
        << "  Resource Types       : " << rag.resource_count() << " (";
    for (size_t j = 0; j < rag.resource_count(); ++j) {
        oss << "R" << j << ":" << rag.resource_capacity(j) << " inst" << (j + 1 < rag.resource_count() ? ", " : ")\n");
    }
    oss << "  System Model         : "
        << (cycle.is_single_instance_only ? "Single-Instance per Resource" : "Multi-Instance Resources") << "\n"
        << "  Cycle Detected       : " << (cycle.has_cycle ? "YES" : "NO") << "\n";

    if (cycle.has_cycle) {
        oss << "  Cycle Path           : ";
        for (size_t k = 0; k < cycle.cycle_path.size(); ++k) {
            oss << cycle.cycle_path[k] << (k + 1 < cycle.cycle_path.size() ? " -> " : "\n");
        }
    }
    oss << "  Theoretical Analysis : " << cycle.interpretation << "\n";
    return oss.str();
}

} // namespace kernox::sim
