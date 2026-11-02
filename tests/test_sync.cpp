#include "kernox/core/result.hpp"
#include "kernox/modules/system/ring_buffer.hpp"
#include "kernox/modules/system/shared_memory.hpp"
#include "kernox/modules/system/sync.hpp"
#include "test_harness.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace kernox;
using namespace kernox::system;

namespace {

/**
 * @brief Bounded waitpid helper to guarantee automated tests never hang indefinitely.
 *        If the child does not exit within the timeout, escalates to SIGKILL and reaps.
 */
bool bounded_waitpid(
    pid_t pid,
    int* status_out,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    auto start = std::chrono::steady_clock::now();
    int status = 0;
    while (std::chrono::steady_clock::now() - start < timeout) {
        pid_t wr = 0;
        do {
            wr = ::wait4(pid, &status, WNOHANG, nullptr);
        } while (wr < 0 && errno == EINTR);

        if (wr > 0) {
            if (status_out != nullptr) {
                *status_out = status;
            }
            return true;
        }
        if (wr < 0 && errno == ECHILD) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Timeout: forcibly kill child to prevent zombie
    ::kill(pid, SIGKILL);
    do {
        ::waitpid(pid, &status, 0);
    } while (errno == EINTR);
    return false;
}

struct PipeHandshake {
    int fds[2]{-1, -1};

    PipeHandshake() {
        if (::pipe(fds) != 0) {
            std::cerr << "pipe() failed in test\n";
        }
    }

    ~PipeHandshake() {
        if (fds[0] >= 0) ::close(fds[0]);
        if (fds[1] >= 0) ::close(fds[1]);
    }

    void notify() {
        char b = 1;
        ssize_t rc = ::write(fds[1], &b, 1);
        (void)rc;
    }

    void wait() {
        char b = 0;
        ssize_t rc = ::read(fds[0], &b, 1);
        (void)rc;
    }

    void close_read() {
        if (fds[0] >= 0) { ::close(fds[0]); fds[0] = -1; }
    }

    void close_write() {
        if (fds[1] >= 0) { ::close(fds[1]); fds[1] = -1; }
    }
};

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  KERNOX: POSIX Process Synchronization Test Suite       \n";
    std::cout << "=========================================================\n\n";

    // ------------------------------------------------------------------------
    // Test 1: Process-Shared Mutex Cross-Process Exclusion
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 1: Process-Shared Mutex Exclusion (2 Processes, 20,000 increments)...\n";
        const std::string shm_name = "/kernox_test_mutex_excl_" + std::to_string(::getpid());

        struct SharedData {
            pthread_mutex_t raw_mutex;
            uint32_t counter;
        };

        auto shm_res = SharedMemory::create(shm_name, sizeof(SharedData));
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto& shm = shm_res.value();

        auto* shared = shm.as<SharedData>();
        shared->counter = 0;

        auto init_res = ProcessSharedMutex::init_in_place(&shared->raw_mutex);
        KERNOX_ASSERT_TRUE(init_res.is_ok());

        constexpr int INCREMENTS_PER_CHILD = 10000;

        // Fork child 1
        pid_t p1 = ::fork();
        KERNOX_ASSERT_TRUE(p1 >= 0);
        if (p1 == 0) {
            auto client_shm = SharedMemory::open_existing(shm_name, sizeof(SharedData));
            if (client_shm.is_err()) {
                ::_exit(10);
            }
            auto* cshared = client_shm.value().as<SharedData>();
            ProcessSharedMutex mutex(&cshared->raw_mutex);

            for (int i = 0; i < INCREMENTS_PER_CHILD; ++i) {
                auto lock_res = mutex.lock();
                if (lock_res.is_err() || lock_res.value() != LockAcquireStatus::Acquired) {
                    ::_exit(11);
                }
                cshared->counter++;
                auto unl_res = mutex.unlock();
                if (unl_res.is_err()) {
                    ::_exit(12);
                }
            }
            ::_exit(0);
        }

        // Fork child 2
        pid_t p2 = ::fork();
        KERNOX_ASSERT_TRUE(p2 >= 0);
        if (p2 == 0) {
            auto client_shm = SharedMemory::open_existing(shm_name, sizeof(SharedData));
            if (client_shm.is_err()) {
                ::_exit(20);
            }
            auto* cshared = client_shm.value().as<SharedData>();
            ProcessSharedMutex mutex(&cshared->raw_mutex);

            for (int i = 0; i < INCREMENTS_PER_CHILD; ++i) {
                auto lock_res = mutex.lock();
                if (lock_res.is_err() || lock_res.value() != LockAcquireStatus::Acquired) {
                    ::_exit(21);
                }
                cshared->counter++;
                auto unl_res = mutex.unlock();
                if (unl_res.is_err()) {
                    ::_exit(22);
                }
            }
            ::_exit(0);
        }

        int s1 = 0;
        int s2 = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(p1, &s1));
        KERNOX_ASSERT_TRUE(bounded_waitpid(p2, &s2));
        KERNOX_ASSERT_TRUE(WIFEXITED(s1) && WEXITSTATUS(s1) == 0);
        KERNOX_ASSERT_TRUE(WIFEXITED(s2) && WEXITSTATUS(s2) == 0);

        // Verification: without exclusion, race conditions cause lost updates
        KERNOX_ASSERT_EQ(shared->counter, static_cast<uint32_t>(INCREMENTS_PER_CHILD * 2));

        auto destroy_res = ProcessSharedMutex::destroy_in_place(&shared->raw_mutex);
        KERNOX_ASSERT_TRUE(destroy_res.is_ok());

        std::cout << "  [PASS] Test 1: Final counter exactly 20,000 without data races\n";
    }

    // ------------------------------------------------------------------------
    // Test 2: Robust Mutex Owner Death & Recovery Protocol
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 2: Robust Mutex Owner Death (SIGKILL) & make_consistent() Recovery...\n";
        const std::string shm_name = "/kernox_test_mutex_robust_" + std::to_string(::getpid());

        struct RobustData {
            pthread_mutex_t raw_mutex;
            sem_t sync_sem; // To notify parent that mutex is held
            uint32_t data_state;
        };

        auto shm_res = SharedMemory::create(shm_name, sizeof(RobustData));
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto& shm = shm_res.value();
        auto* shared = shm.as<RobustData>();
        shared->data_state = 100;

        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&shared->raw_mutex).is_ok());
        KERNOX_ASSERT_TRUE(PosixSemaphore::init_in_place(&shared->sync_sem, 0).is_ok());

        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto client_shm = SharedMemory::open_existing(shm_name, sizeof(RobustData));
            if (client_shm.is_err()) ::_exit(1);
            auto* cdata = client_shm.value().as<RobustData>();
            ProcessSharedMutex mutex(&cdata->raw_mutex);

            auto lk = mutex.lock();
            if (lk.is_err() || lk.value() != LockAcquireStatus::Acquired) ::_exit(2);

            // Mutex is acquired; modify data into tentative state
            cdata->data_state = 999;

            // Signal parent that we hold the lock
            ::sem_post(&cdata->sync_sem);

            // Sleep indefinitely until parent kills us
            while (true) {
                ::pause();
            }
            ::_exit(0);
        }

        // Parent: wait until child confirms it holds the mutex
        auto parent_sem = PosixSemaphore::attach_unnamed(&shared->sync_sem);
        KERNOX_ASSERT_TRUE(parent_sem.wait().is_ok());

        // Terminate child abruptly with SIGKILL while it holds the mutex
        ::kill(child, SIGKILL);
        int child_status = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &child_status));
        KERNOX_ASSERT_TRUE(WIFSIGNALED(child_status) && WTERMSIG(child_status) == SIGKILL);

        // Parent acquires mutex: MUST return LockAcquireStatus::OwnerDead
        ProcessSharedMutex parent_mutex(&shared->raw_mutex);
        auto acq_res = parent_mutex.lock();
        KERNOX_ASSERT_TRUE(acq_res.is_ok());
        KERNOX_ASSERT_EQ(acq_res.value(), LockAcquireStatus::OwnerDead);

        // Verify parent acquired the lock and can inspect the inconsistent state
        KERNOX_ASSERT_EQ(shared->data_state, 999);

        // Repair state to consistent value
        shared->data_state = 200;

        // Restore mutex consistency
        auto cons_res = parent_mutex.make_consistent();
        KERNOX_ASSERT_TRUE(cons_res.is_ok());

        // Unlock normally
        KERNOX_ASSERT_TRUE(parent_mutex.unlock().is_ok());

        // Subsequent lock must now return LockAcquireStatus::Acquired (clean and consistent)
        auto next_acq = parent_mutex.lock();
        KERNOX_ASSERT_TRUE(next_acq.is_ok());
        KERNOX_ASSERT_EQ(next_acq.value(), LockAcquireStatus::Acquired);
        KERNOX_ASSERT_TRUE(parent_mutex.unlock().is_ok());

        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&shared->raw_mutex).is_ok());
        KERNOX_ASSERT_TRUE(PosixSemaphore::destroy_in_place(&shared->sync_sem).is_ok());

        std::cout << "  [PASS] Test 2: Robust mutex correctly handled EOWNERDEAD and restored consistency\n";
    }

    // ------------------------------------------------------------------------
    // Test 3: Unrecoverable Mutex Protocol
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 3: Mutex Unrecoverable Protocol (ENOTRECOVERABLE)...\n";
        const std::string shm_name = "/kernox_test_mutex_unrec_" + std::to_string(::getpid());

        struct UnrecData {
            pthread_mutex_t raw_mutex;
            sem_t sync_sem;
        };

        auto shm_res = SharedMemory::create(shm_name, sizeof(UnrecData));
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto* shared = shm_res.value().as<UnrecData>();

        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&shared->raw_mutex).is_ok());
        KERNOX_ASSERT_TRUE(PosixSemaphore::init_in_place(&shared->sync_sem, 0).is_ok());

        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);
        if (child == 0) {
            auto client_shm = SharedMemory::open_existing(shm_name, sizeof(UnrecData));
            if (client_shm.is_err()) ::_exit(1);
            ProcessSharedMutex mutex(&client_shm.value().as<UnrecData>()->raw_mutex);
            if (mutex.lock().value_or(LockAcquireStatus::Busy) != LockAcquireStatus::Acquired) ::_exit(2);
            ::sem_post(&client_shm.value().as<UnrecData>()->sync_sem);
            while (true) ::pause();
            ::_exit(0);
        }

        auto parent_sem = PosixSemaphore::attach_unnamed(&shared->sync_sem);
        KERNOX_ASSERT_TRUE(parent_sem.wait().is_ok());

        ::kill(child, SIGKILL);
        int s = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &s));

        ProcessSharedMutex parent_mutex(&shared->raw_mutex);
        auto lk = parent_mutex.lock();
        KERNOX_ASSERT_TRUE(lk.is_ok());
        KERNOX_ASSERT_EQ(lk.value(), LockAcquireStatus::OwnerDead);

        // DO NOT call make_consistent()! Unlock directly to abandon recovery:
        KERNOX_ASSERT_TRUE(parent_mutex.unlock().is_ok());

        // Any future attempt to acquire the mutex MUST fail with ENOTRECOVERABLE (InvalidState)
        auto fail_lock = parent_mutex.lock();
        KERNOX_ASSERT_TRUE(fail_lock.is_err());
        KERNOX_ASSERT_EQ(fail_lock.error().code, core::ErrorCode::InvalidState);

        auto fail_try = parent_mutex.try_lock();
        KERNOX_ASSERT_TRUE(fail_try.is_err());
        KERNOX_ASSERT_EQ(fail_try.error().code, core::ErrorCode::InvalidState);

        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&shared->raw_mutex).is_ok());
        KERNOX_ASSERT_TRUE(PosixSemaphore::destroy_in_place(&shared->sync_sem).is_ok());

        std::cout << "  [PASS] Test 3: Unrecovered mutex transitioned permanently to ENOTRECOVERABLE\n";
    }

    // ------------------------------------------------------------------------
    // Test 4: Named Semaphore Lifecycle, Exclusivity, and Ownership
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 4: Named Semaphore Lifecycle and Ownership Unlinking...\n";
        const std::string sem_name = "/kernox_test_named_sem_" + std::to_string(::getpid());

        // Exclusive creation with initial value 2
        auto sem1_res = PosixSemaphore::create_named(sem_name, 2);
        KERNOX_ASSERT_TRUE(sem1_res.is_ok());
        auto& sem1 = sem1_res.value();

        KERNOX_ASSERT_TRUE(sem1.is_valid());
        KERNOX_ASSERT_TRUE(sem1.is_owner());
        KERNOX_ASSERT_EQ(sem1.type(), SemaphoreType::Named);
        KERNOX_ASSERT_EQ(sem1.name(), sem_name);

        // Duplicate creation must fail with InvalidState (EEXIST)
        auto dup_res = PosixSemaphore::create_named(sem_name, 2);
        KERNOX_ASSERT_TRUE(dup_res.is_err());
        KERNOX_ASSERT_EQ(dup_res.error().code, core::ErrorCode::InvalidState);

        // Non-owning opening
        auto sem2_res = PosixSemaphore::open_named(sem_name);
        KERNOX_ASSERT_TRUE(sem2_res.is_ok());
        auto& sem2 = sem2_res.value();
        KERNOX_ASSERT_TRUE(sem2.is_valid());
        KERNOX_ASSERT_FALSE(sem2.is_owner());

        // Value check: initially 2
        auto val = sem2.get_value();
        KERNOX_ASSERT_TRUE(val.is_ok());
        KERNOX_ASSERT_EQ(val.value(), 2);

        // First wait succeeds immediately
        KERNOX_ASSERT_TRUE(sem2.wait().is_ok());
        KERNOX_ASSERT_EQ(sem2.get_value().value_or(-1), 1);

        // Second wait via try_wait succeeds
        auto try1 = sem2.try_wait();
        KERNOX_ASSERT_TRUE(try1.is_ok());
        KERNOX_ASSERT_TRUE(try1.value());
        KERNOX_ASSERT_EQ(sem2.get_value().value_or(-1), 0);

        // Third try_wait must return false (would block, EAGAIN)
        auto try2 = sem2.try_wait();
        KERNOX_ASSERT_TRUE(try2.is_ok());
        KERNOX_ASSERT_FALSE(try2.value());

        // Timed wait with 20ms timeout expires without blocking forever
        auto timed_res = sem2.timed_wait(std::chrono::milliseconds(20));
        KERNOX_ASSERT_TRUE(timed_res.is_ok());
        KERNOX_ASSERT_FALSE(timed_res.value());

        // Post from sem1 wakes/increments counter
        KERNOX_ASSERT_TRUE(sem1.post().is_ok());
        KERNOX_ASSERT_EQ(sem2.get_value().value_or(-1), 1);

        // sem2 consumes the posted token
        KERNOX_ASSERT_TRUE(sem2.wait().is_ok());
        KERNOX_ASSERT_EQ(sem2.get_value().value_or(-1), 0);

        // Destructor of sem2 (non-owner) must NOT unlink
        // Then explicit unlink on sem1 cleans up the kernel object
        KERNOX_ASSERT_TRUE(sem1.unlink().is_ok());
        KERNOX_ASSERT_FALSE(sem1.is_owner());

        // Second unlink is safe and idempotent
        KERNOX_ASSERT_TRUE(sem1.unlink().is_ok());

        std::cout << "  [PASS] Test 4: Named semaphore creation, wait, post, try_wait, and unlinking verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 5: Unnamed Process-Shared Semaphore Across Child and Parent
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 5: Unnamed Process-Shared Semaphore in Shared Memory...\n";
        const std::string shm_name = "/kernox_test_unnamed_sem_" + std::to_string(::getpid());

        struct UnnamedSemLayout {
            sem_t raw_sem;
            uint32_t message_flag;
        };

        auto shm_res = SharedMemory::create(shm_name, sizeof(UnnamedSemLayout));
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto* shared = shm_res.value().as<UnnamedSemLayout>();
        shared->message_flag = 0;

        KERNOX_ASSERT_TRUE(PosixSemaphore::init_in_place(&shared->raw_sem, 0).is_ok());

        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto client_shm = SharedMemory::open_existing(shm_name, sizeof(UnnamedSemLayout));
            if (client_shm.is_err()) ::_exit(1);

            auto* cdata = client_shm.value().as<UnnamedSemLayout>();
            auto sem = PosixSemaphore::attach_unnamed(&cdata->raw_sem);

            // Child waits for parent's signal
            auto wait_res = sem.wait();
            if (wait_res.is_err()) ::_exit(2);

            // Verify parent updated the flag before posting
            if (cdata->message_flag != 0xBEEFCAFE) ::_exit(3);

            ::_exit(0);
        }

        // Parent briefly pauses then writes payload and posts semaphore
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        shared->message_flag = 0xBEEFCAFE;

        auto parent_sem = PosixSemaphore::attach_unnamed(&shared->raw_sem);
        KERNOX_ASSERT_TRUE(parent_sem.post().is_ok());

        int child_status = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &child_status));
        KERNOX_ASSERT_TRUE(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);

        KERNOX_ASSERT_TRUE(PosixSemaphore::destroy_in_place(&shared->raw_sem).is_ok());

        std::cout << "  [PASS] Test 5: Cross-process unnamed semaphore synchronization verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 6: Scoped Lock Guard & Timed Lock Semantics
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 6: ProcessSharedLockGuard RAII & Timed Lock...\n";
        pthread_mutex_t raw_mutex;
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&raw_mutex).is_ok());
        ProcessSharedMutex mutex(&raw_mutex);

        // Test Scoped Guard
        {
            auto acq = mutex.lock();
            KERNOX_ASSERT_TRUE(acq.is_ok());
            ProcessSharedLockGuard guard(mutex, acq.value());
            KERNOX_ASSERT_TRUE(guard.is_locked());
            KERNOX_ASSERT_FALSE(guard.is_owner_dead());

            // try_lock on same thread holding the mutex yields deadlock detection or busy
            auto try_deadlk = mutex.try_lock();
            KERNOX_ASSERT_TRUE(
                (try_deadlk.is_err() && try_deadlk.error().code == core::ErrorCode::InvalidState) ||
                (try_deadlk.is_ok() && try_deadlk.value() == LockAcquireStatus::Busy));

            // try_lock from a separate process must return LockAcquireStatus::Busy
            pid_t try_child = ::fork();
            KERNOX_ASSERT_TRUE(try_child >= 0);
            if (try_child == 0) {
                auto child_try = mutex.try_lock();
                if (child_try.is_ok() && child_try.value() == LockAcquireStatus::Busy) {
                    ::_exit(0);
                }
                ::_exit(1);
            }
            int busy_status = 0;
            KERNOX_ASSERT_TRUE(bounded_waitpid(try_child, &busy_status));
            KERNOX_ASSERT_TRUE(WIFEXITED(busy_status) && WEXITSTATUS(busy_status) == 0);
        }
        // Guard went out of scope: mutex must now be unlocked!

        auto acq2 = mutex.try_lock();
        KERNOX_ASSERT_TRUE(acq2.is_ok() && acq2.value() == LockAcquireStatus::Acquired);
        KERNOX_ASSERT_TRUE(mutex.unlock().is_ok());

        // Timed Lock Expiration Check
        {
            auto lk = mutex.lock();
            KERNOX_ASSERT_TRUE(lk.is_ok() && lk.value() == LockAcquireStatus::Acquired);

            // In a child thread or separate process, timed_lock will time out
            pid_t child = ::fork();
            KERNOX_ASSERT_TRUE(child >= 0);
            if (child == 0) {
                auto tlk = mutex.timed_lock(std::chrono::milliseconds(30));
                if (tlk.is_ok() && tlk.value() == LockAcquireStatus::TimedOut) {
                    ::_exit(0);
                }
                ::_exit(1);
            }

            int s = 0;
            KERNOX_ASSERT_TRUE(bounded_waitpid(child, &s));
            KERNOX_ASSERT_TRUE(WIFEXITED(s) && WEXITSTATUS(s) == 0);

            KERNOX_ASSERT_TRUE(mutex.unlock().is_ok());
        }

        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&raw_mutex).is_ok());

        std::cout << "  [PASS] Test 6: RAII Lock Guard and Timed Lock expiration verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 7: Error Edge Cases and Input Validation
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 7: Error Edge Cases & Defensive Validation...\n";

        // Null pointer initialization
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(nullptr).is_err());
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(nullptr).is_err());
        KERNOX_ASSERT_TRUE(PosixSemaphore::init_in_place(nullptr, 1).is_err());
        KERNOX_ASSERT_TRUE(PosixSemaphore::destroy_in_place(nullptr).is_err());

        // Invalid controller operations
        ProcessSharedMutex empty_mutex;
        KERNOX_ASSERT_FALSE(empty_mutex.is_valid());
        KERNOX_ASSERT_TRUE(empty_mutex.lock().is_err());
        KERNOX_ASSERT_TRUE(empty_mutex.try_lock().is_err());
        KERNOX_ASSERT_TRUE(empty_mutex.timed_lock(std::chrono::milliseconds(10)).is_err());
        KERNOX_ASSERT_TRUE(empty_mutex.unlock().is_err());
        KERNOX_ASSERT_TRUE(empty_mutex.make_consistent().is_err());

        PosixSemaphore empty_sem;
        KERNOX_ASSERT_FALSE(empty_sem.is_valid());
        KERNOX_ASSERT_TRUE(empty_sem.wait().is_err());
        KERNOX_ASSERT_TRUE(empty_sem.try_wait().is_err());
        KERNOX_ASSERT_TRUE(empty_sem.timed_wait(std::chrono::milliseconds(10)).is_err());
        KERNOX_ASSERT_TRUE(empty_sem.post().is_err());
        KERNOX_ASSERT_TRUE(empty_sem.get_value().is_err());

        // Negative timeouts
        pthread_mutex_t m;
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&m).is_ok());
        ProcessSharedMutex valid_mutex(&m);
        KERNOX_ASSERT_TRUE(valid_mutex.timed_lock(std::chrono::milliseconds(-5)).is_err());
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&m).is_ok());

        // Invalid Semaphore Names
        KERNOX_ASSERT_TRUE(PosixSemaphore::validate_name("").is_err());
        KERNOX_ASSERT_TRUE(PosixSemaphore::validate_name("no_slash").is_err());
        KERNOX_ASSERT_TRUE(PosixSemaphore::validate_name("/").is_err());
        KERNOX_ASSERT_TRUE(PosixSemaphore::validate_name("/nested/slash").is_err());
        std::string long_name = "/" + std::string(300, 'a');
        KERNOX_ASSERT_TRUE(PosixSemaphore::validate_name(long_name).is_err());

        // Move Invalidation verification
        pthread_mutex_t raw_m;
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&raw_m).is_ok());
        ProcessSharedMutex m1(&raw_m);
        KERNOX_ASSERT_TRUE(m1.is_valid());
        ProcessSharedMutex m2 = std::move(m1);
        KERNOX_ASSERT_FALSE(m1.is_valid());
        KERNOX_ASSERT_TRUE(m2.is_valid());
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&raw_m).is_ok());

        std::cout << "  [PASS] Test 7: Input boundary validation and defensive guards verified\n";
    }

    // ------------------------------------------------------------------------
    // Test 8: Process-Shared Condition Variable Cross-Process Signaling
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 8: Condition Variable Cross-Process Signaling...\n";
        struct CondSyncBlock {
            pthread_mutex_t mutex;
            pthread_cond_t cond;
            uint32_t predicate_flag;
            uint32_t payload;
        };

        const std::string shm_name = "/kernox_test_cond_sig_" + std::to_string(::getpid());
        auto shm_res = SharedMemory::create(shm_name, sizeof(CondSyncBlock));
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        SharedMemory shm = std::move(shm_res.value());

        auto* block = shm.as<CondSyncBlock>();
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&block->mutex).is_ok());
        KERNOX_ASSERT_TRUE(ProcessSharedConditionVariable::init_in_place(&block->cond).is_ok());
        block->predicate_flag = 0;
        block->payload = 0;

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            ProcessSharedMutex child_mutex(&block->mutex);
            ProcessSharedConditionVariable child_cond(&block->cond);

            auto lk = child_mutex.lock();
            if (!lk.is_ok()) ::_exit(1);

            child_ready.notify();

            auto wait_res = child_cond.wait(child_mutex, [&]() {
                return block->predicate_flag != 0;
            });

            if (!wait_res.is_ok() || wait_res.value() != LockAcquireStatus::Acquired) {
                ::_exit(2);
            }
            if (block->payload != 0xCAFEBABE) {
                ::_exit(3);
            }

            block->payload += 42;
            child_mutex.unlock();
            ::_exit(0);
        }

        child_ready.wait();

        ProcessSharedMutex parent_mutex(&block->mutex);
        ProcessSharedConditionVariable parent_cond(&block->cond);

        auto lk = parent_mutex.lock();
        KERNOX_ASSERT_TRUE(lk.is_ok());
        block->payload = 0xCAFEBABE;
        block->predicate_flag = 1;
        KERNOX_ASSERT_TRUE(parent_cond.notify_one().is_ok());
        KERNOX_ASSERT_TRUE(parent_mutex.unlock().is_ok());

        int status = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &status));
        KERNOX_ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

        KERNOX_ASSERT_EQ(block->payload, 0xCAFEBABE + 42);

        KERNOX_ASSERT_TRUE(ProcessSharedConditionVariable::destroy_in_place(&block->cond).is_ok());
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&block->mutex).is_ok());

        std::cout << "  [PASS] Test 8: Cross-process condition variable signaled and verified successfully\n";
    }

    // ------------------------------------------------------------------------
    // Test 9: Condition Variable Monotonic Timeout
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 9: Condition Variable Monotonic Timeout...\n";
        pthread_mutex_t raw_m;
        pthread_cond_t raw_c;
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&raw_m).is_ok());
        KERNOX_ASSERT_TRUE(ProcessSharedConditionVariable::init_in_place(&raw_c).is_ok());

        ProcessSharedMutex mutex(&raw_m);
        ProcessSharedConditionVariable cond(&raw_c);

        auto lk = mutex.lock();
        KERNOX_ASSERT_TRUE(lk.is_ok());

        auto start = std::chrono::steady_clock::now();
        auto wait_res = cond.wait_for(mutex, std::chrono::milliseconds(50), [&]() {
            return false;
        });
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        KERNOX_ASSERT_TRUE(wait_res.is_ok());
        KERNOX_ASSERT_EQ(wait_res.value(), LockAcquireStatus::TimedOut);
        KERNOX_ASSERT_TRUE(elapsed.count() >= 40);

        // Mutex is still acquired on timeout; unlock must succeed
        KERNOX_ASSERT_TRUE(mutex.unlock().is_ok());

        KERNOX_ASSERT_TRUE(ProcessSharedConditionVariable::destroy_in_place(&raw_c).is_ok());
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&raw_m).is_ok());

        std::cout << "  [PASS] Test 9: Monotonic condition variable timeout verified (" << elapsed.count() << " ms)\n";
    }

    // ------------------------------------------------------------------------
    // Test 10: Single Producer and Single Consumer (SPSC) Ring Buffer
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 10: SPSC Bounded Shared-Memory Ring Buffer...\n";
        const std::string rb_name = "/kernox_test_rb_spsc_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 8;
        config.slot_size = 128;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        constexpr int NUM_MSGS = 50;

        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto child_rb = SharedRingBuffer::open_existing(rb_name);
            if (!child_rb.is_ok()) ::_exit(1);

            for (int i = 0; i < NUM_MSGS; ++i) {
                std::string msg = "msg_" + std::to_string(i);
                auto res = child_rb.value().push(msg);
                if (!res.is_ok()) ::_exit(2);
            }
            ::_exit(0);
        }

        for (int i = 0; i < NUM_MSGS; ++i) {
            auto pop_res = rb.pop(std::chrono::milliseconds(3000));
            KERNOX_ASSERT_TRUE(pop_res.is_ok());
            std::string expected = "msg_" + std::to_string(i);
            KERNOX_ASSERT_EQ(pop_res.value(), expected);
        }

        int status = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &status));
        KERNOX_ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

        std::cout << "  [PASS] Test 10: 50 sequential messages produced and consumed in FIFO order\n";
    }

    // ------------------------------------------------------------------------
    // Test 11: Multiple Producers and Consumers (MPMC: 3 Producers, 2 Consumers)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 11: MPMC Ring Buffer (3 Producers, 2 Consumers, 1,500 messages)...\n";
        const std::string rb_name = "/kernox_test_rb_mpmc_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 16;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        struct MPMCAudit {
            pthread_mutex_t mutex;
            uint32_t counts[3][500];
            uint32_t total_popped;
        };

        const std::string audit_name = "/kernox_test_mpmc_audit_" + std::to_string(::getpid());
        auto audit_shm = SharedMemory::create(audit_name, sizeof(MPMCAudit));
        KERNOX_ASSERT_TRUE(audit_shm.is_ok());
        auto* audit = audit_shm.value().as<MPMCAudit>();
        std::memset(audit, 0, sizeof(MPMCAudit));
        KERNOX_ASSERT_TRUE(ProcessSharedMutex::init_in_place(&audit->mutex).is_ok());

        pid_t producers[3];
        pid_t consumers[2];

        // Launch 2 consumers
        for (int c = 0; c < 2; ++c) {
            consumers[c] = ::fork();
            KERNOX_ASSERT_TRUE(consumers[c] >= 0);
            if (consumers[c] == 0) {
                auto c_rb = SharedRingBuffer::open_existing(rb_name);
                auto c_audit_shm = SharedMemory::open_existing(audit_name);
                if (!c_rb.is_ok() || !c_audit_shm.is_ok()) ::_exit(1);

                auto* a = c_audit_shm.value().as<MPMCAudit>();
                while (true) {
                    auto res = c_rb.value().pop(std::chrono::milliseconds(2000));
                    if (!res.is_ok()) {
                        break;
                    }
                    std::string s = res.value();
                    size_t colon = s.find(':');
                    if (colon != std::string::npos && s[0] == 'P') {
                        int p = std::stoi(s.substr(1, colon - 1));
                        int seq = std::stoi(s.substr(colon + 1));
                        ::pthread_mutex_lock(&a->mutex);
                        a->counts[p][seq]++;
                        a->total_popped++;
                        uint32_t tot = a->total_popped;
                        ::pthread_mutex_unlock(&a->mutex);
                        if (tot >= 1500) {
                            break;
                        }
                    }
                }
                ::_exit(0);
            }
        }

        // Launch 3 producers (500 messages each)
        for (int p = 0; p < 3; ++p) {
            producers[p] = ::fork();
            KERNOX_ASSERT_TRUE(producers[p] >= 0);
            if (producers[p] == 0) {
                auto p_rb = SharedRingBuffer::open_existing(rb_name);
                if (!p_rb.is_ok()) ::_exit(1);

                for (int seq = 0; seq < 500; ++seq) {
                    std::string msg = "P" + std::to_string(p) + ":" + std::to_string(seq);
                    auto pres = p_rb.value().push(msg, std::chrono::milliseconds(3000));
                    if (!pres.is_ok()) ::_exit(2);
                }
                ::_exit(0);
            }
        }

        // Reap producers
        for (int p = 0; p < 3; ++p) {
            int st = 0;
            KERNOX_ASSERT_TRUE(bounded_waitpid(producers[p], &st, std::chrono::milliseconds(10000)));
            KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
        }

        // Reap consumers
        for (int c = 0; c < 2; ++c) {
            int st = 0;
            KERNOX_ASSERT_TRUE(bounded_waitpid(consumers[c], &st, std::chrono::milliseconds(10000)));
            KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
        }

        KERNOX_ASSERT_EQ(audit->total_popped, 1500u);

        // Verify zero duplicates, zero missing messages
        for (int p = 0; p < 3; ++p) {
            for (int seq = 0; seq < 500; ++seq) {
                KERNOX_ASSERT_EQ(audit->counts[p][seq], 1u);
            }
        }

        KERNOX_ASSERT_TRUE(ProcessSharedMutex::destroy_in_place(&audit->mutex).is_ok());
        std::cout << "  [PASS] Test 11: Exactly 1,500 messages verified with 0 duplicates and 0 missing\n";
    }

    // ------------------------------------------------------------------------
    // Test 12: Full-Buffer Blocking
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 12: Full-Buffer Blocking...\n";
        const std::string rb_name = "/kernox_test_rb_full_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 2;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        // Fill buffer to capacity
        KERNOX_ASSERT_TRUE(rb.push("item_1").is_ok());
        KERNOX_ASSERT_TRUE(rb.push("item_2").is_ok());
        KERNOX_ASSERT_TRUE(rb.is_full());

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto c_rb = SharedRingBuffer::open_existing(rb_name);
            if (!c_rb.is_ok()) ::_exit(1);

            child_ready.notify();
            auto pres = c_rb.value().push("overflow_item");
            if (!pres.is_ok()) ::_exit(2);
            ::_exit(0);
        }

        child_ready.wait();

        // Check child is blocked
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        int st = 0;
        pid_t wr = ::waitpid(child, &st, WNOHANG);
        KERNOX_ASSERT_EQ(wr, 0); // Still blocked!

        // Consume one item: creates free slot
        auto p1 = rb.pop();
        KERNOX_ASSERT_TRUE(p1.is_ok() && p1.value() == "item_1");

        // Child must now unblock and complete
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        auto p2 = rb.pop();
        KERNOX_ASSERT_TRUE(p2.is_ok() && p2.value() == "item_2");

        auto p3 = rb.pop();
        KERNOX_ASSERT_TRUE(p3.is_ok() && p3.value() == "overflow_item");

        std::cout << "  [PASS] Test 12: Producer blocked when full and resumed upon slot consumption\n";
    }

    // ------------------------------------------------------------------------
    // Test 13: Empty-Buffer Blocking
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 13: Empty-Buffer Blocking...\n";
        const std::string rb_name = "/kernox_test_rb_empty_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 2;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());
        KERNOX_ASSERT_TRUE(rb.is_empty());

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto c_rb = SharedRingBuffer::open_existing(rb_name);
            if (!c_rb.is_ok()) ::_exit(1);

            child_ready.notify();
            auto pres = c_rb.value().pop();
            if (pres.is_ok() && pres.value() == "wakeup_item") {
                ::_exit(0);
            }
            ::_exit(2);
        }

        child_ready.wait();

        // Verify child is blocked on empty buffer
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        int st = 0;
        pid_t wr = ::waitpid(child, &st, WNOHANG);
        KERNOX_ASSERT_EQ(wr, 0);

        // Push wakeup message
        KERNOX_ASSERT_TRUE(rb.push("wakeup_item").is_ok());

        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        std::cout << "  [PASS] Test 13: Consumer blocked on empty buffer and unblocked on push\n";
    }

    // ------------------------------------------------------------------------
    // Test 14: Graceful Shutdown
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 14: Graceful Shutdown...\n";
        const std::string rb_name = "/kernox_test_rb_shut_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 4;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto c_rb = SharedRingBuffer::open_existing(rb_name);
            if (!c_rb.is_ok()) ::_exit(1);

            child_ready.notify();
            auto pres = c_rb.value().pop();
            if (pres.is_err() && pres.error().code == core::ErrorCode::InvalidState) {
                ::_exit(0); // Successfully woke up with shutdown error
            }
            ::_exit(2);
        }

        child_ready.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        KERNOX_ASSERT_TRUE(rb.shutdown().is_ok());
        KERNOX_ASSERT_TRUE(rb.is_shutdown());

        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);

        // Future pushes must immediately fail
        auto push_res = rb.push("after_shutdown");
        KERNOX_ASSERT_TRUE(push_res.is_err());
        KERNOX_ASSERT_EQ(push_res.error().code, core::ErrorCode::InvalidState);

        std::cout << "  [PASS] Test 14: Graceful shutdown successfully woke blocked consumers\n";
    }

    // ------------------------------------------------------------------------
    // Test 15: Owner-Dead Recovery
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 15: Owner-Dead Recovery...\n";
        const std::string rb_name = "/kernox_test_rb_dead_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 4;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto shm_res = SharedMemory::open_existing(rb_name);
            if (!shm_res.is_ok()) ::_exit(1);
            auto* hdr = shm_res.value().as<RingBufferHeader>();

            ::pthread_mutex_lock(&hdr->mutex);
            size_t tail_idx = hdr->tail % hdr->capacity;
            auto* slot = reinterpret_cast<SlotHeader*>(
                reinterpret_cast<uint8_t*>(hdr) + hdr->slots_offset + (tail_idx * hdr->slot_stride));
            slot->state = static_cast<uint32_t>(SlotState::Writing);

            child_ready.notify();
            // Abruptly kill process while holding mutex!
            ::kill(::getpid(), SIGKILL);
            ::_exit(0);
        }

        child_ready.wait();
        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);

        // Next push must detect OwnerDead, heal the uncommitted slot, and complete successfully
        auto push_res = rb.push("healed_message");
        KERNOX_ASSERT_TRUE(push_res.is_ok());

        auto pop_res = rb.pop();
        KERNOX_ASSERT_TRUE(pop_res.is_ok() && pop_res.value() == "healed_message");

        std::cout << "  [PASS] Test 15: Owner-dead recovery successfully healed in-flight slot\n";
    }

    // ------------------------------------------------------------------------
    // Test 16: Invalid Shared-Memory Layout Rejection
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 16: Invalid Layout & Defensive Rejection...\n";
        const std::string bad_name = "/kernox_test_rb_bad_" + std::to_string(::getpid());
        size_t sz = sizeof(RingBufferHeader) + 512;
        auto shm_res = SharedMemory::create(bad_name, sz);
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto* hdr = shm_res.value().as<RingBufferHeader>();
        std::memset(hdr, 0, sz);

        // 1. Invalid Magic
        hdr->magic = 0xDEADBEEF;
        hdr->version = SharedRingBuffer::VERSION;
        hdr->capacity = 4;
        hdr->slot_size = 64;
        KERNOX_ASSERT_TRUE(SharedRingBuffer::open_existing(bad_name).is_err());

        // 2. Unsupported Version
        hdr->magic = SharedRingBuffer::MAGIC;
        hdr->version = 999;
        KERNOX_ASSERT_TRUE(SharedRingBuffer::open_existing(bad_name).is_err());

        // 3. Zero Capacity
        hdr->version = SharedRingBuffer::VERSION;
        hdr->capacity = 0;
        KERNOX_ASSERT_TRUE(SharedRingBuffer::open_existing(bad_name).is_err());

        // 4. Corrupted State
        hdr->capacity = 4;
        hdr->state = static_cast<uint32_t>(RingBufferState::Corrupted);
        KERNOX_ASSERT_TRUE(SharedRingBuffer::open_existing(bad_name).is_err());

        // 5. Configuration boundary check
        RingBufferConfig bad_cfg;
        bad_cfg.capacity = 0;
        KERNOX_ASSERT_TRUE(SharedRingBuffer::create("/bad_cfg", bad_cfg).is_err());

        std::cout << "  [PASS] Test 16: Malformed headers and invalid layouts safely rejected\n";
    }

    // ------------------------------------------------------------------------
    // Test 17: Fault-Injection: Crash After tail++ Before Committed (Case 2 Recovery)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 17: Crash After tail++ Before Committed (Case 2)...\n";
        const std::string rb_name = "/kernox_test_rb_fpush_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 4;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto shm_res = SharedMemory::open_existing(rb_name);
            if (!shm_res.is_ok()) ::_exit(1);
            auto* hdr = shm_res.value().as<RingBufferHeader>();

            ::pthread_mutex_lock(&hdr->mutex);
            size_t tail_idx = hdr->tail % hdr->capacity;
            auto* slot = reinterpret_cast<SlotHeader*>(
                reinterpret_cast<uint8_t*>(hdr) + hdr->slots_offset + (tail_idx * hdr->slot_stride));

            // Producer simulates writing payload and advancing tail/count, then dying BEFORE setting Committed
            slot->state = static_cast<uint32_t>(SlotState::Writing);
            const char* garbage = "partial_uncommitted_data";
            std::memcpy(reinterpret_cast<uint8_t*>(slot) + sizeof(SlotHeader), garbage, std::strlen(garbage));
            slot->payload_size = static_cast<uint32_t>(std::strlen(garbage));
            slot->sequence = ++hdr->next_push_seq;
            hdr->tail++;
            hdr->count++;

            child_ready.notify();
            // Crash immediately while holding mutex!
            ::kill(::getpid(), SIGKILL);
            ::_exit(0);
        }

        child_ready.wait();
        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);

        // Next push must detect OwnerDead, roll back uncommitted tail/count, and commit cleanly
        auto push_res = rb.push("valid_push_after_recovery");
        KERNOX_ASSERT_TRUE(push_res.is_ok());

        KERNOX_ASSERT_EQ(rb.size(), 1UL);

        auto pop_res = rb.pop();
        KERNOX_ASSERT_TRUE(pop_res.is_ok() && pop_res.value() == "valid_push_after_recovery");
        KERNOX_ASSERT_EQ(rb.size(), 0UL);

        std::cout << "  [PASS] Test 17: Recovery rolled back uncommitted push tail/count and restored consistency\n";
    }

    // ------------------------------------------------------------------------
    // Test 18: Fault-Injection: Crash After head++ Before Empty (Case 4 Recovery)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 18: Crash After head++ Before Empty (Case 4)...\n";
        const std::string rb_name = "/kernox_test_rb_fpop_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 4;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        // Preload one message
        KERNOX_ASSERT_TRUE(rb.push("message_to_pop").is_ok());
        KERNOX_ASSERT_EQ(rb.size(), 1UL);

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto shm_res = SharedMemory::open_existing(rb_name);
            if (!shm_res.is_ok()) ::_exit(1);
            auto* hdr = shm_res.value().as<RingBufferHeader>();

            ::pthread_mutex_lock(&hdr->mutex);
            size_t head_idx = hdr->head % hdr->capacity;
            auto* slot = reinterpret_cast<SlotHeader*>(
                reinterpret_cast<uint8_t*>(hdr) + hdr->slots_offset + (head_idx * hdr->slot_stride));

            // Consumer simulates reading and advancing head/count, then dying BEFORE setting Empty
            slot->state = static_cast<uint32_t>(SlotState::Reading);
            hdr->head++;
            hdr->count--;

            child_ready.notify();
            // Crash immediately while holding mutex!
            ::kill(::getpid(), SIGKILL);
            ::_exit(0);
        }

        child_ready.wait();
        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);

        // Next push must detect OwnerDead, complete freeing the slot to Empty, and succeed
        auto push_res = rb.push("new_message_after_pop_crash");
        KERNOX_ASSERT_TRUE(push_res.is_ok());
        KERNOX_ASSERT_EQ(rb.size(), 1UL);

        auto pop_res = rb.pop();
        KERNOX_ASSERT_TRUE(pop_res.is_ok() && pop_res.value() == "new_message_after_pop_crash");

        std::cout << "  [PASS] Test 18: Recovery completed slot clearing after post-head++ consumer crash\n";
    }

    // ------------------------------------------------------------------------
    // Test 19: Fault-Injection: Crash During Reading Before head++ (At-Least-Once Delivery)
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 19: Crash During Reading Before head++ (At-Least-Once)...\n";
        const std::string rb_name = "/kernox_test_rb_alo_" + std::to_string(::getpid());
        RingBufferConfig config;
        config.capacity = 4;
        config.slot_size = 64;

        auto rb_res = SharedRingBuffer::create(rb_name, config);
        KERNOX_ASSERT_TRUE(rb_res.is_ok());
        SharedRingBuffer rb = std::move(rb_res.value());

        // Preload message
        KERNOX_ASSERT_TRUE(rb.push("at_least_once_payload").is_ok());
        KERNOX_ASSERT_EQ(rb.size(), 1UL);

        PipeHandshake child_ready;
        pid_t child = ::fork();
        KERNOX_ASSERT_TRUE(child >= 0);

        if (child == 0) {
            auto shm_res = SharedMemory::open_existing(rb_name);
            if (!shm_res.is_ok()) ::_exit(1);
            auto* hdr = shm_res.value().as<RingBufferHeader>();

            ::pthread_mutex_lock(&hdr->mutex);
            size_t head_idx = hdr->head % hdr->capacity;
            auto* slot = reinterpret_cast<SlotHeader*>(
                reinterpret_cast<uint8_t*>(hdr) + hdr->slots_offset + (head_idx * hdr->slot_stride));

            // Mark Reading, but DO NOT advance head or count (simulating crash during copy)
            slot->state = static_cast<uint32_t>(SlotState::Reading);

            child_ready.notify();
            ::kill(::getpid(), SIGKILL);
            ::_exit(0);
        }

        child_ready.wait();
        int st = 0;
        KERNOX_ASSERT_TRUE(bounded_waitpid(child, &st));
        KERNOX_ASSERT_TRUE(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);

        // Next pop must detect OwnerDead, roll slot back from Reading to Committed, and pop it successfully!
        auto pop_res = rb.pop();
        KERNOX_ASSERT_TRUE(pop_res.is_ok());
        KERNOX_ASSERT_EQ(pop_res.value(), "at_least_once_payload");
        KERNOX_ASSERT_EQ(rb.size(), 0UL);

        std::cout << "  [PASS] Test 19: Message preserved via rollback to Committed (at-least-once delivery)\n";
    }

    // ------------------------------------------------------------------------
    // Test 20: Initialization Handshake & Uninitialized State Rejection
    // ------------------------------------------------------------------------
    {
        std::cout << ">> Running Test 20: Initialization Handshake & Uninitialized Rejection...\n";
        const std::string uninit_name = "/kernox_test_rb_uninit_" + std::to_string(::getpid());
        size_t sz = sizeof(RingBufferHeader) + 512;
        auto shm_res = SharedMemory::create(uninit_name, sz);
        KERNOX_ASSERT_TRUE(shm_res.is_ok());
        auto* hdr = shm_res.value().as<RingBufferHeader>();
        std::memset(hdr, 0, sz);

        // Magic and version set, but state is explicitly Uninitialized
        hdr->magic = SharedRingBuffer::MAGIC;
        hdr->version = SharedRingBuffer::VERSION;
        hdr->capacity = 4;
        hdr->slot_size = 64;
        hdr->state = static_cast<uint32_t>(RingBufferState::Uninitialized);

        // Must be rejected with InvalidState because initialization is incomplete
        auto open_res = SharedRingBuffer::open_existing(uninit_name);
        KERNOX_ASSERT_TRUE(open_res.is_err());
        KERNOX_ASSERT_EQ(open_res.error().code, core::ErrorCode::InvalidState);

        std::cout << "  [PASS] Test 20: Partially initialized shared memory segments safely rejected\n";
    }

    std::cout << "\n=========================================================\n";
    std::cout << "  [SUCCESS] All 20 POSIX Synchronization Tests Passed!   \n";
    std::cout << "=========================================================\n";
    return 0;
}
