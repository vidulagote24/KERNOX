# KERNOX 2.0 — Demonstration Guide

**Project:** KERNOX 2.0: Android Resource Intelligence & Memory Reclamation Platform
**Target Audience:** Evaluators, Professors, Systems Engineers, and Students
**Environment:** Linux (Ubuntu 22.04 / 24.04 or WSL 2)

---

## 1. Quickstart Automated Demonstration

KERNOX 2.0 includes a fully automated end-to-end demonstration script that executes all retained project workflows in under 30 seconds without requiring external hardware or network access.

From the repository root (`E:\KERNOX(OS CP)` or `/mnt/e/KERNOX(OS CP)` under WSL):

```bash
# Execute automated demonstration
./scripts/demo_kernox.sh
```

The script will:
1. Verify the C++ binary and Python environment.
2. Query host Linux `/proc` telemetry.
3. Execute process intelligence and importance-aware memory reclamation under high pressure.
4. Benchmark all four policies (FIFO, LRU, Priority, ImportanceAware) on identical workloads, exporting JSON and CSV.
5. Check Android ADB availability and handle offline hardware gracefully.
6. Run the Python analytics pipeline, validating schemas and generating publication-quality figures and Markdown reports in `build/demo_output/analytics`.

---

## 2. Interactive Manual Demonstration Steps

To walk through individual capabilities during a viva or technical presentation, run the following commands sequentially:

### Step 1: System Status & Live Linux Telemetry
Demonstrates type-safe system error handling and real-time sampling of Linux `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, and `/proc/uptime`:
```bash
./build/release/bin/kernox status
```

### Step 2: Internal Core Health Verification
Runs deterministic internal self-tests verifying foundational primitives and the process intelligence model:
```bash
./build/release/bin/kernox self-test
```

### Step 3: Interactive Android Resource Simulator
Launches the full interactive, menu-driven Android resource simulation dashboard:
```bash
./build/release/bin/kernox simulate android
# or
./build/release/bin/kernox sim android
```
**Key Highlights to Demonstrate in the Menu:**
- **Option 2:** Display active process table with Android importance tiers (`Foreground`, `Visible`, `Service`, `Background`, `Cached`).
- **Option 3:** Interactively admit a custom application process.
- **Option 5:** Inject memory pressure (Moderate at 75%, Critical at 90%).
- **Option 6:** Trigger reclamation policies and observe victim selection.
- **Option 9:** View ASCII memory distribution bars and lifecycle state charts.
- **Option 10:** Safe export with overwrite confirmation and automated Python analytics bridge.

### Step 4: Process Intelligence & Memory Reclamation
Demonstrates Android-inspired importance tiers and memory pressure reclamation:
```bash
./build/release/bin/kernox experiment run \
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
./build/release/bin/kernox experiment compare \
  --scenario high-pressure \
  --seed 42 \
  --capacity 1073741824 \
  --format json \
  --output build/demo_output/high_pressure_comparison.json

# Generate comparison CSV
./build/release/bin/kernox experiment compare \
  --scenario high-pressure \
  --seed 42 \
  --capacity 1073741824 \
  --format csv \
  --output build/demo_output/high_pressure_comparison.csv
```

### Step 6: Read-Only Android ADB Subsystem
Demonstrates safe mobile telemetry observation:
```bash
# Check ADB availability and bridge status
./build/release/bin/kernox adb status

# List connected devices (gracefully reports hardware absence if no phone is connected)
./build/release/bin/kernox adb devices
```

### Step 7: Python Analytics & Publication Visualizations
Ingests simulation benchmarks to produce publication-quality figures and reports:
```bash
# Set Python path
export PYTHONPATH="tools/analytics/src:${PYTHONPATH:-}"

# Validate schema and data domain isolation
python3 -m kernox_analytics.cli validate \
  --input build/demo_output/

# Generate policy comparison charts and Markdown summary report
python3 -m kernox_analytics.cli analyze \
  --input build/demo_output/high_pressure_comparison.json \
  --output build/demo_output/analytics

# Generate 5-scenario trade-off evaluation chart
python3 -m kernox_analytics.cli compare \
  --input tools/analytics/examples/experiments \
  --output build/demo_output/analytics
```

---

## 3. Reviewing Generated Visualizations

Open the generated PNG figures in `build/demo_output/analytics/`:

1. **`policy_comparison.png`:** Evaluates Reclaimed Memory (MB), Peak vs Final Footprint (MB), Overall Survival Rate (%), and Foreground Protection (%).
2. **`process_survival.png`:** Grouped bar chart by importance tier demonstrating that `ImportanceAware` shields 100% of Foreground processes while FIFO/LRU suffer priority inversion.
3. **`memory_pressure.png`:** Memory utilization bars evaluated against Moderate (75%) and Critical (90%) pressure thresholds.
4. **`scenario_comparison.png`:** Multi-scenario evaluation across all 5 workloads (`Normal`, `HighMemoryPressure`, `BackgroundHeavy`, `ForegroundProtection`, `Mixed`).
5. **`execution_performance.png`:** Observed host execution wall-clock time ($\mu s$) vs simulated logical duration (ticks).
6. **`experiment_summary.md`:** Consolidated Markdown evaluation report.

---

## 4. Key Takeaways for Evaluators

1. **Deterministic Reproducibility:** Fixed PRNG seeds ensure identical process generation and bitwise reproducible reclamation decisions across runs.
2. **Strict Provenance Separation:** Clear separation between simulated processes in user-space (`kernox_simulation`) and real Linux/Android observations (`android_adb_observation`).
3. **Passive Safety:** The ADB subsystem is strictly read-only and will never terminate, kill, or alter external devices.
4. **Non-Additive Accounting:** Accurate representation of Linux `/proc/meminfo` respecting that `MemAvailable` overlaps with `MemFree` and page cache.
