# KERNOX 2.0: Android & Linux Resource Intelligence Platform
## Phase B: Process Intelligence Engine Specification & Verification Report

**Project:** KERNOX 2.0 — Android & Linux Resource Intelligence Platform
**Phase:** Phase B — Process Intelligence Engine Implementation
**Standard:** Modern C++20 (`-std=c++20`) | Target: Linux / POSIX (WSL2 / Ubuntu 24.04 LTS)
**Status:** COMPLETE & VERIFIED

---

## 1. Executive Summary & Objective

In **KERNOX 1.0**, process modeling within the user-space simulation engine (`kernox_sim`) was limited to `sim::ProcessTask`, which tracked minimal properties required for textbook CPU scheduling: `pid`, `arrival_time`, `burst_time`, and `priority`. It lacked memory footprints, execution lifecycle states, friendly identifiers, and mobile-inspired lifecycle importance.

**KERNOX 2.0 Phase B** implements the **Process Intelligence Engine** (`kernox::intel`), providing a rich, type-safe, and decoupled simulated process model. This engine serves as the foundational substrate for Android-inspired memory pressure detection and low-memory killer (LMKD) reclamation policies (Phase C).

Key achievements in Phase B:
1. **Decoupled Architecture:** CPU scheduling priority (`[0, 99]`, lower number = higher scheduling urgency) is decoupled from Android-inspired process importance (`[100, 500]`, lower number = higher protection against termination under memory pressure).
2. **PID Isolation:** Simulated processes use a dedicated `SimulatedPid` identifier generated sequentially from 1000 upwards, completely isolated from Linux host OS PIDs.
3. **Rigorous Lifecycle State Machine:** Enforces valid state transitions across `Created`, `Ready`, `Running`, `Blocked`, `Completed`, and `Terminated`, with terminal state immutability guarded via monadic `core::Result<T, Error>`.
4. **Memory Footprint Accounting:** Implements byte-level resident and peak memory accounting with arithmetic overflow and underflow protection, plus full memory reclamation primitives.
5. **CPU Scheduler Compatibility Adapter:** Provides `.to_task()` projection directly mapping `SimulatedProcess` into `sim::ProcessTask` for seamless scheduling via `sim::CpuScheduler` without altering existing simulation code.
6. **Triple-Preset Quality Verification:** Validated under `dev-debug`, `dev-release`, and `dev-asan` (AddressSanitizer + UndefinedBehaviorSanitizer) with 100% pass rates across all 17 CTest test suites.

---

## 2. Architectural Design & Namespace Isolation

The Process Intelligence Engine resides in its own isolated static library and header namespace:
- **Include Header:** `include/kernox/modules/intel/process_model.hpp`
- **Source Implementation:** `src/modules/intel/process_model.cpp`
- **CMake Target:** `kernox_intel` (links `kernox_core`, `kernox_sim`, and `kernox_compiler_flags`)
- **Namespace:** `kernox::intel`

### 2.1 Decoupling of CPU Priority and Mobile Importance

A key insight in mobile OS engineering (specifically Android) is that a process requiring high CPU responsiveness is not necessarily the process that should be spared during severe memory exhaustion, and vice versa:
- An invisible background compilation or photo-indexing worker may be granted high CPU scheduling priority (`cpu_priority = 0`) to complete quickly, yet it holds low importance (`ProcessImportance::Cached` or `Background`) and should be terminated immediately if physical RAM runs out.
- Conversely, a reading or messaging application currently in the foreground may require very little CPU (`cpu_priority = 90`), but holds top importance (`ProcessImportance::Foreground`) and must never be killed while the user is actively viewing it.

KERNOX 2.0 maintains this separation through distinct types, distinct ranges, and separate member fields.

---

## 3. Core Data Structures & Enumerations

### 3.1 Process Lifecycle States (`ProcessLifecycleState`)

```cpp
enum class ProcessLifecycleState : uint32_t {
    Created = 0, // Initialized but not yet admitted to ready queue
    Ready,       // Waiting to be dispatched to CPU
    Running,     // Actively executing on CPU
    Blocked,     // Awaiting I/O or external resource
    Completed,   // Task finished all burst requirements normally
    Terminated   // Aborted, cancelled, or reclaimed under memory pressure
};
```

### 3.2 Android-Inspired Importance Tiers (`ProcessImportance`)

Values mirror the conceptual hierarchy of Android's Low Memory Killer Daemon (LMKD):

| Tier | Enum Value | Score | Semantics & Termination Urgency |
|:---|:---|:---:|:---|
| **Foreground** | `ProcessImportance::Foreground` | 100 | Top activity with active user focus or direct user input. Protected from reclamation. |
| **Visible** | `ProcessImportance::Visible` | 200 | User is aware of the task (e.g., visible behind a modal dialog), but not focused. |
| **Service** | `ProcessImportance::Service` | 300 | Background process executing an ongoing operation noticeable to the user (e.g., music playback). |
| **Background** | `ProcessImportance::Background` | 400 | Typical background process running tasks invisible to the user. First line of normal reclamation. |
| **Cached** | `ProcessImportance::Cached` | 500 | Empty/cached process kept in memory for quick relaunch. Reclaimed first under memory pressure. |

### 3.3 Resource Footprint (`ProcessResourceFootprint`)

Tracks memory consumption uniformly in bytes (`uint64_t`):
- `memory_requirement_bytes`: Declared total capacity required by the process.
- `memory_consumed_bytes`: Current resident memory consumption.
- `peak_memory_bytes`: High-water mark of resident memory allocated over the process lifetime.

---

## 4. Formal Lifecycle State Machine

The state transitions strictly adhere to standard operating system state diagram rules:

```
              +-------------+
              |   Created   |
              +------+------+
                     | mark_ready()
                     v
+------------>+-------------+<-----------+
|             |    Ready    |            |
|             +------+------+            |
|                    | dispatch_running()|
|                    v                   |
| preempt_to_ready() +-------------+     | unblock_to_ready()
+--------------------+   Running   |     | (or mark_ready())
                     +---+----+----+     |
                         |    |          |
         complete()      |    | block()  |
         +---------------+    +----------+----+
         |                                    |
         v                                    v
  +---------------+                    +-------------+
  |   Completed   |                    |   Blocked   |
  +---------------+                    +------+------+
                                              |
     terminate() (From Created, Ready,        |
                  Running, or Blocked)        v
  +------------------------------------->+------------+
                                         | Terminated |
                                         +------------+
```

### 4.1 State Transition Matrix

| Initial State | Invoked Method | Target State | Invariant Checks & Preconditions |
|:---|:---|:---:|:---|
| **Created** | `mark_ready()` | `Ready` | Transition allowed. |
| **Created** | `terminate(reason, ts, pressure)` | `Terminated` | Records termination reason, timestamp, and pressure flag. |
| **Ready** | `dispatch_running(ts)` | `Running` | Updates `last_active_time_ticks = ts`. |
| **Ready** | `terminate(reason, ts, pressure)` | `Terminated` | Records termination metrics. |
| **Running** | `preempt_to_ready()` | `Ready` | CPU yield or quantum expiration. |
| **Running** | `block(reason)` | `Blocked` | I/O wait. |
| **Running** | `complete(ts)` | `Completed` | Sets `cpu_burst_remaining = 0`, records completion time. |
| **Running** | `terminate(reason, ts, pressure)` | `Terminated` | Records termination metrics. |
| **Blocked** | `unblock_to_ready()` | `Ready` | Resource fulfilled. |
| **Blocked** | `mark_ready()` | `Ready` | Equivalent transition to `unblock_to_ready()`. |
| **Blocked** | `terminate(reason, ts, pressure)` | `Terminated` | Process terminated while waiting. |
| **Completed** | *Any transition* | *Rejected* | Returns `core::ErrorCode::InvalidState`. |
| **Terminated** | *Any transition* | *Rejected* | Returns `core::ErrorCode::InvalidState`. |

---

## 5. Memory Accounting Semantics

Memory accounting is executed strictly in units of bytes using `uint64_t`:

1. **Incremental Allocation (`allocate_memory(bytes)`):**
   - Validates `bytes > 0`.
   - Protects against arithmetic overflow: `memory_consumed_bytes > UINT64_MAX - bytes`.
   - Automatically raises `peak_memory_bytes` when new peak is attained.
   - Rejects allocation if process is in terminal state (`Completed` or `Terminated`).

2. **Incremental Release (`release_memory(bytes)`):**
   - Validates `bytes > 0`.
   - Protects against underflow: rejects release if `bytes > memory_consumed_bytes`.
   - Reduces `memory_consumed_bytes` while preserving `peak_memory_bytes` high-water mark.

3. **Absolute Update (`update_memory_consumption(total_bytes)`):**
   - Replaces current resident consumption with `total_bytes`.
   - Updates `peak_memory_bytes` if `total_bytes` exceeds current peak.
   - Rejects update if process is terminal.

4. **Reclamation Primitive (`reclaim_all_memory()`):**
   - Zeroes `memory_consumed_bytes` and returns the exact number of reclaimed bytes.
   - Preserves `peak_memory_bytes` and `memory_requirement_bytes` for post-mortem analytics.
   - Designed specifically for the Phase C `MemoryReclamationEngine`.

---

## 6. CPU Scheduling Interoperability Adapter

`SimulatedProcess` integrates seamlessly with `kernox::sim::CpuScheduler` via its `.to_task()` projection method:

```cpp
sim::ProcessTask SimulatedProcess::to_task() const noexcept {
    return sim::ProcessTask{
        .pid = pid_,
        .arrival_time = creation_time_ticks_,
        .burst_time = cpu_burst_remaining_,
        .priority = cpu_priority_
    };
}
```

This guarantees:
- Compatibility with all existing CPU scheduling algorithms (`FCFS`, `SJF`, `SRTF`, `RoundRobin`, `Priority`, `MLFQ`).
- Zero modification to existing `CpuScheduler` source code.
- Dynamic burst updates: as `consume_cpu_time()` is called on a running process, `.to_task()` reflects the decreased remaining burst time.

---

## 7. Automated Test Suite (`tests/test_process_intel.cpp`)

The test suite exercises all requirements through 9 comprehensive test modules:

1. `test_process_creation_and_pid_allocation`: Verifies atomic/sequential PID allocation, deterministic generator reset (`reset_pid_generator`), and default attribute initialization.
2. `test_parameter_validation_and_rejections`: Validates rejections for empty names, whitespace-only names, priority > 99, burst = 0, memory = 0, and verifies whitespace trimming.
3. `test_lifecycle_standard_happy_paths`: Exercises normal lifecycle paths: admission, execution, preemption, blocking, unblocking, and completion.
4. `test_lifecycle_invalid_transitions`: Proves that all illegal state jumps and attempts to transition out of terminal states fail with `ErrorCode::InvalidState`.
5. `test_termination_and_under_pressure_flag`: Verifies termination from Created, Ready, Running, and Blocked states, testing the `was_killed_under_pressure` flag and termination reason recording.
6. `test_memory_accounting_and_invariants`: Tests allocation, release underflow protection, peak tracking, 64-bit integer overflow protection, and terminal process allocation rejection.
7. `test_cpu_priority_and_importance_orthogonality`: Proves complete independence between CPU priority (0–99) and Android lifecycle importance (100–500).
8. `test_cpu_burst_consumption_and_task_projection`: Verifies execution tick consumption, overshoot clamping, `.to_task()` projection, and schedules projected tasks through `sim::CpuScheduler::run_fcfs()`.
9. `test_string_conversions_and_formatting`: Validates `to_string()` for all lifecycle states, importance tiers, and process summary strings.

---

## 8. Verification Results

All 17 automated test suites and CLI smoke tests pass across all standard CMake build presets:

| CMake Preset | Compiler Flags | Sanitizers | Test Suites Passed | Overall Result |
|:---|:---|:---:|:---:|:---:|
| `dev-debug` | `-O0 -g -Wall -Wextra -Werror` | None | 17 / 17 (100%) | **PASS** |
| `dev-release` | `-O3 -DNDEBUG -Wall -Wextra -Werror` | None | 17 / 17 (100%) | **PASS** |
| `dev-asan` | `-O1 -g -fsanitize=address,undefined` | ASan + UBSan | 17 / 17 (100%) | **PASS** |

### Test Registration Index

```
 1/17: test_result
 2/17: test_timer
 3/17: test_unique_fd
 4/17: test_child_process
 5/17: test_cli
 6/17: test_cpu_sched
 7/17: test_disk_sched
 8/17: test_deadlock
 9/17: test_memory
10/17: test_telemetry
11/17: test_process_manager
12/17: test_ipc
13/17: test_shm
14/17: test_sync
15/17: test_unix_socket
16/17: test_message_queue
17/17: test_process_intel  <-- NEW (Phase B)
```

---

## 9. Contract for Phase C (Memory Pressure & Reclamation Engine)

Phase B provides all necessary primitives for Phase C implementation:
- `importance()` enables sorting and partitioning processes into LMK victim selection candidate pools.
- `memory().memory_consumed_bytes` and `reclaim_all_memory()` allow the memory reclamation engine to simulate freeing physical memory upon victim termination.
- `last_active_time_ticks()` provides the timestamp required for LRU reclamation policies.
- `creation_time_ticks()` provides the timestamp required for FIFO reclamation policies.
- `cpu_priority()` allows comparative benchmarking against traditional priority-based termination.
- `terminate(reason, timestamp, true)` records that a process was killed under memory pressure.
