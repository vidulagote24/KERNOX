#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <string_view>

namespace kernox::system {

// ----------------------------------------------------------------------------
// Lock Acquisition Status
// ----------------------------------------------------------------------------

/**
 * @brief Represents the exact outcome of a lock acquisition attempt.
 */
enum class LockAcquireStatus {
    Acquired,       // Locked normally; protected state is consistent
    OwnerDead,      // Locked successfully, but previous owner died while holding the lock.
                    // The protected state is inconsistent and requires caller recovery.
    Busy,           // Lock could not be acquired without blocking (try_lock)
    TimedOut        // Lock could not be acquired within the specified timeout duration (timed_lock)
};

[[nodiscard]] constexpr std::string_view to_string(LockAcquireStatus status) noexcept {
    switch (status) {
        case LockAcquireStatus::Acquired:  return "Acquired";
        case LockAcquireStatus::OwnerDead: return "OwnerDead";
        case LockAcquireStatus::Busy:      return "Busy";
        case LockAcquireStatus::TimedOut:  return "TimedOut";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, LockAcquireStatus status) {
    return os << to_string(status);
}

// ----------------------------------------------------------------------------
// ProcessSharedMutex
// ----------------------------------------------------------------------------

/**
 * @brief Non-owning controller for a process-shared, robust POSIX mutex residing in shared memory.
 *
 * Invariants & Contract:
 *  1. The underlying pthread_mutex_t MUST reside in POSIX shared memory (mmap MAP_SHARED).
 *  2. Configured with PTHREAD_PROCESS_SHARED to allow multi-process coordination.
 *  3. Configured with PTHREAD_MUTEX_ROBUST to prevent permanent deadlocks if an owner process crashes.
 *  4. Configured with PTHREAD_MUTEX_ERRORCHECK to catch recursive locking and invalid unlock attempts.
 *  5. This controller does NOT own the memory storage or call pthread_mutex_destroy in its destructor.
 *     Destruction is performed explicitly via destroy_in_place() by the master/creator process.
 *
 * Robust Mutex Rule:
 *  When lock() returns LockAcquireStatus::OwnerDead, the caller holds the lock, but the protected state
 *  is inconsistent. The caller must inspect and repair the state, then call make_consistent().
 *  If recovery is impossible or rejected, unlocking without make_consistent() marks the mutex
 *  permanently unrecoverable (subsequent locks will fail with ENOTRECOVERABLE).
 */
class ProcessSharedMutex {
public:
    /**
     * @brief Initializes a raw pthread_mutex_t in-place with process-shared and robust attributes.
     * @param raw_mutex Pointer to uninitialized pthread_mutex_t in shared memory.
     * @return Result<void> indicating success or error.
     */
    static core::Result<void> init_in_place(pthread_mutex_t* raw_mutex);

    /**
     * @brief Destroys a raw pthread_mutex_t in-place.
     *        Must be called only when no other thread or process will attempt to access it.
     * @param raw_mutex Pointer to initialized pthread_mutex_t.
     * @return Result<void> indicating success or error.
     */
    static core::Result<void> destroy_in_place(pthread_mutex_t* raw_mutex) noexcept;

    /**
     * @brief Constructs a controller referencing an in-place initialized mutex.
     */
    explicit ProcessSharedMutex(pthread_mutex_t* raw_mutex) noexcept;

    /**
     * @brief Default constructor creating an invalid controller.
     */
    ProcessSharedMutex() noexcept = default;

    /**
     * @brief Destructor does NOT destroy the underlying shared mutex.
     */
    ~ProcessSharedMutex() noexcept = default;

    // Move-only semantics with explicit invalidation of the source controller
    ProcessSharedMutex(ProcessSharedMutex&& other) noexcept;
    ProcessSharedMutex& operator=(ProcessSharedMutex&& other) noexcept;

    ProcessSharedMutex(const ProcessSharedMutex&) = delete;
    ProcessSharedMutex& operator=(const ProcessSharedMutex&) = delete;

    [[nodiscard]] bool is_valid() const noexcept { return mutex_ != nullptr; }
    [[nodiscard]] pthread_mutex_t* native_handle() noexcept { return mutex_; }
    [[nodiscard]] const pthread_mutex_t* native_handle() const noexcept { return mutex_; }

    /**
     * @brief Acquires the mutex, blocking until available.
     * @return Result<LockAcquireStatus> containing Acquired or OwnerDead on success,
     *         or Error on system failure (such as ENOTRECOVERABLE).
     */
    core::Result<LockAcquireStatus> lock();

    /**
     * @brief Attempts to acquire the mutex without blocking.
     * @return Result<LockAcquireStatus> containing Acquired, Busy, or OwnerDead on success,
     *         or Error on failure.
     */
    core::Result<LockAcquireStatus> try_lock();

    /**
     * @brief Attempts to acquire the mutex with a bounded timeout.
     * @param timeout Maximum duration to wait for acquisition.
     * @return Result<LockAcquireStatus> containing Acquired, TimedOut, or OwnerDead on success,
     *         or Error on failure.
     */
    core::Result<LockAcquireStatus> timed_lock(std::chrono::milliseconds timeout);

    /**
     * @brief Releases the mutex.
     * @return Result<void> indicating success, or Error if the calling thread does not hold the mutex.
     */
    core::Result<void> unlock() noexcept;

    /**
     * @brief Marks the state protected by this robust mutex as consistent after receiving OwnerDead.
     * @return Result<void> indicating success or error.
     */
    core::Result<void> make_consistent() noexcept;

private:
    pthread_mutex_t* mutex_{nullptr};
};

// ----------------------------------------------------------------------------
// ProcessSharedLockGuard
// ----------------------------------------------------------------------------

/**
 * @brief RAII scoped lock guard for ProcessSharedMutex. Automatically unlocks on destruction.
 */
class ProcessSharedLockGuard {
public:
    explicit ProcessSharedLockGuard(ProcessSharedMutex& mutex, LockAcquireStatus status) noexcept;
    ~ProcessSharedLockGuard() noexcept;

    ProcessSharedLockGuard(const ProcessSharedLockGuard&) = delete;
    ProcessSharedLockGuard& operator=(const ProcessSharedLockGuard&) = delete;

    ProcessSharedLockGuard(ProcessSharedLockGuard&& other) noexcept;
    ProcessSharedLockGuard& operator=(ProcessSharedLockGuard&& other) noexcept;

    [[nodiscard]] bool is_owner_dead() const noexcept { return status_ == LockAcquireStatus::OwnerDead; }
    [[nodiscard]] LockAcquireStatus status() const noexcept { return status_; }
    [[nodiscard]] bool is_locked() const noexcept { return locked_; }

    /**
     * @brief Marks the state as consistent and updates status to Acquired.
     */
    core::Result<void> make_consistent() noexcept;

    /**
     * @brief Manually unlocks before the guard goes out of scope.
     */
    core::Result<void> unlock() noexcept;

private:
    ProcessSharedMutex* mutex_{nullptr};
    LockAcquireStatus status_{LockAcquireStatus::Acquired};
    bool locked_{true};
};

// ----------------------------------------------------------------------------
// PosixSemaphore
// ----------------------------------------------------------------------------

enum class SemaphoreType {
    Named,      // Backed by sem_open / sem_close / sem_unlink (OS kernel object)
    Unnamed     // Backed by sem_init / sem_destroy (resides in shared memory)
};

[[nodiscard]] constexpr std::string_view to_string(SemaphoreType type) noexcept {
    switch (type) {
        case SemaphoreType::Named:   return "Named";
        case SemaphoreType::Unnamed: return "Unnamed";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, SemaphoreType type) {
    return os << to_string(type);
}

/**
 * @brief Move-only RAII controller for POSIX semaphores (named and unnamed process-shared).
 *
 * Invariants & Ownership Contract:
 *  - Named Semaphores:
 *      * Created via create_named() with O_CREAT | O_EXCL. The creating instance is the owner
 *        and will invoke sem_unlink() in its destructor.
 *      * Opened via open_named() without ownership. It calls sem_close() in its destructor
 *        but will NOT invoke sem_unlink(), leaving the semaphore intact for peers.
 *  - Unnamed Process-Shared Semaphores:
 *      * Initialized in-place via init_in_place(raw_sem, value) with pshared=1.
 *      * Attached via attach_unnamed(raw_sem). Controllers for unnamed semaphores do NOT
 *        call sem_destroy() in their destructor; destruction is managed explicitly via destroy_in_place().
 *  - EINTR Safety: All blocking operations automatically loop on EINTR.
 */
class PosixSemaphore {
public:
    static constexpr size_t MAX_NAME_LENGTH = 255;

    /**
     * @brief Validates a POSIX semaphore name according to standard rules:
     *  - Must not be empty.
     *  - Must start with a single slash ('/').
     *  - Must not contain any subsequent slashes ('/').
     *  - Length must be at least 2 and at most MAX_NAME_LENGTH.
     */
    static core::Result<void> validate_name(std::string_view name);

    /**
     * @brief Exclusively creates a new named POSIX semaphore.
     *        The returned instance takes ownership and will call sem_unlink on destruction.
     * @param name Semaphore name starting with '/'.
     * @param initial_value Starting semaphore counter (must be >= 0).
     * @param mode POSIX file permissions (default: 0600).
     */
    static core::Result<PosixSemaphore> create_named(
        const std::string& name,
        unsigned int initial_value,
        mode_t mode = 0600);

    /**
     * @brief Opens an existing named POSIX semaphore without taking unlink ownership.
     * @param name Semaphore name starting with '/'.
     */
    static core::Result<PosixSemaphore> open_named(const std::string& name);

    /**
     * @brief Initializes an unnamed process-shared semaphore in shared memory (pshared = 1).
     * @param raw_sem Pointer to uninitialized sem_t residing in shared memory.
     * @param initial_value Starting semaphore counter.
     */
    static core::Result<void> init_in_place(sem_t* raw_sem, unsigned int initial_value);

    /**
     * @brief Destroys an unnamed process-shared semaphore in shared memory.
     * @param raw_sem Pointer to initialized sem_t.
     */
    static core::Result<void> destroy_in_place(sem_t* raw_sem) noexcept;

    /**
     * @brief Attaches to an existing in-place initialized unnamed semaphore.
     */
    static PosixSemaphore attach_unnamed(sem_t* raw_sem) noexcept;

    /**
     * @brief Default constructor creating an unattached/invalid semaphore.
     */
    PosixSemaphore() noexcept = default;

    /**
     * @brief Destructor. Closes named handles (and unlinks if owner). Does not destroy unnamed semaphores.
     */
    ~PosixSemaphore() noexcept;

    // Move-only semantics with explicit invalidation
    PosixSemaphore(PosixSemaphore&& other) noexcept;
    PosixSemaphore& operator=(PosixSemaphore&& other) noexcept;

    PosixSemaphore(const PosixSemaphore&) = delete;
    PosixSemaphore& operator=(const PosixSemaphore&) = delete;

    [[nodiscard]] bool is_valid() const noexcept { return sem_ != nullptr && sem_ != SEM_FAILED; }
    [[nodiscard]] SemaphoreType type() const noexcept { return type_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] bool is_owner() const noexcept { return is_owner_; }
    [[nodiscard]] sem_t* native_handle() noexcept { return sem_; }
    [[nodiscard]] const sem_t* native_handle() const noexcept { return sem_; }

    /**
     * @brief Decrements (locks) the semaphore. Blocks if value is 0. Automatically retries on EINTR.
     * @return Result<void> indicating success or error.
     */
    core::Result<void> wait();

    /**
     * @brief Attempts to decrement the semaphore without blocking.
     * @return Result<bool> true if decremented, false if semaphore value was 0 (EAGAIN),
     *         or Error on system failure.
     */
    core::Result<bool> try_wait();

    /**
     * @brief Attempts to decrement the semaphore with a bounded timeout.
     * @param timeout Maximum duration to wait.
     * @return Result<bool> true if decremented, false on timeout (ETIMEDOUT), or Error on failure.
     */
    core::Result<bool> timed_wait(std::chrono::milliseconds timeout);

    /**
     * @brief Increments (unlocks) the semaphore. Wakes one waiting process/thread if count was 0.
     * @return Result<void> indicating success or error.
     */
    core::Result<void> post() noexcept;

    /**
     * @brief Queries the current value of the semaphore.
     * @return Result<int> current value on success, or Error on failure.
     */
    core::Result<int> get_value() const noexcept;

    /**
     * @brief Explicitly unlinks a named semaphore from the operating system and revokes ownership.
     */
    core::Result<void> unlink() noexcept;

private:
    PosixSemaphore(sem_t* sem, SemaphoreType type, std::string name, bool is_owner) noexcept;

    sem_t* sem_{SEM_FAILED};
    SemaphoreType type_{SemaphoreType::Unnamed};
    std::string name_{};
    bool is_owner_{false};
};

// ----------------------------------------------------------------------------
// ProcessSharedConditionVariable
// ----------------------------------------------------------------------------

/**
 * @brief Non-owning controller for a process-shared POSIX condition variable residing in shared memory.
 *
 * Invariants & Contract:
 *  1. The underlying pthread_cond_t MUST reside in POSIX shared memory (mmap MAP_SHARED).
 *  2. Initialized with PTHREAD_PROCESS_SHARED to allow multi-process coordination.
 *  3. Initialized with CLOCK_MONOTONIC to guarantee wait deadlines are resilient to system clock changes.
 *  4. Destruction is explicit via destroy_in_place() by the master/creator process.
 *  5. Robust Mutex Contract:
 *     - wait() and wait_for() can return LockAcquireStatus::OwnerDead if the mutex was re-acquired
 *       after the previous owner process died. In this state, the caller holds the mutex, but the
 *       protected state requires recovery before calling make_consistent().
 *     - If wait() returns Acquired or TimedOut or OwnerDead, the caller holds the mutex.
 *     - If wait() returns Error, the mutex is NOT held.
 */
class ProcessSharedConditionVariable {
public:
    /**
     * @brief Initializes a raw pthread_cond_t in-place with PTHREAD_PROCESS_SHARED and CLOCK_MONOTONIC.
     */
    static core::Result<void> init_in_place(pthread_cond_t* raw_cond);

    /**
     * @brief Destroys a raw pthread_cond_t in-place.
     */
    static core::Result<void> destroy_in_place(pthread_cond_t* raw_cond) noexcept;

    /**
     * @brief Constructs a controller referencing an in-place initialized condition variable.
     */
    explicit ProcessSharedConditionVariable(pthread_cond_t* raw_cond) noexcept;

    ProcessSharedConditionVariable() noexcept = default;
    ~ProcessSharedConditionVariable() noexcept = default;

    // Move-only semantics with explicit invalidation
    ProcessSharedConditionVariable(ProcessSharedConditionVariable&& other) noexcept;
    ProcessSharedConditionVariable& operator=(ProcessSharedConditionVariable&& other) noexcept;

    ProcessSharedConditionVariable(const ProcessSharedConditionVariable&) = delete;
    ProcessSharedConditionVariable& operator=(const ProcessSharedConditionVariable&) = delete;

    [[nodiscard]] bool is_valid() const noexcept { return cond_ != nullptr; }
    [[nodiscard]] pthread_cond_t* native_handle() noexcept { return cond_; }
    [[nodiscard]] const pthread_cond_t* native_handle() const noexcept { return cond_; }

    /**
     * @brief Wakes at least one waiting thread or process.
     */
    core::Result<void> notify_one() noexcept;

    /**
     * @brief Wakes all waiting threads or processes.
     */
    core::Result<void> notify_all() noexcept;

    /**
     * @brief Blocks until signaled on the condition variable.
     * Caller MUST hold the mutex prior to calling wait().
     *
     * @param mutex The ProcessSharedMutex held by the caller.
     * @return Result<LockAcquireStatus> containing Acquired or OwnerDead.
     *         The mutex is guaranteed to be held upon return of Ok.
     */
    core::Result<LockAcquireStatus> wait(ProcessSharedMutex& mutex);

    /**
     * @brief Blocks until signaled or timeout expires.
     * Uses CLOCK_MONOTONIC for the absolute deadline.
     *
     * @param mutex The ProcessSharedMutex held by the caller.
     * @param timeout Duration to wait.
     * @return Result<LockAcquireStatus> containing Acquired, TimedOut, or OwnerDead.
     *         The mutex is guaranteed to be held upon return of Ok.
     */
    core::Result<LockAcquireStatus> wait_for(
        ProcessSharedMutex& mutex,
        std::chrono::milliseconds timeout);

    /**
     * @brief Predicate wait loop protecting against spurious wakeups.
     * If wait() encounters OwnerDead, it returns immediately so the caller can recover state.
     */
    template <typename Predicate>
    core::Result<LockAcquireStatus> wait(ProcessSharedMutex& mutex, Predicate pred) {
        while (!pred()) {
            auto res = wait(mutex);
            if (!res.is_ok()) {
                return res;
            }
            if (res.value() == LockAcquireStatus::OwnerDead) {
                return res; // Must return immediately to allow caller state recovery
            }
        }
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
    }

    /**
     * @brief Timed predicate wait loop with bounded deadline.
     */
    template <typename Predicate>
    core::Result<LockAcquireStatus> wait_for(
        ProcessSharedMutex& mutex,
        std::chrono::milliseconds timeout,
        Predicate pred) {
        if (timeout.count() < 0) {
            return core::Result<LockAcquireStatus>::err(
                core::ErrorCode::InvalidArgument,
                "Timeout cannot be negative");
        }
        auto start = std::chrono::steady_clock::now();
        while (!pred()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed >= timeout) {
                return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::TimedOut);
            }
            auto remaining = timeout - elapsed;
            auto res = wait_for(mutex, remaining);
            if (!res.is_ok()) {
                return res;
            }
            if (res.value() == LockAcquireStatus::OwnerDead) {
                return res;
            }
            if (res.value() == LockAcquireStatus::TimedOut) {
                if (pred()) {
                    return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
                }
                return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::TimedOut);
            }
        }
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
    }

private:
    pthread_cond_t* cond_{nullptr};
};

} // namespace kernox::system

namespace kernox::sys {
    using LockAcquireStatus = kernox::system::LockAcquireStatus;
    using ProcessSharedMutex = kernox::system::ProcessSharedMutex;
    using ProcessSharedLockGuard = kernox::system::ProcessSharedLockGuard;
    using SemaphoreType = kernox::system::SemaphoreType;
    using PosixSemaphore = kernox::system::PosixSemaphore;
    using ProcessSharedConditionVariable = kernox::system::ProcessSharedConditionVariable;
}
