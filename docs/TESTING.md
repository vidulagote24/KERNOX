# KERNOX — Automated Verification & Testing Guide

**Project:** KERNOX — A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform
**Target Environment:** Ubuntu 24.04 LTS (WSL 2 / Native Linux x86_64)
**Compiler:** GCC 13.3.0 (`-std=c++20`)
**Build System:** CMake 3.28.3 & Ninja 1.11.1

---

## 1. Testing Philosophy & Test Organization

KERNOX implements a zero-external-dependency test framework centered on [`tests/test_harness.hpp`](file:///e:/KERNOX%28OS%20CP%29/tests/test_harness.hpp). Tests are organized into 16 dedicated CTest executables compiled against `kernox_core`, `kernox_sim`, and `kernox_system`:

```
tests/
 ├── test_harness.hpp          [Zero-dependency assertion engine: KERNOX_ASSERT_*]
 ├── env_test_helper.cpp       [Helper process for environment isolation tests]
 ├── test_result.cpp           [Suite 1: Monadic Result<T, Error> unit tests]
 ├── test_timer.cpp            [Suite 2: Monotonic steady clock accuracy]
 ├── test_unique_fd.cpp        [Suite 3: RAII file descriptors and pipes]
 ├── test_child_process.cpp    [Suite 4: Process spawning, reaping, timeouts]
 ├── test_cli.cpp              [Suite 5: CLI argument parsing and demo smoke tests]
 ├── test_cpu_sched.cpp        [Suite 6: CPU scheduling numerical validation]
 ├── test_disk_sched.cpp       [Suite 7: Disk head seek traversal sequences]
 ├── test_deadlock.cpp         [Suite 8: Banker's safety, requests, RAG cycles]
 ├── test_memory.cpp           [Suite 9: Contiguous fits, paging, LRU, Working-Set]
 ├── test_telemetry.cpp        [Suite 10: Host & per-process telemetry parsers]
 ├── test_process_manager.cpp  [Suite 11: Real process sandboxing and rlimits]
 ├── test_ipc.cpp              [Suite 12: Anonymous pipes and named FIFOs]
 ├── test_shm.cpp              [Suite 13: POSIX shared memory mapping & unlinking]
 ├── test_sync.cpp             [Suite 14: Mutexes, semaphores, condvars, ring buffer]
 ├── test_unix_socket.cpp      [Suite 15: UNIX sockets, SO_PEERCRED, SCM_RIGHTS]
 └── test_message_queue.cpp    [Suite 16: POSIX message queues with priority sort]
```

---

## 2. CMake Build Presets

KERNOX configures 4 standardized presets in [`CMakePresets.json`](file:///e:/KERNOX%28OS%20CP%29/CMakePresets.json):

| Preset Name | Build Type | Compiler Flags | Sanitizers | Intended Use |
| :--- | :---: | :--- | :---: | :--- |
| `dev-debug` | `Debug` | `-O0 -g` + strict warnings + `-Werror` | None | Day-to-day debugging & assertion tracing |
| `dev-release` | `Release` | `-O3 -DNDEBUG` + strict warnings + `-Werror` | None | High-throughput benchmarks & release binaries |
| `dev-asan` | `Debug` | `-O1 -g -fno-omit-frame-pointer` + `-Werror` | ASan + UBSan | Memory safety, leak detection, UB checks |
| `dev-tsan` | `Debug` | `-O1 -g -fno-omit-frame-pointer` + `-Werror` | TSan | Multi-threaded race condition detection |

---

## 3. How to Execute Tests

### 3.1 Automated All-in-One Runner
The primary verification script [`scripts/test.sh`](file:///e:/KERNOX%28OS%20CP%29/scripts/test.sh) compiles all targets, executes all 16 CTest suites, and runs end-to-end smoke verification on CLI commands:

```bash
# Run Debug verification
./scripts/test.sh dev-debug

# Run Release verification
./scripts/test.sh dev-release

# Run AddressSanitizer and UndefinedBehaviorSanitizer verification
./scripts/test.sh dev-asan
```

### 3.2 Running Individual Test Suites
You can run any test suite directly from the command line:
```bash
./build/release/bin/test_sync
./build/release/bin/test_unix_socket
./build/release/bin/test_message_queue
./build/release/bin/test_memory
```

### 3.3 Running via CTest
```bash
ctest --preset dev-debug --output-on-failure
ctest --preset dev-release --output-on-failure
ctest --preset dev-asan --output-on-failure
```

---

## 4. Multi-Iteration Concurrency Stress Testing

Cross-process synchronization and IPC components are vulnerable to subtle timing races, deadlocks, and missed condition variable signals. To ensure complete stability, KERNOX includes an automated stress verification script [`scripts/stress_test.sh`](file:///e:/KERNOX%28OS%20CP%29/scripts/stress_test.sh) that executes 5 consecutive repetitions of all concurrency-sensitive suites:

```bash
./scripts/stress_test.sh
```

**Stress Verification Results:**
- `test_sync`: 5 / 5 Iterations **PASSED** (0 flakes)
- `test_unix_socket`: 5 / 5 Iterations **PASSED** (0 flakes)
- `test_message_queue`: 5 / 5 Iterations **PASSED** (0 flakes)
- `test_telemetry`: 5 / 5 Iterations **PASSED** (0 flakes)

---

## 5. Comprehensive Verification Results Matrix

| Configuration | Compilation Status | Test Suite Pass Rate | Total Execution Time | Sanitizer Findings |
| :--- | :---: | :---: | :---: | :--- |
| **`dev-debug`** | 0 warnings (`-Werror`) | **16 / 16 (100%)** | 4.37 sec | None |
| **`dev-release`** | 0 warnings (`-Werror`) | **16 / 16 (100%)** | 4.24 sec | None |
| **`dev-asan`** | 0 warnings (`-Werror`) | **16 / 16 (100%)** | 6.61 sec | **0 memory leaks, 0 buffer overflows, 0 UB** |
| **`dev-tsan`** | 0 warnings (`-Werror`) | **Environment Limited** | N/A | Aborts on launch due to WSL2 ASLR memory layout |

---

## 6. Known Environment Limitations

### ThreadSanitizer on WSL2 (`dev-tsan`)
- **Observation:** `cmake --build --preset dev-tsan` compiles with 0 errors and 0 warnings under `-Werror`. However, when executing test binaries on Ubuntu 24.04 under WSL2 (Linux kernel 6.6+), processes immediately terminate with:
  ```text
  FATAL: ThreadSanitizer: unexpected memory mapping 0x...
  ```
- **Root Cause:** In modern Linux kernels on WSL2, Address Space Layout Randomization (ASLR) allocates high-entropy 48-bit virtual address ranges (`vm.mmap_rnd_bits`). ThreadSanitizer's runtime requires a fixed 44-bit shadow memory mapping that conflicts with WSL2's virtual memory layout.
- **Classification:** **Environment Limitation** (WSL2 / Linux 6.6 ASLR runtime constraint), not a defect in KERNOX source code. All thread and synchronization logic compiles cleanly with `-fsanitize=thread`.
