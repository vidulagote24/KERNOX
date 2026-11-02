# KERNOX

**A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform**

[![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Standard: POSIX](https://img.shields.io/badge/Standard-POSIX.1--2017-orange.svg)](https://pubs.opengroup.org/onlinepubs/9699919799/)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL2-green.svg)](https://ubuntu.com/)
[![Build: CMake & Ninja](https://img.shields.io/badge/Build-CMake%20%7C%20Ninja-red.svg)](https://cmake.org/)
[![Sanitizers: ASan / UBSan](https://img.shields.io/badge/Sanitizers-ASan%20%7C%20UBSan-purple.svg)](https://github.com/google/sanitizers)
[![Tests: 16/16 Passed](https://img.shields.io/badge/Tests-16%2F16%20Passed%20(100%25)-brightgreen.svg)](#12-testing-and-verification)

---

## 1. Project Title & Overview

**KERNOX** is a production-grade academic operating systems platform and systems engineering laboratory implemented in Modern C++20. Designed for undergraduate and graduate computer science students, faculty evaluators, and Linux systems programmers, KERNOX bridges the gap between abstract textbook algorithms and real-world Linux kernel system interfaces.

---

## 2. Problem Statement

Computer Science Operating Systems education has historically suffered from a significant pedagogical gap:
- **Abstract Toy Simulators:** Students often explore algorithms (such as Banker's safety or page replacement) through high-level Python or Java scripts with high runtime jitter, non-deterministic garbage collection, and zero relationship to real kernel hardware interfaces.
- **Unstructured C Systems Labs:** Conversely, traditional C kernel assignments (e.g. xv6 or Linux kernel modules) introduce steep learning curves, subtle memory corruption vulnerabilities, zombie processes, and unhandled `errno` codes that distract from core architectural lessons.
- **Absence of Unified Duality:** Prior to KERNOX, there was no single platform offering deterministic, reproducible discrete-event simulation side-by-side with genuine Linux kernel systems programming.

---

## 3. Project Objectives

1. **Establish Zero-Falsehood Duality:** Clearly demarcate pure mathematical user-space simulations from live Linux kernel facilities.
2. **Implement Four Core OS Simulation Domains:** CPU scheduling, disk arm traversal, deadlock avoidance, and virtual memory paging with ASCII Gantt charts and exact numerical metrics.
3. **Build a Production-Quality Linux POSIX Runtime:** Real process sandboxing, resource constraints (`setrlimit`), graceful signal escalation, and deep `/proc` host and process telemetry.
4. **Implement Comprehensive IPC & Concurrency:** Real anonymous pipes, named FIFOs, POSIX shared memory (`/dev/shm`), robust process-shared mutexes (`PTHREAD_MUTEX_ROBUST`), semaphores, condition variables, zero-heap bounded ring buffers, UNIX domain sockets (`AF_UNIX` with `SO_PEERCRED` and `SCM_RIGHTS`), and POSIX message queues (`mq_*` with priority preemption).
5. **Enforce Modern C++20 Engineering Standards:** Move-only RAII resource guards, zero-exception systems APIs, functional monadic error handling (`Result<T, Error>`), and clean builds under strict compiler flags (`-Werror`) and sanitizers.

---

## 4. Major Features

- **Algorithmic Simulation Engine (`kernox_sim`):**
  - **CPU Scheduling:** FCFS, SJF, SRTF (preemptive), Round Robin, Priority, and Multilevel Feedback Queue (MLFQ) with aging and ASCII Gantt charts.
  - **Disk Arm Traversal:** FCFS, SSTF, SCAN (Elevator), C-SCAN, LOOK, and C-LOOK with Total Head Movement calculations.
  - **Deadlock Management:** Dijkstra's Banker's Algorithm, dynamic resource requests with rollback, multi-instance detection, and RAG cycle detection.
  - **Memory & Virtual Memory:** First Fit, Best Fit, Worst Fit, Next Fit; MMU page translation; FIFO, LRU, Optimal (Bélady's MIN) page replacement; and Peter Denning's Working-Set locality model.
- **Real Linux Systems Programming Layer (`kernox_system`):**
  - **Process Supervision:** Sandboxed process spawning, error propagation pipes, wall-clock timeout enforcement (`SIGTERM` -> `SIGKILL`), POSIX limits (`RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_NOFILE`, `RLIMIT_FSIZE`), and `wait4` rusage accounting.
  - **Procfs Telemetry:** Live host sampling (`/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, `/proc/uptime`) and per-process status and I/O metrics.
  - **Pipes & FIFOs:** RAII unidirectional/bidirectional pipes and named FIFOs with atomic `PIPE_BUF` validation.
  - **POSIX Shared Memory:** RAII `SharedMemory` lifecycle management with owner-PID tracking.
  - **Process-Shared Concurrency:** `ProcessSharedMutex` with `PTHREAD_MUTEX_ROBUST` error recovery (`EOWNERDEAD`), named/unnamed `PosixSemaphore`, and condition variables.
  - **Bounded Ring Buffer:** Zero-heap multi-process circular queue with sequence tracking and cooperative shutdown signaling.
  - **UNIX Domain Sockets:** Stream and datagram sockets, peer credential validation (`SO_PEERCRED`), and zero-copy file descriptor transfer (`SCM_RIGHTS`).
  - **POSIX Message Queues:** Priority preemption (priorities 0–32,767), equal-priority FIFO order, and timed blocking.

---

## 5. Architecture Overview

KERNOX is organized into five modular targets:
```
+-----------------------------------------------------------------------------------------+
|                                    KERNOX CLI / API                                     |
+-----------------------------------------------------------------------------------------+
|                                   kernox_system                                         |
+-------------------+--------------------+------------------------+-----------------------+
| Process & Host    | Inter-Process Comm | Process-Shared Concurr | Advanced POSIX IPC    |
+-------------------+--------------------+------------------------+-----------------------+
| - ProcessManager  | - PipeChannel      | - ProcessSharedMutex   | - UnixSocket (Stream) |
| - ChildProcess    | - FifoChannel      | - PosixSemaphore       | - UnixDatagramSocket  |
| - SystemTelemetry | - SharedMemory     | - ProcessSharedCondVar | - PosixMessageQueue   |
| - Extended Procfs | - UniqueFd RAII    | - SharedRingBuffer     | - SCM_RIGHTS FD Pass  |
+-------------------+--------------------+------------------------+-----------------------+
|                                    Linux Kernel                                         |
|  fork / execve / wait4 / setrlimit / /proc / mmap / shm_open / futex / AF_UNIX / mq_*   |
+-----------------------------------------------------------------------------------------+
```

---

## 6. Technology Stack

- **Language:** ISO C++20 (`-std=c++20`)
- **Compilers:** GCC 13.3.0+ / Clang 16+
- **Build System:** CMake 3.28.3+ & Ninja 1.11.1
- **Target OS:** Ubuntu 24.04 LTS (WSL2 / Native Linux Kernel 6.6+)
- **System Libraries:** `pthread` (POSIX Threads & Robust Mutexes), `rt` (POSIX Realtime Extensions & Message Queues)
- **Quality Assurance:** AddressSanitizer (`ASan`), UndefinedBehaviorSanitizer (`UBSan`)

---

## 7. Module Descriptions

| Module Target | Primary Directory | Description |
| :--- | :--- | :--- |
| **`kernox_core`** | `src/core/`, `include/kernox/core/` | Foundational primitives: `Result<T>`, `Error`, `UniqueFd`, `ChildProcess`, `Timer`, `Logger`. |
| **`kernox_sim`** | `src/modules/sim/`, `include/kernox/modules/sim/` | Deterministic simulation engines: CPU, Disk, Deadlock, Memory. |
| **`kernox_system`**| `src/modules/system/`, `include/kernox/modules/system/` | Real Linux kernel interfaces: ProcessManager, Telemetry, IPC, SHM, Sync, UnixSocket, MessageQueue. |
| **`kernox_cli`** | `src/cli/`, `include/kernox/cli/` | Command dispatcher, argument parser, ASCII tables, and interactive demo runners. |
| **`kernox`** | `src/main.cpp` | Top-level CLI entry point. |

---

## 8. Build Prerequisites

Ensure your Linux environment has the required toolchain installed:
```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build gcc-13 g++-13
```

---

## 9. Installation and Build Instructions

KERNOX supports standardized CMake presets for streamlined configuration and compilation:

```bash
# Clone the repository
git clone https://github.com/vidulagote24/KERNOX.git
cd KERNOX

# Build Release Preset (Optimized)
cmake --preset dev-release
cmake --build --preset dev-release

# Build Debug Preset (Debug Symbols & Assertions)
cmake --preset dev-debug
cmake --build --preset dev-debug

# Build ASan Preset (AddressSanitizer + UndefinedBehaviorSanitizer)
cmake --preset dev-asan
cmake --build --preset dev-asan
```

---

## 10. Running the Project

Run the compiled executable from the build directory:
```bash
# Display global help and command roster
./build/release/bin/kernox --help

# Display platform version and environment
./build/release/bin/kernox --version

# Query live Linux host telemetry
./build/release/bin/kernox status

# Run foundational self-test
./build/release/bin/kernox self-test
```

---

## 11. CLI Examples & Demos

```bash
# 1. CPU Scheduling (Preemptive Shortest Remaining Time First with arrivals)
./build/release/bin/kernox schedule --algorithm srtf --workload arrivals

# 2. Disk Scheduling (Shortest Seek Time First on canonical workload)
./build/release/bin/kernox disk schedule --algorithm sstf --workload canonical

# 3. Deadlock Avoidance (Dijkstra's Banker's Algorithm)
./build/release/bin/kernox deadlock banker --workload safe

# 4. Memory Management (Page Replacement via Least Recently Used)
./build/release/bin/kernox memory replace --algorithm lru --workload page-reference-demo --frames 3

# 5. POSIX Process Sandboxing (Timeout enforcement and address space limits)
./build/release/bin/kernox process run --timeout 2000 --as-limit 50000000 /bin/ls -la /tmp

# 6. Deep Process Telemetry (/proc/<pid>/status & /proc/<pid>/io)
./build/release/bin/kernox process stat 1

# 7. Process-Shared Synchronization (Shared-Memory Bounded Ring Buffer Demo)
./build/release/bin/kernox sync demo

# 8. UNIX Domain Sockets (Stream, SO_PEERCRED, SCM_RIGHTS FD Passing)
./build/release/bin/kernox ipc unix-demo

# 9. POSIX Message Queues (Priority Preemption Delivery & Equal-Priority FIFO)
./build/release/bin/kernox ipc mq-demo
```

---

## 12. Testing and Verification

KERNOX includes 16 standalone automated CTest test suites and an automated verification runner:

### Automated All-in-One Runner
```bash
./scripts/test.sh dev-debug    # Compiles and validates Debug build + smoke checks
./scripts/test.sh dev-release  # Compiles and validates Release build + smoke checks
./scripts/test.sh dev-asan     # Compiles and validates ASan/UBSan build + smoke checks
```

### Multi-Iteration Stress Runner
```bash
./scripts/stress_test.sh       # Runs 5 repetitions of sync, sockets, queues, and telemetry
```

### Verification Matrix
| Preset | Compiler | Flags | CTest Suites | Status |
| :--- | :--- | :--- | :---: | :---: |
| `dev-debug` | GCC 13.3.0 | `-O0 -g -Werror` | **16 / 16** | **100% PASS** |
| `dev-release` | GCC 13.3.0 | `-O3 -DNDEBUG -Werror` | **16 / 16** | **100% PASS** |
| `dev-asan` | GCC 13.3.0 | `-fsanitize=address,undefined -Werror` | **16 / 16** | **100% PASS (0 Leaks / 0 UB)** |
| `dev-tsan` | GCC 13.3.0 | `-fsanitize=thread -Werror` | N/A | **Environment Limited (WSL2 ASLR)** |

---

## 13. Repository Structure

```text
KERNOX/
├── CMakeLists.txt              # Top-level CMake build configuration
├── CMakePresets.json           # Presets (dev-debug, dev-release, dev-asan, dev-tsan)
├── README.md                   # Main project documentation & landing page
├── .gitignore                  # Git exclusion patterns
├── scripts/
│   ├── test.sh                 # Comprehensive compilation & CTest runner
│   └── stress_test.sh          # 5-iteration concurrency stress verification
├── docs/                       # Technical reports & academic defense guides
│   ├── ARCHITECTURE.md         # Full system architecture & Mermaid diagrams
│   ├── CLI_REFERENCE.md        # Complete CLI syntax, options, and commands
│   ├── DEMO_GUIDE.md           # Step-by-step presentation & demo walkthrough
│   ├── TESTING.md              # Test framework, presets, and stress tests
│   ├── VIVA_QUESTIONS.md       # 45 technical viva defense questions & answers
│   ├── LIMITATIONS_AND_FUTURE_SCOPE.md # Explicit boundary & scope disclosures
│   ├── PROJECT_PRESENTATION_GUIDE.md   # 16-point academic project defense guide
│   ├── PROJECT_STATUS_AUDIT.md # Subsystem inventory and verification status
│   └── ...                     # Subsystem-specific technical specifications
├── include/
│   └── kernox/
│       ├── core/               # Result, Error, UniqueFd, ChildProcess, Timer, Logger
│       ├── modules/
│       │   ├── sim/            # CPU, Disk, Deadlock, Memory simulation models
│       │   └── system/         # ProcessManager, Telemetry, IPC, SHM, Sync, UnixSocket, MQ
│       └── cli/                # CommandDispatcher and formatters
├── src/
│   ├── main.cpp                # Main CLI entry point
│   ├── core/                   # Foundation implementations
│   ├── modules/
│   │   ├── sim/                # Simulation implementations
│   │   └── system/             # Real Linux system implementations
│   └── cli/                    # CLI parsing and command dispatchers
└── tests/                      # 16 CTest suites & zero-dependency assertion harness
```

---

## 14. Documentation Directory

- **[System Architecture & Mermaid Diagrams](docs/ARCHITECTURE.md)**
- **[Complete CLI Command Reference](docs/CLI_REFERENCE.md)**
- **[Academic Demonstration & Faculty Review Guide](docs/DEMO_GUIDE.md)**
- **[Automated Verification & Testing Guide](docs/TESTING.md)**
- **[Technical Viva Defense Questions & Answers](docs/VIVA_QUESTIONS.md)**
- **[System Limitations & Future Scope](docs/LIMITATIONS_AND_FUTURE_SCOPE.md)**
- **[Academic Presentation Defense Guide](docs/PROJECT_PRESENTATION_GUIDE.md)**
- **[Repository Status Audit](docs/PROJECT_STATUS_AUDIT.md)**
- **[Technical Design Specification](docs/TECHNICAL_DESIGN.md)**

---

## 15. Current System Limitations

1. **WSL2 ThreadSanitizer ASLR Restriction:** Modern Linux kernels (6.6+) under WSL2 use high-entropy ASLR virtual address spaces that conflict with compiler-rt's fixed shadow memory layout. The code compiles cleanly under `-fsanitize=thread`, but runtime execution under WSL2 is environment-limited.
2. **Kernel Privileges on `/proc/<pid>/io`:** Querying I/O accounting for root-owned processes (e.g. PID 1) requires `CAP_SYS_PTRACE`. KERNOX gracefully reports `PermissionDenied` without failing.
3. **Structured Disk Export:** Benchmark metrics are currently rendered to the terminal via ASCII tables; universal JSON/CSV export to disk files is planned for future releases.

---

## 16. Future Enhancements

1. **User-Space Thread Pool (ThreadPool):** Work-stealing task queues and spinlock vs mutex benchmarks.
2. **Virtual Inode Filesystem:** Inode allocation bitmaps, directory tree structures, and indirect block mapping simulations.
3. **Interactive Syscall Monitor (`sysmon`):** Real-time `ptrace`-based system call tracer.
4. **Cross-Subsystem Benchmark Suite:** Centralized CLI benchmarking command exporting comparative throughput plots.

---

## License & Attribution

KERNOX is an open-source operating systems platform developed for academic evaluation, systems research, and high-performance Linux engineering.
