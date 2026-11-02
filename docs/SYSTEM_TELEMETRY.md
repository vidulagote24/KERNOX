# KERNOX Linux `/proc` System Telemetry

**Subsystem:** Real POSIX Integration — Host System Telemetry  
**Namespace:** `kernox::system` (aliased as `kernox::sys`)  
**Header:** [`include/kernox/modules/system/telemetry.hpp`](file:///e:/KERNOX%28OS%20CP%29/include/kernox/modules/system/telemetry.hpp)  
**Implementation:** [`src/modules/system/telemetry.cpp`](file:///e:/KERNOX%28OS%20CP%29/src/modules/system/telemetry.cpp)  
**Test Suite:** [`tests/test_telemetry.cpp`](file:///e:/KERNOX%28OS%20CP%29/tests/test_telemetry.cpp)  

---

## 1. Architectural Overview

The System Telemetry subsystem provides non-invasive, production-grade observation of the host Linux operating system. In accordance with KERNOX's architectural principles, the module adheres to:

1. **Strict Separation of Parsing and I/O:**  
   `TelemetryParser` provides pure static string parsing methods (`parse_proc_stat`, `parse_proc_meminfo`, `parse_proc_loadavg`, `parse_proc_uptime`) that do not interact with the filesystem. This guarantees that all edge cases (malformed input, zero delta, rollbacks, missing keys, unit overflows) are unit-tested deterministically without external host dependency.
2. **Dependency Injection for File Access:**  
   `SystemTelemetry` accepts an injectable `TelemetryPaths` configuration, allowing test harnesses to inject synthetic proc fixtures or mock file paths.
3. **Resilient Independent Metric Propagation:**  
   Each telemetry metric is wrapped in an independent `kernox::core::Result<T>`. If `/proc/stat` is temporarily unreadable or invalid, memory, load average, and uptime will still be collected and displayed gracefully.

```
+-------------------------------------------------------------+
|                      SystemTelemetry                        |
|                                                             |
|  +-------------------------------------------------------+  |
|  | read_proc_file(path) [Safely handles st_size == 0]    |  |
|  +-------------------------------------------------------+  |
|                             |                               |
|                             v                               |
|  +-------------------------------------------------------+  |
|  |                 TelemetryParser                       |  |
|  |  - parse_proc_stat(sv)                                |  |
|  |  - calculate_cpu_utilization(snap1, snap2)            |  |
|  |  - parse_proc_meminfo(sv)                             |  |
|  |  - parse_proc_loadavg(sv)                             |  |
|  |  - parse_proc_uptime(sv)                              |  |
|  +-------------------------------------------------------+  |
+-------------------------------------------------------------+
```

---

## 2. `/proc` Sources & Metric Formulations

### 2.1 CPU Utilization (`/proc/stat`)
- **Source:** The aggregate `cpu ` line in `/proc/stat`.
- **Counters Extracted:** `user`, `nice`, `system`, `idle`, `iowait`, `irq`, `softirq`, `steal`, `guest`, `guest_nice`.
- **Accounting Invariants:**
  - In Linux 2.6+, `guest` and `guest_nice` are already accounted within `user` and `nice`.
  - $\text{Idle Ticks} = \text{idle} + \text{iowait}$
  - $\text{Busy Ticks} = \text{user} + \text{nice} + \text{system} + \text{irq} + \text{softirq} + \text{steal}$
  - $\text{Total Ticks} = \text{Idle Ticks} + \text{Busy Ticks}$
- **Two-Snapshot Sampling:**
  - Utilization cannot be measured from a single snapshot because `/proc/stat` counters represent cumulative ticks since system boot.
  - A sampling interval $\Delta t$ (default 100ms in CLI, configurable) is elapsed between two snapshots ($S_1$ and $S_2$).
  - $\Delta \text{Total} = \text{Total}(S_2) - \text{Total}(S_1)$
  - $\Delta \text{Idle} = \text{Idle}(S_2) - \text{Idle}(S_1)$
  - $\text{CPU Utilization \%} = 100.0 \times \frac{\Delta \text{Total} - \Delta \text{Idle}}{\Delta \text{Total}}$
- **Edge-Case Protections:**
  - **Zero Delta:** If $\Delta \text{Total} == 0$, an `ErrorCode::InvalidState` error is returned rather than dividing by zero.
  - **Counter Rollback:** If $\text{Total}(S_2) < \text{Total}(S_1)$ or $\text{Idle}(S_2) < \text{Idle}(S_1)$, non-monotonicity is detected and flagged.

### 2.2 Memory Usage (`/proc/meminfo`)
- **Source:** `/proc/meminfo`.
- **Fields Extracted:** `MemTotal`, `MemAvailable`, `MemFree`, `Buffers`, `Cached`.
- **Unit Conversion:** Values are read in kB ($1024$ bytes) and multiplied to exact byte representations. Bounds checks prevent 64-bit integer overflow before multiplication.
- **Available Memory Resolution:**
  - When present (Linux 3.14+), `MemAvailable` is used directly as the kernel's estimate of memory available for new applications without swapping.
  - Fallback: On older kernels where `MemAvailable` is absent, available memory is approximated via $\text{MemFree} + \text{Buffers} + \text{Cached}$ and flagged via `used_fallback_available = true`.
- **Usage Formulation:**
  - $\text{Used Bytes} = \text{Total Bytes} - \text{Available Bytes}$
  - $\text{Usage \%} = 100.0 \times \frac{\text{Used Bytes}}{\text{Total Bytes}}$

### 2.3 Load Averages (`/proc/loadavg`)
- **Source:** `/proc/loadavg`.
- **Fields Extracted:** First 3 whitespace-delimited tokens representing exponential decaying averages of running and waiting threads over 1, 5, and 15 minutes.
- **Validation:** Must be non-negative floating-point values.

### 2.4 System Uptime (`/proc/uptime`)
- **Source:** `/proc/uptime`.
- **Fields Extracted:** First token represents total system uptime in seconds; second token represents cumulative CPU idle time in seconds.

---

## 3. CLI Integration (`kernox status`)

The `kernox status` command has been augmented to display live telemetry beneath the host environment details:

```text
=========================================================
                KERNOX SYSTEM STATUS REPORT              
=========================================================
Phase: Milestone 0 / Phase 4A.1 (Scaffolding & System Telemetry)

Active Foundation Utilities:
  [OK] Error & Result<T, Error>  - Type-safe error handling
  [OK] UniqueFd                  - RAII POSIX descriptor manager
  [OK] ChildProcess              - Safe process lifecycle controller
  [OK] Monotonic Timer           - std::chrono::steady_clock benchmark timer
  [OK] Structured Logger         - Thread-safe severity logger
  [OK] System Telemetry          - Linux /proc live resource monitor
  [OK] Command Dispatcher        - Modular CLI entry point

Host Environment Telemetry:
  Operating System : Linux
  Node Name        : LAPTOP-ABMH6B2R
  Kernel Release   : 6.6.87.2-microsoft-standard-WSL2
  Kernel Version   : #1 SMP PREEMPT_DYNAMIC Thu Jun  5 18:30:46 UTC 2025
  Architecture     : x86_64

Live Host Telemetry (/proc):
  CPU Utilization  : 5.93% (sampled over 100ms)
  Memory Status    : Used 538.61 MB / Total 7607.20 MB (7.08%)
  Memory Available : 7068.60 MB
  Load Average     : 0.48 (1m), 0.10 (5m), 0.03 (15m)
  System Uptime    : 0h 0m 9s (9.47s)

Notice: Real system telemetry is now operational via Linux /proc.
=========================================================
```

If a metric is unavailable (e.g. running inside an unmounted `/proc` container), `kernox status` displays:
```text
  CPU Utilization  : [UNAVAILABLE: Failed to open proc file: /proc/stat]
```
without crashing or hiding the remaining valid metrics.

---

## 4. WSL2 Environment Nuances & Limitations

1. **Virtual Machine Boundaries:**  
   Under WSL2, Linux runs inside a lightweight utility virtual machine managed by Hyper-V. `/proc/meminfo` reports the memory assigned to the WSL2 VM (defined in `.wslconfig` or defaulting to 50% of physical Windows RAM), not total host Windows physical memory.
2. **CPU Virtualization:**  
   `/proc/stat` reflects scheduling ticks inside the WSL2 guest kernel. In high-contention Windows host workloads, stolen or throttled guest execution can affect measured ticks.
3. **Zero Reported File Size (`st_size == 0`):**  
   Files in `/proc` are kernel-generated virtual streams. Standard filesystem APIs querying file size report 0 bytes. `SystemTelemetry::read_proc_file` reads through chunked buffers until EOF rather than pre-allocating using file size.
