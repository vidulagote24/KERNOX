# KERNOX 2.0 — System Scope, Limitations, and Future Directions

**Project:** KERNOX 2.0: Android Resource Intelligence & Memory Reclamation Platform
**Target Architecture:** Linux/POSIX (WSL 2 / Ubuntu 24.04 LTS)
**Engineering Standard:** Truthful, Non-Hyped Systems Engineering

---

## 1. Classification Overview

To maintain zero-falsehood engineering integrity, this document explicitly categorizes all architectural areas of KERNOX 2.0 into:
1. **Fully Implemented & Verified Subsystems**
2. **Deliberate Design Boundaries & Scope Constraints**
3. **Known Technical Assumptions**
4. **Future Research Directions**

---

## 2. Fully Implemented & Verified Subsystems

The following components are implemented in Modern C++20 and Python, verified by 11 C++ test suites (100% passing in Debug and Release) and 36 Python unit tests:

1. **System Telemetry & Status (`kernox_system`):**
   Live, non-invasive observation of the Linux host environment via `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, and `/proc/uptime`.
2. **Process Intelligence Model (`kernox_intel`):**
   Full application process modeling with Android importance tiers (`Foreground`, `Visible`, `Service`, `Background`, `Cached`), distinct CPU scheduling priorities, resident/peak memory tracking, dynamic demand events, and lifecycle state transitions.
3. **Memory Pressure Detection & Reclamation Engine (`kernox_reclaim`):**
   Configurable pressure detection thresholds (Moderate at 75%, Critical at 90%) and four distinct memory reclamation policies:
   - `FIFO` (Eviction by admission order)
   - `LRU` (Eviction by recency of CPU activity)
   - `Priority` (Eviction by numerical CPU priority)
   - `ImportanceAware` (Android-inspired tier-conscious eviction preserving user-facing foreground apps)
4. **Deterministic Workload Experiment Engine (`kernox_experiment`):**
   Deterministic workload generator backed by Mersenne Twister PRNG (`std::mt19937_64`), 5 built-in scenarios (`Normal`, `HighMemoryPressure`, `BackgroundHeavy`, `ForegroundProtection`, `Mixed`), FNV-1a fingerprinting, and structured JSON/CSV export.
5. **Passive Android ADB Subsystem (`kernox_adb`):**
   Read-only Android device discovery, `dumpsys meminfo` telemetry parsing, and process observation tables. Resilient offline handling when hardware is disconnected.
6. **Interactive Android Resource Simulator (`kernox_simulator`):**
   Terminal-driven interactive menu supporting 10 discrete operations, custom process creation, clock advancement, pressure injection, ASCII visualizations, safe overwrite confirmation, and independent project-root discovery.
7. **Python Analytics Platform (`tools/analytics`):**
   Strict domain provenance enforcement, schema validation, non-additive memory metrics calculations, publication-grade matplotlib chart generation, and automated markdown reports.

---

## 3. Deliberate Design Boundaries & Scope Constraints

KERNOX 2.0 was deliberately designed around the following architectural boundaries:

1. **User-Space Simulation vs. Kernel Mod:**
   The memory reclamation engine executes in user space as a simulation environment. It does **not** alter Linux kernel memory management or terminate host processes. This ensures safety and portability across development workstations.
2. **Read-Only Mobile Observation:**
   The ADB subsystem enforces strict read-only semantics. It never installs packages, kills processes on attached phones, or alters Android system properties.
3. **Strict Domain Provenance:**
   Simulation outputs (`kernox_simulation`) and real device observations (`android_adb_observation`) are strictly segregated. Data from simulated runs cannot be passed as live hardware observations, preventing benchmark conflation.

---

## 4. Known Technical Assumptions

- **Host Telemetry Availability:** Telemetry collection assumes a standard Linux `/proc` filesystem structure (standard on Linux and WSL 2).
- **Single-Run Wall-Clock Latency:** Execution timings recorded in C++ represent single-run host wall-clock latencies. As noted in analytics reports, these reflect host measurement conditions and do not represent algorithmic complexity rankings.

---

## 5. Future Research Directions

1. **Linux PSI (Pressure Stall Information) Integration:**
   Extending the system telemetry collector to monitor `/proc/pressure/memory` for kernel-level memory stall metrics on supported Linux kernels.
2. **zRAM & Memory Compaction Modeling:**
   Incorporating simulated zRAM compression ratios and memory compaction overhead into the reclamation simulator.
3. **Real-Time Live Web Visualizer:**
   Providing an optional lightweight WebSocket server to stream simulator state updates to an interactive browser frontend.
