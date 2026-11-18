# KERNOX 2.0

**Android & Linux Resource Intelligence Platform**

[![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Python: 3.11+](https://img.shields.io/badge/Python-3.11%2B-blue.svg)](https://www.python.org/)
[![Standard: POSIX](https://img.shields.io/badge/Standard-POSIX.1--2017-orange.svg)](https://pubs.opengroup.org/onlinepubs/9699919799/)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL2-green.svg)](https://ubuntu.com/)
[![Build: CMake & Ninja](https://img.shields.io/badge/Build-CMake%20%7C%20Ninja-red.svg)](https://cmake.org/)
[![Sanitizers: ASan / UBSan](https://img.shields.io/badge/Sanitizers-ASan%20%7C%20UBSan-purple.svg)](https://github.com/google/sanitizers)
[![C++ Tests: 20/20 Passed](https://img.shields.io/badge/C%2B%2B%20Tests-20%2F20%20Passed%20(100%25)-brightgreen.svg)](#15-testing-and-verification)
[![Python Tests: 31/31 Passed](https://img.shields.io/badge/Python%20Tests-31%2F31%20Passed%20(100%25)-brightgreen.svg)](#15-testing-and-verification)

---

## 1. Project Title & Introduction

**KERNOX 2.0: Android & Linux Resource Intelligence Platform** is an operating systems simulation platform, systems engineering runtime, and resource management laboratory implemented in modern ISO C++20 and Python 3.11+.

KERNOX bridges the gap between classic theoretical operating systems algorithms and modern resource-constrained mobile and embedded operating systems. The platform combines:
1. **Classical OS Simulation Engine:** Deterministic scheduling, virtual memory paging, and deadlock avoidance.
2. **Production-Grade Linux Systems Programming:** Low-level process supervision, `/proc` telemetry, and high-performance POSIX IPC.
3. **Android-Inspired Process Intelligence:** Importance classification, lifecycle states, and memory footprint accounting.
4. **Memory Pressure Detection & Reclamation Engine:** Configurable moderate (75%) and critical (90%) thresholds with FIFO, LRU, Priority, and ImportanceAware eviction policies.
5. **Deterministic Workload Generation & Benchmarking:** Five reproducible workload scenarios with JSON and CSV structured exporters.
6. **Read-Only Android ADB Subsystem:** Live device discovery, memory inspection (`/proc/meminfo`, `dumpsys meminfo`), and process observation with strict safety boundaries.
7. **Python Analytics & Publication Visualization Layer:** Headless validation, non-additive memory metrics, trade-off evaluation, publication-quality figures, and Markdown reports.

---

## 2. Problem Statement

Modern computing systems face severe memory constraints, particularly in mobile and embedded environments where physical RAM is strictly bounded and swapping to flash storage causes prohibitive write amplification, latency spikes, and battery degradation.

Traditional operating systems education and research platforms suffer from three major shortcomings:
- **Toy Academic Simulators:** High-level simulators fail to expose real systems programming constraints, relying on runtime garbage collection and artificial abstractions detached from real kernel interfaces.
- **Unstructured Kernel Labs:** Native C kernel programming assignments introduce steep learning curves and undefined behaviors without providing reproducible, comparative benchmarking frameworks.
- **The Mobile Memory Reclamation Gap:** Standard OS curricula focus on virtual memory page replacement (e.g., FIFO, LRU) while ignoring how modern production operating systems (such as Android and Linux cgroups) detect systemic memory pressure and reclaim whole-process memory while safeguarding user-facing foreground tasks.

---

## 3. Motivation

KERNOX 2.0 was developed to demystify whole-process memory reclamation and resource management by creating an open, reproducible, laboratory-grade platform. Inspired by Android's Low Memory Killer Daemon (LMKD) and Linux kernel memory pressure mechanisms, KERNOX models how processes transition across lifecycle tiers, tracks aggregate memory footprints, detects systemic pressure events, and evaluates reclamation trade-offs under identical synthetic workloads.

Crucially, KERNOX enforces **strict data and execution provenance**:
- Pure mathematical simulations run in user-space without interfering with host or target processes.
- Android ADB observation is strictly passive and read-only, ensuring physical devices and emulators are never altered.
- Python analytics operates strictly as an offline consumer of structured data without build-time or runtime coupling to the C++ core.

---

## 4. Main Objectives

1. **Establish Multi-Domain Architectural Separation:** Maintain strict isolation between simulated user-space processes, live Linux host telemetry, and passive Android ADB observations.
2. **Implement Process Intelligence (`kernox_intel`):** Model simulated application processes with strongly typed identifiers (`SimulatedPid`), lifecycle transitions, CPU scheduling priorities, and Android-inspired importance tiers (`Foreground`, `Visible`, `Service`, `Background`, `Cached`).
3. **Build Memory Pressure & Reclamation Engine (`kernox_reclaim`):** Detect moderate (75%) and critical (90%) pressure thresholds and implement four distinct victim selection policies:
   - **FIFO:** Arrival-order eviction.
   - **LRU:** Recency-order eviction based on access timestamps.
   - **Priority:** Eviction of processes with lowest static priority.
   - **ImportanceAware:** Tiered eviction protecting foreground and visible processes while systematically reclaiming background and cached processes.
4. **Deliver Deterministic Experimentation (`kernox_experiment`):** Execute identical workloads across multiple policies with fixed PRNG seeds and export results in standardized JSON and CSV formats across five scenarios: `Normal`, `HighMemoryPressure`, `BackgroundHeavy`, `ForegroundProtection`, and `Mixed`.
5. **Passive Android Observation Subsystem (`kernox_adb`):** Query attached Android devices via ADB for hardware specifications, memory statistics, and process hierarchies without modifying target state.
6. **Publication-Ready Analytics (`tools/analytics`):** Provide reproducible data loading, strict schema validation, statistical moments, non-additive memory accounting, publication-quality Matplotlib figures, and automated Markdown reports.

---

## 5. Architecture Overview

```
+-------------------------------------------------------------------------------------------------------+
|                                              KERNOX CLI                                               |
|               status | schedule | disk | deadlock | memory | process | ipc | sync | experiment | adb     |
+-------------------------------------------------------------------------------------------------------+
                                                   |
         +-----------------------------------------+-----------------------------------------+
         |                                                                                   |
         v                                                                                   v
+-----------------------------------+                             +-----------------------------------+
|      Simulation Subsystem         |                             |     Live Systems Subsystem        |
|  - kernox_sim (CPU, Disk, VM)     |                             |  - kernox_system                  |
|  - kernox_intel (Process Model)   |                             |    * ProcessManager (rlimit, rus) |
|  - kernox_reclaim (Engine & Pol)  |                             |    * SystemTelemetry (/proc)      |
|  - kernox_experiment (Workloads)  |                             |    * POSIX IPC (Pipes, SHM, MQ)   |
+-----------------------------------+                             |    * Sync (Robust Mutex, RingBuf) |
         |                                                        +-----------------------------------+
         | (Structured JSON/CSV Export)                                                      |
         v                                                                                   v
+-----------------------------------+                             +-----------------------------------+
|  tools/analytics (Python 3.11+)   |                             |  kernox_adb (Passive Telemetry)   |
|  - Ingestion & Validation         |                             |  - Device Discovery               |
|  - Non-Additive Memory Metrics    |                             |  - /proc/meminfo & dumpsys        |
|  - 5-Scenario Trade-off Analysis  |                             |  - Read-Only Observation Boundary |
|  - Matplotlib High-DPI Figures    |                             +-----------------------------------+
|  - Consolidated Markdown Reports  |                                                |
+-----------------------------------+                                                v
                                                                  +-----------------------------------+
                                                                  |   Android Device / Emulator (AVD) |
                                                                  |   Read-Only ADB Interface         |
                                                                  +-----------------------------------+
```

### Architectural Boundaries & Data Provenance
KERNOX enforces strict domain separation:
1. `kernox_simulation`: Pure virtual simulation data generated by deterministic C++ engines.
2. `android_adb_observation`: Real-time telemetry passively collected from Android devices.
3. `linux_proc_observation`: Real-time host telemetry collected from the host Linux kernel.

Simulated and real telemetry are never conflated or co-mingled.

---

## 6. Technology Stack

- **Core Simulation & Systems Language:** Modern ISO C++20 (`-std=c++20`, strict flags `-Wall -Wextra -Wpedantic -Wconversion -Werror`).
- **Analytics & Visualization Layer:** Python 3.11+ (`pandas>=2.2.0`, `numpy>=1.26.0`, `matplotlib>=3.8.0`, `pytest>=8.0.0`).
- **Build System:** CMake 3.25+ with Ninja generator and CMake Presets.
- **Compilers:** GCC 13.3.0+ / Clang 16+.
- **Host Platform:** Ubuntu 24.04 LTS under WSL2 / Native Linux Kernel 6.6+.
- **System Libraries:** `pthread` (POSIX Threads & Robust Mutexes), `rt` (POSIX Realtime Extensions & Message Queues).
- **Sanitizers:** AddressSanitizer (`ASan`), UndefinedBehaviorSanitizer (`UBSan`).

---

## 7. Main Modules

| Library Target | Path | Purpose |
| :--- | :--- | :--- |
| **`kernox_core`** | `src/core/`, `include/kernox/core/` | Foundational primitives: `Result<T, Error>`, `UniqueFd`, `ChildProcess`, `Timer`, `Logger`. |
| **`kernox_sim`** | `src/modules/sim/`, `include/kernox/modules/sim/` | Classical OS algorithms: CPU, Disk, Deadlock, Memory Management. |
| **`kernox_system`**| `src/modules/system/`, `include/kernox/modules/system/` | Real Linux systems facilities: ProcessManager, Telemetry, Pipes, FIFOs, SHM, Sync, UNIX Sockets, MQs. |
| **`kernox_intel`** | `src/modules/intel/`, `include/kernox/modules/intel/` | Process intelligence: `SimulatedProcess`, `SimulatedPid`, importance tiers, lifecycle state machine. |
| **`kernox_reclaim`**| `src/modules/reclaim/`, `include/kernox/modules/reclaim/` | Memory pressure detector, reclamation engine, and policies (FIFO, LRU, Priority, ImportanceAware). |
| **`kernox_experiment`**| `src/modules/experiment/`, `include/kernox/modules/experiment/`| Deterministic workload generators, multi-policy experiment runner, JSON and CSV exporters. |
| **`kernox_adb`** | `src/modules/adb/`, `include/kernox/modules/adb/` | Read-only Android ADB bridge, parser, and service. |
| **`kernox_cli`** | `src/cli/`, `include/kernox/cli/` | Command dispatcher, CLI options, tabular formatters, and demo runners. |
| **`tools/analytics`**| `tools/analytics/` | Standalone Python package for validation, statistical evaluation, chart rendering, and reports. |

---

## 8. Key Operating Systems Concepts Demonstrated

1. **CPU Scheduling:** FCFS, Shortest Job First (SJF), Shortest Remaining Time First (SRTF), Round Robin (RR), Priority, and Multilevel Feedback Queue (MLFQ) with dynamic arrivals and ASCII Gantt charts.
2. **Disk Arm Scheduling:** FCFS, SSTF, SCAN (Elevator), C-SCAN, LOOK, and C-LOOK with head movement accounting.
3. **Deadlock Management:** Dijkstra's Banker's Algorithm, dynamic safety checks with state rollback, multi-instance resource detection, and Resource Allocation Graph (RAG) cycle detection via Tarjan's algorithm.
4. **Virtual Memory Management:** First-fit, Best-fit, Worst-fit, Next-fit dynamic partitioning; MMU two-level page translation; FIFO, LRU, and Optimal (Bélady's MIN) page replacement; Peter Denning's Working-Set model.
5. **Memory Pressure Detection:** Dynamic utilization monitoring with hysteresis, separating Moderate Pressure (75%) from Critical Pressure (90%).
6. **Whole-Process Memory Reclamation:** Evaluating eviction order strategies (arrival order, recency, static priority, and tiered importance) under memory exhaustion.
7. **Priority Inversion vs Foreground Protection:** Demonstrating why non-importance-aware policies suffer priority inversion by terminating user-visible foreground tasks under pressure, while `ImportanceAware` guarantees foreground survival.
8. **Real Linux Systems Programming:** Sandboxed child processes, signal escalation (`SIGTERM` -> `SIGKILL`), POSIX resource limits (`setrlimit`), `wait4` rusage accounting, atomic ring buffers, and robust process-shared mutexes (`PTHREAD_MUTEX_ROBUST`).
9. **Non-Additive Memory Accounting:** Modeling `/proc/meminfo` accurately, respecting that `MemAvailable` overlaps with `MemFree` and reclaimable cache.

---

## 9. Installation & System Requirements

### Host Prerequisites
- Linux or Windows Subsystem for Linux 2 (WSL2 with Ubuntu 22.04 or 24.04).
- C++20 compliant compiler: GCC 13+ or Clang 16+.
- Build tools: CMake (>= 3.25) and Ninja (>= 1.11).
- Python (>= 3.11) with `python3-venv`.
- Optional: `adb` (`android-tools-adb`) for real Android observation.

### Quick Setup (Ubuntu / Debian / WSL2)
```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3 python3-venv python3-pip
```

---

## 10. Build Instructions

KERNOX uses CMake Presets for reproducible builds:

```bash
# 1. Debug Build (default for development and testing)
cmake --preset dev-debug
cmake --build --preset dev-debug

# 2. Optimized Release Build
cmake --preset dev-release
cmake --build --preset dev-release

# 3. AddressSanitizer & UndefinedBehaviorSanitizer Build
cmake --preset dev-asan
cmake --build --preset dev-asan
```

Binary executables are written to `build/<preset>/bin/kernox`.

---

## 11. Command-Line Interface (CLI) Usage

The unified `kernox` binary provides intuitive subcommands:

```bash
# Display general help and version
./build/debug/bin/kernox --help
./build/debug/bin/kernox --version

# System status and live Linux /proc telemetry
./build/debug/bin/kernox status

# Internal core self-tests
./build/debug/bin/kernox self-test

# Classical CPU scheduling simulation
./build/debug/bin/kernox schedule --algorithm srtf --workload arrivals

# Classical memory page replacement simulation
./build/debug/bin/kernox memory replace --algorithm lru --workload page-reference-demo --frames 3

# Real Linux IPC & synchronization demos
./build/debug/bin/kernox sync demo
./build/debug/bin/kernox ipc unix-demo
./build/debug/bin/kernox ipc mq-demo
```

---

## 12. Experimentation & Memory Reclamation Examples

Run deterministic workloads and compare memory reclamation policies:

```bash
# 1. Run a single workload experiment under ImportanceAware policy
./build/debug/bin/kernox experiment run \
  --scenario high-pressure \
  --policy importance-aware \
  --capacity 1073741824 \
  --format text

# 2. Compare all 4 policies on identical workload and export JSON
./build/debug/bin/kernox experiment compare \
  --scenario high-pressure \
  --seed 42 \
  --capacity 1073741824 \
  --format json \
  --output tools/analytics/output/high_pressure_comparison.json

# 3. Export CSV comparison for spreadsheet analysis
./build/debug/bin/kernox experiment compare \
  --scenario high-pressure \
  --seed 42 \
  --capacity 1073741824 \
  --format csv \
  --output tools/analytics/output/high_pressure_comparison.csv
```

---

## 13. Android ADB Setup & Usage

The Android subsystem is strictly read-only and passive:

```bash
# Check ADB binary availability and service status
./build/debug/bin/kernox adb status

# List connected physical or virtual Android devices
./build/debug/bin/kernox adb devices

# Inspect Android memory telemetry (RAM composition, dumpsys meminfo)
./build/debug/bin/kernox adb memory --format json --output snapshot_mem.json

# Inspect active Android processes, PSS, RSS, and normalized importance tiers
./build/debug/bin/kernox adb processes --format json --output snapshot_proc.json

# Capture consolidated device observation snapshot
./build/debug/bin/kernox adb snapshot --format json --output snapshot_full.json
```

*Note: If no Android device is connected, KERNOX reports device absence gracefully and continues all simulation and benchmarking functions without error.*

---

## 14. Python Analytics & Visualization Setup

The Python analytics package resides in `tools/analytics/`:

```bash
# 1. Create and activate dedicated virtual environment
python3 -m venv tools/analytics/.venv
source tools/analytics/.venv/bin/activate

# 2. Install pinned dependencies
pip install -r tools/analytics/requirements.txt

# 3. Validate dataset schemas and domain isolation
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli validate \
  --input tools/analytics/examples/experiments

# 4. Generate experiment trade-off charts and Markdown summary report
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli analyze \
  --input tools/analytics/output/high_pressure_comparison.json \
  --output tools/analytics/output

# 5. Evaluate multi-scenario performance across all 5 scenarios
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli compare \
  --input tools/analytics/examples/experiments \
  --output tools/analytics/output

# 6. Analyze real or emulated Android telemetry snapshot
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli android \
  --input tools/analytics/examples/android/pixel7_snapshot.json \
  --output tools/analytics/output
```

---

## 15. Testing & Verification

KERNOX features comprehensive automated verification suites covering both C++20 core libraries and Python analytics.

### Running C++ Test Suites & CLI Smoke Tests
Use the repository verification script:
```bash
# Verify Debug preset (20/20 CTest suites + CLI smoke tests)
./scripts/test.sh dev-debug

# Verify Release preset
./scripts/test.sh dev-release

# Verify ASan / UBSan preset (zero memory leaks, zero UB)
./scripts/test.sh dev-asan
```

### Running Python Test Suite
```bash
PYTHONPATH=tools/analytics/src ./tools/analytics/.venv/bin/pytest tools/analytics/tests -v
```

### Automated End-to-End Demonstration Script
Execute the complete multi-phase demonstration:
```bash
./scripts/demo_kernox.sh
```

---

## 16. Technical Limitations & Boundaries

1. **User-Space Simulation:** Memory reclamation algorithms and process life cycles are simulated in user-space. They demonstrate theoretical operating systems and LMKD concepts without modifying host Linux or Android kernel memory management.
2. **Passive Android Observation:** The ADB subsystem is strictly read-only. KERNOX never issues kill, force-stop, remount, or package management commands to attached devices.
3. **Host Latency vs Algorithmic Complexity:** Reported wall-clock execution times reflect host hardware benchmark runtime rather than formal asymptotic complexity proofs.
4. **WSL2 ThreadSanitizer Limitation & Concurrency Testing:** Under WSL2 kernels, ThreadSanitizer (`dev-tsan`) encounters a known Linux VMA address mapping conflict (`FATAL: ThreadSanitizer: unexpected memory mapping`), meaning dynamic data-race detection is unavailable in this environment. AddressSanitizer and UndefinedBehaviorSanitizer (`dev-asan`) are fully verified and pass 100% for memory safety (bounds, lifetime, leaks, UB), but do not detect data races. Concurrency correctness is instead verified through 20 automated multi-process synchronization tests (`test_sync`) and 5-iteration stress testing (`stress_test.sh`).
5. **PID Type Aliases:** `SimulatedPid` and `AndroidPid` are implemented as `using` type aliases of `uint32_t`. While domain separation is maintained architecturally (simulation logic never includes process-killing kernel APIs, and the ADB subsystem provides no execution endpoints), the type system does not enforce compile-time separation against interchangeable assignment. Strong wrapper structs are recommended for absolute compile-time enforcement.

---

## 17. Repository Structure

```text
KERNOX/
├── CMakeLists.txt                         # Root CMake build configuration
├── CMakePresets.json                      # Build & test presets (debug, release, asan, tsan)
├── README.md                              # Main project documentation
├── scripts/
│   ├── test.sh                            # Automated build and CTest verification runner
│   ├── stress_test.sh                     # Concurrency stress tester
│   └── demo_kernox.sh                     # Automated end-to-end demonstration script
├── include/kernox/
│   ├── core/                              # Result<T>, UniqueFd, ChildProcess, Timer, Logger
│   ├── modules/
│   │   ├── sim/                           # Classical CPU, Disk, Deadlock, Memory algorithms
│   │   ├── system/                        # Real Linux ProcessManager, /proc Telemetry, IPC, Sync
│   │   ├── intel/                         # Process intelligence, SimulatedProcess, Importance
│   │   ├── reclaim/                       # Pressure detector, reclamation engine & policies
│   │   ├── experiment/                    # Workload generation, benchmark runner, exporters
│   │   └── adb/                           # Read-only ADB types, parser, bridge, service
│   └── cli/                               # CLI command dispatcher and formatters
├── src/                                   # Source implementations matching include/
├── tests/                                 # 20 automated C++ CTest test suites
│   ├── fixtures/adb/                      # Validated Android ADB dumpsys & /proc fixtures
│   └── CMakeLists.txt                     # Test registrations
├── tools/analytics/                       # Standalone Python analytics package
│   ├── requirements.txt                   # Pinned Python dependencies
│   ├── config/analytics_config.json       # Thresholds and visualization themes
│   ├── src/kernox_analytics/              # Ingestion, validation, metrics, charts, reports
│   ├── tests/                             # 31 automated pytest unit tests
│   └── examples/                          # Authoritative experiment and snapshot samples
└── docs/                                  # Architectural specifications, roadmaps, and reports
```

---

## 18. Future Scope

1. **eBPF Memory Telemetry:** Integrate extended BPF (eBPF) tracepoints to observe kernel page cache reclaims (`shrink_inactive_list`) on Linux hosts.
2. **Pressure Stall Information (PSI):** Ingest real-time Linux PSI (`/proc/pressure/memory`) metrics alongside Android ADB observations.
3. **Interactive Terminal Dashboard:** Develop a terminal UI (TUI) using ncurses for live, interactive visualization of simulated memory pressure queues.
4. **Multi-Node Distributed Experiments:** Extend workload orchestration to evaluate memory pressure trade-offs across distributed container clusters.
