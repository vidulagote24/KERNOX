# KERNOX 2.0 — Phase E: Android ADB Observation Integration

## 1. Executive Summary

Phase E integrates an optional, strictly read-only Android observation subsystem into **KERNOX 2.0: Android & Linux Resource Intelligence Platform**. This subsystem bridges real-world Android memory and process telemetry into KERNOX without requiring a physical device at compile or test time, without introducing any external Python or Android SDK build dependencies, and with absolute architectural guarantees that real Android devices and Linux host processes will never be terminated or modified.

Through the `kernox_adb` static library and the `kernox adb` CLI command family, KERNOX can discover attached Android devices (physical or emulated), inspect kernel memory parameters via `/proc/meminfo`, query Android runtime memory statistics via `dumpsys meminfo`, observe real Android process importance hierarchies via `dumpsys activity processes`, and export structured JSON or tabular text observation reports.

---

## 2. Architecture & Component Decomposition

The Android observation subsystem is encapsulated entirely within the `kernox::adb` namespace and partitioned into decoupled, single-responsibility components:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           KERNOX CLI / Application                          │
│                     (kernox adb <devices|status|...>)                       │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      AdbObservationService (High-Level)                     │
│  - Target device resolution & multi-device ambiguity enforcement           │
│  - Observation lifecycle orchestration & error aggregation                  │
│  - Report generation (Text & JSON serialization)                            │
└──────────────────────┬───────────────────────────────┬──────────────────────┘
                       │                               │
                       ▼                               ▼
┌──────────────────────────────────────┐     ┌────────────────────────────────┐
│         AdbParser (Pure Logic)       │     │     AdbBridge (Adapter Layer)  │
│  - parse_devices()                   │     │  - Read-only command whitelist │
│  - parse_properties()                │     │  - Shell escaping & safety chk │
│  - parse_proc_meminfo()              │     │  - Safe command routing        │
│  - parse_dumpsys_meminfo()           │     └────────────────┬───────────────┘
│  - parse_dumpsys_processes()         │                      │
└──────────────────────────────────────┘                      ▼
                                             ┌────────────────────────────────┐
                                             │         ICommandRunner         │
                                             ├────────────────────────────────┤
                                             │  SystemCommandRunner (POSIX)   │
                                             │  - fork() + execvp()           │
                                             │  - poll() pipe draining        │
                                             │  - Timeout watchdog & SIGKILL  │
                                             ├────────────────────────────────┤
                                             │  MockCommandRunner (Tests)     │
                                             │  - 100% offline fixture replay │
                                             └────────────────────────────────┘
```

### 2.1 Component Breakdown

1. **`ICommandRunner`**: Abstract interface defining process execution (`execute()`) and binary availability checks (`is_executable_available()`). This abstraction decouples parsing and service orchestration from the host OS process management.
2. **`SystemCommandRunner`**: Concrete implementation executing host processes using POSIX system calls (`pipe2`, `fork`, `execvp`, `poll`, `waitpid`). Never invokes shell wrappers (`/bin/sh -c`) to prevent shell injection.
3. **`MockCommandRunner`**: Deterministic test fixture runner that maps command arguments to pre-recorded outputs, enabling rigorous offline verification without requiring the `adb` daemon or an Android device.
4. **`AdbBridge`**: Safe adapter managing communication with the `adb` binary. Validates all command arguments against a strict safety filter before execution, prepending target device serials and formatting shell invocations.
5. **`AdbParser`**: Pure, side-effect-free parser translating raw Android shell command outputs (`adb devices -l`, `getprop`, `/proc/meminfo`, `dumpsys meminfo`, `dumpsys activity processes`) into strongly typed domain structures.
6. **`AdbObservationService`**: Top-level orchestration service providing high-level queries (`discover_devices()`, `query_memory()`, `query_processes()`, `capture_snapshot()`) with automatic device resolution and provenance tracking.

---

## 3. Safe Command Execution Layer

Interacting with external command-line tools from C++ carries inherent risks of shell injection, pipe deadlocks, and process hangs. KERNOX Phase E implements a hardened execution layer adhering to strict systems programming principles:

### 3.1 Direct `execvp()` Without Shell Wrappers
Commands are never passed to `/bin/sh -c` or `system()`. Arguments are constructed as individual null-terminated strings (`char* argv[]`) and passed directly to `execvp()`. This eliminates shell parsing quirks, variable expansions, and arbitrary command chaining.

### 3.2 Positive Read-Only Command Allowlist
The `AdbBridge::is_safe_read_only_command()` filter strictly enforces a positive allowlist of authorized read-only command forms:
- `adb devices [-l]`
- `adb get-state`
- `adb shell getprop [property]`
- `adb shell dumpsys meminfo [package_or_flag]`
- `adb shell dumpsys activity processes`
- `adb shell cat /proc/meminfo`
- `adb shell ps [-A] [-A -o <columns>]`

Device-selection arguments (`-s <serial>`) are strictly validated (`[a-zA-Z0-9._:-]`, max 64 characters, cannot start with `-`).

All other command forms and dangerous inputs are unconditionally rejected at the library boundary with `core::ErrorCode::PermissionDenied`:
- **Unlisted Subcommands**: `forward`, `reverse`, `push`, `pull`, `install`, `uninstall`, `reboot`, `remount`, `root`, `tcpip`, `sync`, `logcat`, `bugreport`
- **Process Termination**: `kill`, `killall`, `am force-stop`, `pm clear`
- **System Alteration**: `setprop`, `su`, `rm`, `rmdir`, `mkfs`, `format`, `wipe`
- **Unauthorized Paths**: any `cat` target other than `/proc/meminfo`
- **Command Chaining & Metacharacters**: `;`, `&&`, `||`, `|`, `>`, `<`, backticks, `$`, newlines, control characters

### 3.3 Concurrency & Pipe Deadlock Prevention
Standard POSIX pipes buffer up to 64 KiB of data. If a child process produces excessive output on stderr while the parent synchronously reads stdout (or vice-versa), the child blocks writing to the filled pipe while the parent waits for child termination—causing an unrecoverable deadlock.

`SystemCommandRunner` solves this using non-blocking I/O and `::poll()` multiplexing:
- Standard output and standard error pipes are configured with `O_NONBLOCK`.
- A single `::poll()` loop monitors both read file descriptors simultaneously.
- Sockets and pipes are drained in chunks until `EOF` (`POLLHUP` / 0 bytes read).
- Deadlock is mathematically impossible regardless of output size.

### 3.4 Timeout Watchdog & Clean Child Reaping
External commands can hang indefinitely due to unresponsive ADB daemons or stuck USB connections. Every command execution enforces an explicit timeout (default: 5000 ms).
- If the timeout expires, `SystemCommandRunner` flags the execution as timed out.
- The runner issues `SIGTERM` to the child process, pauses 50 ms, and escalates to `SIGKILL` if the child has not exited.
- `::waitpid()` is called to reap the child and prevent zombie processes.

---

## 4. Device Discovery & Ambiguity Resolution

Real-world development environments frequently have multiple physical devices or emulators connected simultaneously. Silent selection in multi-device environments leads to observation corruption.

`AdbObservationService` enforces strict resolution rules:

1. **Zero Devices**: Returns an explicit `ErrorCode::NotFound` indicating that no Android devices are currently connected.
2. **Single Device**:
   - If the device is in state `Device` (online), it is automatically selected as the active target.
   - If the device is in state `Unauthorized` or `Offline`, the query fails with an actionable diagnostic message requesting user USB debugging authorization.
3. **Multiple Devices**:
   - If no explicit serial is passed via `--serial`, the service **refuses silent selection**, returning `ErrorCode::InvalidArgument` and listing all discovered device serials.
   - If an explicit serial is specified, the service validates that the device exists in the connected device list before issuing commands.

---

## 5. Telemetry Parsing & Graceful Degradation

Android devices vary drastically across Android versions, kernel revisions, and vendor customizations. The parsing layer handles discrepancies gracefully:

### 5.1 `/proc/meminfo` & Kernel Compatibility
Modern Linux kernels (>= 3.14) provide `MemAvailable:` as an estimate of how much memory can be allocated without swapping. However, older Android kernels (e.g., Linux 3.10 on legacy devices) do not expose `MemAvailable:`.
- `AdbMemorySnapshot::mem_available_bytes` is defined as `std::optional<uint64_t>`.
- When `MemAvailable:` is present, used RAM is computed as:
  $$\text{MemUsed} = \text{MemTotal} - \text{MemAvailable}$$
- When `MemAvailable:` is missing, the parser falls back to:
  $$\text{MemUsed} = \text{MemTotal} - (\text{MemFree} + \text{Buffers} + \text{Cached})$$
  and flags the snapshot with `ObservationConfidence::Partial`.

### 5.2 `dumpsys meminfo` Hierarchy
When available, `dumpsys meminfo` provides Android runtime memory allocations:
- Extracts global totals: **Total PSS**, **Total RSS**, **Free RAM**, **Used RAM**, and **Lost RAM**.
- Parses the **App Summary** breakdown: Java Heap, Native Heap, Code, Stack, Graphics, Private Other, and System.
- Parses per-process memory footprints: Process Name, Android PID, Total PSS, and Private Dirty bytes.

### 5.3 OOM Score & Process Importance
Android assigns `oom_score_adj` values (-1000 to 1000) representing process vulnerability to low-memory killer (LMK) termination.
- Extracted via `dumpsys activity processes` or per-process `oom_score_adj` queries.
- Mapped to KERNOX `ImportanceTier` (Foreground, Visible, Service, Cached, Background).

---

## 6. Strict Type Safety & PID Separation

A fundamental design requirement of KERNOX is that the simulator must never terminate or alter real host or Android processes. To enforce this at compile time:

```cpp
// Explicit type aliases in separate namespaces:
namespace kernox::adb {
    using AndroidPid = uint32_t; // Strictly real-device Android process ID
}

namespace kernox::intel {
    using SimulatedPid = uint32_t; // Strictly simulated process ID
}
```

- Real Android PIDs are captured exclusively within `AdbProcessObservation` as `AndroidPid`.
- `SimulatedPid` is used exclusively within `ProcessIntelligenceEngine` and `MemoryPressureEngine`.
- No conversion constructor, implicit cast, or function exists that passes an `AndroidPid` to process termination functions (`ChildProcess::terminate()`, `::kill()`, etc.).
- All observation structures carry an immutable provenance tag:
  ```json
  "provenance": "android_adb_observation"
  ```
  ensuring that real-world observations can never be confused with simulated telemetry in automated pipelines.

---

## 7. Command-Line Interface (CLI)

The `kernox adb` command suite provides direct access to real Android telemetry:

```bash
# Display help and available subcommands
kernox adb --help

# Inspect ADB binary availability and connection status
kernox adb status

# List all attached Android devices and their states
kernox adb devices

# Inspect real-device memory telemetry (formatted text or JSON)
kernox adb memory
kernox adb memory --format json
kernox adb memory --serial emulator-5554

# Inspect real-device active processes and footprints
kernox adb processes
kernox adb processes --format json

# Export comprehensive snapshot report to file
kernox adb snapshot --format json --output /tmp/android_snapshot.json
```

---

## 8. Hardware Independence & Testing Architecture

CI/CD runners, build servers, and student workstations typically do not have physical Android devices or running emulators. KERNOX Phase E achieves **100% hardware-independent testing** through comprehensive fixture mocking:

### 8.1 Static Fixtures (`tests/fixtures/adb/`)
1. `devices_empty.txt`: ADB output when no devices are connected.
2. `devices_single.txt`: Single online emulator (`emulator-5554`).
3. `devices_multiple.txt`: Multiple physical and virtual devices attached.
4. `devices_unauthorized.txt`: Device requiring user authorization on screen.
5. `devices_offline.txt`: Disconnected or booting device.
6. `getprop_pixel.txt`: System property dump from Google Pixel 7.
7. `meminfo_standard.txt`: Standard `/proc/meminfo` with `MemAvailable:`.
8. `meminfo_no_available.txt`: Legacy kernel `/proc/meminfo` missing `MemAvailable:`.
9. `meminfo_malformed.txt`: Malformed/corrupted memory info testing resilience.
10. `dumpsys_meminfo_summary.txt`: Real Android `dumpsys meminfo` summary block.
11. `dumpsys_meminfo_processes.txt`: Real Android per-process memory table.
12. `dumpsys_activity_processes.txt`: Real Android OOM adj process rankings.

### 8.2 Test Suite Verification (`test_adb_parser`)
The test suite in `tests/test_adb_parser.cpp` validates:
- Device discovery and parsing across all 5 connection states.
- Multi-device ambiguity error handling and serial filtering.
- Property parsing (ABI, Android version, SDK level, security patch).
- `/proc/meminfo` standard and fallback parsing math.
- `dumpsys meminfo` App Summary and process table parsing.
- OOM score extraction and `ImportanceTier` mapping.
- Safety validator rejection of destructive tokens (`kill`, `force-stop`, `pm clear`, `su`, `setprop`).
- Mock execution runner command call tracking and argument matching.
- JSON and text snapshot report generation.

---

## 9. Verification Across Multi-Preset Build Configurations

All 20 test suites in KERNOX passed cleanly across all build presets:

| Preset | Compiler Flags | Tests Run | Result | Sanitizer Findings |
|---|---|---|---|---|
| `dev-debug` | `-g -std=c++20 -Wall -Wextra -Wpedantic -Werror` | 20 / 20 | **100% PASSED** | None |
| `dev-release` | `-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror` | 20 / 20 | **100% PASSED** | None |
| `dev-asan` | `-g -fsanitize=address,undefined -fno-omit-frame-pointer` | 20 / 20 | **100% PASSED** | Zero leaks, zero UB |

---

## 10. Phase F Transition Contract

Phase E completes the observation and telemetry ingestion layer. In Phase F (CLI, System Showcase & Platform Polish), Phase E components will be integrated to:
1. Provide a live Android telemetry feed capable of parameterizing Phase D experiment workloads with real app memory footprints.
2. Compare real Android LMK `oom_score_adj` decisions against KERNOX simulated reclamation policies (`AndroidLmkReclamationPolicy`, `WorkingSetProportionalPolicy`).
3. Display real device status alongside Linux host telemetry in consolidated system showcases.
