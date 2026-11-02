# CPU Scheduling Simulation Engine

KERNOX provides an educational, discrete-event CPU scheduling simulation engine implemented in modern C++20. The module models classic and advanced scheduling algorithms, calculates precise process-level and system-wide metrics, and renders Gantt chart execution timelines.

---

## 1. Domain Model & Principles

### 1.1 Process Representation
Each simulated process is defined by:
- **Process ID (`pid`)**: Unique unsigned 32-bit identifier.
- **Arrival Time ($T_{arr}$)**: Non-negative simulation timestamp when the process enters the ready state.
- **CPU Burst Duration ($T_{burst}$)**: Strictly positive simulation time units required on the CPU.
- **Priority**: Unsigned integer priority value.

### 1.2 Priority Convention
> [!IMPORTANT]
> **Lower numerical value denotes higher scheduling priority.**
> Priority `0` represents the highest possible priority. This follows UNIX/POSIX `nice` conventions and standard operating system literature.

### 1.3 Deterministic Tie-Breaking
When two or more eligible processes share identical scheduling criteria (e.g., identical burst durations in SJF/SRTF, identical priorities, or simultaneous arrivals):
1. The process with the **earlier arrival time** is selected.
2. If arrival times are identical, the process with the **lower Process ID (`pid`)** is selected.

### 1.4 Workload Immutability & Validation
Input workloads are validated prior to execution:
- Empty workloads return an `InvalidArgument` error.
- Non-unique PIDs return an `InvalidArgument` error.
- Zero-duration or negative burst times are rejected.
- Simulations operate on independent copies of the workload, ensuring caller data is never mutated.

---

## 2. Supported Scheduling Algorithms

### 2.1 First-Come, First-Served (FCFS)
* **Type:** Non-preemptive.
* **Policy:** Processes are dispatched in order of arrival time.
* **Idle Periods:** If the CPU is unallocated when no process has arrived, idle intervals are explicitly recorded in the Gantt timeline.

### 2.2 Shortest Job First (SJF)
* **Type:** Non-preemptive.
* **Policy:** Among all arrived processes ready at time $t$, the process with the shortest initial CPU burst is dispatched and runs to completion.
* **Arrival Handling:** Processes arriving while another process is executing are added to the ready pool and evaluated upon completion of the running process.

### 2.3 Shortest Remaining Time First (SRTF)
* **Type:** Preemptive.
* **Policy:** At each scheduling boundary (process arrival or slice completion), the process with the minimum remaining CPU burst time is selected.
* **Preemption:** If a new process arrives with a remaining burst strictly less than the currently running process's remaining burst, the running process is preempted and returned to the ready pool.

### 2.4 Round Robin (RR)
* **Type:** Preemptive with time slicing.
* **Policy:** Ready processes are maintained in a FIFO queue and allocated a maximum time slice ($\text{quantum} > 0$).
* **Quantum Expiration Boundary Ordering:**
  When a running process's time slice expires at time $t$, newly arrived processes arriving at time $t$ are appended to the ready queue **before** the preempted process is requeued. This guarantees fairness and prevents starvation of arriving jobs.

### 2.5 Priority Scheduling
* **Type:** Non-preemptive.
* **Policy:** Selects the eligible process with the highest priority (lowest numerical priority value). Runs to completion once dispatched.

### 2.6 Multilevel Feedback Queue (MLFQ)
* **Type:** Preemptive multi-tier feedback queue.
* **Configuration:**
  - **Q0 (High Priority):** Round Robin with time quantum $Q_0 = 2$.
  - **Q1 (Medium Priority):** Round Robin with time quantum $Q_1 = 4$.
  - **Q2 (Low Priority):** First-Come First-Served (FCFS) queue.
* **Demotion Policy:** A process that exhausts its full allotted quantum in queue $Q_i$ without completing is demoted to queue $Q_{i+1}$.
* **Starvation Prevention (Priority Boost):** Configurable boost interval (default 10 time units). At every boost boundary, all unfinished processes across all queues are reset to Q0 with fresh quantum allocations.
* **Preemption:** Arriving higher-queue processes immediately preempt lower-queue execution. The preempted process retains its remaining burst and returns to the front of its queue.

---

## 3. Metrics Engine Definitions

For every completed process $i$:

$$\text{Turnaround Time} = T_{completion} - T_{arrival}$$

$$\text{Waiting Time} = \text{Turnaround Time} - T_{burst}$$

$$\text{Response Time} = T_{first\_execution} - T_{arrival}$$

### Aggregate Metrics
* **Makespan:** Total elapsed time from the earliest arrival to the latest completion:
  $$\text{Makespan} = \max(T_{completion}) - \min(T_{arrival})$$
* **CPU Busy Time:** Sum of all active process execution intervals.
* **CPU Idle Time:** Sum of all recorded CPU idle intervals.
* **CPU Utilization:**
  $$\text{CPU Utilization} = \left(\frac{\text{CPU Busy Time}}{\text{Makespan}}\right) \times 100\%$$
* **Process Dispatches:** Total number of times a process is assigned to the CPU (each non-idle slice in the timeline).
* **Preemptions:** Total number of involuntary yields due to quantum expiration or higher-priority arrivals.
* **Estimated Context Switches:** Count of direct handoffs between distinct executing processes ($P_A \to P_B$ where $A \neq B$). Transitions to or from idle ($P_A \to \text{IDLE} \to P_B$) represent an involuntary yield to idle and a subsequent cold dispatch from idle; they are explicitly excluded from direct process-to-process context switches.

> [!NOTE]
> Context-switch counts and time units are simulated metrics and do not represent hardware CPU clock cycles or Linux kernel CFS context switches.

---

## 4. Built-in Workload Profiles

KERNOX includes three standardized workloads:

### 1. `basic`
* **P1:** Arrival 0, Burst 10, Priority 3
* **P2:** Arrival 0, Burst 4,  Priority 1
* **P3:** Arrival 0, Burst 2,  Priority 2

### 2. `arrivals` (Staggered Arrivals with Idle Periods)
* **P1:** Arrival 0,  Burst 3, Priority 2
* **P2:** Arrival 2,  Burst 6, Priority 1
* **P3:** Arrival 4,  Burst 4, Priority 3
* **P4:** Arrival 15, Burst 5, Priority 1  *(Induces idle period between $t=13$ and $t=15$ in SRTF/MLFQ)*

### 3. `priority-demo`
* **P1:** Arrival 0, Burst 4, Priority 3
* **P2:** Arrival 1, Burst 5, Priority 1
* **P3:** Arrival 2, Burst 2, Priority 2
* **P4:** Arrival 3, Burst 1, Priority 0

---

## 5. Command-Line Interface (CLI)

### Syntax
```bash
kernox schedule --algorithm <name> --workload <name> [options]
# Alias:
kernox sched --algorithm <name> --workload <name> [options]
```

### Options
* `--algorithm, -a`: `fcfs`, `sjf`, `srtf`, `rr`, `priority`, `mlfq`
* `--workload, -w`: `basic`, `arrivals`, `priority-demo`
* `--quantum, -q`: Positive integer time quantum for Round Robin / MLFQ (default: 2)
* `--boost, -b`: Positive integer priority boost interval for MLFQ (default: 10)

### Examples
```bash
# First-Come First-Served on basic workload
kernox schedule --algorithm fcfs --workload basic

# Shortest Remaining Time First on staggered arrivals
kernox schedule --algorithm srtf --workload arrivals

# Round Robin with custom quantum of 3
kernox schedule --algorithm rr --quantum 3 --workload basic

# Multilevel Feedback Queue with priority boost every 15 units
kernox schedule --algorithm mlfq --quantum 2 --boost 15 --workload arrivals
```

---

## 6. Sample Output Verification

### Example Run: `kernox schedule --algorithm srtf --workload arrivals`
```text
================================================================================
KERNOX CPU SCHEDULING SIMULATION: Shortest Remaining Time First (SRTF Preemptive)
Workload: arrivals (4 processes)
Policy: Preemptive; selects shortest remaining CPU burst; tie-break by arrival time, then PID.
================================================================================

PER-PROCESS EXECUTION METRICS:
+-----+---------+-------+----------+--------+------------+---------+----------+
| PID | Arrival | Burst | Priority | Finish | Turnaround | Waiting | Response |
+-----+---------+-------+----------+--------+------------+---------+----------+
|   1 |       0 |     3 |        2 |      3 |          3 |       0 |        0 |
|   2 |       2 |     6 |        1 |     13 |         11 |       5 |        1 |
|   3 |       4 |     4 |        3 |      8 |          4 |       0 |        0 |
|   4 |      15 |     5 |        1 |     20 |          5 |       0 |        0 |
+-----+---------+-------+----------+--------+------------+---------+----------+

GANTT CHART TIMELINE:
|  P1  |  P2  |  P3  |  P2  |  IDLE  |  P4  |
0      3      4      8      13       15     20

Aggregate Performance Metrics:
  Makespan (Total Elapsed Time) : 20 time units
  CPU Busy Time                 : 18 time units
  CPU Idle Time                 : 2 time units
  CPU Utilization               : 90.00%
  Average Turnaround Time       : 5.75 time units
  Average Waiting Time          : 1.25 time units
  Average Response Time         : 0.25 time units
  Process Dispatches            : 5
  Preemptions                   : 1
  Estimated Context Switches    : 3 (direct process-to-process transitions, excluding idle)

--------------------------------------------------------------------------------
Notice: This simulation models theoretical OS CPU scheduling in user-space.
        It does not alter or monitor actual Linux kernel CFS thread scheduling.
================================================================================
```

---

## 7. Educational & Industry Review Limitations

1. **User-Space Discrete Event Model:** The scheduler is a deterministic discrete-event simulation model. It does not replace or interface with the Linux kernel's `sched_ext` or Completely Fair Scheduler (CFS).
2. **Zero-Overhead Context Switching:** Simulation time models pure CPU execution bursts. Context-switch latencies, translation lookaside buffer (TLB) flushes, and cache warming are excluded to maintain alignment with standard university OS curricula.
3. **Single CPU Core Model:** All six algorithms currently simulate a single-core uniprocessor dispatch model. Multi-core affinity models will be explored in subsequent benchmarking milestones.
