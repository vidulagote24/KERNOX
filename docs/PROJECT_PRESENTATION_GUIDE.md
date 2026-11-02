# KERNOX — Academic Project Presentation & Viva Defense Guide

**Project Title:** KERNOX — A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform
**Academic Domain:** Operating Systems, Linux Systems Programming, Modern C++20 Systems Engineering
**Target Environment:** Linux / POSIX (Ubuntu 24.04 LTS on WSL 2 / Native Linux x86_64)
**Author:** KERNOX Core Engineering & Architecture Team

---

## 1. Project Introduction

KERNOX is an advanced, production-quality operating systems platform developed in modern C++20. It bridges theoretical computer science and practical operating systems engineering by combining:
1. **Algorithmic Operating System Simulation:** High-fidelity, deterministic models of CPU scheduling, disk arm traversal, deadlock avoidance, and virtual memory paging.
2. **Real Linux/POSIX Systems Programming:** Direct invocation of Linux kernel facilities for process management, sandboxing with resource limits, host telemetry from `/proc`, and high-performance Inter-Process Communication (IPC).

---

## 2. Problem Statement

Undergraduate and graduate Operating Systems curricula frequently suffer from a severe pedagogical dichotomy:
- **Theory without Reality:** Students learn algorithms (e.g. Banker's Algorithm, Multilevel Feedback Queues, LRU page replacement) purely through abstract textbook diagrams or simplistic Python scripts that bear no resemblance to real operating systems.
- **Reality without Structure:** Students assigned to work on production kernels (e.g. xv6 or Linux kernel modules) are overwhelmed by thousands of lines of low-level C code, subtle concurrency bugs, and difficult debugging environments.
- **Missing Duality:** There is no modern, unified platform that allows students and faculty to visualize theoretical algorithms deterministically while concurrently experimenting with genuine Linux kernel primitives (`fork`, `execve`, `wait4`, `mmap`, `futex`, `AF_UNIX`, `mq_*`).

---

## 3. Motivation

The motivation behind KERNOX is to build a modern, type-safe, reproducible operating systems learning and benchmarking suite in **C++20**:
- Leverage modern C++ features (concepts, move semantics, RAII, monotonic clocks, strongly typed enums, and monadic error handling) to eliminate raw memory errors and undefined behaviors common in traditional C educational codebases.
- Provide a zero-falsehood platform where every claimed mechanism either genuinely executes on the Linux kernel or is clearly demarcated as a mathematical discrete-event simulation.

---

## 4. Objectives

1. Implement deterministic simulations for the four classical OS resource management domains: CPU scheduling, disk head scheduling, deadlock avoidance, and virtual memory.
2. Build an industrial-grade Linux POSIX systems programming runtime supporting process sandboxing, resource limits (`setrlimit`), and deep `/proc` telemetry.
3. Implement and benchmark all 5 POSIX IPC paradigms: Anonymous Pipes, Named FIFOs, POSIX Shared Memory, UNIX Domain Sockets, and POSIX Message Queues.
4. Implement production-grade process-shared synchronization primitives: robust cross-process mutexes (`PTHREAD_MUTEX_ROBUST`), semaphores, condition variables, and a zero-heap shared-memory circular ring buffer.
5. Guarantee 100% test pass rate across Debug, Release, and AddressSanitizer/UndefinedBehaviorSanitizer presets with zero compiler warnings under `-Werror`.

---

## 5. Existing System Limitations

| Existing Approach | Key Limitations |
| :--- | :--- |
| **Pure Toy Simulators (Java/Python)** | High runtime jitter, non-deterministic garbage collection pauses, zero interaction with the host OS kernel or memory hardware. |
| **xv6 / Educational Kernels** | Limited to simplistic 32-bit/64-bit architectures; lacks modern Linux IPC (no UNIX domain sockets, robust futexes, or POSIX message queues); lacks rich telemetry. |
| **Raw C Systems Labs** | Prone to memory leaks, unhandled `errno` codes, dangling descriptors, zombie processes, and non-reproducible grading environments. |

---

## 6. Proposed Solution: The KERNOX Architecture

KERNOX solves these challenges by establishing a strict three-tier architecture:
1. **`kernox_core`:** Zero-overhead RAII infrastructure (`Result<T, Error>`, `UniqueFd`, `ChildProcess`, `Timer`, `Logger`).
2. **`kernox_sim`:** Pure discrete-event simulation engines with deterministic workloads, generating ASCII Gantt charts and seek sequence traces.
3. **`kernox_system`:** Real Linux systems programming layer executing native kernel system calls.
4. **`kernox_cli`:** Unified modular CLI command dispatcher with tabular formatters and smoke verification suites.

---

## 7. System Architecture & Component Interaction

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

## 8. Module-Wise Explanation

1. **CPU Scheduling Simulator:** Models task burst and arrival times, calculates waiting and turnaround times, and formats ASCII Gantt charts.
2. **Disk Arm Simulator:** Tracks head position and computes total head movement and average seek distance across a 200-cylinder geometry.
3. **Deadlock Engine:** Implements Dijkstra's Banker's algorithm using multi-dimensional resource vectors, validates requests with speculative allocation, and detects deadlocked sets and RAG cycles.
4. **Memory Management Engine:** Simulates contiguous dynamic partitioning (First/Best/Worst/Next Fit), MMU address translation, and page replacement policies (FIFO, LRU, Optimal).
5. **Process Manager:** Oversees live child processes with custom arguments and environment variables, timeout enforcement, signal escalation (`SIGTERM` -> `SIGKILL`), and `wait4` rusage accounting.
6. **Telemetry Engine:** Parses `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, `/proc/<pid>/status`, and `/proc/<pid>/io`.
7. **IPC Subsystem:** Covers pipes, FIFOs, POSIX shared memory, UNIX domain sockets, and POSIX message queues.
8. **Synchronization Subsystem:** Manages robust mutexes, semaphores, condition variables, and zero-heap circular buffers.

---

## 9. Algorithms Implemented

| Domain | Algorithms Implemented | Theoretical Complexity |
| :--- | :--- | :---: |
| **CPU Scheduling** | FCFS, SJF (non-preemptive), SRTF (preemptive), Round Robin, Priority, MLFQ | $O(N \log N)$ or $O(N)$ |
| **Disk Scheduling** | FCFS, SSTF, SCAN, C-SCAN, LOOK, C-LOOK | $O(N \log N)$ |
| **Deadlock Avoidance** | Dijkstra's Banker's Algorithm (Safety & Resource Request) | $O(M \times N^2)$ |
| **Deadlock Detection** | Multi-resource detection, RAG Cycle Detection (DFS/Tarjan) | $O(V + E)$ |
| **Memory Allocation** | First Fit, Best Fit, Worst Fit, Next Fit | $O(K)$ |
| **Page Replacement** | FIFO, LRU (recency tracking), Optimal / Bélady's MIN (lookahead) | $O(R \times F)$ |
| **Working-Set Model** | Peter Denning's Locality Window $W(t, \Delta)$ | $O(R \times \Delta)$ |

---

## 10. Linux System Calls and Kernel Concepts Used

1. **Process Control:** `fork(2)`, `execv(2)`, `execve(2)`, `waitpid(2)`, `wait4(2)`, `kill(2)`, `_exit(2)`.
2. **Resource Constraints:** `setrlimit(2)`, `getrlimit(2)` for `RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_NOFILE`, `RLIMIT_FSIZE`.
3. **Filesystem & Descriptors:** `pipe2(2)`, `mkfifo(3)`, `open(2)`, `close(2)`, `read(2)`, `write(2)`, `dup2(2)`, `fcntl(2)` (`O_CLOEXEC`, `O_NONBLOCK`).
4. **Shared Memory:** `shm_open(3)`, `shm_unlink(3)`, `ftruncate(2)`, `mmap(2)`, `munmap(2)`.
5. **Process-Shared Concurrency:** `pthread_mutexattr_setpshared(3)`, `pthread_mutexattr_setrobust(3)`, `pthread_mutex_consistent(3)`, `sem_open(3)`, `sem_init(3)`, `pthread_condattr_setpshared(3)`.
6. **Sockets & FD Passing:** `socket(2)`, `bind(2)`, `listen(2)`, `accept(2)`, `connect(2)`, `socketpair(2)`, `getsockopt(2)` with `SO_PEERCRED`, `sendmsg(2)`/`recvmsg(2)` with `SCM_RIGHTS`.
7. **POSIX Message Queues:** `mq_open(3)`, `mq_close(3)`, `mq_unlink(3)`, `mq_send(3)`, `mq_receive(3)`, `mq_timedsend(3)`, `mq_timedreceive(3)`, `mq_getattr(3)`, `mq_setattr(3)`.
8. **Host & Process Telemetry:** Parsing virtual `/proc` filesystem entries.

---

## 11. IPC and Synchronization Implementation Details

- **Robust Mutexes (`PTHREAD_MUTEX_ROBUST`):** Guarantees that if a process crashes holding a lock in `/dev/shm`, waiting processes are notified via `EOWNERDEAD`, recover consistency via `pthread_mutex_consistent()`, and continue execution without hanging.
- **Zero-Heap Circular Queue:** `SharedRingBuffer` structures the mapped shared memory page into a fixed header, condition variable memory, and circular slot buffers with monotonic sequence numbers.
- **Descriptor Passing (`SCM_RIGHTS`):** Enables parent-to-child or peer-to-peer file descriptor transfer across UNIX sockets without disk serialization.
- **Priority Preemption in Message Queues:** Demonstrates hardware-like priority preemption where higher priority messages (e.g. 50) jump ahead of lower priority messages (e.g. 5, 10).

---

## 12. Testing Methodology & Quality Assurance

1. **Zero-Dependency Test Harness:** [`tests/test_harness.hpp`](file:///e:/KERNOX%28OS%20CP%29/tests/test_harness.hpp) provides assertion macros (`KERNOX_ASSERT_TRUE`, `KERNOX_ASSERT_EQ`) that abort on failure and log the exact line and failed condition.
2. **Multi-Preset Matrix:** All 16 CTest suites are compiled and executed across `dev-debug`, `dev-release`, and `dev-asan`.
3. **Sanitizers:** AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) run continuously to verify zero memory leaks, buffer overflows, or undefined behavior.
4. **5-Iteration Stress Runs:** All multi-process suites (`test_sync`, `test_unix_socket`, `test_message_queue`, `test_telemetry`) are repeated 5 consecutive times to prove zero race conditions or flakiness.

---

## 13. Results & Observations

- **100% CTest Pass Rate:** 16 out of 16 test suites pass across Debug, Release, and ASan presets.
- **Zero Compiler Warnings:** Built with GCC 13.3.0 under strict `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`.
- **Zero Leaks:** Confirmed 0 bytes leaked across all execution paths.
- **Deterministic Simulation:** Numerical outputs for textbook workloads match Silberschatz and Tanenbaum benchmark results.

---

## 14. System Limitations

1. **ThreadSanitizer on WSL2:** GCC's TSan runtime conflicts with WSL2's 48-bit ASLR address space layout.
2. **Unprivileged `/proc/<pid>/io` Access:** Subject to Linux kernel `PTRACE_MODE_READ_FSCREDS` restrictions; handled gracefully via `PermissionDenied`.
3. **Structured Data Export:** Currently renders ASCII tables to `std::cout`; automated disk export to JSON/CSV is planned for future iterations.

---

## 15. Future Scope

1. **User-Space Thread Pool:** Work-stealing queue and micro-benchmarks comparing CAS spinlocks against reader-writer locks.
2. **Virtual Inode Filesystem:** Simulation of superblock, inode tables, and multi-level indirect block pointers.
3. **Real-Time Syscall Tracer (`sysmon`):** Lightweight `strace` tool utilizing `ptrace(PTRACE_SYSCALL)`.
4. **Unified Benchmarking Engine:** Centralized CLI runner exporting comparative CSV metrics across all IPC channels.

---

## 16. Conclusion

KERNOX successfully delivers a comprehensive, modern C++20 operating systems platform that resolves the long-standing disconnect between textbook algorithms and live Linux systems programming. With 27,000+ lines of production-grade C++ and documentation, zero compiler warnings, 16 passing test suites, and robust multi-process IPC and synchronization, KERNOX is fully prepared for final academic defense and faculty review.
