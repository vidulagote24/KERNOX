# KERNOX — System Limitations and Future Scope

**Project:** KERNOX — A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform
**Status:** Phase 5 Final Release Candidate
**Auditor:** KERNOX Systems Engineering Team

---

## 1. Classification Overview

To maintain **Zero-Falsehood Engineering Integrity**, this document explicitly categorizes every architectural domain of KERNOX into:
1. **Fully Implemented & Verified Functionality**
2. **Partially Implemented Functionality**
3. **Known Technical Limitations**
4. **Features Deliberately Not Implemented (Out of Scope)**
5. **Future Scope (Potential Post-Phase 5 Extensions)**

---

## 2. Fully Implemented & Verified Functionality

The following subsystems have been fully implemented in Modern C++20, linked into `kernox`, and verified by 16 automated CTest suites passing 100% across Debug, Release, and AddressSanitizer presets:

1. **CPU Scheduling Engine:** FCFS, SJF, SRTF, Round Robin, Priority Scheduling, and Multilevel Feedback Queue (MLFQ) with priority aging and ASCII Gantt chart generation.
2. **Disk Scheduling Engine:** FCFS, SSTF, SCAN, C-SCAN, LOOK, and C-LOOK with direction tracking and Total Head Movement (THM) metrics.
3. **Deadlock Management Engine:** Dijkstra's Banker's Algorithm, dynamic resource request simulation with automatic state rollback, multiple-instance deadlock detection, and Resource Allocation Graph (RAG) cycle detection via DFS.
4. **Memory Management Engine:** Contiguous First Fit, Best Fit, Worst Fit, Next Fit with dynamic block splitting and coalescing; MMU paging translation; page replacement policies (FIFO, LRU, Optimal/MIN); and Peter Denning's Working-Set model $W(t, \Delta)$.
5. **Real POSIX Process Management:** RAII process spawning (`fork`/`execv`/`execve`), error pipe propagation, wall-clock timeout enforcement with signal escalation (`SIGTERM` -> grace -> `SIGKILL`), POSIX resource constraints (`RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_NOFILE`, `RLIMIT_FSIZE`), and `wait4` `rusage` accounting.
6. **System & Process Telemetry:** Live host sampling (`/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, `/proc/uptime`) and per-process telemetry (`/proc/<pid>/stat`, `/proc/<pid>/status`, `/proc/<pid>/io`).
7. **Pipes & Named FIFOs:** Move-only RAII descriptors, throughput benchmarking, named FIFOs (`mkfifo`), and `PIPE_BUF` atomic write validation.
8. **POSIX Shared Memory:** Move-only RAII `SharedMemory` mapping (`shm_open`, `ftruncate`, `mmap`, `munmap`, `shm_unlink`), owner-PID unlinking, and multi-region coordination.
9. **Process-Shared Synchronization:** Robust process-shared mutexes (`PTHREAD_PROCESS_SHARED`, `PTHREAD_MUTEX_ROBUST`, `EOWNERDEAD` recovery), named and unnamed POSIX semaphores (`sem_open`, `sem_init`), and process-shared condition variables.
10. **Bounded Shared-Memory Ring Buffer:** Zero-heap multi-process circular queue with monotonic slot sequence numbers and cooperative shutdown signaling.
11. **UNIX Domain Sockets:** Full-duplex stream and datagram sockets (`AF_UNIX`), socketpairs, peer credential authentication (`SO_PEERCRED`), and file descriptor transfer (`SCM_RIGHTS`).
12. **POSIX Message Queues:** Move-only RAII `PosixMessageQueue` (`mq_open`, `mq_send`, `mq_receive`, `mq_timedsend`, `mq_timedreceive`), strict priority preemption (priorities 0–32,767), and equal-priority FIFO ordering.

---

## 3. Partially Implemented Functionality

1. **Structured Data Export:**
   - **Current State:** Benchmark numbers and simulation traces are rendered to `std::cout` using formatted ASCII tables and Gantt charts.
   - **Partial Element:** Automated structured machine-readable export (`--format json` or `--format csv` directly to disk files) is not yet universally available across all simulation subcommands.
2. **Workload Ingestion:**
   - **Current State:** Simulations accept standard built-in workloads (`basic`, `arrivals`, `priority-demo`, `canonical`, `allocation-demo`, `paging-demo`) or CLI parameters.
   - **Partial Element:** Loading arbitrary external JSON or YAML trace files from disk is not yet implemented.

---

## 4. Known Technical Limitations

1. **WSL2 ThreadSanitizer ASLR Restriction (`dev-tsan`):**
   - **Limitation:** Running TSan binaries on Ubuntu 24.04 under WSL2 terminates with `FATAL: ThreadSanitizer: unexpected memory mapping`.
   - **Cause:** Linux kernel 6.6 on WSL2 configures high-entropy 48-bit address randomization that conflicts with compiler-rt's fixed shadow memory mapping.
   - **Mitigation:** The codebase compiles cleanly with `-fsanitize=thread` with 0 warnings, and ASan/UBSan runs with 100% pass rate.
2. **Kernel Privileges on `/proc/<pid>/io`:**
   - **Limitation:** Reading `/proc/<pid>/io` for root-owned processes (such as PID 1) requires root privileges (`PTRACE_MODE_READ_FSCREDS`).
   - **Mitigation:** When run as an unprivileged user, KERNOX gracefully falls back, displaying `PermissionDenied` without failing or crashing.
3. **Single-Node Linux Restriction:**
   - **Limitation:** Real systems programming modules (`kernox_system`) rely on POSIX/Linux-specific APIs (`AF_UNIX`, `epoll`, `shm_open`, `mq_open`, `wait4`, `/proc`) and cannot execute on native Windows without WSL or on macOS without POSIX compatibility adjustments.

---

## 5. Features Deliberately Not Implemented (Out of Scope)

The following features were intentionally excluded to maintain architectural focus and academic clarity:
1. **Linux Kernel Loadable Modules (LKM) & Device Drivers:** KERNOX is an educational platform and user-space systems engineering platform, not a bare-metal kernel.
2. **Full In-Memory Filesystems (e.g. ext4/ZFS):** Implementing complete physical disk block drivers and journaling file systems is outside the scope of user-space operating systems simulations.
3. **Distributed Network IPC:** Network sockets (`AF_INET`/`AF_INET6`) were deliberately excluded to focus on local high-performance IPC (`AF_UNIX`, POSIX shared memory, message queues).

---

## 6. Future Scope (Long-Term Extensions)

For future cohorts, researchers, or open-source contributors, the following milestones represent high-value extensions:

1. **User-Space Thread Pool & Work-Stealing Runtime (Phase 5A):**
   - Implement a C++20 thread pool with task stealing and lock-free ring queues.
   - Benchmark spinlocks vs reader-writer locks (`pthread_rwlock_t`) vs `std::mutex`.
2. **Virtual Inode Filesystem Simulator (Phase 5B):**
   - Simulate a virtual disk platter with superblock, inode allocation bitmap, data block bitmaps, and indirect block pointers.
   - Support simulated `mkdir`, `create`, `write`, `read`, and `unlink`.
3. **System Call Tracer (`sysmon` via `ptrace`) (Phase 5C):**
   - Build a lightweight `strace` alternative using `ptrace(PTRACE_SYSCALL)` to intercept and catalog child process system calls in real time.
4. **Unified Cross-Subsystem Benchmark Runner (Phase 5D):**
   - Provide a top-level `kernox bench` command that executes CPU, memory, and IPC benchmarks side-by-side and produces comparative performance plots.
