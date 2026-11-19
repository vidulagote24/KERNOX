# KERNOX 2.0 — Architecture Specification

**Project:** KERNOX 2.0: Android Resource Intelligence & Memory Reclamation Platform
**Repository:** https://github.com/vidulagote24/KERNOX
**Standard:** Modern ISO C++20 (`-std=c++20`) & Python 3.11+
**Target Environment:** Ubuntu 24.04 LTS (WSL 2 / Linux Kernel 6.6+)
**Build System:** CMake 3.25+ & Ninja

---

## 1. Architectural Mission & Scope

KERNOX 2.0 is an **Android Resource Intelligence & Memory Reclamation Platform** designed to model, benchmark, and evaluate operating system resource management under memory pressure.

The platform focuses on six major functional areas:

1. **Project Status & System Health:** Host Linux `/proc` telemetry (CPU, RAM, load averages, uptime), type-safe error handling, and modular CLI dispatching.
2. **Interactive Android Resource Simulator:** A menu-driven terminal dashboard supporting process creation, workload generation, simulated pressure injection, step-by-step clock advancement, and ASCII visualization.
3. **Memory Pressure & Reclamation Engine:** Dynamic memory pressure detection (Moderate at 75%, Critical at 90%) and four memory reclamation algorithms (FIFO, LRU, Priority, and Android-inspired Importance-Aware).
4. **Workload Experiment & Benchmark Comparison:** Deterministic workload generation across 5 scenarios (`Normal`, `HighMemoryPressure`, `BackgroundHeavy`, `ForegroundProtection`, `Mixed`) and comparative multi-policy benchmarking.
5. **Android ADB Telemetry & Observation:** Passive, read-only Android device discovery, memory observation (`dumpsys meminfo`), process inspection, and offline snapshot parsing.
6. **Python Analytics Platform:** Automated dataset schema validation, non-additive memory metrics computation, statistical analysis, publication-grade figure generation, and markdown reporting.

---

## 2. Target Dependency Architecture

The C++ platform is organized into clean, modular static libraries with strictly unidirectional dependencies:

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
              +-------------------------------+-------------------------------+
              |                               |                               |
              v                               v                               v
    +-------------------+           +-------------------+           +-------------------+
    |  kernox_simulator |           |     kernox_adb    |           |   kernox_system   |
    +-------------------+           +-------------------+           +-------------------+
              |                               |                               |
              v                               | (read-only adb CLI)           | (/proc telemetry)
    +-------------------+                     |                               v
    | kernox_experiment |                     |                     +-------------------+
    +-------------------+                     |                     |    kernox_core    |
              |                               |                     +-------------------+
              v                               |                               ^
    +-------------------+                     |                               |
    |   kernox_reclaim  |                     +---------------+---------------+
    +-------------------+                                     |
              |                                               |
              v                                               |
    +-------------------+                                     |
    |    kernox_intel   |                                     |
    +-------------------+                                     |
              |                                               |
              +-----------------------------------------------+
```

### Library Descriptions

| Target | Responsibilities | Key Dependencies |
| :--- | :--- | :--- |
| `kernox_core` | Foundational primitives: `Result<T, Error>`, `UniqueFd`, `ChildProcess`, `Timer`, `Logger` | Standard C++20, POSIX headers |
| `kernox_system` | Linux `/proc` host telemetry parser and collector (`telemetry.cpp`) | `kernox_core` |
| `kernox_intel` | Simulated application process model, Android lifecycle states, and importance tiers | `kernox_core` |
| `kernox_reclaim` | Pressure detection and reclamation policies (`Fifo`, `Lru`, `Priority`, `ImportanceAware`) | `kernox_intel`, `kernox_core` |
| `kernox_experiment` | Deterministic PRNG workload generator, runner, and JSON/CSV exporters | `kernox_reclaim`, `kernox_intel`, `kernox_core` |
| `kernox_adb` | Passive Android observation service, ADB command bridge, dumpsys parser | `kernox_core` |
| `kernox_simulator` | Interactive menu, ASCII visualizer, terminal reader, and Python analytics bridge | `kernox_experiment`, `kernox_adb`, `kernox_core` |
| `kernox_cli` | Command dispatcher and CLI argument parsing | All subsystems |

---

## 3. Strict Provenance Separation

A primary architectural invariant of KERNOX 2.0 is the **strict separation between simulation and live hardware**:

- **Simulation Datasets:** Tagged with domain `kernox_simulation`. Generated purely in user space by `ExperimentRunner`. Fully reproducible from a random seed.
- **Android Observation Datasets:** Tagged with domain `android_adb_observation`. Captured passively from real devices or emulators via read-only ADB commands.
- **Python Verification:** The Python analytics loader enforces strict domain validation. An attempt to process an Android snapshot with simulation analysis tools or vice-versa raises a `ProvenanceError`.

---

## 4. Subprocess Execution & Security

All cross-boundary operations (ADB inspection and Python analytics invocation) adhere to strict process security:
- **No Shell Interpolation:** Processes are launched directly via `ChildProcess::spawn(executable, args)` using `fork()` and `execve()`.
- **Injection Immunity:** User-supplied paths, filenames, and serial IDs are passed as discrete elements in the `argv` array, preventing shell command injection.
- **Resource Limits & Timeouts:** All child process executions enforce bounded timeouts and automatic process group reaping to prevent hanging or orphaned processes.
