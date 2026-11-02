# KERNOX Linux Process-Shared Synchronization Subsystem (Phase 4B.3.1, Phase 4B.3.2 & Phase 4B.3.3)

## 1. Overview and Architecture

The KERNOX Process-Shared Synchronization Subsystem provides production-quality, RAII-governed synchronization primitives and lock-free/transactional shared memory structures engineered specifically for multi-process coordination across POSIX shared memory boundaries:

- **`kernox::system::ProcessSharedMutex`**: An in-place wrapper for `pthread_mutex_t` configured with `PTHREAD_PROCESS_SHARED`, `PTHREAD_MUTEX_ROBUST`, and `PTHREAD_MUTEX_ERRORCHECK`. It provides robust owner-death detection (`EOWNERDEAD`), state consistency restoration (`make_consistent()`), monotonic timed acquisition, and permanent unrecoverable state enforcement (`ENOTRECOVERABLE`).
- **`kernox::system::ProcessSharedLockGuard`**: A move-only RAII lock guard tailored for robust mutex acquisition, supporting dead-owner awareness, automatic scoped unlocking, and explicit manual release.
- **`kernox::system::PosixSemaphore`**: A dual-mode RAII semaphore encapsulating POSIX named semaphores (`sem_open`, `sem_close`, `sem_unlink`) and unnamed process-shared semaphores (`sem_init(..., 1, ...)`, `sem_destroy`), with monotonic timeouts, signal retry loops (`EINTR`), and creator-based lifecycle management.
- **`kernox::system::ProcessSharedConditionVariable`**: An in-place wrapper for `pthread_cond_t` configured with `PTHREAD_PROCESS_SHARED` and `CLOCK_MONOTONIC`. Supports monotonic timed waiting, predicate loops resilient to spurious wakeups, and robust mutex death propagation (`EOWNERDEAD`).
- **`kernox::system::SharedRingBuffer`**: A bounded, multi-producer multi-consumer (MPMC) transactional ring buffer residing in POSIX shared memory with automatic crash recovery, transactional slot commits, memory barriers, initialization publication handshakes, and graceful shutdown.

```
+--------------------------------------------------------------------------------------------------+
|                                        KERNOX Application                                        |
|                          (Multi-Process Scheduling / Telemetry / IPC / CLI)                      |
+------------------------------------------------+-------------------------------------------------+
                                                 |
         +---------------------------------------+---------------------------------------+
         |                                                                               |
         v                                                                               v
+---------------------------------------+                               +--------------------------------+
|       ProcessSharedMutex              |                               |         PosixSemaphore         |
|  - In-place pthread_mutex_t in SHM    |                               |  - Named: sem_open / unlink    |
|  - PTHREAD_PROCESS_SHARED             |                               |  - Unnamed: sem_init (shm)     |
|  - PTHREAD_MUTEX_ROBUST               |                               |  - Atomic Post / Wait          |
|  - EOWNERDEAD / make_consistent()     |                               |  - Monotonic Timed Wait        |
|  - ENOTRECOVERABLE Protocol           |                               |  - EINTR Interruption Retries  |
+-------------------+-------------------+                               +--------------------------------+
                    |
     +--------------+--------------+
     |                             |
     v                             v
+------------------------+  +--------------------------------+
| ProcessSharedLockGuard |  | ProcessSharedConditionVariable |
|  - RAII Scoped Lock    |  |  - In-place pthread_cond_t    |
|  - is_owner_dead()     |  |  - PTHREAD_PROCESS_SHARED      |
|  - make_consistent()   |  |  - CLOCK_MONOTONIC Deadlines   |
|  - No Double Unlock    |  |  - Predicate Wait Loops        |
+------------------------+  +---------------+----------------+
                                            |
                                            v
+--------------------------------------------------------------------------------------------------+
|                                    SharedRingBuffer (MPMC)                                       |
|  - Fixed-capacity bounded producer-consumer queue in POSIX Shared Memory                         |
|  - Two process-shared condition variables: not_empty and not_full                                |
|  - Transaction state machine: Empty -> Writing -> Committed -> Reading -> Empty                  |
|  - Invariant crash recovery: Incomplete writes rolled back; in-progress reads restored           |
|  - Hardened memory barriers and publication handshake (Uninitialized -> Ready)                   |
|  - Graceful shutdown: Wakes blocked waiters; drains committed messages without data loss         |
+--------------------------------------------------------------------------------------------------+
```

---

## 2. In-Place Placement in POSIX Shared Memory

All process-shared synchronization primitives (`ProcessSharedMutex`, unnamed `PosixSemaphore`, and `ProcessSharedConditionVariable`) and `SharedRingBuffer` are designed to reside directly inside POSIX shared memory segments managed by `kernox::system::SharedMemory`.

### Zero Dynamic Allocation
None of these primitives perform dynamic heap allocation. The underlying `pthread_mutex_t`, `pthread_cond_t`, and `sem_t` control blocks live entirely within shared memory mapped across distinct process address spaces (`MAP_SHARED`).

### In-Place Lifecycle
1. **Creation/Initialization (`init_in_place`)**:
   - The creator process initializes the raw memory location using:
     - `ProcessSharedMutex::init_in_place(raw_mutex)`
     - `PosixSemaphore::init_in_place(raw_sem, initial_value)`
     - `ProcessSharedConditionVariable::init_in_place(raw_cond)`
   - The mutex is configured with `PTHREAD_PROCESS_SHARED` and `PTHREAD_MUTEX_ROBUST`.
   - The condition variable is configured with `PTHREAD_PROCESS_SHARED` and `CLOCK_MONOTONIC`.
   - Unnamed semaphores are initialized with `pshared = 1`.
2. **Attaching (Non-Owning Wrapper Constructors)**:
   - Secondary processes attach to the pre-initialized control block using:
     - `ProcessSharedMutex(raw_mutex)`
     - `PosixSemaphore::attach_unnamed(raw_sem)`
     - `ProcessSharedConditionVariable(raw_cond)`
   - Attached controllers operate on the existing synchronization primitives without reinitializing them.
3. **Destruction (`destroy_in_place`)**:
   - Only the designated owner/creator calls `destroy_in_place()` once all participating processes have terminated.
   - Non-owning controllers destruct safely without destroying the shared OS object.

---

## 3. Robust Mutex Recovery Protocol

When a process holding a standard mutex terminates unexpectedly (e.g. `SIGKILL`, `SIGSEGV`, or `_exit(2)`), traditional mutexes deadlock all surviving processes.

`ProcessSharedMutex` eliminates deadlocks via POSIX Robust Mutex semantics:

```
[ Process A holds Lock ] ----( SIGKILL / Crash )----+
                                                    |
                                                    v
[ Process B calls lock() ] <--- Returns EOWNERDEAD (Lock Acquired)
             |
             +---> Inspect / Repair Shared State Integrity
             |
             +---> Case 1: State Repaired Successfully
             |       |
             |       v
             |     Call mutex.make_consistent()
             |       |
             |       v
             |     Call mutex.unlock()
             |       |
             |       v
             |     Mutex remains healthy for all future callers!
             |
             +---> Case 2: State Unrecoverable / Corrupted
                     |
                     v
                   Call mutex.unlock() (without make_consistent)
                     |
                     v
                   Mutex transitions permanently to ENOTRECOVERABLE!
                   (All subsequent lock attempts fail safely with InvalidState)
```

### Protocol Guarantees:
1. **Acquisition on Death**: When `lock()`, `try_lock()`, or `timed_lock()` detects that the previous owner terminated while holding the lock, it returns `LockAcquireStatus::OwnerDead`. The lock **is acquired** by the calling thread.
2. **State Repair**: The new owner must inspect and repair shared memory invariants corrupted by the previous owner's abrupt termination.
3. **Restoration**: Calling `make_consistent()` notifies the kernel that consistency has been restored. Upon subsequent `unlock()`, the mutex returns to normal operation.
4. **Permanent Failure Prevention**: If the new owner unlocks without calling `make_consistent()`, the mutex transitions to `ENOTRECOVERABLE`. Any subsequent attempt to lock the mutex returns `core::ErrorCode::InvalidState` with error code `ENOTRECOVERABLE`, preventing processes from acting on silent corruption.

---

## 4. Condition Variable Architecture and Robust Mutex Integration

`ProcessSharedConditionVariable` encapsulates `pthread_cond_t` with process-shared and monotonic clock attributes.

### Clock Selection (`CLOCK_MONOTONIC`)
Condition variable timed waits configure `pthread_condattr_setclock(&attr, CLOCK_MONOTONIC)`. This ensures deadlines are measured against a steady monotonically increasing clock and are unaffected by system wall-clock adjustments (NTP steps, daylight savings).

### Predicate-Based Waiting
To guard against spurious wakeups, `wait()` and `wait_for()` provide predicate overloads:
```cpp
template <typename Predicate>
core::Result<LockAcquireStatus> wait(ProcessSharedMutex& mutex, Predicate pred);
```
The condition variable repeatedly checks the predicate under mutex protection.

### Critical Robust Mutex Return Guarantee
When waiting on a condition variable backed by a robust mutex, if the thread currently owning the mutex dies, POSIX specifies that `pthread_cond_wait()` returns `EOWNERDEAD`.
KERNOX preserves this information:
1. `wait()` and `wait_for()` return `core::Result<LockAcquireStatus>`.
2. If `LockAcquireStatus::OwnerDead` is returned, **the calling thread holds the mutex**, but the state requires repair.
3. The predicate wait loop terminates immediately upon encountering `OwnerDead` rather than evaluating a potentially corrupted predicate on inconsistent memory. The caller repairs invariants, invokes `make_consistent()`, and resumes execution.

---

## 5. Bounded Shared-Memory Ring Buffer (`SharedRingBuffer`)

`SharedRingBuffer` is a bounded multi-producer multi-consumer queue allocated entirely in POSIX shared memory.

### Shared Memory Layout
```
+---------------------------------------------------------------------------------+
| Offset 0x00: RingBufferHeader (alignas(64))                                    |
|   - magic (0x4B524E58, 'KRNX')                                                  |
|   - version (1)                                                                 |
|   - state (Uninitialized=0, Ready=1, Shutdown=2, Corrupted=3)                   |
|   - capacity (Total slots)                                                      |
|   - slot_size (Configured bytes per slot)                                       |
|   - slot_stride (Aligned sizeof(SlotHeader) + slot_size, alignas(8))            |
|   - head (64-bit consumer monotonic pop counter)                                |
|   - tail (64-bit producer monotonic push counter)                               |
|   - count (Committed item counter)                                              |
|   - is_shutdown (1 if shutdown, 0 otherwise)                                    |
|   - slots_offset (Byte offset to Slot 0)                                        |
|   - mutex (pthread_mutex_t, robust, process-shared, alignas(64))                |
|   - not_empty (pthread_cond_t, process-shared, monotonic, alignas(64))          |
|   - not_full (pthread_cond_t, process-shared, monotonic, alignas(64))           |
+---------------------------------------------------------------------------------+
| Offset slots_offset: Slots Array                                                |
|   [ Slot 0: SlotHeader (state, size, seq) + payload bytes (slot_size) ]         |
|   [ Slot 1: SlotHeader (state, size, seq) + payload bytes (slot_size) ]         |
|   ...                                                                           |
|   [ Slot N-1: SlotHeader ... ]                                                  |
+---------------------------------------------------------------------------------+
```

### Initialization Publication Handshake
To eliminate race conditions where a secondary process opens a shared memory segment before in-place mutex and condition variable initialization completes:
1. `create()` initializes `hdr->magic = 0` and `hdr->state = RingBufferState::Uninitialized`.
2. `pthread_mutex_init` and `pthread_cond_init` are executed and checked for errors.
3. All slot headers are initialized to `SlotState::Empty`.
4. A memory release barrier (`std::atomic_thread_fence(std::memory_order_release)`) is emitted.
5. `hdr->version = VERSION`, `hdr->magic = MAGIC`, and `hdr->state = RingBufferState::Ready` are published.
6. A concluding release fence ensures global visibility across cores.
7. `open_existing()` issues an acquire memory barrier and explicitly rejects any segment where `magic != MAGIC` or `state == Uninitialized` with `ErrorCode::InvalidState`.

### Transaction State Machine & Boundary Hardening
Each slot carries per-slot atomic state markers:
- `Empty` (`0x00000000`): Available for writing.
- `Writing` (`0x57524954` / `'WRIT'`): Producer is writing payload. Incomplete/uncommitted.
- `Committed` (`0x434F4D4D` / `'COMM'`): Message fully written and available for consumption.
- `Reading` (`0x52454144` / `'READ'`): Consumer is actively reading payload.

#### Hardened Push Sequence
1. Slot at `hdr->tail % capacity` set to `Writing`.
2. Copy message payload into slot buffer.
3. Set payload size and sequence number.
4. Advance `hdr->tail++` and `hdr->count++`.
5. Transition slot state to `Committed`.
6. Signal `not_empty.notify_one()` and release mutex.

#### Hardened Pop Sequence
1. Slot at `hdr->head % capacity` set to `Reading`.
2. Copy message payload into caller buffer.
3. Advance `hdr->head++` and `hdr->count--`.
4. Transition slot state to `Empty` and clear metadata.
5. Signal `not_full.notify_one()` and release mutex.

### Transaction Boundary Crash Analysis & Exact Delivery Semantics

| Failure Point | Exact Boundary Point | Recovery Action in `recover_after_owner_death()` | Delivery Guarantee |
| :--- | :--- | :--- | :--- |
| **P1** | Producer crashes in `push()` during copy, before `tail++` | Slot at `tail % capacity` is `Writing`. Cleared to `Empty`, payload zeroed. `tail` and `count` untouched. | **At-most-once (No corruption)**: Partial write completely discarded. |
| **P2** | Producer crashes after `tail++` / `count++`, before `Committed` | Slot at `(tail - 1) % capacity` is `Writing`. Decrement `tail--`, `count--`, zero slot and mark `Empty`. | **At-most-once (No corruption)**: Partial write rolled back; queue indices restored to valid state. |
| **P3** | Producer crashes after `Committed`, before mutex release | Invariant check validates `Committed` slot in active range `[head, tail)`. Mutex made consistent. | **Exactly-once preserved**: Message remains committed in buffer for next consumer. |
| **C1** | Consumer crashes in `pop()` during copy, before `head++` | Slot at `head % capacity` is `Reading`. Rolled back to `Committed`. `head` and `count` untouched. | **At-least-once delivery**: The dead consumer's uncompleted read is preserved; next consumer pops it. |
| **C2** | Consumer crashes after `head++` / `count--`, before `Empty` | Slot at `(head - 1) % capacity` is `Reading`. Mark `Empty`, zero payload and metadata. `head` / `count` untouched. | **At-most-once processing**: The message was already popped; slot freeing is completed. |
| **C3** | Consumer crashes inside application processing after `pop()` returns | Message is outside the ring buffer; slot is already `Empty`. | **End of Ring Buffer Guarantees**: Queue guarantees terminate upon `pop()` return. Application must persist or acknowledge messages at the application layer if downstream processing failures must be survived. |

### Strict Invariant Verification
Upon detecting `OwnerDead`, `recover_after_owner_death()` executes a full queue integrity audit:
1. Verifies `hdr->tail >= hdr->head`.
2. Verifies `active_count = hdr->tail - hdr->head <= hdr->capacity`.
3. Verifies `hdr->count == active_count`.
4. Verifies all slots in `[head, tail)` are strictly `SlotState::Committed`.
5. Verifies all slots outside `[head, tail)` are strictly `SlotState::Empty`.
6. If any invariant is violated, the buffer is marked `RingBufferState::Corrupted` and fails permanently, preventing silent corruption.

---

## 6. Semaphore and Mutex Ownership and Lifetime Semantics

### Semaphore Rules:
- **`create_named`**: Creates and unlinks named semaphores. Only the creating instance holds `is_owner_ = true`. Its destructor issues `sem_close()` followed by `sem_unlink()`.
- **`open_named`**: Opens an existing named semaphore with `is_owner_ = false`. Its destructor issues `sem_close()` only; it **never calls `sem_unlink()`**.
- **`attach_unnamed`**: Wraps an unnamed process-shared `sem_t` in shared memory with `type = Unnamed` and `is_owner_ = false`. Its destructor never calls `sem_close()` or `sem_unlink()`.
- **Move semantics**: Transfer ownership safely while zeroing source instances (`sem_ = SEM_FAILED`, `is_owner_ = false`), preventing double-closing or double-unlinking.
- **`init_in_place` / `destroy_in_place`**: Explicit static methods for in-place shared-memory lifecycles. `destroy_in_place` is only called by the owning shared memory manager.

### Mutex Rules:
- Mutexes are allocated in shared memory with `PTHREAD_PROCESS_SHARED`, `PTHREAD_MUTEX_ROBUST`, and `PTHREAD_MUTEX_ERRORCHECK`.
- If an owner process dies, the surviving process detects `EOWNERDEAD`. It must repair invariants and call `make_consistent()` before unlocking.
- Unlocking without calling `make_consistent()` permanently moves the mutex to `ENOTRECOVERABLE`.

---

## 7. C++20 API Usage Examples

### Producer-Consumer with `SharedRingBuffer`

```cpp
#include <kernox/modules/system/ring_buffer.hpp>
#include <iostream>

using namespace kernox::system;

// --- Creator / Producer Process ---
RingBufferConfig config;
config.capacity = 16;
config.slot_size = 256;

auto rb = SharedRingBuffer::create("/kernox_telemetry_queue", config);
if (rb.is_ok()) {
    // Push messages (blocks if full, monotonic timeout supported)
    rb->push("TELEMETRY: CPU=45% MEM=2.1GB");
    rb->push("EVENT: Process 412 started");
}

// --- Consumer Process ---
auto rb = SharedRingBuffer::open_existing("/kernox_telemetry_queue");
if (rb.is_ok()) {
    while (true) {
        auto msg = rb->pop(std::chrono::milliseconds(1000));
        if (msg.is_ok()) {
            std::cout << "Received: " << msg.value() << "\n";
        } else {
            break; // Timeout or shutdown
        }
    }
}
```

### CLI Demonstration

The KERNOX CLI includes an integrated demonstration command:

```bash
# Run a live cross-process ring buffer producer-consumer demonstration
kernox sync demo --messages 10 --capacity 4 --slot-size 128

# Display synchronization command help
kernox sync --help
```

---

## 8. Automated Test Suite & Verification Matrix

The test suite in `tests/test_sync.cpp` provides **20 automated test suites** containing **228 assertions** (`KERNOX_ASSERT_*`), registered as the `test_sync` target in CTest alongside the other 13 KERNOX subsystem test suites (totaling 14 registered CTest test suites):

| Test ID | Name | Subsystem | Verification Description |
| :---: | :--- | :--- | :--- |
| **1** | Process-Shared Mutex Exclusion | Mutex | 2 concurrent processes execute 20,000 serialized increments. Verify final counter == 20,000 without data races. |
| **2** | Robust Mutex Owner Death Recovery | Mutex | Child terminated with `SIGKILL` while holding lock. Parent handles `EOWNERDEAD`, restores state, calls `make_consistent()`. |
| **3** | Mutex Unrecoverable Protocol | Mutex | Child killed with `SIGKILL`. Parent unlocks without `make_consistent()`. Confirms permanent transition to `ENOTRECOVERABLE`. |
| **4** | Named Semaphore Lifecycle | Semaphore | Creator exclusive creation, cross-process `wait()` / `post()`, non-blocking `try_wait()`, and RAII unlinking. |
| **5** | Unnamed Shared Semaphore | Semaphore | Cross-process synchronization using an in-place `sem_t` in shared memory. |
| **6** | Scoped Guard & Timed Acquisition | Guard / Mutex | `ProcessSharedLockGuard` lifecycle, cross-process `try_lock()` busy check, and monotonic `timed_lock()` timeout. |
| **7** | Defensive Boundary Validation | Core | Null pointer rejection, unattached controller guards, negative timeouts, and malformed semaphore names. |
| **8** | Condition Variable Cross-Process Signaling | CondVar | Forked child blocks on shared predicate; parent sets predicate and signals `notify_one()`; child wakes and verifies. |
| **9** | Condition Variable Monotonic Timeout | CondVar | Bounded 50ms wait on unmet predicate; verifies `TimedOut` status and clock accuracy; confirms mutex is still held. |
| **10** | SPSC Ring Buffer | Ring Buffer | 50 sequential messages transferred from child producer to parent consumer; verifies exact FIFO sequence. |
| **11** | MPMC Concurrent Ring Buffer | Ring Buffer | 3 producers (500 msgs each) and 2 consumers. Exactly 1,500 messages consumed with 0 duplicates and 0 dropped. |
| **12** | Full-Buffer Blocking | Ring Buffer | Buffer filled to capacity (2 slots); additional producer blocks until parent consumes one item; producer unblocks. |
| **13** | Empty-Buffer Blocking | Ring Buffer | Consumer blocks on empty buffer; parent pushes message; consumer wakes and receives item. |
| **14** | Graceful Shutdown | Ring Buffer | Blocked consumer on empty buffer wakes immediately when `shutdown()` is called; future pushes rejected. |
| **15** | Owner-Dead Crash Recovery (Case 1) | Ring Buffer | Child crashes (`SIGKILL`) while in `Writing` state before `tail++`; parent heals slot and pushes successfully. |
| **16** | Layout Validation & Error Rejection | Ring Buffer | Corrupt magic, invalid layout versions, zero capacity, and corrupted states are safely rejected on open. |
| **17** | Push Crash Recovery (Case 2) | Ring Buffer | Child crashes after `tail++` and `count++` before `Committed`. Parent rolls back `tail--`, `count--`, heals slot to `Empty`. |
| **18** | Pop Crash Recovery (Case 4) | Ring Buffer | Child crashes after `head++` and `count--` before `Empty`. Parent completes freeing slot to `Empty` and preserves invariant. |
| **19** | Pop Crash Rollback (Case 3) | Ring Buffer | Child crashes during read before `head++`. Parent rolls back slot to `Committed`, preserving message (at-least-once). |
| **20** | Initialization Handshake Validation | Ring Buffer | Shared memory created with state `Uninitialized`. `open_existing()` rejects with `InvalidState`, preventing peer races. |

### Build and Test Execution Across Presets

```bash
# Debug preset
./scripts/test.sh dev-debug

# Release preset (optimized -O3)
./scripts/test.sh dev-release

# AddressSanitizer + UndefinedBehaviorSanitizer
./scripts/test.sh dev-asan

# Direct execution of synchronization test suite
./build/debug/bin/test_sync
```
