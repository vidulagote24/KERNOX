# Deadlock Detection & Resource Allocation Simulation Engine

KERNOX provides an academic, discrete-event Deadlock Detection and Resource Allocation simulation engine implemented in modern C++20. The module models Coffman conditions, Resource Allocation Graph (RAG) cycle analysis, Edsger Dijkstra's Banker's Safety and Resource-Request algorithms, and multiple-instance Deadlock Detection.

---

## 1. Theoretical Foundations & Deadlock Conditions

### 1.1 The Four Coffman Conditions
A deadlock can arise if and only if the following four conditions hold simultaneously in a system:
1. **Mutual Exclusion**: At least one resource must be held in a non-shareable mode (only one process can use the resource at any given time).
2. **Hold and Wait**: A process must be holding at least one resource and waiting to acquire additional resources that are currently being held by other processes.
3. **No Preemption**: Resources cannot be preempted; a resource can only be released voluntarily by the process holding it after completion of its task.
4. **Circular Wait**: A closed chain of processes $\{P_0, P_1, \dots, P_n\}$ exists such that $P_0$ is waiting for a resource held by $P_1$, $P_1$ is waiting for a resource held by $P_2$, $\dots$, and $P_n$ is waiting for a resource held by $P_0$.

### 1.2 System Characterization: Prevention vs. Avoidance vs. Detection
- **Deadlock Prevention**: Structurally invalidating at least one of the four Coffman conditions (e.g., total resource ordering to eliminate circular wait).
- **Deadlock Avoidance (Banker's Algorithm)**: Dynamically inspecting every resource request and granting it if and only if the resulting system state remains **Safe**.
- **Deadlock Detection**: Periodically inspecting current allocations and outstanding requests to identify processes that are deadlocked and cannot proceed.

---

## 2. Resource Allocation Graph (RAG) Modeling & Cycle Analysis

### 2.1 Graph Definition
A directed graph $G = (V, E)$ where vertices $V$ are partitioned into two disjoint sets:
- **Process Nodes**: $P = \{P_0, P_1, \dots, P_{n-1}\}$
- **Resource Nodes**: $R = \{R_0, R_1, \dots, R_{m-1}\}$, each with instance capacity $C_j \ge 1$

Edges $E$ represent states of dependency:
- **Request Edge ($P_i \to R_j$)**: Process $P_i$ has requested an instance of resource type $R_j$ and is currently waiting.
- **Assignment Edge ($R_j \to P_i$)**: An instance of resource type $R_j$ has been allocated to process $P_i$.

### 2.2 Cycle Analysis: Single vs. Multi-Instance Resources

> [!IMPORTANT]
> **Fundamental Theorem of Resource Allocation Graphs**:
> 1. **Single-Instance Resources ($C_j = 1$ for all $j$)**:
>    A directed cycle in the Resource Allocation Graph is **necessary and sufficient** for deadlock. A cycle directly proves that a circular wait exists and the system is deadlocked.
> 2. **Multiple-Instance Resources ($\exists j: C_j > 1$)**:
>    A directed cycle is **necessary but NOT sufficient** for deadlock. A cycle indicates that a deadlock *may* exist. However, if processes holding instances of the cycled resources are outside the cycle and can run to completion, they will release their instances and break the cycle.

---

## 3. Banker's Algorithm (Deadlock Avoidance)

### 3.1 State Representation
For $n$ processes and $m$ resource types:
- **Available $[m]$**: Vector of available instances for each resource type.
- **Max Claim $[n \times m]$**: Maximum declared demand of each process.
- **Allocation $[n \times m]$**: Currently allocated instances to each process.
- **Need $[n \times m]$**: Remaining demand required to complete:
  $$\text{Need}[i][j] = \text{Max}[i][j] - \text{Allocation}[i][j]$$

### 3.2 State Invariants & Consistency
The system state is valid if and only if:
1. $\text{Allocation}[i][j] \le \text{Max}[i][j]$ for all $i, j$.
2. $\text{Available}[j] + \sum_{i=0}^{n-1} \text{Allocation}[i][j] = \text{Total}[j]$ without arithmetic overflow.

### 3.3 Safety Algorithm (Dijkstra)
1. Initialize $\text{Work} = \text{Available}$.
2. Initialize $\text{Finish}[i] = \text{false}$ for $i = 0, \dots, n-1$.
3. Find an index $i$ such that:
   $$\text{Finish}[i] == \text{false} \quad \text{and} \quad \text{Need}[i][j] \le \text{Work}[j] \;\; \forall j \in [0, m-1]$$
   *(Tie-breaking: Lowest process index $i$ selected first for deterministic execution)*.
4. If such $i$ is found:
   $$\text{Work} = \text{Work} + \text{Allocation}[i]$$
   $$\text{Finish}[i] = \text{true}$$
   Append $P_i$ to $\text{SafeSequence}$. Return to Step 3.
5. If $\text{Finish}[i] == \text{true}$ for all $i$, the state is **SAFE**. Otherwise, the state is **UNSAFE**.

> [!WARNING]
> **Safe vs. Unsafe vs. Deadlock**:
> - A **Safe state** is NOT deadlocked, and guarantees that there exists at least one order of execution where all processes finish even if all simultaneously request their maximum claims.
> - An **Unsafe state** is NOT necessarily deadlocked. It simply means the system cannot guarantee completion without preemption if all processes assert their worst-case demand.

### 3.4 Resource-Request Algorithm (Transactional Semantics)
When process $P_i$ makes a request vector $\text{Request}_i$:
1. **Claim Check**: If $\text{Request}_i[j] > \text{Need}[i][j]$ for any $j$, return `ExceededClaim` error.
2. **Availability Check**: If $\text{Request}_i[j] > \text{Available}[j]$ for any $j$, return `InsufficientAvailable` ($P_i$ must wait).
3. **Tentative Allocation**:
   $$\text{Available}' = \text{Available} - \text{Request}_i$$
   $$\text{Allocation}'[i] = \text{Allocation}[i] + \text{Request}_i$$
   $$\text{Need}'[i] = \text{Need}[i] - \text{Request}_i$$
4. **Safety Verification**: Run the Safety Algorithm on the tentative state.
   - If safe: Commit the transaction, update system state, return `Granted`.
   - If unsafe: Roll back tentative state, restore original state unmodified, return `UnsafeState`.

---

## 4. Deadlock Detection Algorithm (Multiple Instances)

When processes do not declare maximum claims in advance, the system tracks current outstanding requests $\text{Request}[n \times m]$:
1. Initialize $\text{Work} = \text{Available}$.
2. Initialize $\text{Finish}[i]$:
   $$\text{Finish}[i] = \begin{cases} \text{true} & \text{if } \text{Allocation}[i][j] == 0 \; \forall j \\ \text{false} & \text{otherwise} \end{cases}$$
3. Find an index $i$ such that:
   $$\text{Finish}[i] == \text{false} \quad \text{and} \quad \text{Request}[i][j] \le \text{Work}[j] \;\; \forall j$$
4. If such $i$ is found:
   $$\text{Work} = \text{Work} + \text{Allocation}[i]$$
   $$\text{Finish}[i] = \text{true}$$
   Return to Step 3.
5. If $\text{Finish}[i] == \text{false}$ for any $i$, then process $P_i$ is **DEADLOCKED**.

---

## 5. Worked Numerical Examples for OS Examiners

### 5.1 Canonical Banker's Safety Example
Consider a system with 5 processes ($P_0 \dots P_4$) and 3 resource types ($A, B, C$ with total instances $10, 5, 7$):

#### Initial State
$$\text{Available} = [3, 3, 2]$$

| Process | Allocation ($A, B, C$) | Max Claim ($A, B, C$) | Remaining Need ($A, B, C$) |
| :---: | :---: | :---: | :---: |
| $P_0$ | $0, 1, 0$ | $7, 5, 3$ | $7, 4, 3$ |
| $P_1$ | $2, 0, 0$ | $3, 2, 2$ | $1, 2, 2$ |
| $P_2$ | $3, 0, 2$ | $9, 0, 2$ | $6, 0, 0$ |
| $P_3$ | $2, 1, 1$ | $2, 2, 2$ | $0, 1, 1$ |
| $P_4$ | $0, 0, 2$ | $4, 3, 3$ | $4, 3, 1$ |

#### Step-by-Step Manual Execution:
1. $\text{Work} = [3, 3, 2]$.
   - Check $P_0$: $\text{Need} = [7, 4, 3] \le [3, 3, 2]$? False.
   - Check $P_1$: $\text{Need} = [1, 2, 2] \le [3, 3, 2]$? **True!**
   - $P_1$ finishes: $\text{Work} = [3, 3, 2] + [2, 0, 0] = [5, 3, 2]$.
2. $\text{Work} = [5, 3, 2]$.
   - Check $P_0$: $[7, 4, 3] \le [5, 3, 2]$? False.
   - Check $P_2$: $[6, 0, 0] \le [5, 3, 2]$? False.
   - Check $P_3$: $[0, 1, 1] \le [5, 3, 2]$? **True!**
   - $P_3$ finishes: $\text{Work} = [5, 3, 2] + [2, 1, 1] = [7, 4, 3]$.
3. $\text{Work} = [7, 4, 3]$.
   - Check $P_0$: $[7, 4, 3] \le [7, 4, 3]$? **True!**
   - $P_0$ finishes: $\text{Work} = [7, 4, 3] + [0, 1, 0] = [7, 5, 3]$.
4. $\text{Work} = [7, 5, 3]$.
   - Check $P_2$: $[6, 0, 0] \le [7, 5, 3]$? **True!**
   - $P_2$ finishes: $\text{Work} = [7, 5, 3] + [3, 0, 2] = [10, 5, 5]$.
5. $\text{Work} = [10, 5, 5]$.
   - Check $P_4$: $[4, 3, 1] \le [10, 5, 5]$? **True!**
   - $P_4$ finishes: $\text{Work} = [10, 5, 5] + [0, 0, 2] = [10, 5, 7]$.

**Result**: State is **SAFE**. Safe Sequence: $\langle P_1 \to P_3 \to P_0 \to P_2 \to P_4 \rangle$. Final Work equals total resources $[10, 5, 7]$.

---

## 6. CLI Command Guide

```bash
# 1. Run Banker's Safety Algorithm
kernox deadlock banker --workload safe
kernox deadlock banker --workload unsafe

# 2. Run Deadlock Detection on current requests
kernox deadlock detect --workload detection-demo
kernox deadlock detect --workload detection-free

# 3. Analyze Resource Allocation Graph
kernox deadlock graph --workload cycle-demo     # Single-instance cycle: Definite Deadlock
kernox deadlock graph --workload cycle-multi    # Multi-instance cycle: Warning only
kernox deadlock graph --workload no-cycle       # Deadlock-free

# 4. Resource Request Transactions
# Grantable request (system remains safe)
kernox deadlock request --workload safe --process 1 --request 1,0,2

# Rejected request (would lead to unsafe state)
kernox deadlock request --workload safe --process 4 --request 3,3,0
```

---

## 7. Assumptions & Known Limitations

1. **Static Maximum Claims**: Banker's algorithm assumes processes accurately know and declare their maximum resource claims upfront. In general-purpose desktop/server OSs (like Linux), dynamic user processes rarely know maximum memory or file descriptor needs in advance.
2. **Independent Processes**: Processes are assumed to execute independently without synchronization order constraints beyond resource access.
3. **Finite Execution Time**: Assumes that once a process acquires all requested resources, it runs to completion within finite time and releases all allocated resources.

---

## 8. Correctness Audit & Verification Findings (Phase 3C.1)

A systematic correctness audit was conducted across all Deadlock module components:
1. **Banker's Algorithm State & Safety**:
   - **Incomplete Safe-Sequence Clearing**: In `run_banker_safety()`, if an execution stalled partially after servicing some processes (e.g., $k < n$), the partial sequence was previously left in `safe_sequence`. Under strict avoidance semantics, an unsafe state has no valid safe sequence; `safe_sequence` is now explicitly cleared whenever `is_safe == false`.
   - **Zero-Need Invariants**: Verified that processes with zero remaining need are prioritized under lowest-index tie-breaking, releasing their allocation to `Work` and unblocking subsequent processes.
2. **Transactional Requests**:
   - Proved state invariance for all rejected request classes: `ExceededClaim`, `InsufficientAvailable`, and `UnsafeState`. If a request is rejected, `resulting_state` retains the exact original state vector and matrix entries, with zero partially applied mutations surviving.
3. **Multiple-Instance Deadlock Detection**:
   - **DetectionState Overflow Check**: In `DetectionState::validate()`, added arithmetic overflow protection for total resource count $\text{Available}[j] + \sum_i \text{Allocation}[i][j] > \text{UINT32\_MAX}$ to eliminate potential `Work` vector overflow.
   - **Process Classification**: Verified that processes holding no allocations are initialized as finished ($\text{Finish}[i] = \text{true}$), preventing non-holding blocked processes from falsely being reported as deadlocked.
4. **Resource Allocation Graph (RAG) Analysis**:
   - **Cycle Resource Classification**: Refined `RAGCycleResult` with `cycle_is_single_instance`. Even in graphs containing multi-instance resources elsewhere, if all resource nodes *participating in the cycle path* have capacity 1, the cycle is classified as a **Definite Deadlock**.
   - **Capacity Bounds Enforcement**: Added immediate validation to `add_assignment_edge` preventing allocations exceeding capacity, and enforced $C_j > 0$ in `ResourceAllocationGraph::validate()`.
