# KERNOX 2.0 — Interactive Android Resource Simulator

**Project:** KERNOX 2.0: Android Resource Intelligence & Memory Reclamation Platform
**Subsystem:** Interactive Android-Inspired Resource Simulation & Analytics Integration
**Namespace:** `kernox::simulator`
**Header:** [`include/kernox/modules/simulator/interactive_menu.hpp`](file:///e:/KERNOX%28OS%20CP%29/include/kernox/modules/simulator/interactive_menu.hpp)
**Implementation:** [`src/modules/simulator/`](file:///e:/KERNOX%28OS%20CP%29/src/modules/simulator/)
**Test Suite:** [`tests/test_interactive_simulator.cpp`](file:///e:/KERNOX%28OS%20CP%29/tests/test_interactive_simulator.cpp)

---

## 1. Architectural Overview

The **Interactive Android Resource Simulator** provides a terminal-driven interactive dashboard that bridges pure user-space Android lifecycle modeling, dynamic memory pressure induction, multi-policy memory reclamation, live ASCII visualizations, and seamless Python analytics automation.

```text
+----------------------------------------------------------------------------+
|                         InteractiveSimulator (Menu)                        |
+----------------------------------------------------------------------------+
    |               |               |                |               |
    v               v               v                v               v
+-------+     +-----------+   +-----------+    +-----------+   +-------------+
|Status |     |  Process  |   |Reclamation|    |   ASCII   |   |  Analytics  |
|& Host |     | Workload  |   |  Engine   |    |Visualizer |   |   Bridge    |
| /proc |     |  Builder  |   |  Policies |    | (Memory/  |   |  (Python    |
|Telemet|     | (Custom)  |   | (4 algos) |    | Lifecycle)|   |  Automate)  |
+-------+     +-----------+   +-----------+    +-----------+   +-------------+
```

### Key Components

- **`InteractiveSimulator`:** Central menu loop providing 10 discrete simulator operations, input dispatching, state persistence, and session management.
- **`WorkloadBuilder`:** Dynamic builder for constructing custom multi-process workloads with tailored memory sizes, CPU bursts, arrival times, and Android lifecycle priorities.
- **`TerminalReader`:** Robust terminal input reader with input sanitization, type parsing, and default fallback handling.
- **`AsciiVisualizer`:** Visual rendering engine generating ASCII memory distribution bars, process lifecycle breakdown charts, and policy comparison tables directly in the terminal.
- **`AnalyticsBridge`:** Subprocess orchestration bridge invoking Python analytics CLI tools to validate datasets, generate publication-quality matplotlib charts, and produce markdown analysis reports.

---

## 2. CLI Invocation

Launch the interactive simulator directly from the KERNOX command line:

```bash
# Full command
kernox simulate android

# Shortcut alias
kernox sim android
```

---

## 3. Menu Operations (All 10 Operations)

The simulator presents an interactive 10-option main menu:

```text
================================================================================
           KERNOX 2.0: ANDROID RESOURCE SIMULATION & BENCHMARK MENU
================================================================================
  1. Inspect System Status & Telemetry
  2. Display Active Process Table
  3. Create Custom Process
  4. Advance Simulation Clock (Ticks)
  5. Inject Memory Pressure Event
  6. Trigger Memory Reclamation Policy
  7. Run Full Workload Simulation Scenario
  8. Run Multi-Policy Comparative Benchmark
  9. Render ASCII Memory & Lifecycle Visualizations
 10. Export Results & Run Python Analytics
--------------------------------------------------------------------------------
  0. Exit Simulation
================================================================================
```

### Operation 1: Inspect System Status & Telemetry
Queries and formats live host information from Linux `/proc` (CPU utilization, memory usage, load averages, uptime) and confirms platform readiness.

### Operation 2: Display Active Process Table
Displays an aligned tabular view of all currently admitted simulated processes:
- PID, Process Name, Android Importance Tier (`Foreground`, `Visible`, `Service`, `Background`, `Cached`)
- CPU Scheduling Priority `[0..99]`
- Memory Footprint (Resident, Declared, Peak)
- Lifecycle State (`Created`, `Ready`, `Running`, `Blocked`, `Completed`, `Terminated`)

### Operation 3: Create Custom Process
Prompts the user interactively to configure and admit a new simulated process:
- Process Name (e.g., `com.custom.app`)
- Android Importance Tier (0: Foreground, 1: Visible, 2: Service, 3: Background, 4: Cached)
- Declared Memory Footprint in MiB
- CPU Burst Duration in ticks
- Optional dynamic allocation events

### Operation 4: Advance Simulation Clock (Ticks)
Advances the simulation clock by a user-specified tick duration. Dispatches running processes, consumes CPU bursts, triggers scheduled dynamic memory demand events, and transitions completed processes.

### Operation 5: Inject Memory Pressure Event
Simulates sudden host memory demand by allocating synthetic memory blocks, driving the system past pressure thresholds (Moderate at 75%, Critical at 90%) to trigger kernel low-memory conditions.

### Operation 6: Trigger Memory Reclamation Policy
Executes one of the four memory reclamation algorithms against current resident processes:
- **FIFO:** Evicts oldest admitted victim.
- **LRU:** Evicts least-recently dispatched victim.
- **Priority:** Evicts process with worst CPU priority.
- **ImportanceAware:** Evaluates Android tier hierarchy, protecting foreground/visible tasks while reclaiming cached/background processes first.

### Operation 7: Run Full Workload Simulation Scenario
Executes a complete lifecycle simulation against one of the five built-in workload scenarios:
1. `Normal` — Standard balanced mobile workload.
2. `HighMemoryPressure` — Aggressive allocation inducing critical memory pressure.
3. `BackgroundHeavy` — High ratio of background/cached processes.
4. `ForegroundProtection` — Mixed workload testing protection of active user-facing apps.
5. `Mixed` — Heterogeneous priorities, memory sizes, and arrival times.

### Operation 8: Run Multi-Policy Comparative Benchmark
Executes all four reclamation policies sequentially against the exact same workload scenario and seed, generating comparative metrics:
- Process survival rate (%)
- Foreground protection efficacy (%)
- Reclaimed memory volume (MiB)
- Wall-clock host execution latency (μs)

### Operation 9: Render ASCII Memory & Lifecycle Visualizations
Outputs clean text-based graphical charts:
- Proportional memory bar chart indicating allocated vs available capacity
- Distribution of memory across Android importance tiers
- Comparative policy survival and eviction breakdown

### Operation 10: Export Results & Run Python Analytics
Exports the latest simulation or benchmark comparison to JSON and optionally invokes the Python analytics pipeline:
1. Prompts for export target path (with default path suggestion).
2. Performs safe overwrite protection: warns if destination file already exists and requests explicit confirmation.
3. Automatically locates Python via independent project-root discovery (`/proc/self/exe` + `.venv/bin/python`).
4. Invokes `kernox_analytics.cli` to generate publication-grade PNG charts and markdown reports.
5. Preserves C++ simulator usability if Python is not installed (JSON export always succeeds).

---

## 4. Subprocess Safety & Resilience Architecture

The simulator's integration with Python adheres to strict production safety standards:

1. **Safe Executable & Argument Separation:**
   All child processes are spawned directly via `fork()` + `execve()` without shell command concatenation. Paths containing spaces or special characters are safely isolated from injection.

2. **Independent Project-Root Discovery:**
   `AnalyticsBridge` dynamically resolves the repository root using `/proc/self/exe` traversal and known project markers (`CMakeLists.txt`, `include/kernox`). The simulator functions reliably regardless of the current working directory.

3. **Safe Overwrite Confirmation:**
   If a user requests export to an existing JSON file, the simulator prompts for confirmation:
   `File already exists. Overwrite? (y/n)`. Aborting preserves existing data intact.

4. **Optional Dependency Fallback:**
   Python analytics is strictly decoupled. If Python, matplotlib, or pandas are unavailable on the host, the JSON export completes successfully and a clear advisory message is displayed without interrupting the simulation session.
