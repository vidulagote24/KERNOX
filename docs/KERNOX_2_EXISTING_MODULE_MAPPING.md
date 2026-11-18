# KERNOX 2.0: Existing Module Mapping & Integration Matrix

**Project:** KERNOX 2.0 — Android & Linux Resource Intelligence Platform
**Baseline Repository:** KERNOX 1.0 (Commit `d5c0f88`)
**Standard:** Modern C++20 | Target: Linux / POSIX (WSL2 / Ubuntu 24.04 LTS)
**Status:** Architectural Discovery & Module Inventory (Phase A Complete)

---

## 1. Mapping Overview

This document provides a comprehensive mapping of all existing KERNOX 1.0 source files, headers, and CMake targets against the planned capabilities of KERNOX 2.0. It identifies:
1. **Existing components that can be reused directly** without modification.
2. **Existing components to be extended** via clean adapter interfaces.
3. **Brand-new modules required** to realize the Android & Linux Resource Intelligence vision.
4. **Concrete integration points and dependencies** between subsystems.

---

## 2. Existing File Inventory & Reusability Matrix

### 2.1 Core Infrastructure (`kernox_core`)

| Existing File Path | Primary Responsibility | Reusability in KERNOX 2.0 | Integration Points |
| :--- | :--- | :--- | :--- |
| `include/kernox/core/result.hpp`<br/>`include/kernox/core/error.hpp`<br/>`src/core/error.cpp` | Functional monadic error handling (`Result<T, Error>`) and `ErrorCode` enum. | **100% Direct Reuse.** Fundamental foundation for all new KERNOX 2.0 modules. Eliminates exceptions across process and memory operations. | Used by `ProcessModel`, `MemoryPressureDetector`, `AdbBridge`, and `ExperimentRunner`. |
| `include/kernox/core/unique_fd.hpp`<br/>`src/core/unique_fd.cpp` | Move-only RAII container for POSIX file descriptors with `::close` guard. | **100% Direct Reuse.** Provides descriptor safety for error pipes and sub-process monitoring. | Used in `AdbBridge` process execution pipes and JSON export streams. |
| `include/kernox/core/child_process.hpp`<br/>`src/core/child_process.cpp` | Move-only RAII process lifecycle guard; guarantees `waitpid` reaping and prevents zombies. | **100% Direct Reuse.** Leveraged to spawn and supervise external `adb` CLI processes reliably. | Powers `AdbBridge::execute_adb_command()` without orphan/zombie process leaks. |
| `include/kernox/core/timer.hpp`<br/>`src/core/timer.cpp` | High-resolution monotonic stopwatch backed by `std::chrono::steady_clock`. | **100% Direct Reuse.** Used for wall-clock benchmark timing and duration accounting. | Used by `ExperimentRunner` to measure reclamation decision latency and throughput. |
| `include/kernox/core/logger.hpp`<br/>`src/core/logger.cpp` | Thread-safe structured logging with severity levels (`DEBUG`, `INFO`, `WARN`, `ERROR`). | **100% Direct Reuse.** Provides clean console diagnostics across all new simulation engines. | Used across `MemoryReclamationEngine` and `AdbBridge` for diagnostic tracing. |

---

### 2.2 Simulation Subsystem (`kernox_sim`)

| Existing File Path | Primary Responsibility | Reusability in KERNOX 2.0 | Proposed Extension / Adapter |
| :--- | :--- | :--- | :--- |
| `include/kernox/modules/sim/cpu_sched.hpp`<br/>`src/modules/sim/cpu_sched.cpp` | Discrete-event single-processor CPU scheduling (FCFS, SJF, SRTF, RR, Priority, MLFQ) with Gantt charts. | **High Reusability (via Adapter).** Scheduling algorithms are mathematical and deterministic. | `intel::SimulatedProcess` will provide a `.to_task()` projection method converting to `sim::ProcessTask`, allowing KERNOX 2.0 experiments to simulate CPU execution of surviving tasks using existing SRTF/MLFQ engines. |
| `include/kernox/modules/sim/memory.hpp`<br/>`src/modules/sim/memory.cpp` | Contiguous partition allocation (First/Best/Worst/Next Fit), MMU paging translation, FIFO/LRU/Optimal page replacement. | **Selective Reusability (Remains Independent).** Core data structures (`MemoryBlock`, `PageTableEntry`) remain intact for Phase 3/5 textbook simulations. | **Deliberately kept separate** from the new `reclaim::MemoryReclamationEngine`. The new mobile engine models system-wide memory exhaustion, threshold-based pressure tiers, and process kill hierarchies, which operate at a higher architectural level than contiguous partition fitting. |
| `include/kernox/modules/sim/disk_sched.hpp`<br/>`src/modules/sim/disk_sched.cpp` | Disk head traversal algorithms (FCFS, SSTF, SCAN, C-SCAN, LOOK, C-LOOK) and seek metrics. | **Preserved Unchanged.** Maintained as part of the complete KERNOX OS academic suite. | Can be optionally invoked in holistic multi-resource experiments. |
| `include/kernox/modules/sim/deadlock.hpp`<br/>`src/modules/sim/deadlock.cpp` | Banker's Algorithm, dynamic resource requests with speculative rollback, and RAG cycle detection. | **Preserved Unchanged.** Maintained as part of the complete KERNOX OS academic suite. | The speculative rollback design pattern from Banker's algorithm is adapted in `MemoryReclamationEngine` for tentative memory allocations. |

---

### 2.3 Real Linux Systems Layer (`kernox_system`)

| Existing File Path | Primary Responsibility | Reusability in KERNOX 2.0 | Proposed Extension / Adapter |
| :--- | :--- | :--- | :--- |
| `include/kernox/modules/system/telemetry.hpp`<br/>`src/modules/system/telemetry.cpp` | Host Linux telemetry (`/proc/stat`, `meminfo`, `loadavg`) and deep per-process telemetry (`/proc/<pid>/status`, `/proc/<pid>/io`). | **High Reusability.** Provides baseline Linux memory metrics (`MemoryInfo` total, free, available, cached) and per-process memory footprints (`VmSize`, `VmRSS`). | Serves as the Linux counterpart to the Android `AdbAdapter`. Enables comparative telemetry: local host Linux memory pressure vs remote Android memory pressure. |
| `include/kernox/modules/system/process_manager.hpp`<br/>`src/modules/system/process_manager.cpp` | Real process sandboxing, POSIX resource constraints (`RLIMIT_AS`, `RLIMIT_CPU`), signal escalation, and `wait4` rusage. | **Preserved Unchanged.** Strictly represents native host OS processes. | Explicitly distinguished from `intel::SimulatedProcess`. Prevents confusion between simulated discrete-event tasks and live Linux OS processes. |
| `include/kernox/modules/system/shared_memory.hpp`<br/>`src/modules/system/shared_memory.cpp` | POSIX shared memory RAII mapping (`shm_open`, `ftruncate`, `mmap`, `munmap`, `shm_unlink`). | **Preserved Unchanged.** High-performance IPC channel. | Available for future high-speed inter-process telemetry feeds. |
| `include/kernox/modules/system/sync.hpp`<br/>`src/modules/system/sync.cpp`<br/>`include/kernox/modules/system/ring_buffer.hpp`<br/>`src/modules/system/ring_buffer.cpp` | Robust process-shared mutexes (`PTHREAD_MUTEX_ROBUST`), semaphores, condition variables, and bounded ring buffer. | **Preserved Unchanged.** Complete multi-process synchronization subsystem. | Available for multi-threaded experiment runners or live telemetry stream buffering. |
| `include/kernox/modules/system/unix_socket.hpp`<br/>`src/modules/system/unix_socket.cpp`<br/>`include/kernox/modules/system/message_queue.hpp`<br/>`src/modules/system/message_queue.cpp` | Full-duplex UNIX domain sockets (`AF_UNIX`) with `SCM_RIGHTS` fd passing; POSIX message queues with priority preemption. | **Preserved Unchanged.** Verified IPC communication channels. | Can serve as an IPC transport for external daemon integration if needed. |

---

### 2.4 Command-Line Interface (`kernox_cli`)

| Existing File Path | Primary Responsibility | Reusability in KERNOX 2.0 | Proposed Extension |
| :--- | :--- | :--- | :--- |
| `include/kernox/cli/command.hpp`<br/>`src/cli/command.cpp`<br/>`src/main.cpp` | Argument parsing, subcommand dispatching, help rendering, ASCII table formatting. | **Extended via New Subcommands.** Existing commands (`schedule`, `disk`, `deadlock`, `memory`, `process`, `ipc`, `sync`, `status`, `self-test`) remain 100% backward-compatible. | Register new command groups: `kernox intel`, `kernox reclaim`, `kernox experiment`, `kernox adb`, and add `--format json` support for structured data export. |

---

## 3. New Modules Required for KERNOX 2.0

To realize the Android & Linux Resource Intelligence Platform, the following new modular components are specified:

```
include/kernox/modules/
 ├── intel/
 │   └── process_model.hpp          [SimulatedProcess, LifecycleState, ProcessImportance, Footprint]
 ├── reclaim/
 │   ├── pressure_detector.hpp      [MemoryPressureDetector, MemoryPressureLevel, Thresholds]
 │   ├── reclamation_policy.hpp     [IReclamationPolicy, FIFO, LRU, Priority, ImportanceAware]
 │   └── reclamation_engine.hpp     [MemoryReclamationEngine, ReclamationReport, VictimDecisions]
 ├── experiment/
 │   ├── workload_generator.hpp     [WorkloadGenerator, PRNG Seed, Standard Scenarios]
 │   └── experiment_runner.hpp      [ExperimentRunner, PolicyComparisonReport, JSON/CSV Exporters]
 └── adb/
     ├── adb_bridge.hpp             [AdbBridge RAII process execution, device probe]
     └── adb_parser.hpp             [dumpsys meminfo and activity process table parsers]

src/modules/
 ├── intel/
 │   └── process_model.cpp
 ├── reclaim/
 │   ├── pressure_detector.cpp
 │   ├── reclamation_policy.cpp
 │   └── reclamation_engine.cpp
 ├── experiment/
 │   ├── workload_generator.cpp
 │   └── experiment_runner.cpp
 └── adb/
     ├── adb_bridge.cpp
     └── adb_parser.cpp

tools/analytics/
 ├── requirements.txt               [Python dependencies: matplotlib, pandas, pytest]
 ├── experiment_parser.py           [JSON loader and comparative metric calculator]
 ├── plot_reclamation.py            [Memory curve and survival rate visualizer]
 └── generate_report.py             [Automated markdown/PDF evaluation report builder]
```

---

## 4. Dependencies & CMake Target Hierarchy

```mermaid
graph TD
    flags[kernox_compiler_flags (INTERFACE)] --> core[kernox_core (STATIC)]
    flags --> sim[kernox_sim (STATIC)]
    flags --> sys[kernox_system (STATIC)]
    flags --> intel[kernox_intel (STATIC)]
    flags --> reclaim[kernox_reclaim (STATIC)]
    flags --> exp[kernox_experiment (STATIC)]
    flags --> adb[kernox_adb (STATIC)]
    flags --> cli[kernox_cli (STATIC)]
    flags --> app[kernox (EXECUTABLE)]

    core --> sim
    core --> sys
    core --> intel
    core --> adb
    intel --> reclaim
    reclaim --> exp
    core --> exp

    sim --> cli
    sys --> cli
    intel --> cli
    reclaim --> cli
    exp --> cli
    adb --> cli
    core --> cli

    cli --> app
```

### Build & Link Rules:
- **`kernox_intel`:** Depends strictly on `kernox_core` and `kernox_compiler_flags`. Zero cyclic dependencies.
- **`kernox_reclaim`:** Depends on `kernox_intel`, `kernox_core`, and `kernox_compiler_flags`.
- **`kernox_experiment`:** Depends on `kernox_reclaim`, `kernox_intel`, `kernox_core`, and `kernox_compiler_flags`.
- **`kernox_adb`:** Depends on `kernox_core` (`ChildProcess`, `Result<T, Error>`) and `kernox_compiler_flags`. Zero dependencies on simulation or experiment engines.
- **Existing Targets:** `kernox_core`, `kernox_sim`, and `kernox_system` compile independently without any new dependencies.

---

## 5. Verification & Regression Safety

To maintain the verified status of the repository:
1. **Existing Test Preservation:** All 16 existing CTest suites (`test_result`, `test_timer`, `test_unique_fd`, `test_child_process`, `test_cli`, `test_cpu_sched`, `test_disk_sched`, `test_deadlock`, `test_memory`, `test_telemetry`, `test_process_manager`, `test_ipc`, `test_shm`, `test_sync`, `test_unix_socket`, `test_message_queue`) must compile and pass with 100% success on every future phase.
2. **New Test Targets:**
   - `test_process_intel`: 100% unit coverage for `SimulatedProcess`.
   - `test_reclamation`: 100% unit coverage for threshold pressure tiers and all 4 reclamation policies.
   - `test_experiment`: Deterministic reproducibility tests (seed invariance).
   - `test_adb_parser`: Unit tests using synthetic `dumpsys` test fixtures.
3. **Compiler Warnings:** The `-Werror` policy is enforced across all new library targets without exceptions.
