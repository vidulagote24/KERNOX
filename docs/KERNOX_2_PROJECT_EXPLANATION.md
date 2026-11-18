# KERNOX 2.0 — Comprehensive Project Explanation

**Project:** KERNOX 2.0: Android & Linux Resource Intelligence Platform
**Target Audience:** Evaluators, Viva Committees, Students, and Systems Engineers
**Authoritative Implementation:** C++20 (`kernox_core`, `kernox_sim`, `kernox_system`, `kernox_intel`, `kernox_reclaim`, `kernox_experiment`, `kernox_adb`) & Python 3.11+ (`tools/analytics`)

---

## 1. What is KERNOX 2.0?

KERNOX 2.0 is a dual-subsystem operating systems engineering platform:
1. A **pure user-space discrete-event simulator** modeling classical OS algorithms (CPU scheduling, disk scheduling, deadlock detection, and virtual memory paging) alongside an Android-inspired process intelligence and memory reclamation engine.
2. A **production-quality Linux systems programming environment** and read-only mobile telemetry observer providing sandboxed process execution, `/proc` host telemetry, POSIX IPC, and Android Debug Bridge (ADB) device profiling.

The platform provides a bridge between textbook operating system theories and real-world resource management in modern mobile and embedded computing.

---

## 2. Why Was KERNOX Developed?

In modern mobile computing (such as Android and embedded Linux), memory is one of the most strictly constrained resources. Unlike desktop computers that can allocate tens of gigabytes of virtual memory swap files on high-speed solid-state drives, mobile operating systems cannot rely on heavy swap files:
- Flash storage wear-out (write amplification).
- Severe UI latency spikes (jank) caused by blocking on swap page-ins.
- Elevated thermal throttling and rapid battery drain.

Consequently, modern mobile operating systems solve memory exhaustion by **terminating whole processes** according to strict priority and importance hierarchies rather than merely paging out individual 4 KiB virtual pages.

Traditional operating systems curricula rarely address whole-process memory reclamation. Academic platforms typically focus either on abstract toy simulators (in Python/Java) or on low-level kernel hacking (in C). KERNOX 2.0 was developed to provide a unified, reproducible, and type-safe laboratory where students and researchers can study and benchmark whole-process memory pressure management under identical, controlled conditions.

---

## 3. Which Operating Systems Problems Does KERNOX Address?

1. **Systemic Memory Pressure vs Page Faults:** Distinguishing between micro-level page replacement (reclaiming individual physical frames via FIFO, LRU, or Optimal algorithms) and macro-level whole-process reclamation (reclaiming entire application memory spaces under critical system pressure).
2. **Priority Inversion in Memory Eviction:** Demonstrating how naive eviction policies (such as FIFO or LRU) suffer from priority inversion by killing active foreground UI applications simply because they were created earlier or have not executed recently.
3. **Graceful Degradation Under Memory Exhaustion:** Modeling how operating systems maintain interactive responsiveness by systematically evicting empty cached applications and background services before touching visible or foreground tasks.
4. **Deterministic Evaluation of Heuristic Algorithms:** Providing a controlled simulation environment where different reclamation strategies can be evaluated against identical process arrival patterns, memory demand spikes, and burst lengths.
5. **Non-Additive Memory Accounting:** Overcoming common misconceptions about Linux and Android memory telemetry (e.g., recognizing that `MemAvailable` is an estimated metric that overlaps with `MemFree` and page cache, rather than an additive partition).

---

## 4. Why Does KERNOX Use C++20?

ISO C++20 was selected for three primary engineering reasons:
1. **Zero-Overhead Abstractions:** C++20 allows strong typing (`SimulatedPid` vs `pid_t` vs `AndroidPid`), move-only RAII resource management (`UniqueFd`, `ProcessSharedMutex`), and functional error handling (`Result<T, Error>`) without incurring garbage collection pauses or runtime overhead.
2. **Deterministic Execution:** Unlike Python or Java, C++ executes without non-deterministic memory collection or runtime jitter, which is critical for reproducible discrete-event simulation and low-level Linux systems programming.
3. **Safe POSIX Systems Programming:** Traditional C systems code often suffers from unchecked return values, resource leaks, and memory unsafety. C++20 enables modern idioms:
   - Structured error propagation (`Result<T, Error>`) instead of global `errno`.
   - Strong lifetime enforcement via RAII destructors.
   - Comprehensive static analysis and sanitizer integration (ASan, UBSan).

---

## 5. How Process Intelligence Works

The `kernox_intel` module represents simulated processes inside the resource laboratory using `SimulatedProcess`. Each process is defined by:
- **Process Identifier (`SimulatedPid`):** Declared as `using SimulatedPid = uint32_t;`, providing semantic differentiation from native `pid_t` in code interfaces.
- **Android-Inspired Importance Tier (`ProcessImportance`):**
  - `Foreground`: User is currently interacting with the application.
  - `Visible`: Process is visible to the user but not in the active foreground window.
  - `Service`: Background process executing user-perceptible work (e.g., audio playback).
  - `Background`: Dormant background process without active user interaction.
  - `Cached`: Empty application process kept in memory for fast relaunch.
- **Formal 6-State Lifecycle (`ProcessLifecycleState`):**
  - `Created`: Initial instantiated state.
  - `Ready`: Admitted to scheduling runqueue.
  - `Running`: Actively executing on simulated CPU.
  - `Blocked`: Waiting for simulated I/O or resources.
  - `Completed`: Terminal state reached when CPU burst finishes normally.
  - `Terminated`: Terminal state reached when evicted under memory pressure or aborted.
- **Memory Footprint Accounting:** Tracks allocated, peak, and resident memory.

---

## 6. How Memory Pressure is Detected

The `MemoryPressureDetector` in `kernox_reclaim` monitors simulated system utilization:
$$\text{Utilization Ratio} = \frac{\text{Current Allocated Bytes}}{\text{Total Capacity Bytes}}$$

Thresholds are defined as:
- **Moderate Pressure (75%):** System utilization reaches 75%. Background reclamation is initiated to recover memory before user-visible performance degrades.
- **Critical Pressure (90%):** System utilization reaches 90%. Aggressive emergency reclamation is initiated.

When pressure is detected, the detector calculates the exact byte deficit required to return memory safely to below the moderate threshold with an engineering safety margin.

---

## 7. How Reclamation Policies Differ

When systemic memory pressure occurs, the `ReclamationEngine` selects victim processes using one of four pluggable policies:

1. **FIFO (First-In, First-Out):**
   - Evicts processes based strictly on creation timestamp (`created_at`).
   - *Behavior:* Simple and fast, but completely blind to process importance. If a foreground UI task was launched early in the session, FIFO will terminate it, causing severe priority inversion.
2. **LRU (Least Recently Used):**
   - Evicts processes based on last activity timestamp (`last_active_at`).
   - *Behavior:* Evicts dormant processes first, but can terminate an active foreground process if the user paused input briefly.
3. **Priority:**
   - Evicts processes with the lowest integer scheduling priority.
   - *Behavior:* Protects high-priority tasks, but static priorities do not dynamically reflect whether an application is currently visible on screen.
4. **ImportanceAware:**
   - Evaluates process importance tiers in strict order:
     $$\text{Cached} \longrightarrow \text{Background} \longrightarrow \text{Service} \longrightarrow \text{Visible}$$
   - *Behavior:* **Strictly shields Foreground processes from eviction.** Under moderate and critical pressure, foreground survival is guaranteed (100% survival rate), preserving interactive responsiveness.

---

## 8. How Deterministic Experiments Work

The `kernox_experiment` module provides controlled benchmarking:
1. **Workload Generation:** Given a scenario name and an integer seed (`seed=42`), a pseudo-random number generator generates a repeatable synthetic population of processes with predetermined arrival times, importance tiers, and memory allocation bursts.
2. **Supported Scenarios:**
   - `Normal`: Balanced memory footprint below pressure limits.
   - `HighMemoryPressure`: Aggressive allocations driving utilization beyond 90%.
   - `BackgroundHeavy`: Dominance of cached and background tasks.
   - `ForegroundProtection`: Active foreground processes competing with background processes.
   - `Mixed`: Multi-phase fluctuating workload.
3. **Independent Simulation Isolation:** Each policy is evaluated against the exact same workload in a fresh, isolated simulation context.
4. **Structured Export:** Multi-metric results are exported to RFC-compliant JSON and CSV schemas.

---

## 9. How Android ADB Observations Are Collected

The `kernox_adb` module interfaces with Android devices or Android Virtual Devices (AVDs) via standard Android Debug Bridge (`adb`) commands:
- **Device Enumeration:** Queries `adb devices -l` to discover target devices and serial numbers.
- **Hardware & OS Properties:** Queries `adb shell getprop` for device model, manufacturer, Android OS version, and API level.
- **RAM Telemetry:** Parses `adb shell cat /proc/meminfo` and `adb shell dumpsys meminfo` to extract total, used, free, cached, and swap memory.
- **Process Hierarchy:** Inspects active processes, extracting Proportional Set Size (PSS), Resident Set Size (RSS), and normalized importance tiers.

---

## 10. How Python Analytics Consumes the Results

The standalone Python package in `tools/analytics/`:
1. Ingests exported JSON and CSV datasets.
2. Validates schemas and enforces data provenance boundaries (`kernox_simulation` vs `android_adb_observation`).
3. Computes statistical metrics and evaluates non-additive memory metrics.
4. Produces publication-quality Matplotlib figures (300 DPI) and consolidated Markdown reports.

---

## 11. What is Simulated vs What is Observed From Real Systems

| Domain | Mechanism | Real or Simulated? | Safety Impact |
| :--- | :--- | :---: | :--- |
| **CPU Scheduling** | Discrete event queue (Gantt charts, turnaround times) | **Simulated** | Pure user-space simulation; does not affect Linux CFS. |
| **Disk Scheduling** | Cylinder arm movement queue (SSTF, SCAN, LOOK) | **Simulated** | Models physical drive kinematics in software. |
| **Deadlock Avoidance**| Banker's algorithm matrix checks & RAG cycle search | **Simulated** | Algorithmic simulation without kernel lock holding. |
| **Page Replacement** | MMU frame queues (FIFO, LRU, Optimal) | **Simulated** | Software models of virtual memory translation. |
| **Memory Reclamation**| Whole-process eviction under pressure (FIFO, LRU, Importance) | **Simulated** | Pure user-space simulation; never terminates real Linux/Android processes. |
| **Linux Telemetry** | `/proc/stat`, `/proc/meminfo`, `/proc/loadavg` | **Real** | Direct passive read of host Linux kernel state. |
| **Linux Process Mgr** | `fork()`, `execve()`, `setrlimit()`, `wait4()` | **Real** | Supervised native Linux process execution. |
| **Linux IPC & Sync** | Anonymous pipes, FIFOs, POSIX SHM, robust mutexes, AF_UNIX, MQs | **Real** | Native POSIX kernel primitives. |
| **Android ADB Obs** | Passive queries via `adb shell` (`dumpsys`, `/proc/meminfo`) | **Real** | Passive observation; strictly read-only. |

---

## 12. Technical Limitations and Design Trade-Offs

1. **User-Space Memory Simulation:** Whole-process reclamation is simulated in user-space rather than implemented as an in-tree Linux kernel module. This trade-off ensures safety, zero risk of system crashes, and cross-platform reproducibility under WSL2.
2. **Simplified LMKD Mapping:** While `ImportanceAware` models the core principles of Android's Low Memory Killer Daemon (prioritizing foreground survival and evicting cached tiers), real Android LMKD incorporates additional kernel-level mechanisms (e.g., dynamic `oom_score_adj` adjustment by `ActivityManager`, Linux cgroups v2 memory controllers, and Pressure Stall Information epoll triggers).
3. **Passive ADB Telemetry:** ADB polling has higher latency (10–50 ms per command) compared to direct kernel tracing (eBPF). However, ADB requires zero root permissions or kernel compilation, making it universally accessible.
4. **WSL2 ThreadSanitizer Limitation:** Under WSL2, the kernel's virtual memory address randomization conflicts with ThreadSanitizer's fixed application memory map. AddressSanitizer and UndefinedBehaviorSanitizer are used for memory safety verification instead.
