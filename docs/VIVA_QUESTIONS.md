# KERNOX — Technical Viva Questions and Answers

**Project:** KERNOX — A Comprehensive Operating Systems Simulation and Linux Systems Programming Platform
**Target Audience:** Operating Systems Faculty, Viva Examiners, and Senior Systems Evaluators
**Scope:** 45 Exhaustive Technical Questions & Answers Grounded in the Real KERNOX C++20 Implementation

---

## Category 1: Operating System & Architectural Fundamentals

### Q1: What is the architectural philosophy of "Zero-Falsehood Duality" in KERNOX?
**Answer:** It is the strict design principle separating theoretical user-space simulations from genuine Linux kernel system calls. In KERNOX, algorithmic models (CPU, disk, memory, deadlock) run pure discrete-event state machines deterministically without suspending real host threads, while systems modules (`ProcessManager`, `SharedMemory`, `UnixSocket`, `PosixMessageQueue`) invoke real Linux kernel system calls (`fork`, `execve`, `mmap`, `futex`, `AF_UNIX`, `mq_*`) on the host operating system.

### Q2: Why did KERNOX choose modern C++20 instead of traditional C for systems programming?
**Answer:** C++20 provides zero-overhead abstractions that prevent common systems programming bugs:
1. **Move Semantics & RAII:** Guarantees deterministic cleanup of file descriptors, sockets, processes, and memory mappings without manual `close()` or `free()`.
2. **Monadic Error Handling:** `core::Result<T, Error>` enforces explicit error checking at compile time without the runtime overhead or stack-unwinding hazards of C++ exceptions across `fork()` boundaries.
3. **Concepts & Strong Typing:** Enforces type safety across identifiers (e.g. distinguishing PIDs, UIDs, and file descriptors).
4. **Monotonic Clocks:** `std::chrono::steady_clock` ensures high-precision benchmarking impervious to wall-clock NTP adjustments.

### Q3: Why does KERNOX strictly avoid throwing C++ exceptions across `fork()` boundaries?
**Answer:** Throwing an exception in a child process after `fork()` can unwind the stack, invoking destructors for objects created in the parent process (including file descriptors, temporary files, or shared memory mappings). This can lead to duplicate cleanup, closing parent resources, or corrupting heap state. KERNOX uses `core::Result<T, Error>` for all systems calls, returning error codes safely.

---

## Category 2: CPU Scheduling

### Q4: How does KERNOX compute context switch overhead in the SRTF simulation?
**Answer:** In `CpuScheduler::run_srtf`, a context switch is counted whenever the currently executing process ID differs from the previously executing process ID, provided the previous execution was not an idle CPU tick. If the CPU was idle, starting a new process is an arrival, not a preemptive context switch.

### Q5: What is the primary advantage of Multilevel Feedback Queue (MLFQ) over Round Robin, and how does KERNOX implement aging?
**Answer:** Round Robin penalizes I/O-bound interactive processes by giving all processes the same time slice regardless of behavior. MLFQ uses multiple priority queues ($Q_0, Q_1, Q_2$); CPU-bound processes consume their time quantum and are demoted to lower queues with longer quanta, while I/O-bound jobs remain in high-priority queues. KERNOX implements priority aging via a configurable `--boost <ticks>` interval, which periodically flushes all tasks from lower queues back to $Q_0$, completely preventing starvation.

### Q6: What is the exact mathematical relationship between Waiting Time (WT), Turnaround Time (TAT), and Burst Time (BT)?
**Answer:**
$$\text{Turnaround Time} = \text{Completion Time} - \text{Arrival Time}$$
$$\text{Waiting Time} = \text{Turnaround Time} - \text{Burst Time}$$
In preemptive algorithms (SRTF, RR), Waiting Time represents the sum of all time intervals the process spent in the ready queue waiting for the CPU.

---

## Category 3: Disk Scheduling

### Q7: Why does SCAN (Elevator Algorithm) yield higher Total Head Movement than SSTF on the Silberschatz canonical workload, and why is SCAN still preferred in practice?
**Answer:** On the canonical workload (`98, 183, 37, 122, 14, 124, 65, 67` starting at 53), SSTF yields 236 cylinders while SCAN yields 208 (or 236 depending on boundary sweep). However, SSTF selects the nearest request greedily, which causes starvation for distant requests if a cluster of requests keeps arriving near the head. SCAN guarantees an upper bound on waiting time by sweeping continuously from one platter edge to the other.

### Q8: What is the critical mechanical difference between C-SCAN and C-LOOK?
**Answer:**
- **C-SCAN (Circular SCAN):** Travels all the way to the absolute end cylinder of the disk (e.g. cylinder 199), then immediately jumps to cylinder 0 without servicing requests on the return trip.
- **C-LOOK:** Only travels as far as the highest requested cylinder in the current direction, then jumps directly to the lowest requested cylinder without traveling all the way to cylinders 199 or 0, saving unnecessary seek travel.

---

## Category 4: Deadlock Management

### Q9: In Dijkstra's Banker's Algorithm, what is the significance of the $\text{Need}$ matrix, and how is it derived?
**Answer:**
$$\text{Need}[i][j] = \text{Max}[i][j] - \text{Allocation}[i][j]$$
The $\text{Need}$ matrix represents the remaining resource units process $i$ may request before completing. If $\text{Need}[i][j] > \text{Work}[j]$ for all active processes, the system cannot guarantee that any process can finish, indicating an unsafe state.

### Q10: How does KERNOX implement transaction-safe resource requests in `deadlock request`?
**Answer:** KERNOX performs speculative allocation:
1. It verifies that $\text{Request} \le \text{Need}$ and $\text{Request} \le \text{Available}$.
2. It speculatively modifies the state vectors:
   $$\text{Available} \leftarrow \text{Available} - \text{Request}$$
   $$\text{Allocation} \leftarrow \text{Allocation} + \text{Request}$$
   $$\text{Need} \leftarrow \text{Need} - \text{Request}$$
3. It executes the Banker's safety evaluation. If the resulting state is safe, the transaction is committed; if unsafe, KERNOX rolls back all three vectors to their original values and returns `ErrorCode::InvalidState`.

### Q11: In a Resource Allocation Graph (RAG), does the presence of a cycle always imply a deadlock?
**Answer:** No. A cycle is a necessary and sufficient condition for deadlock **only if** every resource class has exactly one single instance. If resources contain multiple instances, a cycle is a necessary condition but not sufficient, because processes outside the cycle may release instances that break the deadlock. KERNOX uses Tarjan's DFS for single-instance RAG cycles and a dedicated multi-instance vector detection algorithm for multi-instance configurations.

---

## Category 5: Memory Management & Virtual Memory

### Q12: How does Next Fit differ from First Fit, and what state must the allocator maintain?
**Answer:** First Fit always scans the free block list from the beginning (index 0) and allocates the first block large enough. Next Fit begins scanning from the location of the previous allocation (using a persistent cursor). It spreads allocations across memory, reducing fragmentation at the start of the list, but requires maintaining a stateful cursor that wraps around modulo the block count.

### Q13: In KERNOX's MMU paging simulation, how does it distinguish a Page Fault from a Segmentation Fault?
**Answer:**
1. **Segmentation Fault:** Occurs when the requested logical address exceeds the process's allocated address space ($\text{Logical Address} \ge \text{Virtual Limit}$). The address is fundamentally invalid.
2. **Page Fault:** Occurs when the logical address is within the valid virtual limit, but the page table entry indicates the page is currently not present in physical RAM ($\text{ValidBit} == 0$). The operating system must service a page-in trap from backing store.

### Q14: What is Bélady's Anomaly, and which page replacement algorithms are immune to it?
**Answer:** Bélady's Anomaly is the counter-intuitive phenomenon where increasing the number of physical page frames results in an increase in the number of page faults. It occurs in FIFO replacement. Stack-based algorithms (such as LRU and Optimal) are mathematically immune because the set of pages in an $N$-frame memory is always a strict subset of the pages in an $(N+1)$-frame memory.

### Q15: What is Peter Denning's Working-Set model, and how does KERNOX compute $W(t, \Delta)$?
**Answer:** The Working-Set model defines the set of pages referenced by a process in the most recent time window $\Delta$:
$$W(t, \Delta) = \{ p \in \text{Pages} \mid p \text{ was referenced in } [t - \Delta + 1, t] \}$$
KERNOX slides a window of size $\Delta$ across the reference trace and computes the set cardinality $|W(t, \Delta)|$, measuring process memory locality and predicting thrashing.

---

## Category 6: Linux Process Management & Sandboxing

### Q16: How does KERNOX pass error information from the child process back to the parent across `execve()`?
**Answer:** Before calling `fork()`, KERNOX creates an anonymous pipe with the `pipe2(O_CLOEXEC)` flag. If `execve()` succeeds, the pipe descriptor in the child is automatically closed by the kernel without sending data. If `execve()` fails, the child writes the `errno` integer into the pipe and calls `::_exit(127)`. The parent reads the pipe: if read returns 0 (EOF), `execve` succeeded; if it reads 4 bytes, it extracts the exact `errno` code.

### Q17: What is the signal escalation policy used in `ProcessManager::run`?
**Answer:** When a process exceeds its wall-clock execution timeout:
1. The supervisor sends `SIGTERM` to allow graceful termination and resource cleanup.
2. It enters a configurable grace period (default: 200ms), periodically checking `waitpid(pid, &status, WNOHANG)`.
3. If the process does not terminate within the grace period, the supervisor escalates to `SIGKILL`, which cannot be caught or ignored by the process.

### Q18: Which POSIX resource limits does KERNOX enforce using `setrlimit(2)`?
**Answer:**
- `RLIMIT_CPU`: Maximum CPU execution time in seconds (sends `SIGXCPU`).
- `RLIMIT_AS`: Maximum virtual address space in bytes (causes memory allocations like `malloc` or `mmap` to fail with `ENOMEM`).
- `RLIMIT_NOFILE`: Maximum number of open file descriptors.
- `RLIMIT_FSIZE`: Maximum file size in bytes that the process may create (sends `SIGXFSZ`).

### Q19: How does `wait4(2)` differ from standard `waitpid(2)`?
**Answer:** `wait4` accepts a pointer to `struct rusage`. Upon reaping the child process, the kernel populates this structure with high-resolution resource accounting metrics, including user CPU time (`ru_utime`), system CPU time (`ru_stime`), maximum resident set size (`ru_maxrss`), voluntary context switches (`ru_nvcsw`), and nonvoluntary context switches (`ru_nivcsw`).

---

## Category 7: System Telemetry & Procfs Parsing

### Q20: How does KERNOX calculate CPU utilization from `/proc/stat`?
**Answer:** It parses the first `cpu` line into tick counters: `user`, `nice`, `system`, `idle`, `iowait`, `irq`, `softirq`, `steal`.
$$\text{Total Ticks} = \text{user} + \text{nice} + \text{system} + \text{idle} + \text{iowait} + \text{irq} + \text{softirq} + \text{steal}$$
$$\text{Active Ticks} = \text{Total Ticks} - (\text{idle} + \text{iowait})$$
It samples these counters at $t_1$, sleeps for $\Delta t = 100\text{ms}$, samples at $t_2$, and computes:
$$\text{Utilization \%} = \frac{\Delta \text{Active}}{\Delta \text{Total}} \times 100\%$$

### Q21: What information is extracted from `/proc/<pid>/status` and `/proc/<pid>/io`?
**Answer:**
- `/proc/<pid>/status`: Process state (`R`, `S`, `Z`), parent PID, thread count, 4-tuple security credentials (real, effective, saved, and filesystem UIDs/GIDs), virtual memory footprint (`VmSize`, `VmRSS`), and context switch counts.
- `/proc/<pid>/io`: Characters read/written (`rchar`, `wchar`), read/write syscall counts (`syscr`, `syscw`), and physical storage byte transfers (`read_bytes`, `write_bytes`).

### Q22: Why does reading `/proc/1/io` fail with `PermissionDenied` when run as an unprivileged user?
**Answer:** Linux protects process I/O accounting via `PTRACE_MODE_READ_FSCREDS`. To read `/proc/<pid>/io`, the calling process must have `CAP_SYS_PTRACE` or share the same real/effective UID as the target process. PID 1 is owned by `root`; hence, an unprivileged user receives `EACCES`. KERNOX catches this and gracefully returns `ErrorCode::PermissionDenied`.

---

## Category 8: Inter-Process Communication (IPC)

### Q23: What is the POSIX `PIPE_BUF` guarantee, and how does KERNOX test it?
**Answer:** Under POSIX, writes to a pipe or FIFO of up to `PIPE_BUF` bytes (defined as 4096 bytes on Linux) are guaranteed to be atomic. If multiple processes write concurrently, their payloads will not be interleaved. In `test_ipc`, KERNOX verifies this invariant by spawning concurrent processes writing blocks $\le 4096$ bytes and confirming that reading processes never receive fragmented or mixed data packets.

### Q24: How does a named FIFO differ from an anonymous pipe?
**Answer:**
- **Anonymous Pipe:** Exists only in kernel memory and is accessible only to processes that inherit its file descriptors across `fork()`.
- **Named FIFO:** Created in the filesystem namespace via `mkfifo(path, mode)`. Any unrelated processes with appropriate filesystem permissions can open it by path (`open(path, O_RDONLY)` or `open(path, O_WRONLY)`).

### Q25: Why must a named FIFO open with `O_NONBLOCK` when opening in read-only mode if no writer is yet present?
**Answer:** Under POSIX, opening a FIFO with `O_RDONLY` blocks until another process opens it for writing (`O_WRONLY`). By specifying `O_NONBLOCK`, `open()` succeeds immediately without waiting for a writer, allowing event-driven asynchronous initialization.

---

## Category 9: POSIX Shared Memory

### Q26: What is the exact sequence of system calls required to create and map POSIX shared memory?
**Answer:**
1. `shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0660)`: Creates a virtual file in `/dev/shm`.
2. `ftruncate(fd, size)`: Sets the allocated size of the shared memory object.
3. `mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)`: Maps the shared memory into the process's virtual address space.
4. `close(fd)`: Closes the file descriptor (the memory mapping remains active).
5. `munmap(ptr, size)`: Unmaps the virtual address space on destruction.
6. `shm_unlink(name)`: Removes the shared memory object from the kernel VFS.

### Q27: How does KERNOX prevent child processes from accidentally unlinking shared memory when they terminate?
**Answer:** `SharedMemory` tracks ownership using `is_owner_` and `owner_pid_`. The creating process sets `owner_pid_ = ::getpid()`. When a child process is forked or when another process attaches via `SharedMemory::open_existing()`, `is_owner_` is set to `false`. On destruction, only the process matching `owner_pid_` invokes `shm_unlink()`.

---

## Category 10: Process-Shared Synchronization & Ring Buffer

### Q28: How is a `pthread_mutex_t` configured to operate across different processes in shared memory?
**Answer:**
1. The mutex attribute object must be initialized with `pthread_mutexattr_init(&attr)`.
2. The process-shared attribute must be explicitly set: `pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)`.
3. The mutex memory must reside in a memory region mapped with `MAP_SHARED` (such as POSIX shared memory), not heap or private stack memory.
4. `pthread_mutex_init(&mutex, &attr)` initializes the mutex in that shared memory region.

### Q29: What is `PTHREAD_MUTEX_ROBUST`, and what happens if a process crashes while holding a robust mutex?
**Answer:** `PTHREAD_MUTEX_ROBUST` is a mutex attribute (`pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST)`). If a process terminates (or is killed with `SIGKILL`) while holding the mutex, the Linux kernel detects the dead owner via the robust futex list. The next process calling `pthread_mutex_lock` unblocks and receives the return code `EOWNERDEAD`. The acquiring process must verify/restore shared invariants, call `pthread_mutex_consistent(&mutex)`, and continue safely.

### Q30: How does KERNOX's `SharedRingBuffer` achieve zero-heap cross-process communication?
**Answer:** The entire data structure is laid out in a single contiguous block of shared memory (`/dev/shm`). The memory is segmented into:
1. `RingBufferHeader`: Magic number, layout version, capacity, slot size, head index, tail index, item count, and embedded `ProcessSharedMutex` and `ProcessSharedConditionVariable` primitives.
2. Fixed-size `SlotHeader` structures and payload byte arrays.
No pointers or dynamic `malloc`/`new` allocations exist within the shared region; all slot indexing uses integer offsets relative to the base address.

### Q31: How does `SharedRingBuffer` signal shutdown to blocked producer and consumer processes?
**Answer:** It provides a `shutdown()` method that acquires the shared mutex, transitions the state to `RingBufferState::ShuttingDown`, and broadcasts on both `not_empty` and `not_full` condition variables (`pthread_cond_broadcast`). Any blocked threads/processes wake up, detect the `ShuttingDown` state, release the lock, and return `ErrorCode::Cancelled` without deadlocking.

---

## Category 11: UNIX Domain Sockets & Descriptor Passing

### Q32: How does a UNIX domain socket (`AF_UNIX`) differ from an Internet domain socket (`AF_INET`)?
**Answer:** `AF_UNIX` operates entirely within the Linux kernel virtual memory subsystem without the network stack:
- It bypasses network routing, packetization, checksumming, TCP windowing, and congestion control algorithms, achieving significantly lower latency and higher throughput.
- It uses filesystem paths (e.g. `/tmp/kernox.sock`) for binding instead of IP addresses and port numbers.
- It enables unique kernel security and resource transfer features: `SO_PEERCRED` and `SCM_RIGHTS`.

### Q33: How does `SO_PEERCRED` work, and why is it impervious to spoofing?
**Answer:** `getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &len)` queries the Linux kernel for the connected peer's credentials (`ucred.pid`, `ucred.uid`, `ucred.gid`). Because the kernel directly assigns these credentials from the task struct of the peer endpoint at connection time, the peer process cannot forge or spoof its identity.

### Q34: How does file descriptor passing work via `SCM_RIGHTS`?
**Answer:** A file descriptor integer in user space is merely an index into that specific process's file descriptor table. Passing the integer `4` across a pipe does not give another process access to file 4. With `SCM_RIGHTS`, `sendmsg(2)` sends the descriptor as ancillary control data (`struct cmsghdr`). The Linux kernel intercepts the control message, increments the reference count of the underlying open file description (`struct file`), allocates a new, valid file descriptor index in the receiving process's table, and delivers the new integer to `recvmsg(2)`.

---

## Category 12: POSIX Message Queues

### Q35: How does priority preemption work in POSIX Message Queues (`PosixMessageQueue`)?
**Answer:** In `mq_send(mqd, msg, len, priority)`, each message carries an integer priority (from 0 to 32,767). The Linux kernel inserts messages into the queue ordered by priority: higher-priority messages are inserted before lower-priority messages. When a consumer calls `mq_receive()`, it always dequeues the highest-priority message currently in the queue. Messages of identical priority are delivered in strict FIFO order.

### Q36: Why was a synchronization handshake necessary in `kernox ipc mq-demo`?
**Answer:** If the consumer child reads the queue concurrently while the parent is enqueuing messages, the consumer may dequeue message 1 (Priority 5) before the parent has had time to enqueue message 4 (Priority 50). To prove priority preemption deterministically, KERNOX uses an anonymous pipe handshake: the consumer waits until the parent finishes enqueuing all 5 messages. Once notified, the consumer reads the queue and receives them in strict priority order: 50 -> 30 -> 10 -> 5 -> 5.

### Q37: How does `mq_timedreceive(3)` implement timeouts?
**Answer:** `mq_timedreceive` takes a pointer to `struct timespec` representing an **absolute** wall-clock expiration time, not a relative duration. KERNOX converts the requested relative timeout (e.g. 500ms) by sampling `clock_gettime(CLOCK_REALTIME, &now)`, adding 500ms, and passing the absolute time. If no message arrives before that time, the call returns `ETIMEDOUT`, which KERNOX translates to `core::ErrorCode::Timeout`.

---

## Category 13: Memory Safety, Concurrency & RAII

### Q38: What is the Rule of Five, and how is it applied in KERNOX RAII wrappers?
**Answer:** The Rule of Five states that if a class customizes any of the destructor, copy constructor, copy assignment, move constructor, or move assignment, it should explicitly define all five. In `UniqueFd`, `ChildProcess`, `SharedMemory`, `UnixSocket`, and `PosixMessageQueue`:
- Copy constructor and copy assignment are `= delete` (resources cannot be duplicated).
- Move constructor and move assignment are implemented `noexcept` (transferring ownership and resetting the source to an invalid/null handle).
- Destructor cleans up the resource safely (`close`, `waitpid`, `munmap`, `mq_close`).

### Q39: What is the purpose of `std::atomic_thread_fence`, and why did GCC's ThreadSanitizer reject it?
**Answer:** `std::atomic_thread_fence(std::memory_order_release)` enforces a hardware CPU memory barrier ensuring that all prior memory writes (e.g. initializing mutexes, condvars, and slots in shared memory) are committed and visible to other CPU cores before publishing the `MAGIC` header value. GCC's TSan runtime rejects standalone thread fences (`-Werror=tsan`) because TSan tracks races per address and cannot model unaddressed fences without variable instrumentation. KERNOX guards fences with `#ifndef __SANITIZE_THREAD__`.

### Q40: What specific bugs are detected by AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan)?
**Answer:**
- **ASan:** Out-of-bounds heap, stack, and global buffer accesses; use-after-free; use-after-return; double free; and memory leaks.
- **UBSan:** Signed integer overflow, misaligned pointer dereferencing, division by zero, null pointer arithmetic, and invalid enum value casts.
KERNOX compiles and runs cleanly under both sanitizers with 0 warnings and 0 errors.

---

## Category 14: Practical Systems Scenarios & Edge Cases

### Q41: What happens if a process calls `execve()` on a non-existent binary or a file without execute permissions?
**Answer:** `execve` returns -1 and sets `errno` to `ENOENT` (No such file or directory) or `EACCES` (Permission denied). KERNOX's child error pipe captures this integer, sends it to the parent, and exits via `::_exit(127)`. The parent reads the code and wraps it in `core::Result::err(ErrorCode::NotFound)` or `ErrorCode::PermissionDenied`.

### Q42: What is a zombie process, and how does `ChildProcess` guarantee that zombies are never created?
**Answer:** A zombie process occurs when a child terminates but its parent has not yet read its exit status via `waitpid()`, leaving an entry in the kernel process table. `ChildProcess` prevents zombies via its destructor: if the child is still running, it terminates it (`SIGTERM`/`SIGKILL`), and unconditionally calls `waitpid(pid_, &status, 0)` to reap the process table entry.

### Q43: What happens if a process attempts to write to a pipe whose read end has been closed?
**Answer:** The Linux kernel generates a `SIGPIPE` signal and sets `errno = EPIPE`. If `SIGPIPE` is not handled or ignored, the default action terminates the process. KERNOX handles or prevents `SIGPIPE` (e.g. using `MSG_NOSIGNAL` on sockets or ignoring `SIGPIPE`), ensuring the write call returns an error mapped to `core::ErrorCode::IoError` rather than crashing.

### Q44: Why does KERNOX use `_exit()` instead of `exit()` in child processes after `fork()`?
**Answer:** Standard `exit(3)` flushes and closes user-space C stdio buffers (`stdout`, `stderr`) and executes functions registered with `atexit()`. Because `fork()` duplicates user-space memory, calling `exit()` in the child could flush buffered parent data a second time or invoke parent cleanup handlers. `_exit(2)` terminates the process immediately at the kernel level without touching parent stdio buffers.

### Q45: How does KERNOX guarantee that named POSIX objects in `/dev/shm` and `/dev/mqueue` are not leaked if a test or process crashes?
**Answer:**
1. In normal execution, RAII destructors call `shm_unlink()` and `mq_unlink()`.
2. In automated tests and CLI commands, object names incorporate the unique process ID (e.g. `/kernox_mq_demo_<pid>`), ensuring no name collisions occur.
3. Tests include explicit cleanup teardown blocks that execute `unlink` even if an assertion triggers early exit.
