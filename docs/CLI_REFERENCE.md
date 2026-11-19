# KERNOX 2.0 — Command-Line Interface (CLI) Reference

**Project:** KERNOX 2.0: Android Resource Intelligence & Memory Reclamation Platform
**Standard:** Modern C++20 | **Target Platform:** Linux / POSIX (WSL 2 / Ubuntu 24.04 LTS)
**Binary Location:** `build/release/bin/kernox` or `build/debug/bin/kernox`

---

## 1. Global Commands & Syntax Overview

The KERNOX CLI follows standard POSIX style subcommands and options:
```bash
kernox [global_options] <subsystem> [subcommand] [arguments...]
```

### Global Options
| Flag | Alternative | Description | Exit Code |
| :--- | :--- | :--- | :--- |
| `--help` | `-h` | Display the global or command-specific help menu and exit | 0 |
| `--version` | `-v` | Display platform version, C++ standard, and toolchain info | 0 |
| `status` | — | Query live Linux host telemetry and core utility health | 0 |
| `self-test` | — | Run internal deterministic sanity tests on foundational primitives | 0 |

---

## 2. Command Overview

KERNOX provides 4 primary operational command groups:

```text
kernox
 ├── status              [Host Linux /proc Telemetry]
 ├── self-test           [Internal Deterministic Health Checks]
 ├── simulate (sim)      [Interactive Android Resource Simulator]
 ├── experiment (exp)    [Workload Generation & Policy Benchmarks]
 └── adb                 [Passive Read-Only Android Observation]
```

---

## 3. Detailed Command Reference

### 3.1 Status & Health Checks

#### `kernox status`
Collects and displays host system telemetry sampled directly from Linux `/proc`:
```bash
kernox status
```
Output includes:
- Kernel release, version, node name, and architecture via `uname`
- CPU utilization sampled over 100 ms via `/proc/stat`
- Memory statistics (Used, Total, Available, Usage %) via `/proc/meminfo`
- Load averages (1m, 5m, 15m) via `/proc/loadavg`
- Host system uptime via `/proc/uptime`

#### `kernox self-test`
Executes internal deterministic unit checks verifying foundational primitives:
```bash
kernox self-test
```
Verifies:
1. `Result<T>` monadic propagation and type safety
2. `UniqueFd` RAII lifecycle and pipe IPC transfer
3. Monotonic `Timer` accuracy and positive duration
4. `ChildProcess` lifecycle control and exit status reaping
5. `TelemetryParser` `/proc/meminfo` parsing and unit conversion
6. `SimulatedProcess` process intelligence and importance classification

---

### 3.2 Interactive Android Simulator

#### `kernox simulate android` (alias: `kernox sim android`)
Launches the full interactive, menu-driven Android resource simulation dashboard:
```bash
kernox simulate android
# or
kernox sim android
```
Provides 10 interactive operations for creating custom processes, advancing the simulation clock, injecting memory pressure, executing reclamation policies, generating ASCII visualizations, and triggering Python analytics exports.

---

### 3.3 Workload Experiment Engine

#### `kernox experiment run`
Executes a single reclamation policy against a selected workload scenario:
```bash
kernox experiment run [options]
```

**Options:**
- `--scenario, -s <name>`: Workload scenario name:
  `normal` | `high-pressure` | `background-heavy` | `foreground-protection` | `mixed` (default: `normal`)
- `--policy, -p <name>`: Reclamation policy algorithm:
  `fifo` | `lru` | `priority` | `importance-aware` (default: `importance-aware`)
- `--seed <num>`: Deterministic PRNG seed (default: `42`)
- `--processes, -n <num>`: Total simulated process count
- `--capacity, -c <bytes>`: Total simulated memory capacity in bytes (default: `1073741824` / 1 GiB)
- `--format, -f <fmt>`: Output format: `text` | `json` | `csv` (default: `text`)
- `--output, -o <path>`: Write output directly to a file

**Example:**
```bash
kernox experiment run --scenario high-pressure --policy importance-aware --format json --output result.json
```

#### `kernox experiment compare`
Executes all four policies (`FIFO`, `LRU`, `Priority`, `ImportanceAware`) sequentially against the identical workload:
```bash
kernox experiment compare [options]
```

**Options:**
- `--scenario, -s <name>`: Workload scenario (default: `normal`)
- `--seed <num>`: Deterministic PRNG seed (default: `42`)
- `--processes, -n <num>`: Total simulated process count
- `--capacity, -c <bytes>`: Total simulated memory capacity in bytes
- `--format, -f <fmt>`: Output format: `text` | `json` | `csv` (default: `text`)
- `--output, -o <path>`: Write output directly to a file

**Example:**
```bash
kernox experiment compare --scenario high-pressure --format json --output comparison.json
```

---

### 3.4 Android ADB Telemetry & Observation

All ADB commands are strictly read-only and non-invasive.

#### `kernox adb status`
Inspects ADB host availability, resolving the ADB binary path and reporting connected devices:
```bash
kernox adb status
```

#### `kernox adb devices`
Lists all attached Android devices and emulators:
```bash
kernox adb devices
```

#### `kernox adb memory`
Queries live Android memory statistics via `/proc/meminfo` or `dumpsys meminfo`:
```bash
kernox adb memory [--serial <id>] [--format <text|json>] [--output <path>]
```

#### `kernox adb processes`
Inspects active Android processes, resolving package names, PSS/RSS footprints, and oom_score_adj:
```bash
kernox adb processes [--serial <id>] [--format <text|json>] [--output <path>]
```

#### `kernox adb snapshot`
Generates a consolidated observation snapshot combining device metadata, memory, and process tables:
```bash
kernox adb snapshot [--serial <id>] [--format <text|json>] [--output <path>]
```

---

## 4. Python Analytics CLI Reference

The companion Python package is invoked via the module entry point:
```bash
export PYTHONPATH="tools/analytics/src:${PYTHONPATH:-}"
python -m kernox_analytics.cli <subcommand> [options]
```

### Subcommands
| Subcommand | Description | Example |
| :--- | :--- | :--- |
| `validate` | Validates dataset JSON/CSV schema and strict domain provenance | `python -m kernox_analytics.cli validate --input build/demo_output` |
| `analyze` | Generates metrics, publication charts, and markdown report from an experiment comparison | `python -m kernox_analytics.cli analyze --input comparison.json --output report_dir/` |
| `compare` | Analyzes multi-scenario trade-offs across all 5 workload scenarios | `python -m kernox_analytics.cli compare --input tools/analytics/examples/experiments/ --output report_dir/` |
| `android` | Analyzes Android snapshot memory metrics and generates PSS breakdown chart | `python -m kernox_analytics.cli android --input snapshot.json --output report_dir/` |
| `report` | Generates a consolidated markdown evaluation report | `python -m kernox_analytics.cli report --input comparison.json --output report.md` |
