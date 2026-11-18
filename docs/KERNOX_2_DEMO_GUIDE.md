# KERNOX 2.0 — Demonstration Guide

**Project:** KERNOX 2.0: Android & Linux Resource Intelligence Platform
**Target Audience:** Evaluators, Professors, Systems Engineers, and Students
**Environment:** Linux (Ubuntu 22.04 / 24.04 or WSL2)

---

## 1. Quickstart Automated Demonstration

KERNOX 2.0 includes a fully automated end-to-end demonstration script that executes all seven project workflows in under 60 seconds without requiring external hardware or network access.

From the repository root (`E:\KERNOX(OS CP)` or `/mnt/e/KERNOX(OS CP)` under WSL):

```bash
# Execute automated demonstration
./scripts/demo_kernox.sh
```

The script will:
1. Verify the C++ binary and Python virtual environment.
2. Query host `/proc` telemetry.
3. Run classical OS simulations (SRTF CPU scheduling, LRU page replacement, atomic IPC ring buffer).
4. Run process intelligence and importance-aware memory reclamation.
5. Benchmark all four policies (FIFO, LRU, Priority, ImportanceAware) on identical workloads.
6. Check Android ADB availability and fall back safely to offline fixture snapshots.
7. Run Python analytics, generating publication-quality figures and Markdown reports in `build/demo_output/analytics`.

---

## 2. Interactive Manual Demonstration Steps

To walk through individual capabilities during a viva or technical presentation, run the following commands sequentially:

### Step 1: System Status & Live Linux Telemetry
Demonstrates type-safe system error handling and real-time sampling of Linux `/proc/stat`, `/proc/meminfo`, and `/proc/loadavg`:
```bash
./build/debug/bin/kernox status
```

### Step 2: Classical OS Algorithmic Simulation
Demonstrates discrete event simulation with ASCII Gantt charts:
```bash
# CPU Scheduling: Shortest Remaining Time First with dynamic arrivals
./build/debug/bin/kernox schedule --algorithm srtf --workload arrivals

# Virtual Memory: LRU Page Replacement with 3 physical frames
./build/debug/bin/kernox memory replace --algorithm lru --workload page-reference-demo --frames 3

# Deadlock Avoidance: Dijkstra's Banker's Algorithm
./build/debug/bin/kernox deadlock banker --workload safe
```

### Step 3: Linux Systems Programming & IPC
Demonstrates real Linux kernel facilities, resource limits, and inter-process communication:
```bash
# Sandboxed process supervision with timeout enforcement
./build/debug/bin/kernox process run /bin/echo "KERNOX Systems Runtime"

# Shared-Memory Atomic Ring Buffer Demo
./build/debug/bin/kernox sync demo

# UNIX Domain Socket IPC with Peer Credential Checking (SO_PEERCRED)
./build/debug/bin/kernox ipc unix-demo

# POSIX Message Queue with Priority Preemption
./build/debug/bin/kernox ipc mq-demo
```

### Step 4: Process Intelligence & Memory Reclamation
Demonstrates Android-inspired importance tiers and memory pressure reclamation:
```bash
# Run single-policy experiment under high memory pressure
./build/debug/bin/kernox experiment run \
  --scenario high-pressure \
  --policy importance-aware \
  --capacity 1073741824 \
  --format text
```
*Key Point to Highlight:* Notice that under `importance-aware`, background and cached processes are reclaimed first, while foreground UI processes remain 100% protected.

### Step 5: Multi-Policy Comparative Benchmarking
Executes identical synthetic workloads under all four policies (FIFO, LRU, Priority, ImportanceAware) and exports structured data:
```bash
# Generate comparison JSON
./build/debug/bin/kernox experiment compare \
  --scenario high-pressure \
  --seed 42 \
  --capacity 1073741824 \
  --format json \
  --output tools/analytics/output/high_pressure_comparison.json

# Generate comparison CSV
./build/debug/bin/kernox experiment compare \
  --scenario high-pressure \
  --seed 42 \
  --capacity 1073741824 \
  --format csv \
  --output tools/analytics/output/high_pressure_comparison.csv
```

### Step 6: Read-Only Android ADB Subsystem
Demonstrates safe mobile telemetry observation:
```bash
# Check ADB availability and bridge status
./build/debug/bin/kernox adb status

# List connected devices (gracefully reports hardware absence if no phone is connected)
./build/debug/bin/kernox adb devices
```

### Step 7: Python Analytics & Publication Visualizations
Ingests simulation benchmarks and Android observation snapshots to produce publication-quality figures and reports:
```bash
# Activate virtual environment
source tools/analytics/.venv/bin/activate

# Validate schema and data domain isolation
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli validate \
  --input tools/analytics/output/high_pressure_comparison.json

# Generate policy comparison charts and Markdown summary report
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli analyze \
  --input tools/analytics/output/high_pressure_comparison.json \
  --output tools/analytics/output

# Generate 5-scenario trade-off evaluation chart
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli compare \
  --input tools/analytics/examples/experiments \
  --output tools/analytics/output

# Analyze real/emulated Android memory snapshot
PYTHONPATH=tools/analytics/src python3 -m kernox_analytics.cli android \
  --input tools/analytics/examples/android/pixel7_snapshot.json \
  --output tools/analytics/output
```

---

## 3. Reviewing Generated Visualizations

Open the generated PNG figures in `tools/analytics/output/` (or `build/demo_output/analytics/`):

1. **`policy_comparison.png`:** Evaluates Reclaimed Memory (MB), Peak vs Final Footprint (MB), Overall Survival Rate (%), and Foreground Protection (%).
2. **`process_survival.png`:** Grouped bar chart by importance tier demonstrating that `ImportanceAware` shields 100% of Foreground processes while FIFO/LRU suffer priority inversion.
3. **`memory_pressure.png`:** Memory utilization bars evaluated against Moderate (75%) and Critical (90%) pressure thresholds.
4. **`scenario_comparison.png`:** Multi-scenario evaluation across all 5 workloads (`Normal`, `HighMemoryPressure`, `BackgroundHeavy`, `ForegroundProtection`, `Mixed`).
5. **`execution_performance.png`:** Observed benchmark wall-clock elapsed time ($\mu s$) vs simulated logical duration (ticks).
6. **`android/device_memory.png`:** Non-additive comparative measurements of Used, Available, Free, Cached, and Swap RAM against Total RAM.
7. **`android/process_memory.png`:** Top 10 Android processes ranked by Proportional Set Size (PSS, MB) color-coded by importance tier.

---

## 4. Key Takeaways for Evaluators

1. **Deterministic Reproducibility:** Fixed PRNG seeds ensure identical process generation and bitwise reproducible reclamation decisions across runs.
2. **Zero Falsehood Duality:** Clear separation between simulated processes in user-space and real Linux/Android processes.
3. **Passive Safety:** The ADB subsystem is strictly read-only and will never terminate, kill, or alter external devices.
4. **Non-Additive Accounting:** Accurate representation of Linux `/proc/meminfo` respecting that `MemAvailable` overlaps with `MemFree` and page cache.
