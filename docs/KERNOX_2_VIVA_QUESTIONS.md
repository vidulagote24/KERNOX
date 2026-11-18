# KERNOX 2.0 — Viva Examination Questions & Comprehensive Technical Answers

**Project:** KERNOX 2.0: Android & Linux Resource Intelligence Platform
**Target Audience:** Viva Voce Panels, Academic Evaluators, Systems Engineers, and Students
**Implementation Baseline:** Modern ISO C++20 (`kernox_core`, `kernox_sim`, `kernox_system`, `kernox_intel`, `kernox_reclaim`, `kernox_experiment`, `kernox_adb`) & Python 3.11+ (`tools/analytics`)

---

## Category 1: Operating Systems Fundamentals

### Q1: What is the primary architectural duality in KERNOX 2.0?
**Answer:** KERNOX 2.0 explicitly demarcates two operating domains:
1. **Pure User-Space Simulation Domain:** Deterministic discrete-event simulation of classical OS algorithms (CPU scheduling, disk scheduling, deadlock detection, paging) and an Android-inspired process memory reclamation laboratory.
2. **Production-Grade Linux Systems Programming Domain:** Real POSIX systems execution, resource limits (`setrlimit`), live Linux `/proc` telemetry, inter-process communication, and read-only mobile observation via the Android Debug Bridge (ADB).
Simulated entities (e.g. `SimulatedPid`) never interact with native Linux or Android system calls, guaranteeing zero unintended side-effects.

### Q2: What is the fundamental difference between micro-level page replacement and macro-level process reclamation?
**Answer:** Micro-level page replacement operates at the virtual memory granularity of individual 4 KiB physical memory frames. When a page fault occurs and physical frames are exhausted, the kernel selects a specific page (via FIFO, LRU, or Optimal) to evict to disk swap.
Macro-level process reclamation operates at the whole-process granularity. When systemic physical memory pressure exceeds sustainable thresholds (e.g., 75% moderate, 90% critical) and swapping is either unavailable or undesirable (such as on mobile flash memory), the system selects an entire process container to terminate, immediately reclaiming its entire resident memory footprint.

### Q3: Why is heavy swapping to disk unsuitable for mobile operating systems like Android?
**Answer:** Mobile devices rely on NAND flash storage rather than rotating platters or replaceable enterprise SSDs. Swapping causes:
1. **Write Amplification & Flash Wear:** Flash memory has limited write-erase endurance cycles; continuous swapping rapidly degrades hardware longevity.
2. **Interactive Latency Spikes (Jank):** Flash access latency during synchronous page-ins blocks the main UI rendering thread, causing visible frame drops.
3. **Power & Thermal Budget:** High I/O bandwidth rapidly drains mobile batteries and induces thermal throttling.
Consequently, mobile operating systems discard cached processes rather than maintaining traditional swap partitions.

### Q4: What is the difference between a monolithic kernel and a microkernel, and where does KERNOX fit?
**Answer:** In a monolithic kernel (e.g., standard Linux), all core OS services (scheduling, memory management, VFS, device drivers, IPC) execute in a single shared address space in kernel privilege mode (Ring 0). In a microkernel (e.g., seL4, QNX), only minimal primitives (thread management, low-level address space mapping, IPC) remain in Ring 0, while device drivers and file systems execute as user-space servers (Ring 3).
KERNOX is an educational systems laboratory and user-space simulator running entirely in Linux user-space (Ring 3), leveraging Linux system calls for its systems programming modules while running algorithmic simulations in user memory.

---

## Category 2: Process Management & Lifecycle

### Q5: How does `kernox_intel` model process importance, and why is this classification critical?
**Answer:** `kernox_intel` defines a strongly typed enum `ProcessImportance` reflecting the Android application lifecycle:
- `Foreground`: Active window receiving user touch input.
- `Visible`: Visible on screen but paused or non-focused.
- `Service`: Background execution carrying out user-perceptible work (e.g., music streaming, file download).
- `Background`: Dormant background task not perceptible to the user.
- `Cached`: Empty process container retained in memory solely to speed up relaunch.
This hierarchy is critical because memory reclamation must protect user-facing processes (`Foreground` and `Visible`) while preferentially harvesting `Cached` and `Background` tasks under pressure.

### Q6: How does KERNOX handle process identifier separation, and what is its type-safety status?
**Answer:** KERNOX defines distinct type aliases:
- `using SimulatedPid = uint32_t;` in `kernox::intel` (initialized starting at 1000).
- `using AndroidPid = uint32_t;` in `kernox::adb` (representing parsed remote PIDs).
- `pid_t` (standard POSIX signed integer for native Linux processes).
*Source-Level Finding:* Because `SimulatedPid` and `AndroidPid` are type aliases (`using = uint32_t`), they do not provide compile-time type separation. Safety is currently maintained through strict architectural decoupling (the simulation engine never includes or links process termination system calls, and `kernox_adb` provides zero execution APIs). To achieve compile-time type safety preventing accidental cross-domain assignment, strong wrapper structs (e.g. `struct SimulatedPid { uint32_t value; ... };`) are recommended.

### Q7: Describe the formal state machine and transitions of a `SimulatedProcess`.
**Answer:** The actual implementation in `kernox::intel::ProcessLifecycleState` defines six distinct states:
1. `Created`: The initial state upon `SimulatedProcess::create()`.
2. `Ready`: Admitted to the runqueue (`mark_ready()`, `preempt_to_ready()`, or `unblock_to_ready()`).
3. `Running`: Actively executing on simulated CPU (`dispatch_running()`).
4. `Blocked`: Waiting for simulated I/O or resources (`block()`).
5. `Completed`: Terminal state reached when `cpu_burst_remaining_ == 0` (`complete()`).
6. `Terminated`: Terminal state reached when evicted under memory pressure or aborted (`terminate()`).
*Key Distinction:* Distinguishing `Completed` from `Terminated` is essential for survivor accounting: processes that complete their execution are survivors, whereas terminated processes fell victim to memory pressure.

### Q8: What metrics constitute the memory footprint of a `SimulatedProcess`?
**Answer:** The `MemoryFootprint` structure tracks:
- `allocated_bytes`: Total logical memory requested by the process.
- `resident_bytes`: Physical memory currently backed by simulated frames (analogous to RSS).
- `peak_allocated_bytes`: Maximum memory watermark reached during process lifetime.
- `last_active_tick`: Simulation timestamp of the last CPU dispatch or memory access, used for LRU eviction calculation.

---

## Category 3: CPU Scheduling Algorithms

### Q9: What CPU scheduling algorithms are implemented in `kernox_sim`, and how are they classified?
**Answer:**
1. **Non-Preemptive:**
   - **First-Come, First-Served (FCFS):** Dispatches in strict arrival order; susceptible to the convoy effect.
   - **Shortest Job First (SJF):** Dispatches the process with the shortest CPU burst; provably optimal for minimizing average waiting time among non-preemptive algorithms.
   - **Non-Preemptive Priority:** Dispatches highest priority; susceptible to starvation.
2. **Preemptive:**
   - **Shortest Remaining Time First (SRTF):** Preempts running process if a new arrival has a shorter remaining burst.
   - **Round Robin (RR):** Allocates a fixed time quantum ($q$); ensures bounded response times.
   - **Multilevel Feedback Queue (MLFQ):** Multiple priority queues with dynamic feedback and priority boosting to prevent starvation.

### Q10: How does KERNOX's MLFQ prevent indefinite starvation of CPU-bound processes?
**Answer:** The MLFQ simulator employs two rules:
1. **Rule of Demotion:** If a process consumes its entire time quantum without yielding for I/O, its priority is reduced by one level (moved to a lower queue with a larger time quantum).
2. **Priority Boost:** After a configurable time interval (`boost_interval`), all processes across all queues are reset to the highest priority queue. This prevents starvation of low-priority CPU-bound processes when interactive high-priority processes arrive frequently.

### Q11: What is the convoy effect, and which algorithm eliminates it?
**Answer:** The convoy effect occurs in FCFS scheduling when a long CPU-bound process occupies the CPU, causing multiple short, I/O-bound processes to queue up behind it, leading to low CPU and device utilization. Preemptive algorithms such as Round Robin (RR) and Shortest Remaining Time First (SRTF) eliminate the convoy effect by preempting long jobs in favor of shorter or interactive tasks.

### Q12: How are Average Turnaround Time and Average Waiting Time calculated?
**Answer:**
- $\text{Turnaround Time} = \text{Completion Time} - \text{Arrival Time}$
- $\text{Waiting Time} = \text{Turnaround Time} - \text{Total CPU Burst Time}$
- $\text{Response Time} = \text{Time of First CPU Dispatch} - \text{Arrival Time}$
Average metrics are computed by dividing the sum of each metric across all processes by the total process count $N$.

---

## Category 4: Memory Management & Allocation

### Q13: Explain the dynamic contiguous memory allocation algorithms evaluated in `kernox_sim`.
**Answer:**
- **First-Fit:** Scans the free block list from the beginning and allocates the first hole large enough. Fast, but tends to accumulate small fragments at the beginning of the list.
- **Best-Fit:** Scans the entire free list to find the hole closest in size to the requested block. Minimizes wasted space per allocation, but produces very small, unusable external fragments.
- **Worst-Fit:** Scans the entire free list to find the largest available hole. Leaves larger remaining fragments, but is slow and generally yields poor memory utilization.
- **Next-Fit:** Begins scanning from the location of the previous allocation. Distributes allocations more evenly, avoiding fragmentation clustering at the start of memory.

### Q14: Distinguish between internal fragmentation and external fragmentation.
**Answer:**
- **Internal Fragmentation:** Occurs when allocated memory is larger than requested memory (e.g., when a process requesting 3500 bytes is given a fixed 4096-byte page frame). The unused 596 bytes inside the partition are wasted.
- **External Fragmentation:** Occurs when total free memory is sufficient to satisfy a request, but the free memory is divided into small, non-contiguous holes such that no single contiguous block is large enough.

### Q15: How does two-level paging work in `kernox_sim`, and what problem does it solve?
**Answer:** In a 32-bit linear address space with 4 KiB pages, a single flat page table requires $2^{20}$ page table entries (PTEs) $\approx 4\text{ MiB}$ of contiguous physical memory per process, even if the process only uses a small fraction of its address space.
Two-level paging breaks the 32-bit address into:
- 10 bits: Page Directory Index (references the Page Directory).
- 10 bits: Page Table Index (references a Page Table).
- 12 bits: Page Offset ($4\text{ KiB}$ page size).
This allows page tables to be allocated dynamically on-demand; unused regions of the virtual address space do not require allocated physical page tables, saving significant physical RAM.

---

## Category 5: Virtual Memory & Page Replacement

### Q16: What is Bélady's Anomaly, and which algorithms are susceptible to it?
**Answer:** Bélady's Anomaly is the counterintuitive phenomenon where increasing the number of physical page frames results in an *increase* in the number of page faults for a given memory reference string.
- **Susceptible:** FIFO (First-In, First-Out) page replacement is susceptible because its eviction queue is based on arrival time rather than access history.
- **Immune:** Stack algorithms—such as LRU (Least Recently Used) and Optimal (Bélady's MIN)—are mathematically immune to Bélady's Anomaly because the set of pages in an $n$-frame allocation is always a strict subset of the pages in an $(n+1)$-frame allocation.

### Q17: How does Bélady's Optimal (MIN) algorithm function, and why cannot it be implemented in a real operating system?
**Answer:** Bélady's Optimal algorithm replaces the page that will not be referenced for the longest period of time in the future. It is provably optimal, producing the absolute lowest page fault rate for any reference string.
It cannot be implemented in a general-purpose operating system because it requires perfect, clairvoyant knowledge of future memory access strings, which is impossible to predict in non-deterministic multi-threaded environments. It serves strictly as a theoretical benchmark against which other algorithms are measured.

### Q18: What is Peter Denning's Working-Set model, and how does KERNOX simulate it?
**Answer:** Peter Denning's Working-Set model is based on the principle of locality of reference. The working set $W(t, \Delta)$ of a process at time $t$ is the set of distinct pages referenced during the most recent time window $\Delta$.
KERNOX simulates this by maintaining a sliding reference window of size $\Delta$ over the reference stream. If the total working set demand across all active processes exceeds available physical frames, the system enters *thrashing*; KERNOX tracks working set size over time to demonstrate thrashing prevention.

---

## Category 6: Memory Pressure & Reclamation Engine

### Q19: What are the memory pressure thresholds defined in KERNOX 2.0 Phase C?
**Answer:** `kernox::reclaim::PressureDetector` defines two authoritative thresholds:
- **Moderate Pressure Threshold:** **75%** utilization ($\text{moderate\_threshold\_ratio} = 0.75$).
- **Critical Pressure Threshold:** **90%** utilization ($\text{critical\_threshold\_ratio} = 0.90$).
These thresholds trigger proactive and emergency whole-process reclamation passes, respectively.

### Q20: Explain the four reclamation policies evaluated in KERNOX 2.0.
**Answer:**
1. **FIFO:** Evicts victim processes strictly in order of creation time (`created_at`).
2. **LRU:** Evicts victim processes that have been idle the longest based on `last_active_at`.
3. **Priority:** Evicts processes with the lowest integer scheduling priority.
4. **ImportanceAware:** Evaluates processes according to Android-inspired importance tiers, evicting `Cached` first, followed by `Background`, then `Service`, then `Visible`. **Foreground processes are strictly shielded from eviction.**

### Q21: What is priority inversion in whole-process memory reclamation, and how does `ImportanceAware` prevent it?
**Answer:** In memory reclamation, priority inversion occurs when a naive policy (like FIFO or LRU) kills an important, user-visible task (e.g., an active UI foreground app) simply because it was started early (FIFO) or was briefly idle while waiting for network input (LRU), while simultaneously preserving unimportant background tasks.
`ImportanceAware` prevents this by partitioning processes into explicit importance tiers and enforcing an invariant: lower tiers must be completely exhausted before higher tiers are evaluated. Furthermore, `Foreground` processes are shielded by an explicit protection guarantee under moderate and critical pressure.

### Q22: What constitutes a "survivor" in KERNOX experiment accounting?
**Answer:** In accordance with `ExperimentRunner::calculate_metrics()`, a process is counted as a survivor (`processes_survived`) if it was not terminated by memory pressure during the experiment. This includes:
1. Processes that completed their entire CPU burst normally (`processes_completed`).
2. Processes that remained active in memory at the conclusion of the simulation.
Completed processes are never counted as reclamation victims. The overall survival rate is:
$$\text{Survival Rate} = \frac{\text{processes\_survived}}{\text{total\_processes}}$$

### Q23: How does the `ReclamationEngine` avoid infinite loops when memory cannot be freed?
**Answer:** The `ReclamationEngine` bounds every reclamation round by the count of active processes. If an eviction pass scans all processes without finding any eligible victim (or if all remaining processes are protected foreground tasks), the engine terminates the loop immediately and returns `ReclamationStatus::NoEligibleVictims`. This prevents infinite execution loops when memory targets cannot be met.

---

## Category 7: Linux Systems Programming & POSIX IPC

### Q24: How does `ChildProcess` implement safe, sandboxed process execution?
**Answer:** `ChildProcess` manages process creation using `fork()` and `execve()`. To prevent deadlocks and race conditions:
1. It creates an internal synchronization pipe before forking with `FD_CLOEXEC` enabled.
2. In the child process, file descriptor redirection is applied, POSIX resource limits (`setrlimit`) are configured, and `execve()` is invoked.
3. If `execve()` fails, the child writes the resulting `errno` to the synchronization pipe and immediately calls `_exit(1)`.
4. In the parent process, if the pipe receives an error, it reaps the failed child and returns a typed `Result<T, Error>` to the caller.
5. Lifetime is governed by RAII: if the parent process drops the `ChildProcess` handle, the child is reaped.

### Q25: What is graceful signal escalation in `ProcessManager`?
**Answer:** When supervising a target process with an execution timeout, `ProcessManager` avoids immediately sending an uncatchable `SIGKILL`. Instead:
1. When timeout occurs, it first delivers `SIGTERM` to allow the process to flush buffers, release file locks, and exit cleanly.
2. It waits for a configurable grace period (default: 200 ms) using non-blocking `waitpid(WNOHANG)`.
3. If the process has not terminated after the grace period, `ProcessManager` escalates to `SIGKILL` to force termination.

### Q26: What is `PTHREAD_MUTEX_ROBUST`, and how does KERNOX use it in shared memory?
**Answer:** When multiple processes share a mutex in POSIX shared memory (`/dev/shm`), if a process crashes or is killed by `SIGKILL` while holding the mutex, a standard mutex remains locked indefinitely, causing all other processes to deadlock.
`PTHREAD_MUTEX_ROBUST` solves this: when a process dies holding the mutex, the next process attempting `pthread_mutex_lock()` receives `EOWNERDEAD`. KERNOX catches this error, verifies and recovers shared state consistency, calls `pthread_mutex_consistent()`, and unlocks the mutex, guaranteeing cross-process crash resilience.

### Q27: How does KERNOX pass file descriptors between processes using UNIX domain sockets?
**Answer:** Using UNIX domain stream sockets (`AF_UNIX`) and ancillary control data via `sendmsg()` and `recvmsg()`. The file descriptor integer from the sending process is packed into a `struct cmsghdr` message with `cmsg_level = SOL_SOCKET` and `cmsg_type = SCM_RIGHTS`.
When the Linux kernel delivers the message to the receiving process, it allocates a new file descriptor entry in the receiver's file descriptor table pointing to the exact same underlying `struct file` in the kernel VFS.

### Q28: How do POSIX Message Queues (`mq_*`) differ from anonymous pipes?
**Answer:**
1. **Message Boundaries:** Pipes are unstructured byte streams requiring application-level framing; message queues preserve discrete message boundaries.
2. **Priority Preemption:** Messages sent via `mq_send()` carry a priority integer (0 to 32,767). High-priority messages are placed at the head of the queue, preempting lower-priority messages. Equal-priority messages are preserved in strict FIFO order.
3. **Named Kernel Persistence:** Pipes exist only while open file descriptors exist; POSIX message queues have filesystem-like names (`/name`) and persist in the kernel until explicitly unlinked via `mq_unlink()`.

---

## Category 8: Android Process & Memory Observations

### Q29: What is the purpose of the `kernox_adb` module, and what is its safety boundary?
**Answer:** `kernox_adb` provides passive, read-only telemetry observation of physical Android devices and Android Virtual Devices (AVDs).
**Safety Boundary:** To guarantee device safety, KERNOX strictly prohibits any commands that modify device state:
- No `kill` or `kill -9` commands.
- No `am force-stop` or application disabling.
- No package installation or uninstallation (`pm install/uninstall`).
- No filesystem remounting (`remount`) or root privilege operations.
All telemetry is collected using passive read-only queries (`getprop`, `/proc/meminfo`, `dumpsys meminfo`, `ps`).

### Q30: What is the mathematical relationship between `MemTotal`, `MemFree`, `Cached`, and `MemAvailable` in `/proc/meminfo`?
**Answer:** In Linux and Android `/proc/meminfo`:
$$\text{MemAvailable} \approx \text{MemFree} + \text{Reclaimable Page Cache} + \text{Reclaimable Slab} - \text{Low-Watermark Reservation}$$
`MemAvailable` is **not** a distinct additive slice of physical RAM. It is an *estimate* of how much memory can be allocated without causing the system to swap or drop below memory watermarks.
Because `MemAvailable` includes `MemFree` and reclaimable `Cached` memory, adding them together ($MemAvailable + MemFree + Cached$) is mathematically invalid and produces double-counting.

### Q31: What is Proportional Set Size (PSS), and why is it preferred over Resident Set Size (RSS) on Android?
**Answer:** Resident Set Size (RSS) counts all physical pages mapped into a process's address space, including shared libraries (e.g., `libc.so`, Android framework runtime) and shared memory. If 10 processes each share a 20 MiB framework library, summing their RSS counts that library 10 times (200 MiB), which severely inflates memory totals.
Proportional Set Size (PSS) divides each shared page's size by the number of processes sharing it. If 10 processes share a page, each process is charged $\frac{1}{10}$th of that page's size. Summing the PSS across all processes yields the exact total physical memory consumed by those processes.

### Q32: What is an Android Virtual Device (AVD), and how is emulator provenance tracked?
**Answer:** An AVD is an emulated Android device running the Android OS image inside a QEMU virtual machine with hardware acceleration (KVM on Linux/WSL2). KERNOX detects emulators by checking device serial numbers (e.g., `emulator-5554`) and product properties (`sdk_gphone64_x86_64`).
In `tools/analytics`, provenance metadata records `is_emulator: true` and `target_type: "Android Virtual Device (AVD) / Emulator"` to prevent presenting emulator benchmark data as physical silicon telemetry.

---

## Category 9: C++ Architecture & Software Design

### Q33: How does KERNOX implement monadic error handling without C++ exceptions?
**Answer:** KERNOX defines `Result<T, Error>` in `include/kernox/core/result.hpp`, implemented as a discriminated union over `std::variant<T, Error>`.
- Methods return a `Result<T>` containing either the valid output or a typed `Error` with an `ErrorCode` and a human-readable context string.
- Callers check `res.is_ok()` before calling `res.value()` or handle `res.error()`.
- This ensures zero exception overhead, deterministic call stacks, and explicit compiler enforcement of error checking across all systems programming boundaries.

### Q34: What is the Rule of Five, and how is it applied in `UniqueFd`?
**Answer:** The Rule of Five states that if a class defines a destructor, it should explicitly define or delete:
1. Destructor (`~UniqueFd()`)
2. Copy Constructor (`UniqueFd(const UniqueFd&) = delete`)
3. Copy Assignment Operator (`UniqueFd& operator=(const UniqueFd&) = delete`)
4. Move Constructor (`UniqueFd(UniqueFd&& other) noexcept`)
5. Move Assignment Operator (`UniqueFd& operator=(UniqueFd&& other) noexcept`)
`UniqueFd` encapsulates a POSIX file descriptor integer (`int fd_`). To prevent double-close bugs and resource leaks, copy operations are explicitly deleted, making `UniqueFd` move-only. When moved, the source descriptor is reset to `-1`. The destructor automatically invokes `::close(fd_)` if `fd_ >= 0`.

### Q35: What sanitizers are integrated into KERNOX, and what is the difference between memory safety and concurrency verification?
**Answer:**
1. **AddressSanitizer (ASan) & UndefinedBehaviorSanitizer (UBSan):** Configured under preset `dev-asan`. Instruments memory allocations and pointer dereferences to detect heap/stack buffer overflows, use-after-free, double-free, and memory leaks. In KERNOX, all 20 test suites pass cleanly under ASan/UBSan with zero leaks and zero errors. *Crucial Distinction:* ASan/UBSan verifies memory safety, but does *not* prove the absence of data races.
2. **ThreadSanitizer (TSan):** Configured under preset `dev-tsan`. Designed to detect data races by tracking memory access timestamps. *Environment Limitation:* Under standard WSL2 kernels, TSan aborts at startup with `unexpected memory mapping` due to Linux virtual memory randomization conflicts.
3. **Concurrency & Synchronization Verification:** Because TSan dynamic analysis is unavailable in WSL2, concurrency is verified through:
   - `test_sync`: 20 unit tests validating robust mutex crash recovery (`EOWNERDEAD`, `pthread_mutex_consistent`), condition variables, and atomic ring buffer slot states.
   - `scripts/stress_test.sh`: 5-iteration stress testing across multi-process synchronization suites with zero flakes.

### Q36: Why are compile flags `-Wall -Wextra -Wpedantic -Wconversion -Werror` enforced in CMake?
**Answer:** To enforce strict code quality. `-Wall` and `-Wextra` enable extensive compiler diagnostic warnings; `-Wpedantic` enforces strict ISO C++20 standard conformance; `-Wconversion` flags implicit narrowing and sign conversions that could cause integer underflow or data loss; and `-Werror` converts all warnings into fatal compilation errors, preventing warning accumulation.

---

## Category 10: Experiment Methodology & Analytics

### Q37: Why must multi-policy comparison experiments use identical PRNG seeds?
**Answer:** To ensure fair, scientific, and reproducible comparative evaluation. By seeding the pseudo-random number generator with the same seed (`seed=42`), every policy is evaluated against an identical workload: the exact same process arrival times, burst requirements, memory footprints, and importance tier distributions. Any observed difference in performance is caused strictly by the algorithmic decisions of the reclamation policy, eliminating experimental confounding.

### Q38: What are the five workload scenarios evaluated in KERNOX 2.0?
**Answer:**
1. **`Normal`:** Balanced process arrivals and moderate memory allocations; operating memory remains within safe capacity.
2. **`HighMemoryPressure`:** Aggressive process generation driving memory utilization beyond 90%, forcing emergency multi-round reclamation passes.
3. **`BackgroundHeavy`:** Large populations of cached and background applications; tests the efficiency of background memory harvesting.
4. **`ForegroundProtection`:** Critical mix of user-facing UI processes competing against background services; tests whether policies protect active foreground tasks.
5. **`Mixed`:** Multi-phase fluctuating workload with varied burst durations.

### Q39: Why does wall-clock execution latency not prove asymptotic algorithmic complexity?
**Answer:** Host wall-clock latency ($\mu s$) measures the actual elapsed time of executing the simulation on specific host hardware under specific OS scheduler conditions. It incorporates host CPU caching, context switching, memory allocator overhead, and background host activity.
Proving asymptotic complexity $\mathcal{O}(n)$ requires theoretical mathematical analysis of algorithm operations (e.g., number of victim comparisons or queue traversals). Benchmarked wall-clock latency reflects practical runtime overhead, not a formal asymptotic proof.

### Q40: How does `tools/analytics` enforce data provenance separation?
**Answer:** `tools/analytics/src/kernox_analytics/validators.py` inspects the root `source` or `domain` field of every ingested JSON file. If an experiment analysis script receives a dataset tagged with `source: "android_adb_observation"`, or if an Android analyzer receives a simulation dataset tagged with `domain: "kernox_simulation"`, the validator raises a `ProvenanceError` and aborts. This prevents accidental blending of simulated and real telemetry.
