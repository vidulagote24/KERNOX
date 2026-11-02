# KERNOX — Academic Demonstration & Viva Showcase Guide

**Project:** KERNOX — A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform
**Audience:** Operating Systems Faculty, Academic Reviewers, and Systems Engineers
**Target Platform:** Ubuntu 24.04 LTS (WSL2 / Linux Kernel 6.6+)
**Binary Location:** `./build/release/bin/kernox`

---

## 1. Executive Demonstration Roadmap

This guide provides an end-to-end walkthrough for demonstrating KERNOX in an academic evaluation or viva presentation. The demonstration highlights the core architectural achievement of the project: **Zero-Falsehood Duality**:
1. **Algorithmic Operating System Simulation:** Pure, deterministic user-space models of kernel scheduling, memory management, and deadlock avoidance.
2. **Real Linux/POSIX Systems Programming:** Direct invocation of Linux kernel system calls (`fork`, `execve`, `wait4`, `setrlimit`, `mmap`, `futex`, `AF_UNIX`, `mq_*`, and `/proc` telemetry).

```
Demonstration Sequence (Approx 15–20 minutes):
  Part 1: Platform & Host Telemetry (kernox status, self-test)
  Part 2: CPU Scheduling & Gantt Charts (SRTF, MLFQ)
  Part 3: Disk Arm Scheduling & Head Traversal (SSTF, SCAN)
  Part 4: Deadlock Management & Banker's Algorithm (Safety, Request Rollback, RAG)
  Part 5: Memory Management & MMU Virtual Memory (Paging, LRU, Working Set)
  Part 6: Real POSIX Process Management & Limits (Sandboxing, wait4 rusage)
  Part 7: Process-Shared Synchronization & Ring Buffer (Robust Mutex, CondVar)
  Part 8: UNIX Domain Sockets & Descriptor Passing (SO_PEERCRED, SCM_RIGHTS)
  Part 9: POSIX Message Queues & Priority Preemption (mq_open, Priorities 0-50)
```

---

## 2. Step-by-Step Demonstration Script

### Part 1: Platform Scaffolding & Live Host Telemetry

#### Command 1.1: Platform Status & Real Host Telemetry
```bash
./build/release/bin/kernox status
```
- **What It Demonstrates:**
  - Reads directly from Linux `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, and `/proc/uptime`.
  - Calculates live CPU utilization percentage over a 100ms delta without floating-point inaccuracies.
  - Queries physical RAM breakdown (total, used, available, buffers, cache).
- **Faculty Discussion Point:**
  - *"Why calculate CPU utilization over a delta instead of reading a single instantaneous value?"*
  - **Answer:** `/proc/stat` stores cumulative CPU tick counters since boot. An instantaneous read only gives total ticks; to calculate current CPU load, one must sample ticks at $T_1$, sample at $T_2 = T_1 + \Delta t$, and compute $\frac{\Delta \text{ActiveTicks}}{\Delta \text{TotalTicks}} \times 100\%$.

#### Command 1.2: Foundation Self-Test
```bash
./build/release/bin/kernox self-test
```
- **What It Demonstrates:** Deterministic validation of `Result<T>` monadic error handling, `UniqueFd` descriptor safety, monotonic clock accuracy, and logging severity.

---

### Part 2: CPU Scheduling Simulator

#### Command 2.1: Preemptive Shortest Remaining Time First (SRTF)
```bash
./build/release/bin/kernox schedule --algorithm srtf --workload arrivals
```
- **What It Demonstrates:**
  - Discrete-event simulation of task arrivals at staggered time intervals.
  - Dynamic preemption when a shorter job arrives while a longer job is executing.
  - Generates an ASCII Gantt chart with exact context switch overhead tracking.
  - Computes Average Turnaround Time ($\text{TAT} = \text{Completion} - \text{Arrival}$) and Average Waiting Time ($\text{WT} = \text{TAT} - \text{Burst}$).

#### Command 2.2: Multilevel Feedback Queue (MLFQ)
```bash
./build/release/bin/kernox schedule --algorithm mlfq --workload priority-demo --quantum 2 --boost 10
```
- **What It Demonstrates:**
  - 3 priority queues ($Q_0, Q_1, Q_2$) with decreasing priority and increasing time slices.
  - Dynamic demotion of CPU-bound processes.
  - Periodic priority boost (interval = 10) to prevent starvation of low-priority tasks (aging).

---

### Part 3: Disk Scheduling Simulator

#### Command 3.1: Shortest Seek Time First (SSTF) vs SCAN
```bash
./build/release/bin/kernox disk schedule --algorithm sstf --workload canonical
./build/release/bin/kernox disk schedule --algorithm scan --workload canonical --direction up
```
- **What It Demonstrates:**
  - Canonical Silberschatz textbook workload (`98, 183, 37, 122, 14, 124, 65, 67`, starting at head position 53).
  - SSTF selects the nearest cylinder, reducing Total Head Movement (THM) to 236 cylinders.
  - SCAN (Elevator algorithm) sweeps in one direction to the platter boundary (cylinder 199) before reversing, achieving 208 cylinders.
- **Faculty Discussion Point:**
  - *"Why does SSTF suffer from starvation, and how does SCAN solve it?"*
  - **Answer:** In SSTF, a continuous stream of requests near the current head position will starve distant cylinders indefinitely. SCAN guarantees bounded waiting time by servicing requests in one directional sweep across the entire platter.

---

### Part 4: Deadlock Management Engine

#### Command 4.1: Dijkstra's Banker's Algorithm
```bash
./build/release/bin/kernox deadlock banker --workload safe
```
- **What It Demonstrates:**
  - Multi-resource safety algorithm with vector arithmetic: $\text{Need}[i][j] = \text{Max}[i][j] - \text{Allocation}[i][j]$.
  - Identifies whether the system is in a safe state and computes a valid execution sequence (e.g. $P_1 \to P_3 \to P_4 \to P_0 \to P_2$).

#### Command 4.2: Resource Request with Speculative Allocation & Rollback
```bash
./build/release/bin/kernox deadlock request --workload safe --process 1 --request 1,0,2
```
- **What It Demonstrates:**
  - Speculative transaction: $\text{Available} \leftarrow \text{Available} - \text{Request}$, $\text{Allocation} \leftarrow \text{Allocation} + \text{Request}$, $\text{Need} \leftarrow \text{Need} - \text{Request}$.
  - Runs Banker's safety check on the proposed state. If unsafe, rolls back to original vectors immediately.

#### Command 4.3: Resource Allocation Graph (RAG) Cycle Detection
```bash
./build/release/bin/kernox deadlock graph --workload cycle-demo
```
- **What It Demonstrates:**
  - Builds a directed bipartite graph (Processes $\to$ Resources and Resources $\to$ Processes).
  - Executes Tarjan/DFS cycle detection with back-edge path extraction to isolate the deadlocked loop.

---

### Part 5: Memory Management & Virtual Memory Engine

#### Command 5.1: Contiguous Allocation Strategies
```bash
./build/release/bin/kernox memory allocate --algorithm first-fit --workload allocation-demo
./build/release/bin/kernox memory allocate --algorithm best-fit --workload allocation-demo
```
- **What It Demonstrates:**
  - Compares First Fit (first block $\ge \text{size}$) against Best Fit (smallest block $\ge \text{size}$).
  - Dynamic block splitting and instant coalescing of adjacent free blocks upon deallocation.

#### Command 5.2: MMU Paging & Address Translation
```bash
./build/release/bin/kernox memory translate --process 0 --address 4096 --workload paging-demo
```
- **What It Demonstrates:**
  - Logical address breakdown: $\text{Page Number} = \lfloor \text{Address} / \text{PageSize} \rfloor$, $\text{Offset} = \text{Address} \pmod{\text{PageSize}}$.
  - Checks page validity bit, looks up frame table, and calculates physical address: $(\text{Frame} \times \text{PageSize}) + \text{Offset}$.
  - Correctly distinguishes simulated Page Faults (page not in memory) from Segmentation Faults (address exceeds virtual limit).

#### Command 5.3: Page Replacement (LRU vs Optimal) & Working-Set Locality
```bash
./build/release/bin/kernox memory replace --algorithm lru --workload page-reference-demo --frames 3
./build/release/bin/kernox memory ws --workload page-reference-demo --window 4
```
- **What It Demonstrates:**
  - 20-reference page access string simulated across 3 physical frames.
  - LRU tracks least-recently referenced page; Optimal looks ahead into future references (Bélady's MIN).
  - Peter Denning's Working-Set model $W(t, \Delta)$ tracks unique pages referenced within window $\Delta = 4$.

---

### Part 6: Real POSIX Process Management & Limits

#### Command 6.1: Sandboxing & Resource Limits
```bash
./build/release/bin/kernox process run --timeout 2000 --as-limit 50000000 /bin/ls -la /tmp
```
- **What It Demonstrates:**
  - Spawns `/bin/ls` via `fork()` + `execv()`.
  - Configures POSIX resource limits (`RLIMIT_AS` = 50 MB) before execution in the child process.
  - High-resolution accounting via `wait4()` capturing user CPU time, system CPU time, and peak RSS.

#### Command 6.2: Enriched Process Telemetry
```bash
./build/release/bin/kernox process stat 1
```
- **What It Demonstrates:**
  - Direct kernel telemetry of PID 1 (`systemd` / `init`): state, thread count, 4-tuple UIDs/GIDs, context switches (voluntary and nonvoluntary), memory footprint, and I/O stats.

---

### Part 7: Process-Shared Synchronization & Ring Buffer

#### Command 7.1: Bounded Shared-Memory Ring Buffer Demo
```bash
./build/release/bin/kernox sync demo
```
- **What It Demonstrates:**
  - Allocates shared memory via `shm_open()` + `mmap()`.
  - Initializes `ProcessSharedMutex` (`PTHREAD_PROCESS_SHARED`, `PTHREAD_MUTEX_ROBUST`) and `ProcessSharedConditionVariable`.
  - Zero-heap circular queue with monotonic slot sequence numbers.
  - Forks producer and consumer child processes, streaming 10 messages across boundaries with verified integrity.

---

### Part 8: UNIX Domain Sockets & Descriptor Passing

#### Command 8.1: Full-Duplex Sockets & SCM_RIGHTS FD Passing
```bash
./build/release/bin/kernox ipc unix-demo
```
- **What It Demonstrates:**
  - Creates an `AF_UNIX` stream socket server.
  - Forks a client child process that connects to the socket path.
  - Authenticates the client process via `SO_PEERCRED` (extracts verified PID, UID, GID from Linux kernel).
  - Demonstrates zero-copy file descriptor transfer via `SCM_RIGHTS` ancillary control data (`sendmsg`/`recvmsg`), allowing the client to read data from a pipe opened exclusively by the parent.
  - Automatically unlinks the socket file upon completion.

---

### Part 9: POSIX Message Queues & Priority Preemption

#### Command 9.1: Priority Preemption & Equal-Priority FIFO
```bash
./build/release/bin/kernox ipc mq-demo
```
- **What It Demonstrates:**
  - Creates a POSIX message queue (`/dev/mqueue/*`) via `mq_open()`.
  - Forks a consumer child process.
  - Enqueues 5 messages with varied priorities: Priority 5, 10, 30, 50, and 5.
  - The worker dequeues messages strictly in kernel priority order:
    1. `EMERGENCY_SHUTDOWN` [Priority 50]
    2. `CRITICAL_ALERT_HIGH_PRIO` [Priority 30]
    3. `REGULAR_JOB_NORMAL_PRIO` [Priority 10]
    4. `BATCH_JOB_LOW_PRIO` [Priority 5]
    5. `MAINTENANCE_LOG` [Priority 5] (Preserves FIFO arrival order for identical priorities).
  - Unlinks the queue from kernel VFS automatically.
