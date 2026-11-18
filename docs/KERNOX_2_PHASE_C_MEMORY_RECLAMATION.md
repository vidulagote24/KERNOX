# KERNOX 2.0: Android & Linux Resource Intelligence Platform
## Phase C: Memory Pressure & Reclamation Engine Specification & Verification Report

**Project:** KERNOX 2.0 — Android & Linux Resource Intelligence Platform
**Phase:** Phase C — Memory Pressure & Reclamation Engine
**Standard:** Modern C++20 (`-std=c++20`) | Target: Linux / POSIX (WSL2 / Ubuntu 24.04 LTS)
**Status:** COMPLETE & VERIFIED

---

## 1. Executive Summary & Objective

In **KERNOX 1.0**, memory simulation was limited to single-workload contiguous allocation strategies (First Fit, Best Fit, Worst Fit, Next Fit) and virtual memory paging frame replacement (FIFO, LRU, Optimal, Working Set). It did not model system-wide global memory exhaustion, multi-tier pressure transitions, or mobile-inspired process reclamation.

**KERNOX 2.0 Phase C** introduces the **Memory Pressure & Reclamation Engine** (`kernox::reclaim`), an operating systems simulation layer modeling mobile low-memory killer (LMKD) dynamics.

Key achievements in Phase C:
1. **Configurable Memory Pressure Detector (`MemoryPressureDetector`):** Evaluates aggregate memory consumption against validated thresholds (`Normal < 75%`, `Moderate [75%, 90%)`, `Critical >= 90%`), supporting custom configurations and overcommit scenarios without division-by-zero or overflow hazards.
2. **Global System Memory Accounting (`MemoryReclamationEngine`):** Tracks total capacity, current consumption, and available memory in bytes uniformly, enforcing strict capacity constraints and safe allocation/release without conflicting with process-level source of truth.
3. **Pluggable Policy Abstraction (`IReclamationPolicy`):** Clean strategy interface receiving candidate process pools and selecting single victims deterministically without modifying process state directly.
4. **Four Diverse Reclamation Policies:**
   - **FIFO:** Reclaims oldest resident process by creation timestamp with deterministic PID tie-breaking.
   - **LRU:** Reclaims least recently active process by last execution timestamp.
   - **Priority-Based:** Reclaims lowest CPU scheduling urgency (highest numeric CPU priority 0–99).
   - **Android-Inspired Importance-Aware:** Shields Foreground processes and reclaims through mobile importance tiers (`Cached [500] -> Background [400] -> Service [300] -> Visible [200] -> Foreground [100]`), breaking ties by largest resident memory footprint.
5. **Multi-Step Engine Orchestration:** Coordinates iterative victim selection, validation, memory reclamation, lifecycle transition to `Terminated` (`was_killed_under_pressure = true`), and structured metrics reporting.
6. **Snapshot Cloning for Experiments:** Engine provides `.clone()` deep-copy semantics enabling Phase D comparative benchmarks on identical process populations.
7. **Triple-Preset Quality Verification:** 100% pass rate across all 18 test suites under `dev-debug`, `dev-release`, and `dev-asan` (AddressSanitizer + UndefinedBehaviorSanitizer).

---

## 2. Relationship to Existing Memory Subsystems

It is critical to distinguish between KERNOX's two memory subsystems:

| Dimension | `kernox::sim::ContiguousMemoryManager` (Phase 2/3) | `kernox::reclaim::MemoryReclamationEngine` (Phase C) |
|:---|:---|:---|
| **Domain** | Micro-level physical address space partitioning | Macro-level system memory pressure & process lifecycle |
| **Focus** | Hole allocation, splitting, coalescing, fragmentation | Aggregate memory utilization, threshold transitions, LMK |
| **Algorithms** | First Fit, Best Fit, Worst Fit, Next Fit | FIFO, LRU, CPU-Priority, Importance-Aware (LMKD) |
| **Unit of Action** | Block of contiguous address space (`[start, start+size)`) | Entire application process lifecycle termination |
| **Mobile Context** | Traditional desktop/server partition compaction | Modern mobile OS without swap (Android memory management) |

These subsystems are complementary: the reclamation engine resolves global system shortfalls by selecting and terminating victim processes, which frees aggregate memory capacity.

---

## 3. Architecture & Core Components

```
+---------------------------------------------------------------------------------+
|                                 kernox::reclaim                                 |
+---------------------------------------------------------------------------------+
|                                                                                 |
|  +----------------------------+             +--------------------------------+  |
|  |   MemoryPressureDetector   |             |       IReclamationPolicy       |  |
|  |----------------------------|             |--------------------------------|  |
|  | - Total Capacity           |             | + select_victim(candidates)    |  |
|  | - Normal / Moderate / Crit |             +---------------+----------------+  |
|  | - Integer Boundary Checks  |                             ^                   |
|  +--------------+-------------+                             |                   |
|                 ^                   +-----------------------+----------------+  |
|                 |                   |           |           |                |  |
|                 |                 +-+-+       +-+-+       +-+-+            +-+-+|
|                 |                 |FIFO|      |LRU|       |Prio|     |Importance|
|                 |                 +---+       +---+       +---+      |  -Aware  |
|                 |                                                    +----------+
|  +--------------+------------------------------------------------------------+  |
|  |                          MemoryReclamationEngine                          |  |
|  |---------------------------------------------------------------------------|  |
|  | - tracks aggregate capacity, consumed, and available bytes                |  |
|  | - manages registered SimulatedProcess instances                           |  |
|  | - executes iterative reclamation to byte or pressure target               |  |
|  | - records ReclamationReport with structured ReclamationDecision logs      |  |
|  +---------------------------------------------------------------------------+  |
+---------------------------------------------------------------------------------+
```

---

## 4. Component Details

### 4.1 Memory Pressure Detection (`MemoryPressureDetector`)

- **Calculation Method:** Threshold boundaries use integer arithmetic for byte comparisons, complemented by floating-point ratios for diagnostic reporting:
  - `moderate_limit_bytes = static_cast<uint64_t>(total_memory_bytes * moderate_threshold_ratio)`
  - `critical_limit_bytes = static_cast<uint64_t>(total_memory_bytes * critical_threshold_ratio)`
- **Classification Rules:**
  - `used_bytes >= critical_limit_bytes` $\rightarrow$ **`Critical`**
  - `used_bytes >= moderate_limit_bytes` $\rightarrow$ **`Moderate`**
  - `used_bytes < moderate_limit_bytes` $\rightarrow$ **`Normal`**
- **Overcommit Support:** If `used_bytes > total_memory_bytes`, `detect()` safely returns `Critical`, and `utilization_ratio()` returns values $> 1.0$ (e.g. $1.2$ for $120\%$) without overflow.
- **Relief Metrics:**
  - `bytes_to_reach_normal(used)`: Bytes to drop strictly below the moderate threshold.
  - `bytes_to_reach_moderate(used)`: Bytes to drop strictly below the critical threshold.

### 4.2 Pluggable Policies (`IReclamationPolicy`)

All policies implement:
```cpp
[[nodiscard]] virtual std::optional<VictimSelection> select_victim(
    const std::vector<const intel::SimulatedProcess*>& candidates
) const = 0;
```

#### Policy A: FIFO (`FifoReclamationPolicy`)
- **Criteria:** Process with minimum `creation_time_ticks()`.
- **Tie-Breaking:** Smaller `SimulatedPid`.
- **Semantics:** Models naive time-in-system queue purging.

#### Policy B: LRU (`LruReclamationPolicy`)
- **Criteria:** Process with minimum `last_active_time_ticks()`.
- **Tie-Breaking:** Earlier `creation_time_ticks()`, then smaller `SimulatedPid`.
- **Semantics:** Reclaims tasks that have remained idle or unfocused longest.

#### Policy C: Priority-Based (`PriorityReclamationPolicy`)
- **Criteria:** Process with maximum numeric `cpu_priority()` ($0 \le p \le 99$). Larger numbers indicate lower CPU scheduling urgency, making them the preferred victims.
- **Tie-Breaking:** Earlier `creation_time_ticks()`, then smaller `SimulatedPid`.
- **Semantics:** Classical OS simulation policy where CPU-heavy, low-urgency batch jobs are culled first. CPU priority is strictly preserved on the process object post-mortem.

#### Policy D: Android-Inspired Importance-Aware (`ImportanceAwareReclamationPolicy`)
- **Criteria:** Process with maximum `ProcessImportance` numeric score:
  $$\text{Cached (500)} \rightarrow \text{Background (400)} \rightarrow \text{Service (300)} \rightarrow \text{Visible (200)} \rightarrow \text{Foreground (100)}$$
- **Foreground Protection:** Configured with `min_protected_importance` (default `Foreground = 100`). Any process with $\text{score} \le \text{min\_protected}$ is shielded and never selected.
- **Tie-Breaking within Tier:**
  1. Largest `memory_consumed_bytes` (frees the biggest memory hog in that tier).
  2. Least recently active (`last_active_time_ticks`).
  3. Smaller `SimulatedPid`.
- **Safety:** If only protected processes remain, returns `std::nullopt`, triggering `ReclamationStopReason::NoEligibleVictim` without culling protected tasks.

---

## 5. Memory Accounting & Invariant Protection

To prevent dual sources of truth:
- **Process Memory Truth:** Each `SimulatedProcess` owns its resident footprint (`proc.memory().memory_consumed_bytes`).
- **System Memory Truth:** `MemoryReclamationEngine` tracks `total_capacity_bytes_` and `current_consumed_bytes_`.
- **Synchronization Invariants:**
  1. `allocate_process_memory(pid, bytes)` checks `bytes <= available_bytes()`, invokes `proc->allocate_memory(bytes)`, and increments `current_consumed_bytes_`.
  2. `release_process_memory(pid, bytes)` invokes `proc->release_memory(bytes)` (which checks underflow), and decrements `current_consumed_bytes_`.
  3. `reclaim_all_memory()` zeroes process resident memory, decrements `current_consumed_bytes_`, and transitions the process to `Terminated` (`was_killed_under_pressure = true`).
  4. Capacity overflow or process double-registration is rejected with explicit `core::ErrorCode` returns.

---

## 6. Reclamation Engine Execution Workflow

```
[Start Reclaim Request]
        |
        v
Check Initial State Satisfied? ----(Yes)----> [Return InitialStateSatisfied]
        |
      (No)
        |
        v
+-------------------------------+
| Loop: Check Target Achieved?  |----(Yes)----> [Return TargetAchieved]
+---------------+---------------+
                |
              (No)
                |
                v
Filter Eligible Candidates (alive && resident > 0)
                |
        Candidates Empty? -----------(Yes)----> [Return NoEligibleVictim / PoolExhausted]
                |
              (No)
                |
                v
Policy::select_victim(candidates)
                |
         Victim Selected? -----------(No)-----> [Return NoEligibleVictim (Protected)]
                |
              (Yes)
                |
                v
1. Reclaim Memory: uint64_t freed = proc->reclaim_all_memory()
2. Update Accounting: current_consumed -= freed
3. Terminate Process: proc->terminate(reason, tick, true)
4. Record Decision in Report
                |
                +----> [Repeat Loop]
```

---

## 7. Automated Test Suite (`tests/test_reclamation.cpp`)

Covered across 8 comprehensive test modules:

1. `test_pressure_detector`: Validates zero capacity rejection, threshold ratio range checks, boundary comparisons (74.9% vs 75.0% vs 89.9% vs 90.0%), overcommit handling, and relief byte calculations.
2. `test_memory_accounting`: Tests allocation within capacity, exceeding capacity rejection (`ResourceExhausted`), safe release, underflow release rejection, and unregistration capacity adjustments.
3. `test_fifo_policy`: Validates oldest resident process selection, deterministic PID tie-breaking, and exclusion of dead/empty tasks.
4. `test_lru_policy`: Validates least recently active selection, activity updates, and multi-tier tie-breaking.
5. `test_priority_policy`: Validates largest numeric CPU priority victim ordering, CPU priority immutability, and deterministic tie-breaking.
6. `test_importance_aware_policy`: Tests Cached $\rightarrow$ Background $\rightarrow$ Service $\rightarrow$ Visible ordering, Foreground protection, and configurable Visible protection.
7. `test_reclamation_engine_multi_step`: Exercises multi-step reclamation under Critical pressure, reducing pressure to Normal, target byte satisfaction, victim state verification, and safe stop when only protected processes remain.
8. `test_policy_comparison_cloning`: Deep-clones identical 4-process engines and proves that FIFO, LRU, Priority, and Importance-Aware select completely different victims from identical initial inputs.

---

## 8. Verification Results

All 18 automated test suites pass across all standard CMake build presets:

| CMake Preset | Compiler Flags | Sanitizers | Test Suites Passed | Overall Result |
|:---|:---|:---:|:---:|:---:|
| `dev-debug` | `-O0 -g -Wall -Wextra -Werror` | None | 18 / 18 (100%) | **PASS** |
| `dev-release` | `-O3 -DNDEBUG -Wall -Wextra -Werror` | None | 18 / 18 (100%) | **PASS** |
| `dev-asan` | `-O1 -g -fsanitize=address,undefined` | ASan + UBSan | 18 / 18 (100%) | **PASS** |

### Test Registration Index (`ctest`)

```
 1/18: test_result                  [Passed]
 2/18: test_timer                   [Passed]
 3/18: test_unique_fd               [Passed]
 4/18: test_child_process           [Passed]
 5/18: test_cli                     [Passed]
 6/18: test_cpu_sched               [Passed]
 7/17: test_disk_sched              [Passed]
 8/18: test_deadlock                [Passed]
 9/18: test_memory                  [Passed]
10/18: test_telemetry               [Passed]
11/18: test_process_manager         [Passed]
12/18: test_ipc                     [Passed]
13/18: test_shm                     [Passed]
14/18: test_sync                    [Passed]
15/18: test_unix_socket             [Passed]
16/18: test_message_queue           [Passed]
17/18: test_process_intel           [Passed] (Phase B)
18/18: test_reclamation             [Passed] <-- (NEW Phase C)
```

---

## 9. Contract for Phase D (Workload & Experiment Engine)

Phase C establishes a clean interface for Phase D:
1. **Engine Snapshot Cloning (`engine.clone()`):** Phase D can generate a synthetic process population once, clone the engine 4 times, and feed each clone to a different policy under identical shortfalls.
2. **Structured Reports (`ReclamationReport`):** Provides detailed metrics (initial/final pressure, initial/final used, actual reclaimed, decisions vector) easily serializable to JSON/CSV in Phase D/E.
3. **Pluggable Policy Injection:** Phase D benchmark runners invoke `engine.reclaim_bytes(policy, target)` or `engine.reclaim_to_pressure(policy, target_pressure)` dynamically.
