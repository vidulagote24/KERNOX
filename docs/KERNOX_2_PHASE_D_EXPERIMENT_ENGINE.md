# KERNOX 2.0: Android & Linux Resource Intelligence Platform
## Phase D: Workload Generation & Experiment Engine Specification & Verification Report

**Project:** KERNOX 2.0 — Android & Linux Resource Intelligence Platform
**Phase:** Phase D — Workload Generation & Experiment Engine
**Standard:** Modern C++20 (`-std=c++20`) | Target: Linux / POSIX (WSL2 / Ubuntu 24.04 LTS)
**Status:** COMPLETE & VERIFIED

---

## 1. Executive Summary & Objective

In **Phase B** and **Phase C**, KERNOX 2.0 established the Process Intelligence Engine (`kernox::intel`) and the Memory Pressure & Reclamation Engine (`kernox::reclaim`). While individual policies (FIFO, LRU, Priority, Importance-Aware) could be executed on synthetic static process collections, there was no deterministic, reproducible benchmark harness to evaluate and compare policies under identical dynamic stress conditions.

**Phase D** delivers the **Workload Generation & Experiment Engine** (`kernox::experiment`). This subsystem transforms our algorithms into a rigorous, measurable experimental platform.

### Core Deliverables Achieved:
1. **Deterministic Workload Generation (`WorkloadGenerator`):**
   - Driven by 64-bit Mersenne Twister (`std::mt19937_64`) and an explicit seed.
   - Computes an immutable 64-bit FNV-1a deterministic non-cryptographic workload fingerprint (`WKL-<HEX16>`) verifying that identical logical workloads are presented to competing policies.
   - Five distinct built-in scenario profiles (`Normal`, `HighMemoryPressure`, `BackgroundHeavy`, `ForegroundProtection`, `Mixed`).
2. **Fair Multi-Policy Comparison Framework (`ExperimentRunner`):**
   - Independent simulation state reconstruction for each policy under comparison.
   - Strict process ID generator isolation (`DEFAULT_INITIAL_SIM_PID = 1000`) guaranteeing identical PIDs, identical arrival orders, and identical memory footprints.
   - Zero state leakage between successive policy runs.
3. **Comprehensive Multi-Dimensional Metrics (`ExperimentMetrics`):**
   - Detailed memory telemetry: capacity, initial used, peak resident, final used, average utilization, and total reclaimed bytes.
   - Process lifecycle accounting: completed count, killed-under-pressure count, total survived, and global survival rate.
   - Per-importance tier survival tracking: Foreground, Visible, Service, Background, and Cached.
   - Operational reclamation diagnostics: total invocations, successes, victims, average reclaimed per victim, and allocation failures.
   - Timing measurements: logical simulation ticks and microsecond-precision host wall-clock execution time.
4. **Lightweight Exporter Framework (`ExperimentExporter`):**
   - Zero external dependencies: pure standard library JSON (RFC 8259 compliant escaping) and CSV generation.
   - Structured JSON output consumable by web dashboards and data notebooks.
   - Tabular summary CSV and per-decision trace CSV.
5. **Unified CLI Command Family (`kernox experiment`):**
   - Subcommands `run` and `compare` supporting options for scenario, policy, seed, process count override, memory capacity, output format (`text`, `json`, `csv`), and destination file paths.
6. **Multi-Preset Automated Quality Verification:**
   - 100% pass rate across all 19 test suites (18 existing + `test_experiment.cpp`) under `dev-debug`, `dev-release`, and `dev-asan` (AddressSanitizer + UndefinedBehaviorSanitizer).

---

## 2. Architecture & Subsystem Interactions

```
+-----------------------------------------------------------------------------------------+
|                                    kernox::experiment                                    |
+-----------------------------------------------------------------------------------------+
|                                                                                         |
|  +-----------------------------+                  +----------------------------------+  |
|  |   WorkloadGeneratorConfig   |                  |        WorkloadDefinition        |  |
|  |-----------------------------|                  |----------------------------------|  |
|  | - scenario: ScenarioType    |  Generate()      | - scenario_name / seed           |  |
|  | - seed: uint64_t            |=================>| - total_memory_bytes             |  |
|  | - process_count: size_t     | (mt19937_64)     | - process_specs: std::vector     |  |
|  | - total_memory_bytes        |                  | - fingerprint: "WKL-XXXX..."     |  |
|  +-----------------------------+                  +-----------------+----------------+  |
|                                                                     |                   |
|                                                                     v                   |
|  +------------------------------------------------------------------+----------------+  |
|  |                             ExperimentRunner::compare_all_policies()              |  |
|  +------------------------------------------------------------------------------------+  |
|  |                                                                                    |  |
|  |  +------------------------------------------------------------------------------+  |  |
|  |  | [Policy Run 1: FIFO]            -> Fresh Engine Clone, PID generator reset   |  |  |
|  |  | [Policy Run 2: LRU]             -> Fresh Engine Clone, PID generator reset   |  |  |
|  |  | [Policy Run 3: Priority]        -> Fresh Engine Clone, PID generator reset   |  |  |
|  |  | [Policy Run 4: ImportanceAware] -> Fresh Engine Clone, PID generator reset   |  |  |
|  |  +------------------------------------------------------------------------------+  |  |
|  |                                                                                    |  |
|  +------------------------------------------+-----------------------------------------+  |
|                                             |                                           |
|                                             v                                           |
|                              +------------------------------+                           |
|                              |     ExperimentComparison     |                           |
|                              |------------------------------|                           |
|                              | - policy_results: std::vector|                           |
|                              | - to_table_string()          |                           |
|                              | - to_summary_string()        |                           |
|                              +--------------+---------------+                           |
|                                             |                                           |
|                      +----------------------+----------------------+                    |
|                      v                                             v                    |
|     +--------------------------------+             +--------------------------------+   |
|     |  ExperimentExporter::to_json() |             |  ExperimentExporter::to_csv()  |   |
|     +--------------------------------+             +--------------------------------+   |
+-----------------------------------------------------------------------------------------+
```

---

## 3. Workload Generation Subsystem

### 3.1 Workload Scenarios (`WorkloadScenarioType`)

The generator provides 5 built-in profiles representing diverse operational environments:

1. **`Normal`:**
   - Balanced distribution of interactive and background processes.
   - Aggregate resident memory requirements remain within 50%–75% of total capacity.
   - Serves as the nominal baseline where memory pressure transitions are rare or non-existent.
2. **`HighMemoryPressure`:**
   - Heavy memory consumers admitted simultaneously or across a compressed arrival window.
   - Aggregate memory demand exceeds 120%–160% of capacity.
   - Triggers rapid escalation into Moderate and Critical memory pressure levels, forcing active victim reclamation.
3. **`BackgroundHeavy`:**
   - Models mobile device multitasking where many applications have been placed in the background or retained in cache.
   - High ratio of `Background [400]` and `Cached [500]` processes relative to `Foreground [100]`.
   - Tests a policy's capacity to identify and prune inactive applications without impacting user responsiveness.
4. **`ForegroundProtection`:**
   - Active foreground user interactions accompanied by substantial background churn and dynamic allocation surges.
   - Designed to evaluate whether policies protect user-facing applications or blindly terminate them due to early arrival or low CPU usage.
5. **`Mixed`:**
   - Heterogeneous mix of application types, variable CPU burst durations, dynamic memory allocations and releases, and unpredictable arrival sequences.

### 3.2 Deterministic Non-Cryptographic Fingerprinting (`WKL-<HEX16>`)

To eliminate ambiguity and support reproducible policy comparison, every generated `WorkloadDefinition` calculates a 64-bit FNV-1a hash over:
- Generator seed and scenario identifier.
- Total memory capacity.
- Total process count.
- Process specification parameters (name, priority, importance tier, initial memory, burst ticks, arrival tick).
- Dynamic allocation and release events.

The resultant 64-bit integer is formatted as a 16-character uppercase hexadecimal token prefixed with `WKL-` (e.g., `WKL-4EADB75EFE044AD2`). This fingerprint appears in CLI summaries, JSON exports, and CSV logs, providing a convenient verification token that competing policies received identical inputs. Note that FNV-1a is a lightweight, non-cryptographic hash; while collisions are practically improbable within tested seed spaces, it is not an absolute mathematical collision-free guarantee.

---

## 4. Fair Policy Comparison Engine

### 4.1 Strict Isolation & Anti-Leakage Protocol

In operating systems benchmarking, a policy comparison is valid only if each algorithm is subjected to the identical arrival sequence, identical memory requests, and identical execution timeline.

`ExperimentRunner::compare_all_policies()` guarantees this through:
1. **Independent Engine Instantiation:** Each policy run instantiates an isolated `MemoryReclamationEngine` with the workload's specified capacity and thresholds.
2. **PID Counter Reset:** `intel::SimulatedProcess::reset_pid_generator(DEFAULT_INITIAL_SIM_PID)` is invoked prior to process population generation for every policy run. This guarantees that process $i$ receives the exact same `SimulatedPid` (e.g. 1000, 1001, 1002...) regardless of which policy is executing.
3. **Decoupled Execution State:** Processes are admitted tick-by-tick based on `arrival_tick`. Terminations performed by Policy A have zero influence on Policy B.

### 4.2 Simulation Loop Mechanics

The logical simulation advances in discrete unit ticks:
1. **Admission Phase:** Checks for processes whose `arrival_tick == current_tick`.
   - Registers process with `engine.register_process()`.
   - Attempts initial memory allocation via `engine.allocate()`.
   - If allocation fails due to capacity constraints, memory reclamation is immediately triggered under the assigned policy.
   - If sufficient memory is reclaimed, the process is admitted and transitions to `Ready`.
   - If reclamation cannot free sufficient memory, `failed_allocations` is incremented.
2. **Dynamic Event Phase:** Applies any runtime memory demand spikes or voluntary releases scheduled for `current_tick`.
   - Allocations that breach the Moderate threshold trigger pressure relief according to the policy.
3. **Execution & CPU Phase:** Ready processes advance to `Running`, consume execution ticks against their declared `cpu_burst_ticks`, and transition to `Completed` when execution concludes.
4. **Completion Phase:** When a process completes normally, all its resident memory is returned to the global pool via `engine.release()`.
5. **Metric Sampling:** Samples global memory utilization and pressure levels at every tick.

---

## 5. Experiment Metrics & Formulas

| Metric | Definition / Formula | Operational Meaning |
|:---|:---|:---|
| `survival_rate` | $\frac{\text{processes\_survived}}{\text{total\_processes}}$ | Overall percentage of processes that completed or remained alive. Completed processes are counted as survivors (`processes_survived = processes_completed + alive_at_end = total_processes - processes_terminated_pressure`); only processes terminated by memory reclamation (`was_killed_under_pressure = true`) are non-survivors. |
| `foreground_survival_rate` | $\frac{\text{FG\_survived}}{\text{FG\_total}}$ | Fraction of Foreground processes that completed normally or remained alive without termination under pressure. |
| `cached_survival_rate` | $\frac{\text{Cached\_survived}}{\text{Cached\_total}}$ | Fraction of expendable cached processes retained. |
| `average_utilization_ratio`| $\frac{1}{N} \sum_{t=0}^{N} \frac{\text{used\_bytes}(t)}{\text{capacity\_bytes}}$ | Mean resident memory utilization throughout execution. |
| `processes_terminated_pressure` | Direct counter (`was_killed_under_pressure == true`) | Processes terminated by reclamation algorithms to relieve pressure. |
| `failed_allocations` | Direct counter | Number of allocation requests denied due to memory exhaustion. |
| `wall_clock_elapsed_microseconds` | Host elapsed time | Execution time of the simulation in real host hardware. |

---

## 6. Exporter Subsystem (JSON & CSV)

The `ExperimentExporter` produces external artifacts directly consumable by Python (pandas, matplotlib, seaborn), R, or web dashboards without requiring external libraries.

### 6.1 JSON RFC 8259 Compliance
Strings are escaped to prevent injection or invalid JSON:
- Quotes (`"`) -> `\"`
- Backslashes (`\`) -> `\\`
- Control characters ($< 0x20$) -> `\u00XX` hex encodings.

### 6.2 Comparison CSV Format
Header:
```csv
scenario,seed,fingerprint,capacity_bytes,policy,reclaimed_bytes,processes_reclaimed,processes_survived,total_processes,survival_rate,fg_survival_rate,vis_survival_rate,svc_survival_rate,bg_survival_rate,cached_survival_rate,peak_used_bytes,final_used_bytes,failed_allocations,simulated_ticks,wall_clock_us
```

### 6.3 Decisions Trace CSV Format
Header:
```csv
policy,scenario,seed,victim_pid,victim_name,importance_tier,importance_score,cpu_priority,memory_reclaimed_bytes,timestamp_ticks,reason
```

---

## 7. Command-Line Interface Guide

The CLI exposes the experiment engine via `kernox experiment` (or alias `kernox exp`):

```bash
# Display help and options
kernox experiment --help

# Run single policy simulation
kernox experiment run --scenario normal --policy importance-aware
kernox experiment run --scenario high-pressure --policy fifo

# Run comparative benchmark across all 4 policies
kernox experiment compare --scenario foreground-protection
kernox experiment compare --scenario high-pressure

# Export benchmark comparison to JSON
kernox experiment compare --scenario high-pressure --format json --output benchmark.json

# Export benchmark comparison to CSV
kernox experiment compare --scenario background-heavy --format csv --output benchmark.csv
```

---

## 8. Empirical Benchmark Evaluation

### Scenario: `HighMemoryPressure` (Seed 42, 1024 MiB Capacity)
Workload Fingerprint: `WKL-4EADB75EFE044AD2`

```
========================================================================================================
KERNOX 2.0 EXPERIMENT POLICY COMPARISON BENCHMARK
Scenario: HighMemoryPressure | Seed: 42 | Fingerprint: WKL-4EADB75EFE044AD2 | Capacity: 1024 MiB
--------------------------------------------------------------------------------------------------------
Policy             Reclaimed   Victims  Survived    Survival %   FG Surv %   Peak Util % Fail Allocs
--------------------------------------------------------------------------------------------------------
FIFO               1158.0 MB         6         4         40.0%        0.0%         96.1%           0
LRU                1158.0 MB         6         4         40.0%        0.0%         96.1%           0
Priority            985.0 MB         6         4         40.0%      100.0%         90.7%           0
ImportanceAware    1066.0 MB         6         4         40.0%      100.0%         94.4%           0
========================================================================================================

POLICY TRADE-OFF ANALYSIS:
  * Importance-Aware: Foreground survival rate = 100.0%. [PROTECTION VERIFIED: All foreground tasks survived]
  * FIFO: Reclaimed 1158.0 MB across 6 oldest resident victims.
  * LRU: Reclaimed 1158.0 MB across 6 least recently active victims.
  * Priority: Reclaimed 985.0 MB across 6 low-scheduling-urgency victims.
```

### Simulated Behavioral Observations:
- **FIFO & LRU Under Stress:** In this simulated run, because foreground applications arrived early during initial user interactions, FIFO and LRU selected them as victims, resulting in a **0.0% Foreground Survival Rate** under this specific high-pressure workload.
- **Simulated Importance-Aware Behavior:** The simulated `ImportanceAware` policy achieved a **100.0% Foreground Survival Rate** by strictly cascading eviction through the simulated `Cached`, `Background`, and `Service` tiers, reflecting intended priority-based shielding in this experiment.

---

## 9. Test Suite & Verification Results

The automated test suite `tests/test_experiment.cpp` evaluates the following test modules:

1. `test_workload_generator_determinism`: Identical seeds generate bit-for-bit identical fingerprints; different seeds produce distinct fingerprints; zero capacity or zero process count is rejected with `ErrorCode::InvalidArgument`.
2. `test_scenarios_profiles`: Verifies valid generation and structural integrity across all 5 built-in scenario configurations.
3. `test_fair_comparison_isolation`: Confirms that 4-policy comparison runs maintain identical workload fingerprints, reset PIDs from 1000, and execute with zero cross-policy state leakage.
4. `test_experiment_runner_execution`: Evaluates metric calculation invariants, process accounting, and tick advances.
5. `test_foreground_protection_scenario`: Verifies that `ImportanceAware` maintains superior foreground survival rates compared to naive eviction.
6. `test_exporter_json_and_csv`: Validates RFC 8259 escaping, JSON dictionary keys, comparison CSV column counts, and decision logs.

### Multi-Preset Verification Summary

| Preset | Build Type | Flags / Sanitizers | Result | Details |
|:---|:---|:---|:---|:---|
| `dev-debug` | Debug | `-g -Wall -Wextra -Werror` | **PASS (19/19)** | Zero compiler warnings, 100% CTest pass rate |
| `dev-release` | Release | `-O3 -DNDEBUG -Werror` | **PASS (19/19)** | High performance, zero compiler warnings |
| `dev-asan` | Debug | AddressSanitizer + UndefinedBehaviorSanitizer | **PASS (19/19)** | Zero memory leaks, zero buffer overflows, zero UB |

---

## 10. Contract & Readiness for Phase E

With Phase D complete, the repository provides:
1. `kernox::intel::SimulatedProcess`: Comprehensive process representation.
2. `kernox::reclaim::MemoryReclamationEngine`: Multi-step pressure and victim reclamation.
3. `kernox::experiment::WorkloadGenerator`: Deterministic scenario generation.
4. `kernox::experiment::ExperimentRunner`: Fair comparative policy benchmark harness.
5. `kernox::experiment::ExperimentExporter`: Zero-dependency JSON and CSV serialization.

This establishes the complete data foundation for **Phase E: Live Telemetry, Visual Reporting & Dashboard Integration**. Phase D is officially complete and verified.
