# KERNOX 2.0 — Final Architecture Specification

**Project:** KERNOX 2.0: Android & Linux Resource Intelligence Platform
**Repository:** https://github.com/vidulagote24/KERNOX
**Standard:** Modern ISO C++20 (`-std=c++20`) & Python 3.11+
**Target Environment:** Ubuntu 24.04 LTS (WSL2 / Linux Kernel 6.6+)

---

## 1. Architectural Mission & Philosophy

KERNOX 2.0 transforms an operating systems simulation platform into a unified Android and Linux resource intelligence laboratory. The core philosophy centers on **strict architectural duality and provenance separation**:
- **Pure User-Space Simulation:** Deterministic modeling of operating systems algorithms without intrusive kernel hooks or background daemons.
- **Production-Grade Linux Systems Programming:** Low-level, non-blocking process supervision, resource accounting, and IPC built on POSIX standards.
- **Passive Mobile Telemetry:** Read-only observation of Android devices and emulators, enforcing zero side-effects on target systems.
- **Decoupled Analytical Evaluation:** Pure mathematical ingestion, validation, and visualization in Python without compile-time or runtime dependencies on the C++ core.

---

## 2. Module Responsibilities & System Decomposition

KERNOX is decomposed into eight specialized C++ libraries and one independent Python analytics layer:

```text
                                  +-----------------------+
                                  |      kernox (main)    |
                                  +-----------------------+
                                              |
                                              v
                                  +-----------------------+
                                  |       kernox_cli      |
                                  +-----------------------+
                                              |
          +-------------------+---------------+-------------------+-------------------+
          |                   |                                   |                   |
          v                   v                                   v                   v
+-------------------+ +-------------------+             +-------------------+ +-------------------+
|  kernox_experiment| |   kernox_system   |             |     kernox_adb    | |    kernox_sim     |
+-------------------+ +-------------------+             +-------------------+ +-------------------+
          |                   |                                   |                   |
          v                   | (Linux kernel)                    | (adb CLI)         v
+-------------------+         |                                   |             +-------------------+
|   kernox_reclaim  |         +---------------+-------------------+             |    kernox_core    |
+-------------------+                         |                                 +-------------------+
          |                                   v                                           ^
          v                         +-------------------+                                 |
+-------------------+               |    kernox_core    |---------------------------------+
|    kernox_intel   |               +-------------------+
+-------------------+
          |
          v
+-------------------+
|    kernox_sim     |
+-------------------+
```

### 2.1 `kernox_core`
- **Scope:** Base platform utilities and foundation infrastructure.
- **Responsibilities:**
  - `Result<T, Error>`: Monadic, zero-exception error handling with structured error codes (`ErrorCode::InvalidArgument`, `ErrorCode::NotFound`, `ErrorCode::Timeout`, etc.).
  - `UniqueFd`: Move-only RAII file descriptor wrapper ensuring deterministic closure without resource leaks.
  - `ChildProcess`: Process lifecycle wrapper managing `fork()`, pipe redirection, error reporting, and process harvesting.
  - `MonotonicTimer`: High-resolution microsecond timer built on `std::chrono::steady_clock`.
  - `Logger`: Thread-safe, leveled structured logging (`DEBUG`, `INFO`, `WARN`, `ERROR`).

### 2.2 `kernox_sim`
- **Scope:** Classical operating systems algorithms and discrete event simulations.
- **Responsibilities:**
  - `CpuScheduler`: Discrete event queue evaluating FCFS, SJF, SRTF (preemptive), Round Robin, Priority, and MLFQ algorithms.
  - `DiskScheduler`: Track head motion traversal across FCFS, SSTF, SCAN, C-SCAN, LOOK, and C-LOOK.
  - `DeadlockManager`: Banker's safety algorithm, dynamic allocation with rollback, and Tarjan's cycle detection on Resource Allocation Graphs (RAG).
  - `MemoryManager`: Contiguous allocation (First-Fit, Best-Fit, Worst-Fit, Next-Fit), two-level page translation, and page replacement (FIFO, LRU, Optimal).

### 2.3 `kernox_system`
- **Scope:** Live Linux systems programming and host kernel facilities.
- **Responsibilities:**
  - `ProcessManager`: Spawns and supervises native Linux processes, applies POSIX limits (`RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_NOFILE`, `RLIMIT_FSIZE`), enforces timeouts, and executes graceful signal escalation (`SIGTERM` followed by `SIGKILL`).
  - `SystemTelemetry`: Parses `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, and `/proc/<pid>/stat` for live host telemetry.
  - `POSIX IPC`: High-throughput anonymous pipes, named FIFOs, and shared memory objects (`shm_open`, `mmap`).
  - `Synchronization`: Robust process-shared mutexes (`PTHREAD_MUTEX_ROBUST`), named and unnamed semaphores, condition variables, and zero-heap atomic ring buffers.
  - `Advanced POSIX IPC`: UNIX domain stream and datagram sockets (`AF_UNIX`), peer authentication (`SO_PEERCRED`), zero-copy descriptor passing (`SCM_RIGHTS`), and POSIX message queues with priority preemption.

### 2.4 `kernox_intel`
- **Scope:** Process intelligence and behavioral classification.
- **Responsibilities:**
  - `SimulatedProcess`: Strongly typed representation of an application process in the memory laboratory.
  - `SimulatedPid`: Opaque, type-safe integer identifier preventing accidental mixing with native Linux PIDs or Android PIDs.
  - `ProcessImportance`: Android-aligned hierarchy:
    1. `Foreground` (Active UI, user interaction)
    2. `Visible` (Visible window, non-active)
    3. `Service` (Background processing, user-perceptible)
    4. `Background` (Cached or dormant background task)
    5. `Cached` (Killable empty application container)
  - `ProcessLifecycleState`: Formal 6-state machine:
    - Initial state: `Created`
    - Non-terminal states: `Ready`, `Running`, `Blocked`
    - Terminal states: `Completed` (burst finished normally) and `Terminated` (evicted under memory pressure)
    - Valid transitions: `Created` $\to$ `Ready`; `Ready` $\longleftrightarrow$ `Running`; `Running` $\longleftrightarrow$ `Blocked`; `Running` $\to$ `Completed`; Any non-terminal state $\to$ `Terminated`.
  - `MemoryFootprint`: Tracks resident, heap, stack, and mapped memory sizes.

### 2.5 `kernox_reclaim`
- **Scope:** Systemic memory pressure detection and whole-process reclamation.
- **Responsibilities:**
  - `MemoryPressureDetector`: Dynamic utilization calculator evaluating instantaneous and smoothed memory usage against Moderate (75%) and Critical (90%) thresholds.
  - `ReclamationPolicy`: Abstract policy strategy interface.
  - Policy Implementations:
    - `FifoReclamationPolicy`: Selects oldest created process (arrival order).
    - `LruReclamationPolicy`: Selects least recently scheduled/active process.
    - `PriorityReclamationPolicy`: Selects lowest static scheduling priority.
    - `ImportanceAwarePolicy`: Tiered selection evicting Cached and Background tiers first; strictly shields Foreground processes from eviction under moderate and critical pressure.
  - `ReclamationEngine`: Executes eviction rounds until memory returns below target threshold or no eligible victims remain. Prevents infinite reclamation loops.

### 2.6 `kernox_experiment`
- **Scope:** Deterministic synthetic workload generation and comparative benchmarking.
- **Responsibilities:**
  - `WorkloadGenerator`: Generates identical process populations, memory demands, and burst durations given a fixed PRNG seed (`seed=42`).
  - Supported Scenarios:
    1. `Normal`: Balanced memory consumption below pressure thresholds.
    2. `HighMemoryPressure`: Aggressive allocations driving system past 90% critical threshold.
    3. `BackgroundHeavy`: Large population of cached/background tasks.
    4. `ForegroundProtection`: Mixed population testing survival of active UI tasks under severe pressure.
    5. `Mixed`: Fluctuating synthetic multi-stage workload.
  - `ExperimentRunner`: Executes identical workloads across all four reclamation policies in independent, isolated simulations.
  - `DataExporter`: Formats multi-policy benchmark results into RFC-compliant JSON and CSV schemas.

### 2.7 `kernox_adb`
- **Scope:** Passive, read-only Android Debug Bridge observation.
- **Responsibilities:**
  - `AdbBridge`: Subprocess execution harness for `adb` CLI commands (`devices`, `getprop`, `cat /proc/meminfo`, `dumpsys meminfo`, `ps`).
  - `AdbParser`: Robust string parsing extracting device properties, RAM composition, and process hierarchies.
  - `AdbService`: High-level facade providing typed telemetry snapshots.
  - **Safety Boundary:** Strict prohibition of state-altering commands (`shell kill`, `am force-stop`, `pm uninstall`, `remount`).

### 2.8 `kernox_cli`
- **Scope:** Unified terminal user interface.
- **Responsibilities:**
  - Modular argument parsing and subcommand dispatching.
  - Rich ANSI/ASCII tabular formatting for scheduling traces, Gantt charts, and memory metrics.

### 2.9 `tools/analytics` (Python 3.11+)
- **Scope:** Standalone analytics, validation, statistical aggregation, and visualization.
- **Responsibilities:**
  - Ingestion of JSON and CSV simulation outputs and Android telemetry snapshots.
  - Provenance separation enforcement (`kernox_simulation` vs `android_adb_observation`).
  - Calculation of non-additive Android memory metrics (`MemAvailable` vs `MemFree` + `Cached`).
  - Comparative trade-off evaluation across policies and scenarios.
  - Headless publication-quality chart rendering via Matplotlib (300 DPI).
  - Generation of structured Markdown summary reports.

---

## 3. Data Flow & Inter-Subsystem Interaction

```
+-------------------------------------------------------------------------------------------------------+
| 1. WORKLOAD GENERATION                                                                                |
|    WorkloadGenerator(scenario, seed) -> WorkloadProfile [Process Definitions, Memory Demands, Ticks]  |
+-------------------------------------------------------------------------------------------------------+
                                                   |
                                                   v
+-------------------------------------------------------------------------------------------------------+
| 2. EXPERIMENT EXECUTION LOOP                                                                          |
|    For each Policy in [FIFO, LRU, Priority, ImportanceAware]:                                         |
|      - Initialize isolated SimulatedMemoryContext (Capacity, Free, Used)                             |
|      - Instantiate fresh ProcessTable                                                                 |
|      - For each event tick:                                                                           |
|          * Dispatch allocations                                                                       |
|          * Evaluate MemoryPressureDetector (Ratio >= 0.75 Moderate, >= 0.90 Critical)                 |
|          * If Pressure Detected -> Invoke ReclamationEngine(Policy, TargetBytes)                      |
|          * Record metrics (reclaimed bytes, victims, tier survival rates, wall-clock latency)         |
+-------------------------------------------------------------------------------------------------------+
                                                   |
                                                   v
+-------------------------------------------------------------------------------------------------------+
| 3. STRUCTURED EXPORT                                                                                  |
|    DataExporter::to_json() / to_csv() -> Write to disk (e.g., high_pressure_comparison.json)          |
+-------------------------------------------------------------------------------------------------------+
                                                   |
                                                   v
+-------------------------------------------------------------------------------------------------------+
| 4. OFFLINE PYTHON ANALYTICS                                                                           |
|    kernox_analytics.loader -> Dataset Ingestion                                                       |
|    kernox_analytics.validators -> Schema & Provenance Check                                           |
|    kernox_analytics.metrics -> Mathematical Derivations & Non-Additive Accounting                     |
|    kernox_analytics.visualizations -> High-DPI Charts (policy_comparison.png, process_survival.png)    |
|    kernox_analytics.report_generator -> Consolidated Markdown Reports (experiment_summary.md)         |
+-------------------------------------------------------------------------------------------------------+
```

---

## 4. Process and PID Separation Guarantees

KERNOX segregates process identifiers across three conceptual domains:

1. **`SimulatedPid` (Simulation Domain):**
   - Declared as `using SimulatedPid = uint32_t;` in `kernox::intel`.
   - Represents simulated processes inside `kernox_intel` and `kernox_experiment`, starting from initial value 1000.
   - *Audit Finding:* As a standard `using` alias of `uint32_t`, `SimulatedPid` provides conceptual clarity in function signatures, but does not enforce compile-time type separation from other `uint32_t` values.

2. **`pid_t` (Host Linux Domain):**
   - Standard POSIX signed integer (`int`) representing real host processes managed by `ChildProcess` and `ProcessManager`.
   - Passed to kernel system calls (`fork`, `execve`, `wait4`, `kill`).

3. **`AndroidPid` (Android Observation Domain):**
   - Declared as `using AndroidPid = uint32_t;` in `kernox::adb`.
   - Represents remote process identifiers passively parsed from Android `dumpsys` and `ps` output.
   - *Audit Finding:* Like `SimulatedPid`, this is currently an alias of `uint32_t`. While the `kernox_adb` module provides zero APIs that could pass `AndroidPid` to process-killing calls, the C++ type system does not prevent interchangeable assignment with `SimulatedPid`. Strong wrapper structs are recommended to achieve true compile-time enforcement.

---

## 5. Memory Pressure & Reclamation Architecture

### 5.1 Pressure Detection Mathematics
The `MemoryPressureDetector` continuously monitors simulated memory utilization:
$$\text{Utilization Ratio} = \frac{\text{Current Allocated Bytes}}{\text{Total Capacity Bytes}}$$

- **Normal State:** $\text{Utilization Ratio} < 0.75$
- **Moderate Pressure:** $0.75 \le \text{Utilization Ratio} < 0.90$
- **Critical Pressure:** $\text{Utilization Ratio} \ge 0.90$

When pressure is detected, the required reclamation target is computed to return memory safely to below the moderate threshold with a safety buffer.

### 5.2 Victim Selection Policies

| Policy | Selection Criterion | Foreground Behavior Under Extreme Pressure | Trade-off Profile |
| :--- | :--- | :--- | :--- |
| **FIFO** | Oldest `created_at` timestamp | **Vulnerable:** Foreground tasks are evicted if they arrived early. | High memory reclamation; severe priority inversion. |
| **LRU** | Oldest `last_active_at` timestamp | **Vulnerable:** Active foreground tasks may be evicted if idle. | Recency bias; ignores process importance hierarchy. |
| **Priority** | Lowest integer priority value | **Semi-Protected:** Protected only if manually given high priority. | Static priority allocation; susceptible to aging inversion. |
| **ImportanceAware** | Tiered: `Cached` $\to$ `Background` $\to$ `Service` $\to$ `Visible` | **Guaranteed Protection:** Foreground tasks are shielded from eviction. | Zero foreground degradation; matches mobile OS design principles. |

---

## 6. Android Observation & Safety Boundaries

The `kernox_adb` subsystem interacts with physical devices and Android Virtual Devices (AVDs) via standard Android Debug Bridge commands:
- **Device Discovery:** `adb devices -l`
- **System Properties:** `adb -s <serial> shell getprop`
- **Memory Telemetry:** `adb -s <serial> shell cat /proc/meminfo` and `dumpsys meminfo`
- **Process Telemetry:** `adb -s <serial> shell ps -A -o PID,NAME,PSS,RSS`

### Command Validation & Execution Architecture
1. **Subcommand Allowlist at CLI:** At the CLI dispatch level (`CommandDispatcher::execute_adb`), a strict positive allowlist restricts execution to five read-only subcommands: `devices`, `status`, `memory`, `processes`, and `snapshot`. Arbitrary subcommands cannot be supplied from the command line.
2. **Positive Allowlist Filtering at Bridge Level:** Inside `AdbBridge::is_safe_read_only_command`, a strict positive allowlist validates that every command matches one of the explicitly authorized read-only command forms:
   - `adb devices [-l]`
   - `adb get-state`
   - `adb shell getprop [property]`
   - `adb shell dumpsys meminfo [package_or_flag]`
   - `adb shell dumpsys activity processes`
   - `adb shell cat /proc/meminfo`
   - `adb shell ps [-A] [-A -o <columns>]`
   Device-selection arguments (`-s <serial>`) are strictly validated against character and length boundaries (`[a-zA-Z0-9._:-]`, 1-64 chars, cannot start with `-`). Any unlisted subcommands (`forward`, `push`, `pull`, `install`, `reboot`, `remount`, `root`, `tcpip`), mutating shell commands (`kill`, `am force-stop`, `pm clear`, `su`, `setprop`, `rm`), unauthorized cat paths, arbitrary shell utilities, or shell metacharacters are rejected at the C++ library boundary with `ErrorCode::PermissionDenied`.
3. **No Shell Execution:** `SystemCommandRunner::execute` passes arguments directly to POSIX `execvp()` rather than routing through `/bin/sh -c`. This eliminates shell parsing and shell injection vulnerabilities.

### Non-Additive Memory Invariants
In Android memory analysis, the following structural invariants are enforced:
$$\text{MemAvailable} \approx \text{MemFree} + \text{Reclaimable Cached} + \text{Reclaimable Slab}$$
Because `MemAvailable` overlaps with `MemFree` and page cache, KERNOX analytics never adds them together and never renders stacked charts implying mutual exclusivity.

---

## 7. Error Handling & Safety Guarantees

1. **Zero-Exception APIs:** All internal methods return `kernox::core::Result<T, Error>`. No C++ exceptions are thrown across module boundaries.
2. **Infinite Loop Prevention:** `ReclamationEngine` bounds eviction passes by the total process population. If no eligible victim can reclaim positive memory, the engine aborts cleanly with `ReclamationStatus::NoEligibleVictims`.
3. **RAII Resource Management:** All POSIX file descriptors, memory-mapped buffers, temporary files, and IPC queues are governed by move-only RAII destructors.
4. **Sandboxed Subprocess Safety:** Subprocesses spawned by `ChildProcess` use pipes to report `execve` failures back to the parent and enforce strict execution timeouts.
