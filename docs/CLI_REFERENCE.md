# KERNOX — Complete Command-Line Interface (CLI) Reference

**Project:** KERNOX — A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform
**Standard:** Modern C++20 | **Target Platform:** Linux / POSIX (WSL2 / Ubuntu 24.04 LTS)
**Binary Location:** `build/<preset>/bin/kernox`

---

## 1. Global Commands & Syntax Overview

The KERNOX CLI follows standard POSIX style subcommands and options:
```bash
kernox [global_options] <subsystem> [subcommand] [arguments...]
```

### Global Options
| Flag | Alternative | Description |
| :--- | :--- | :--- |
| `--help` | `-h` | Display the global or command-specific help menu and exit (Exit Code: 0) |
| `--version` | `-v` | Display platform version, C++ standard, and toolchain info (Exit Code: 0) |
| `status` | — | Query live Linux host telemetry and core utility health (Exit Code: 0) |
| `self-test` | — | Run internal deterministic sanity tests on foundational primitives (Exit Code: 0) |

---

## 2. Command Groups

KERNOX provides 7 dedicated command groups:

```
kernox
 ├── status              [Host & Foundation Telemetry]
 ├── self-test           [Internal Health Checks]
 ├── schedule (sched)    [CPU Scheduling Engine]
 ├── disk                [Disk Head Scheduling Engine]
 ├── deadlock            [Deadlock & Resource Allocation]
 ├── memory (mem)        [Memory & Virtual Memory Engine]
 ├── process (proc)      [POSIX Process Lifecycle & Limits]
 ├── ipc                 [Pipes, FIFOs, Shared Memory, Sockets, MQs]
 └── sync                [Process-Shared Concurrency & Ring Buffer]
```

---

## 3. Detailed Command Reference

### 3.1 Host Telemetry & Health Checks

#### `kernox status`
Reads and formats live system information directly from Linux `/proc`:
- **Syntax:** `kernox status`
- **Output Metrics:**
  - Host kernel version (`uname(2)`)
  - Live CPU utilization percentage (calculated over a 100ms delta from `/proc/stat`)
  - Physical memory usage, total, and available KB/MB from `/proc/meminfo`
  - 1m, 5m, 15m system load averages from `/proc/loadavg`
  - System uptime from `/proc/uptime`
- **Example:**
  ```bash
  ./build/release/bin/kernox status
  ```

#### `kernox self-test`
Executes internal unit validations on `Result<T>`, `UniqueFd`, `Timer`, `ChildProcess`, and logger utilities.
- **Syntax:** `kernox self-test`
- **Exit Code:** `0` on success, `1` on failure.

---

### 3.2 CPU Scheduling Simulator (`schedule`)

Simulates discrete-event single-processor CPU scheduling with deterministic workloads and produces ASCII Gantt charts.

- **Syntax:**
  ```bash
  kernox schedule --algorithm <algo> --workload <workload> [--quantum <num>] [--boost <num>]
  ```
- **Arguments:**
  - `--algorithm, -a <name>` (Required):
    - `fcfs`: First-Come First-Served (non-preemptive)
    - `sjf`: Shortest Job First (non-preemptive)
    - `srtf`: Shortest Remaining Time First (preemptive)
    - `rr`: Round Robin (preemptive with time slice)
    - `priority`: Priority Scheduling (preemptive/non-preemptive based on workload)
    - `mlfq`: Multilevel Feedback Queue (3 priority queues with aging and periodic priority boost)
  - `--workload, -w <name>` (Required):
    - `basic`: Uniform arrivals, varied bursts
    - `arrivals`: Staggered arrival times designed for preemption evaluation
    - `priority-demo`: Staggered tasks with distinct priority levels
  - `--quantum, -q <num>` (Optional, default: 2): Time quantum slice for Round Robin and MLFQ.
  - `--boost, -b <num>` (Optional, default: 10): Priority boost interval for MLFQ starvation prevention.
- **Example:**
  ```bash
  ./build/release/bin/kernox schedule --algorithm srtf --workload arrivals
  ```
- **Expected Output:** Formatted ASCII Gantt chart showing execution intervals, context switches, per-process Turnaround Time (TAT), Waiting Time (WT), and overall CPU utilization.

---

### 3.3 Disk Scheduling Simulator (`disk`)

Simulates cylinder head movements across a hard disk drive geometry.

- **Syntax:**
  ```bash
  kernox disk schedule --algorithm <algo> [--workload <name>] [--head <cylinder>] [--cylinders <num>] [--direction <up|down>]
  ```
- **Arguments:**
  - `--algorithm, -a <name>` (Required):
    - `fcfs`: First-Come First-Served
    - `sstf`: Shortest Seek Time First
    - `scan`: Elevator algorithm (moves in one direction to end, then reverses)
    - `cscan`: Circular SCAN (moves in one direction to end, jumps to opposite end without servicing)
    - `look`: Reverses direction at the last requested cylinder
    - `clook`: Circular LOOK (jumps to first request on opposite end without servicing)
  - `--workload, -w <name>` (Optional, default: `canonical`):
    - `canonical`: Silberschatz textbook workload (`98, 183, 37, 122, 14, 124, 65, 67`)
  - `--head, -h <num>` (Optional, default: 53): Starting cylinder position.
  - `--cylinders, -c <num>` (Optional, default: 200): Total cylinders on platter (0 to cylinders - 1).
  - `--direction, -d <up|down>` (Optional, default: `up`): Initial head traversal direction.
- **Example:**
  ```bash
  ./build/release/bin/kernox disk schedule --algorithm sstf --workload canonical
  ```
- **Expected Output:** Step-by-step track seek sequence table, Total Head Movement (THM), and Average Seek Distance.

---

### 3.4 Deadlock Management Simulator (`deadlock`)

Implements Dijkstra's Banker's Algorithm, dynamic resource requests with rollback, multiple-instance deadlock detection, and Resource Allocation Graph (RAG) cycle detection.

- **Subcommands:**
  1. `banker`: Evaluates Banker's safety state and prints a safe sequence if one exists.
     ```bash
     kernox deadlock banker --workload <safe|unsafe>
     ```
  2. `request`: Simulates a process requesting additional resource units. Validates against `Need` and `Available`, performs speculative allocation, checks safety, and executes rollback if unsafe.
     ```bash
     kernox deadlock request --workload safe --process 1 --request 1,0,2
     ```
  3. `detect`: Multiple-instance deadlock detection algorithm identifying exact deadlocked processes.
     ```bash
     kernox deadlock detect --workload detection-demo
     ```
  4. `graph`: Cycle detection on Resource Allocation Graphs using DFS with back-edge path extraction.
     ```bash
     kernox deadlock graph --workload cycle-demo
     ```

---

### 3.5 Memory Management & Virtual Memory Simulator (`memory`)

Simulates contiguous allocation strategies, MMU page translation, and page replacement policies.

- **Subcommands:**
  1. `allocate`: Simulates First Fit, Best Fit, Worst Fit, or Next Fit with dynamic block splitting and coalescing.
     ```bash
     kernox memory allocate --algorithm <first-fit|best-fit|worst-fit|next-fit> --workload allocation-demo
     ```
  2. `paging`: Displays frame table and page table configuration.
     ```bash
     kernox memory paging --workload paging-demo
     ```
  3. `translate`: Translates logical address to physical address via simulated MMU, checking page boundaries and access permissions.
     ```bash
     kernox memory translate --process 0 --address 4096 --workload paging-demo
     ```
  4. `replace`: Simulates virtual memory page replacement (FIFO, LRU, Optimal/MIN) and counts page faults.
     ```bash
     kernox memory replace --algorithm <fifo|lru|optimal> --workload page-reference-demo --frames <num>
     ```
  5. `ws`: Peter Denning's Working-Set model $W(t, \Delta)$ analyzing locality and working-set size over time.
     ```bash
     kernox memory ws --workload page-reference-demo --window <num>
     ```

---

### 3.6 POSIX Process Management (`process`)

Manages and monitors real Linux processes with sandboxing, POSIX resource limits, and telemetry.

- **Subcommands:**
  1. `run`: Executes a process with resource limits and timeout enforcement.
     ```bash
     kernox process run [options] <executable> [args...]
     ```
     **Options:**
     - `--timeout <ms>`: Wall-clock timeout in milliseconds.
     - `--grace <ms>`: Grace period between `SIGTERM` and `SIGKILL` (default: 200ms).
     - `--cpu-limit <sec>`: CPU execution limit in seconds (`RLIMIT_CPU`).
     - `--as-limit <bytes>`: Maximum virtual address space in bytes (`RLIMIT_AS`).
     - `--nofile-limit <num>`: Max open file descriptors (`RLIMIT_NOFILE`).
     - `--fsize-limit <bytes>`: Max output file size in bytes (`RLIMIT_FSIZE`).
     **Example:**
     ```bash
     ./build/release/bin/kernox process run --timeout 2000 /bin/sleep 0.1
     ```
  2. `stat`: Enriched real-time process telemetry from `/proc/<pid>/stat`, `/proc/<pid>/status`, and `/proc/<pid>/io`.
     ```bash
     kernox process stat <pid>
     ```
     **Example:**
     ```bash
     ./build/release/bin/kernox process stat 1
     ```

---

### 3.7 Inter-Process Communication (`ipc`)

Provides benchmarks, interactive demonstrations, and protocol validations across all 5 Linux IPC mechanisms.

- **Subcommands:**
  1. `pipe`: Benchmark anonymous pipe throughput and round-trip latency.
     ```bash
     kernox ipc pipe [--size <bytes>] [--count <num>] [--timeout <ms>] [--verify]
     ```
  2. `fifo`: Benchmark named FIFO throughput and round-trip latency.
     ```bash
     kernox ipc fifo [--dir <path>] [--size <bytes>] [--count <num>] [--timeout <ms>] [--verify]
     ```
  3. `shm`: Demonstrate POSIX shared memory cross-process synchronization.
     ```bash
     kernox ipc shm [--name <str>] [--size <bytes>]
     ```
  4. `unix-demo`: Full-duplex UNIX domain stream socket demonstration featuring `SO_PEERCRED` credential validation and `SCM_RIGHTS` zero-copy descriptor passing.
     ```bash
     kernox ipc unix-demo [--path <path>]
     ```
  5. `mq-demo`: POSIX message queue demonstration featuring priority preemption (0–50), equal-priority FIFO order, worker child synchronization, and owner unlinking.
     ```bash
     kernox ipc mq-demo [--name <str>] [--messages <num>]
     ```
  6. `benchmark`: Comprehensive comparative IPC benchmark suite.
     ```bash
     kernox ipc benchmark [--type <pipe|fifo|all>] [--sizes <csv>] [--iterations <n>]
     ```

---

### 3.8 Process-Shared Synchronization (`sync`)

Demonstrates robust cross-process mutexes, semaphores, condition variables, and zero-heap ring buffers.

- **Subcommand:** `demo`
  - **Syntax:** `kernox sync demo [--slots <num>] [--slot-size <bytes>] [--messages <num>]`
  - **Description:** Initializes a bounded shared-memory ring buffer with `ProcessSharedMutex` and `ProcessSharedConditionVariable`, forks producer and consumer worker processes, and validates cross-process message sequence integrity.
  - **Example:**
    ```bash
    ./build/release/bin/kernox sync demo
    ```

---

## 4. Error Handling & Exit Codes

| Exit Code | Classification | Meaning |
| :---: | :--- | :--- |
| `0` | Success | Command completed successfully and all invariants verified |
| `1` | Command Error | Invalid argument, unknown option, or execution failed |
| `2` | Resource Error | Memory mapping failed, process spawn failed, or timeout exceeded |
| `130` | Interrupted | Interrupted by user signal (`SIGINT` / Ctrl+C) |

When an error occurs, KERNOX prints a clear diagnostic message to `std::cerr` detailing:
1. The subsystem where the failure occurred.
2. The specific `ErrorCode` enumeration.
3. The underlying POSIX `errno` description (if applicable).
