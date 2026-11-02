# KERNOX

## A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform

**GitHub:** https://github.com/vidulagote24/KERNOX\
**Branch:** `master`\
**Latest reported commit:** `489d5ced80eebb00762618b5236fa67dae6e6217`\
**Language:** C++20\
**Environment:** Ubuntu 24.04 LTS on WSL2, GCC 13.3.0, CMake 3.28.3,
Ninja 1.11.1

------------------------------------------------------------------------

## 1. Project Overview

KERNOX is a modular C++20 Operating Systems simulation and Linux systems
programming platform. It combines two complementary areas:

1.  **Operating Systems simulations:** CPU scheduling, disk scheduling,
    memory management, and deadlock algorithms.
2.  **Linux systems programming:** system telemetry, process management,
    inter-process communication (IPC), and POSIX shared memory.

The project provides reusable C++ modules, a command-line interface
(CLI), automated tests, CMake build configuration, and technical
documentation. Its purpose is to make OS concepts executable and
observable while demonstrating practical use of Linux and POSIX
facilities.

This description is based on the implementation and GitHub integration
reports shared so far. Items not conclusively established by those
reports are marked as requiring verification rather than being presented
as completed.

## 2. Project Objectives

-   Demonstrate important Operating Systems algorithms through working
    C++ implementations.
-   Connect theoretical OS concepts with practical Linux/POSIX
    programming.
-   Organize functionality into reusable modules with clear interfaces.
-   Provide CLI access to simulations and system-level demonstrations.
-   Validate behavior with automated tests and repeatable builds.
-   Practice defensive systems programming, resource ownership, error
    handling, process lifecycle management, and robust parsing.
-   Maintain a project that can be extended and demonstrated
    incrementally.

## 3. Technology Stack

  Area                      Technology
  ------------------------- ------------------------------------
  Language                  C++20
  OS                        Ubuntu 24.04 LTS
  Development environment   Windows Subsystem for Linux 2
  Compiler                  GCC 13.3.0
  Build system              CMake 3.28.3
  Build backend             Ninja 1.11.1
  Version control           Git and GitHub
  Testing                   CTest and project test executables
  System interfaces         Linux `/proc` and POSIX APIs

The reported compiler configuration uses
`-Wall -Wextra -Wpedantic -Werror`. The project primarily uses C++20 and
Linux/POSIX facilities.

## 4. Architecture

KERNOX is organized into five broad areas.

### 4.1 Core foundation

The reported core utilities include:

-   `Result` and `Error` abstractions for representing operation
    outcomes.
-   `UniqueFd` for automatic ownership and closing of file descriptors.
-   Timer utilities for measuring elapsed time.
-   Logging utilities.
-   `ChildProcess` abstractions for process lifecycle operations.

These utilities provide common functionality to higher-level modules and
help centralize resource management.

### 4.2 OS simulation layer

This layer contains CPU scheduling, disk scheduling, memory management,
and deadlock-related algorithms.

### 4.3 Linux systems layer

This layer interacts with Linux/POSIX interfaces and includes system
telemetry, process management, IPC, and shared memory.

### 4.4 CLI layer

The CLI provides a user-facing interface for running simulations and
system demonstrations. The integration report identifies command
families or dispatchers for `process`, `ipc`, `shm`, `telemetry`, and
`status`, in addition to simulation commands. Use the executable's help
output for exact current syntax.

### 4.5 Build and testing layer

CMake organizes libraries, executables, and test targets. CTest runs the
registered automated test suites. Debug and sanitizer presets have been
reported.

## 5. Operating Systems Simulation Modules

### 5.1 CPU Scheduling

The CPU scheduling module demonstrates how an operating system selects
processes for CPU execution.

Reported algorithms:

-   **First Come, First Served (FCFS):** processes are selected in
    arrival order; the approach is non-preemptive.
-   **Shortest Job First (SJF):** selects the available process with the
    shortest CPU burst.
-   **Shortest Remaining Time First (SRTF):** preemptive shortest-job
    scheduling; a process may be preempted when another available
    process has a shorter remaining burst.
-   **Round Robin (RR):** gives each process a time quantum and returns
    unfinished processes to the ready queue.
-   **Priority Scheduling:** selects processes according to priority;
    preemptive and non-preemptive variants are reported.
-   **Multilevel Feedback Queue (MLFQ):** uses multiple priority queues
    and can move processes between queues according to scheduling
    behavior. Earlier reports describe periodic boosts and aging in
    MLFQ.

Reported scheduling metrics include waiting time, turnaround time,
response time, makespan, and context-switch tracking.

**Verification note:** Earlier review notes did not establish dynamic
aging for the standalone Priority Scheduling algorithm. Do not claim
that capability unless current source code confirms it.

### 5.2 Disk Scheduling

The disk scheduling module models how I/O requests can be ordered to
reduce disk-head movement.

-   **FCFS:** services requests in arrival order.
-   **SSTF:** chooses the request closest to the current head position.
-   **SCAN:** moves in one direction, servicing requests, then reverses.
-   **C-SCAN:** services requests in one direction and returns to the
    beginning side before continuing.
-   **LOOK:** similar to SCAN but reverses at the last pending request
    rather than necessarily travelling to the physical boundary.
-   **C-LOOK:** circular LOOK variant that moves between the ends of the
    pending request range.

The module reports head movement and seek-related metrics. Boundary
handling and direction behavior are important correctness concerns.

### 5.3 Memory Management

The memory-management module covers contiguous allocation and
paging-related concepts.

**Allocation strategies reported across project work:**

-   **First Fit:** chooses the first free block large enough for the
    request.
-   **Best Fit:** chooses the smallest free block that can satisfy the
    request.
-   **Worst Fit:** chooses the largest available free block.
-   **Next Fit:** appeared in the earlier project scope; verify its
    presence in the final committed implementation before claiming it.

The reported implementation also includes deallocation, free-space
handling/coalescing, and invariant checks.

**Paging and address translation:** The project reports paging-related
address translation functionality. Paging divides memory into fixed-size
units and maps virtual pages to physical frames.

**Page replacement algorithms:**

-   **FIFO:** replaces the page that has been in memory the longest.
-   **LRU:** replaces the page that has not been used for the longest
    time.
-   **Optimal:** replaces the page whose next use is farthest in the
    future; it is useful as a theoretical comparison baseline.

**Working-set model:** The project reports a working-set model for
reasoning about pages actively used over a time window.

### 5.4 Deadlock Management

The deadlock module demonstrates deadlock avoidance and graph-based
analysis.

**Banker's Algorithm:** - Safety checking to determine whether the
system is in a safe state. - Resource-request handling. - Transactional
request behavior and rollback when a request cannot safely be granted.

**Graph-based analysis:** - Earlier reports describe Resource Allocation
Graph cycle analysis. - Wait-For Graph cycle detection is also reported
in the project scope.

The exact graph model and resource assumptions should be checked in the
current source before making detailed claims about detection guarantees.

## 6. Linux Systems Programming Modules

### 6.1 System Telemetry

The telemetry module reads Linux system information from the `/proc`
pseudo-filesystem.

  Procfs source          Information
  ---------------------- -----------------------------------------
  `/proc/stat`           CPU time counters and system statistics
  `/proc/meminfo`        Memory statistics
  `/proc/loadavg`        System load averages
  `/proc/uptime`         System uptime and idle time
  `/proc/[pid]/stat`     Per-process statistics
  `/proc/[pid]/status`   Per-process status information

Reported functionality includes CPU utilization based on counter
sampling/deltas, memory information, load average, uptime, and
per-process telemetry.

Earlier hardening reports describe handling malformed or non-finite
numeric input and bounding proc-file reads. Since `/proc` is a virtual
filesystem, telemetry values depend on the running Linux system and
change over time.

### 6.2 Process Management

The process-management module provides Linux process execution and
supervision functionality.

Reported capabilities include:

-   Creating child processes.
-   Executing programs through an exec-family interface.
-   Passing arguments and environment variables.
-   Applying process resource limits.
-   Monitoring child processes.
-   Enforcing execution timeouts.
-   Sending SIGTERM and escalating to SIGKILL when required.
-   Collecting process resource usage through `wait4`.
-   Reading process information from `/proc`.
-   Reporting child execution errors through a close-on-exec error-pipe
    protocol.

Reported resource limits include `RLIMIT_CPU`, `RLIMIT_AS`,
`RLIMIT_NOFILE`, and `RLIMIT_FSIZE`.

Earlier implementation reports also describe defensive handling of
partial child-error-payload reads and writes, truncated payloads, and
exact payload sizing.

**Lifecycle consideration:** Child reaping needs a clear owner. Broad
process-reaping operations in unrelated code can interfere with a
process manager that expects to collect status for a particular child.
Earlier project documentation describes a single-owner reaping contract
for KERNOX-managed children.

### 6.3 Inter-Process Communication (IPC)

IPC allows separate processes to exchange data. The committed
implementation report identifies anonymous pipes and named FIFOs, along
with benchmarking functionality.

**Anonymous pipes:** provide a byte stream between processes. Earlier
reports describe helpers for handling partial reads and writes.

**Named FIFOs:** allow processes to communicate through a
filesystem-named pipe. The implementation reports FIFO creation and
ownership handling, along with bounded opening behavior.

**I/O and framing helpers reported:** - `write_all` for handling partial
writes. - `read_exact` and `read_up_to` for controlled reads. -
Poll-based timeout handling. - A binary framed-message protocol. -
Close-on-exec file-descriptor handling.

**IPC benchmarks:** The project reports throughput and ping-pong
round-trip latency measurements for pipe/FIFO communication. Results
depend on hardware, process scheduling, message size, and runtime
conditions; they are not universal performance guarantees.

**Scope clarification:** POSIX shared memory is described separately
below. UNIX domain sockets and POSIX message queues appeared in earlier
scope descriptions, but their status as fully implemented and tested
mechanisms is not established by the final integration report alone.

### 6.4 POSIX Shared Memory

The shared-memory module uses POSIX shared-memory interfaces. Reported
APIs include:

-   `shm_open`
-   `ftruncate`
-   `fstat`
-   `mmap`
-   `munmap`
-   `shm_unlink`

Reported functionality includes:

-   Creating shared-memory objects.
-   Opening existing shared-memory objects.
-   Read-only and read-write access modes.
-   Mapping shared memory into a process address space.
-   Resizing shared-memory objects.
-   Managing object ownership and unlink behavior.
-   Move semantics for the shared-memory manager.
-   Access through spans, raw pointers, and typed access helpers.
-   A cross-process demonstration in which a child writes data and the
    parent verifies it.

Shared memory allows processes to access a common memory region without
transferring every data item through a pipe.

**Synchronization distinction:** Shared memory provides shared storage;
it does not automatically make concurrent access safe. Processes
accessing the same region concurrently need a synchronization strategy.
Earlier reports mention synchronization-related coordination in the
shared-memory scope, but the exact guarantees and completeness of the
final implementation should be verified from source and tests.

## 7. Command-Line Interface

The CLI is intended to expose simulations and system demonstrations
without requiring users to call internal C++ APIs directly.

The integration report identifies command families or dispatchers for:

-   OS simulations.
-   `process`
-   `ipc`
-   `shm`
-   `telemetry`
-   `status`

The project reports CLI smoke tests for simulation and system telemetry
commands. Exact subcommand syntax should be taken from the current
executable's help output and repository documentation.

## 8. Testing and Quality Verification

The latest GitHub integration report records:

  Verification                  Reported result
  --------------------------- -----------------
  Debug compilation targets         25/25 built
  Debug CTest suites               13/13 passed
  ASan/UBSan CTest suites          13/13 passed
  CLI smoke tests               Reported passed

The Debug build was reported to compile with strict warning flags,
including warnings treated as errors.

**AddressSanitizer (ASan)** helps detect memory-safety errors such as
invalid memory accesses. **UndefinedBehaviorSanitizer (UBSan)** detects
certain categories of undefined behavior in C/C++.

Passing sanitizer-enabled tests is useful evidence, but does not prove
that all possible execution paths are defect-free.

The integration report lists test coverage for core result/error
utilities, timers, unique file descriptors, child processes, CLI
behavior, CPU scheduling, disk scheduling, deadlock management, memory
management, telemetry, process management, IPC, and shared memory.

The report identifies 13 registered CTest tests. The exact number of
individual test cases and assertions should be obtained from current
test output if required.

## 9. Build and Run

The following representative commands are based on the reported preset
names. Confirm the preset definitions and executable output path in the
current repository.

**Debug build:**

``` bash
cmake --preset dev-debug
cmake --build --preset dev-debug
ctest --preset dev-debug --output-on-failure
```

**Sanitizer build:**

``` bash
cmake --preset dev-asan
cmake --build --preset dev-asan
ctest --preset dev-asan --output-on-failure
```

**CLI help:** Use the executable path defined by the build
configuration. A possible path, if it matches the current CMake output
settings, is:

``` bash
./build/dev-debug/kernox --help
```

## 10. Indicative Repository Organization

The integration report identifies these important paths:

``` text
KERNOX/
├── CMakeLists.txt
├── include/
│   └── kernox/
│       ├── core/
│       ├── cli/
│       └── modules/
│           └── system/
│               ├── ipc.hpp
│               ├── process_manager.hpp
│               ├── shared_memory.hpp
│               └── telemetry.hpp
├── src/
│   ├── core/
│   ├── cli/
│   └── modules/
│       └── system/
│           ├── ipc.cpp
│           ├── process_manager.cpp
│           ├── shared_memory.cpp
│           └── telemetry.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── env_test_helper.cpp
│   ├── test_ipc.cpp
│   ├── test_process_manager.cpp
│   ├── test_shm.cpp
│   └── test_telemetry.cpp
└── docs/
    ├── PROCESS_MANAGEMENT.md
    ├── SYSTEM_TELEMETRY.md
    ├── PROJECT_STATUS_AUDIT.md
    └── TECHNICAL_DESIGN.md
```

This is an indicative structure, not an exhaustive file listing. The
actual repository tree is authoritative.

## 11. GitHub Integration Checkpoint

The latest reported integration details are:

  -------------------------------------------------------------------------------------------------------------------
  Item                                Value
  ----------------------------------- -------------------------------------------------------------------------------
  Repository                          `https://github.com/vidulagote24/KERNOX`

  Branch                              `master`

  Commit                              `489d5ced80eebb00762618b5236fa67dae6e6217`

  Commit message                      `feat: implement KERNOX OS simulations and Linux systems programming modules`

  Files in commit                     21

  Insertions                          6,851

  Deletions                           9
  -------------------------------------------------------------------------------------------------------------------

The integration report states that the commit was pushed to
`origin/master`, the remote HEAD and branch reference pointed to the
reported commit, the local branch tracked `origin/master`, and the
working tree was clean after the push. It also states that no force push
or destructive Git operation was used.

Repository: https://github.com/vidulagote24/KERNOX\
Commit:
https://github.com/vidulagote24/KERNOX/commit/489d5ced80eebb00762618b5236fa67dae6e6217

These details represent the reported state at the time of integration.
Check Git directly for the current state before further development.

## 12. Current Scope: Implemented Work and Items to Verify

### Reported as implemented and committed

-   Core C++ utility abstractions.
-   CPU scheduling simulations.
-   Disk scheduling simulations.
-   Memory management simulations.
-   Deadlock-related algorithms.
-   Linux system telemetry.
-   Process management.
-   Anonymous pipes and named FIFOs.
-   IPC benchmarking.
-   POSIX shared-memory functionality.
-   CLI integration.
-   Automated test registration and test suites.
-   CMake build integration and technical documentation.

### Requires source-level verification before being claimed as complete

-   Exact UNIX domain socket support.
-   POSIX message queue support.
-   Full synchronization feature set, including named semaphores and
    process-shared synchronization primitives.
-   Complete producer-consumer implementation.
-   Dynamic priority aging in standalone Priority Scheduling.
-   Exact behavior and failure guarantees of shared-memory resizing.
-   Current test-case/assertion totals and breadth of stress testing.

These items must be checked against implementation, tests, and CLI
behavior---not only against a plan, header declaration, or earlier
report.

## 13. Recommended Further Development

Further work should be selected after a fresh source-level audit.

### 13.1 Complete the implementation audit

-   Inspect all modules and public interfaces.
-   Match documented features to source code and tests.
-   Check whether CLI commands expose implemented functionality.
-   Identify incomplete behavior and correctness issues.

### 13.2 Improve synchronization and concurrent communication

If not already complete, evaluate and implement:

-   POSIX named semaphores.
-   Process-shared mutexes.
-   Process-shared condition variables.
-   Robust mutex behavior and owner-death recovery where appropriate.
-   A bounded producer-consumer demonstration using shared memory.
-   Correct shutdown, timeout, and abnormal-process-exit behavior.

### 13.3 Expand reliability testing

-   Concurrent producer/consumer stress tests.
-   Abnormal child-termination tests.
-   Resource-cleanup tests.
-   Timeout and cancellation tests.
-   Repeated-execution tests.
-   Sanitizer runs where supported.

### 13.4 Benchmark IPC mechanisms

Compare implemented communication mechanisms using equivalent workloads.
Record message sizes, throughput, latency, process topology, and test
environment. Avoid presenting one machine's measurements as universal
results.

### 13.5 Final integration and demonstration

-   Run Debug and Release builds.
-   Run the complete test suite.
-   Run sanitizer tests.
-   Verify documented CLI examples.
-   Prepare a concise demonstration sequence.
-   Update the README and technical documentation to match verified
    implementation.

## 14. Project Summary

KERNOX combines foundational Operating Systems simulations with
practical Linux systems programming in a modular C++20 application.

The work reported so far spans CPU scheduling, disk scheduling, memory
management, deadlock algorithms, Linux telemetry, process management,
inter-process communication, and POSIX shared memory. It includes a CLI,
CMake-based build configuration, automated tests, and technical
documentation.

The latest reported GitHub checkpoint is commit `489d5ce` on the
`master` branch. The integration report records successful Debug and
sanitizer test runs, with 13 out of 13 CTest tests passing in each
configuration.

The next step is to audit the current source, verify every claimed
capability, close correctness and testing gaps, and then extend the
platform in a controlled way.

**KERNOX --- Connecting Operating Systems theory with practical Linux
systems programming.**
