#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "kernox/modules/system/sync.hpp"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kernox::system {

// ----------------------------------------------------------------------------
// ProcessSharedMutex Implementation
// ----------------------------------------------------------------------------

core::Result<void> ProcessSharedMutex::init_in_place(pthread_mutex_t* raw_mutex) {
    if (raw_mutex == nullptr) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot initialize mutex: raw_mutex pointer cannot be null");
    }

    pthread_mutexattr_t attr;
    int rc = ::pthread_mutexattr_init(&attr);
    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_mutexattr_init() failed",
                rc));
    }

    rc = ::pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
        ::pthread_mutexattr_destroy(&attr);
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED) failed",
                rc));
    }

    rc = ::pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (rc != 0) {
        ::pthread_mutexattr_destroy(&attr);
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_mutexattr_setrobust(PTHREAD_MUTEX_ROBUST) failed",
                rc));
    }

    rc = ::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    if (rc != 0) {
        ::pthread_mutexattr_destroy(&attr);
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_mutexattr_settype(PTHREAD_MUTEX_ERRORCHECK) failed",
                rc));
    }

    rc = ::pthread_mutex_init(raw_mutex, &attr);
    ::pthread_mutexattr_destroy(&attr);

    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_mutex_init() failed in shared memory",
                rc));
    }

    return core::Result<void>::ok();
}

core::Result<void> ProcessSharedMutex::destroy_in_place(pthread_mutex_t* raw_mutex) noexcept {
    if (raw_mutex == nullptr) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot destroy mutex: raw_mutex pointer cannot be null");
    }

    int rc = ::pthread_mutex_destroy(raw_mutex);
    if (rc != 0) {
        if (rc == EBUSY) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidState,
                "Cannot destroy mutex: mutex is currently locked or referenced (EBUSY)",
                rc);
        }
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_mutex_destroy() failed",
                rc));
    }

    return core::Result<void>::ok();
}

ProcessSharedMutex::ProcessSharedMutex(pthread_mutex_t* raw_mutex) noexcept
    : mutex_(raw_mutex) {}

ProcessSharedMutex::ProcessSharedMutex(ProcessSharedMutex&& other) noexcept
    : mutex_(other.mutex_) {
    other.mutex_ = nullptr;
}

ProcessSharedMutex& ProcessSharedMutex::operator=(ProcessSharedMutex&& other) noexcept {
    if (this != &other) {
        mutex_ = other.mutex_;
        other.mutex_ = nullptr;
    }
    return *this;
}

core::Result<LockAcquireStatus> ProcessSharedMutex::lock() {
    if (!is_valid()) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot lock: ProcessSharedMutex controller is unattached or invalid");
    }

    int rc = ::pthread_mutex_lock(mutex_);
    if (rc == 0) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
    }
    if (rc == EOWNERDEAD) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::OwnerDead);
    }
    if (rc == ENOTRECOVERABLE) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Mutex is permanently unrecoverable (ENOTRECOVERABLE): previous owner died and consistency was not restored",
            rc);
    }
    if (rc == EDEADLK) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Deadlock detected (EDEADLK): calling thread already holds this mutex",
            rc);
    }

    return core::Result<LockAcquireStatus>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "pthread_mutex_lock() failed",
            rc));
}

core::Result<LockAcquireStatus> ProcessSharedMutex::try_lock() {
    if (!is_valid()) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot try_lock: ProcessSharedMutex controller is unattached or invalid");
    }

    int rc = ::pthread_mutex_trylock(mutex_);
    if (rc == 0) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
    }
    if (rc == EOWNERDEAD) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::OwnerDead);
    }
    if (rc == EBUSY) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Busy);
    }
    if (rc == EDEADLK) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Deadlock detected (EDEADLK): calling thread already holds this mutex",
            rc);
    }
    if (rc == ENOTRECOVERABLE) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Mutex is permanently unrecoverable (ENOTRECOVERABLE)",
            rc);
    }

    return core::Result<LockAcquireStatus>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "pthread_mutex_trylock() failed",
            rc));
}

core::Result<LockAcquireStatus> ProcessSharedMutex::timed_lock(std::chrono::milliseconds timeout) {
    if (!is_valid()) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot timed_lock: ProcessSharedMutex controller is unattached or invalid");
    }

    if (timeout.count() < 0) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidArgument,
            "Timeout duration cannot be negative");
    }

    struct timespec ts{};
#if defined(_GNU_SOURCE) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30))
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return core::Result<LockAcquireStatus>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "clock_gettime(CLOCK_MONOTONIC) failed",
                errno));
    }
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - secs);
    ts.tv_sec += static_cast<time_t>(secs.count());
    ts.tv_nsec += static_cast<long>(nsecs.count());
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }
    int rc = ::pthread_mutex_clocklock(mutex_, CLOCK_MONOTONIC, &ts);
#else
    if (::clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return core::Result<LockAcquireStatus>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "clock_gettime(CLOCK_REALTIME) failed",
                errno));
    }
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - secs);
    ts.tv_sec += static_cast<time_t>(secs.count());
    ts.tv_nsec += static_cast<long>(nsecs.count());
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }
    int rc = ::pthread_mutex_timedlock(mutex_, &ts);
#endif

    if (rc == 0) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
    }
    if (rc == EOWNERDEAD) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::OwnerDead);
    }
    if (rc == ETIMEDOUT) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::TimedOut);
    }
    if (rc == EDEADLK) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Deadlock detected (EDEADLK): calling thread already holds this mutex",
            rc);
    }
    if (rc == ENOTRECOVERABLE) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Mutex is permanently unrecoverable (ENOTRECOVERABLE)",
            rc);
    }

    return core::Result<LockAcquireStatus>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "pthread_mutex_timedlock() failed",
            rc));
}

core::Result<void> ProcessSharedMutex::unlock() noexcept {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot unlock: ProcessSharedMutex controller is unattached or invalid");
    }

    int rc = ::pthread_mutex_unlock(mutex_);
    if (rc == 0) {
        return core::Result<void>::ok();
    }
    if (rc == EPERM) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot unlock mutex: calling thread does not hold the mutex (EPERM)",
            rc);
    }

    return core::Result<void>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "pthread_mutex_unlock() failed",
            rc));
}

core::Result<void> ProcessSharedMutex::make_consistent() noexcept {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot make_consistent: ProcessSharedMutex controller is unattached or invalid");
    }

    int rc = ::pthread_mutex_consistent(mutex_);
    if (rc == 0) {
        return core::Result<void>::ok();
    }
    if (rc == EINVAL) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Mutex was not in an inconsistent state or does not support robustness (EINVAL)",
            rc);
    }

    return core::Result<void>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "pthread_mutex_consistent() failed",
            rc));
}

// ----------------------------------------------------------------------------
// ProcessSharedLockGuard Implementation
// ----------------------------------------------------------------------------

ProcessSharedLockGuard::ProcessSharedLockGuard(ProcessSharedMutex& mutex, LockAcquireStatus status) noexcept
    : mutex_(&mutex), status_(status), locked_(status == LockAcquireStatus::Acquired || status == LockAcquireStatus::OwnerDead) {}

ProcessSharedLockGuard::~ProcessSharedLockGuard() noexcept {
    if (locked_ && mutex_ != nullptr && mutex_->is_valid()) {
        static_cast<void>(mutex_->unlock());
        locked_ = false;
    }
}

ProcessSharedLockGuard::ProcessSharedLockGuard(ProcessSharedLockGuard&& other) noexcept
    : mutex_(other.mutex_), status_(other.status_), locked_(other.locked_) {
    other.locked_ = false;
    other.mutex_ = nullptr;
}

ProcessSharedLockGuard& ProcessSharedLockGuard::operator=(ProcessSharedLockGuard&& other) noexcept {
    if (this != &other) {
        if (locked_ && mutex_ != nullptr && mutex_->is_valid()) {
            static_cast<void>(mutex_->unlock());
        }
        mutex_ = other.mutex_;
        status_ = other.status_;
        locked_ = other.locked_;
        other.locked_ = false;
        other.mutex_ = nullptr;
    }
    return *this;
}

core::Result<void> ProcessSharedLockGuard::make_consistent() noexcept {
    if (!locked_ || mutex_ == nullptr || !mutex_->is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "LockGuard is not locked or references an invalid mutex");
    }

    auto res = mutex_->make_consistent();
    if (res.is_ok()) {
        status_ = LockAcquireStatus::Acquired;
    }
    return res;
}

core::Result<void> ProcessSharedLockGuard::unlock() noexcept {
    if (!locked_ || mutex_ == nullptr || !mutex_->is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "LockGuard is not currently locked");
    }

    auto res = mutex_->unlock();
    if (res.is_ok()) {
        locked_ = false;
    }
    return res;
}

// ----------------------------------------------------------------------------
// PosixSemaphore Implementation
// ----------------------------------------------------------------------------

core::Result<void> PosixSemaphore::validate_name(std::string_view name) {
    if (name.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Semaphore name cannot be empty");
    }

    if (name.front() != '/') {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Semaphore name must start with a leading slash ('/'): " + std::string(name));
    }

    if (name.size() < 2) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Semaphore name cannot be just a single slash ('/')");
    }

    if (name.size() > MAX_NAME_LENGTH) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Semaphore name exceeds maximum length (" + std::to_string(MAX_NAME_LENGTH) +
            " bytes): " + std::string(name));
    }

    if (name.substr(1).find('/') != std::string_view::npos) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Semaphore name must not contain interior or trailing slashes: " + std::string(name));
    }

    if (name.find('\0') != std::string_view::npos) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Semaphore name must not contain null characters");
    }

    return core::Result<void>::ok();
}

core::Result<PosixSemaphore> PosixSemaphore::create_named(
    const std::string& name,
    unsigned int initial_value,
    mode_t mode) {
    auto name_val = validate_name(name);
    if (name_val.is_err()) {
        return core::Result<PosixSemaphore>::err(name_val.error());
    }

    sem_t* sem = SEM_FAILED;
    do {
        sem = ::sem_open(name.c_str(), O_CREAT | O_EXCL, mode, initial_value);
    } while (sem == SEM_FAILED && errno == EINTR);

    if (sem == SEM_FAILED) {
        int err = errno;
        if (err == EEXIST) {
            return core::Result<PosixSemaphore>::err(
                core::ErrorCode::InvalidState,
                "Named semaphore already exists: " + name);
        }
        if (err == EACCES) {
            return core::Result<PosixSemaphore>::err(
                core::ErrorCode::PermissionDenied,
                "Permission denied creating named semaphore: " + name);
        }
        if (err == EINVAL) {
            return core::Result<PosixSemaphore>::err(
                core::ErrorCode::InvalidArgument,
                "Invalid initial value (exceeds SEM_VALUE_MAX): " + std::to_string(initial_value));
        }
        return core::Result<PosixSemaphore>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "sem_open() failed creating named semaphore: " + name,
                err));
    }

    return core::Result<PosixSemaphore>::ok(
        PosixSemaphore(sem, SemaphoreType::Named, name, true /* is_owner */));
}

core::Result<PosixSemaphore> PosixSemaphore::open_named(const std::string& name) {
    auto name_val = validate_name(name);
    if (name_val.is_err()) {
        return core::Result<PosixSemaphore>::err(name_val.error());
    }

    sem_t* sem = SEM_FAILED;
    do {
        sem = ::sem_open(name.c_str(), 0);
    } while (sem == SEM_FAILED && errno == EINTR);

    if (sem == SEM_FAILED) {
        int err = errno;
        if (err == ENOENT) {
            return core::Result<PosixSemaphore>::err(
                core::ErrorCode::NotFound,
                "Named semaphore does not exist: " + name);
        }
        if (err == EACCES) {
            return core::Result<PosixSemaphore>::err(
                core::ErrorCode::PermissionDenied,
                "Permission denied opening named semaphore: " + name);
        }
        return core::Result<PosixSemaphore>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "sem_open() failed opening named semaphore: " + name,
                err));
    }

    return core::Result<PosixSemaphore>::ok(
        PosixSemaphore(sem, SemaphoreType::Named, name, false /* is_owner */));
}

core::Result<void> PosixSemaphore::init_in_place(sem_t* raw_sem, unsigned int initial_value) {
    if (raw_sem == nullptr) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot initialize semaphore: raw_sem pointer cannot be null");
    }

    int rc = ::sem_init(raw_sem, 1 /* pshared */, initial_value);
    if (rc != 0) {
        int err = errno;
        if (err == EINVAL) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Initial value exceeds SEM_VALUE_MAX: " + std::to_string(initial_value));
        }
        if (err == ENOSYS) {
            return core::Result<void>::err(
                core::ErrorCode::SystemError,
                "Process-shared unnamed semaphores are not supported by operating system");
        }
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "sem_init() failed",
                err));
    }

    return core::Result<void>::ok();
}

core::Result<void> PosixSemaphore::destroy_in_place(sem_t* raw_sem) noexcept {
    if (raw_sem == nullptr) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot destroy semaphore: raw_sem pointer cannot be null");
    }

    int rc = ::sem_destroy(raw_sem);
    if (rc != 0) {
        int err = errno;
        if (err == EBUSY) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidState,
                "Cannot destroy semaphore: other processes or threads are currently blocked on it (EBUSY)",
                err);
        }
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "sem_destroy() failed",
                err));
    }

    return core::Result<void>::ok();
}

PosixSemaphore PosixSemaphore::attach_unnamed(sem_t* raw_sem) noexcept {
    return PosixSemaphore(raw_sem, SemaphoreType::Unnamed, "", false /* is_owner */);
}

PosixSemaphore::PosixSemaphore(sem_t* sem, SemaphoreType type, std::string name, bool is_owner) noexcept
    : sem_(sem), type_(type), name_(std::move(name)), is_owner_(is_owner) {}

PosixSemaphore::~PosixSemaphore() noexcept {
    if (is_valid()) {
        if (type_ == SemaphoreType::Named) {
            ::sem_close(sem_);
            if (is_owner_ && !name_.empty()) {
                ::sem_unlink(name_.c_str());
            }
        }
        sem_ = SEM_FAILED;
        is_owner_ = false;
    }
}

PosixSemaphore::PosixSemaphore(PosixSemaphore&& other) noexcept
    : sem_(other.sem_),
      type_(other.type_),
      name_(std::move(other.name_)),
      is_owner_(other.is_owner_) {
    other.sem_ = SEM_FAILED;
    other.type_ = SemaphoreType::Unnamed;
    other.name_.clear();
    other.is_owner_ = false;
}

PosixSemaphore& PosixSemaphore::operator=(PosixSemaphore&& other) noexcept {
    if (this != &other) {
        if (is_valid() && type_ == SemaphoreType::Named) {
            ::sem_close(sem_);
            if (is_owner_ && !name_.empty()) {
                ::sem_unlink(name_.c_str());
            }
        }

        sem_ = other.sem_;
        type_ = other.type_;
        name_ = std::move(other.name_);
        is_owner_ = other.is_owner_;

        other.sem_ = SEM_FAILED;
        other.type_ = SemaphoreType::Unnamed;
        other.name_.clear();
        other.is_owner_ = false;
    }
    return *this;
}

core::Result<void> PosixSemaphore::wait() {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot wait: PosixSemaphore is unattached or invalid");
    }

    int rc = 0;
    do {
        rc = ::sem_wait(sem_);
    } while (rc < 0 && errno == EINTR);

    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "sem_wait() failed",
                errno));
    }

    return core::Result<void>::ok();
}

core::Result<bool> PosixSemaphore::try_wait() {
    if (!is_valid()) {
        return core::Result<bool>::err(
            core::ErrorCode::InvalidState,
            "Cannot try_wait: PosixSemaphore is unattached or invalid");
    }

    int rc = 0;
    do {
        rc = ::sem_trywait(sem_);
    } while (rc < 0 && errno == EINTR);

    if (rc == 0) {
        return core::Result<bool>::ok(true);
    }

    if (errno == EAGAIN) {
        return core::Result<bool>::ok(false);
    }

    return core::Result<bool>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "sem_trywait() failed",
            errno));
}

core::Result<bool> PosixSemaphore::timed_wait(std::chrono::milliseconds timeout) {
    if (!is_valid()) {
        return core::Result<bool>::err(
            core::ErrorCode::InvalidState,
            "Cannot timed_wait: PosixSemaphore is unattached or invalid");
    }

    if (timeout.count() < 0) {
        return core::Result<bool>::err(
            core::ErrorCode::InvalidArgument,
            "Timeout duration cannot be negative");
    }

    struct timespec ts{};
#if defined(_GNU_SOURCE) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30))
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return core::Result<bool>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "clock_gettime(CLOCK_MONOTONIC) failed",
                errno));
    }
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - secs);
    ts.tv_sec += static_cast<time_t>(secs.count());
    ts.tv_nsec += static_cast<long>(nsecs.count());
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }
    int rc = 0;
    do {
        rc = ::sem_clockwait(sem_, CLOCK_MONOTONIC, &ts);
    } while (rc < 0 && errno == EINTR);
#else
    if (::clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return core::Result<bool>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "clock_gettime(CLOCK_REALTIME) failed",
                errno));
    }
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - secs);
    ts.tv_sec += static_cast<time_t>(secs.count());
    ts.tv_nsec += static_cast<long>(nsecs.count());
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }
    int rc = 0;
    do {
        rc = ::sem_timedwait(sem_, &ts);
    } while (rc < 0 && errno == EINTR);
#endif

    if (rc == 0) {
        return core::Result<bool>::ok(true);
    }

    if (errno == ETIMEDOUT) {
        return core::Result<bool>::ok(false);
    }

    return core::Result<bool>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "sem_timedwait() failed",
            errno));
}

core::Result<void> PosixSemaphore::post() noexcept {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot post: PosixSemaphore is unattached or invalid");
    }

    if (::sem_post(sem_) != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "sem_post() failed",
                errno));
    }

    return core::Result<void>::ok();
}

core::Result<int> PosixSemaphore::get_value() const noexcept {
    if (!is_valid()) {
        return core::Result<int>::err(
            core::ErrorCode::InvalidState,
            "Cannot get_value: PosixSemaphore is unattached or invalid");
    }

    int val = 0;
    if (::sem_getvalue(sem_, &val) != 0) {
        return core::Result<int>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "sem_getvalue() failed",
                errno));
    }

    return core::Result<int>::ok(val);
}

core::Result<void> PosixSemaphore::unlink() noexcept {
    if (type_ == SemaphoreType::Named && !name_.empty()) {
        int rc = ::sem_unlink(name_.c_str());
        is_owner_ = false; // Revoke deletion ownership so dtor will not attempt double-unlink
        if (rc != 0 && errno != ENOENT) {
            return core::Result<void>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    "sem_unlink() failed",
                    errno));
        }
    }
    return core::Result<void>::ok();
}

// ----------------------------------------------------------------------------
// ProcessSharedConditionVariable Implementation
// ----------------------------------------------------------------------------

core::Result<void> ProcessSharedConditionVariable::init_in_place(pthread_cond_t* raw_cond) {
    if (!raw_cond) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "raw_cond pointer cannot be null");
    }

    pthread_condattr_t attr;
    int rc = ::pthread_condattr_init(&attr);
    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_condattr_init() failed",
                rc));
    }

    rc = ::pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
        ::pthread_condattr_destroy(&attr);
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_condattr_setpshared(PTHREAD_PROCESS_SHARED) failed",
                rc));
    }

    rc = ::pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (rc != 0) {
        ::pthread_condattr_destroy(&attr);
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_condattr_setclock(CLOCK_MONOTONIC) failed",
                rc));
    }

    rc = ::pthread_cond_init(raw_cond, &attr);
    ::pthread_condattr_destroy(&attr);

    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_cond_init() failed",
                rc));
    }

    return core::Result<void>::ok();
}

core::Result<void> ProcessSharedConditionVariable::destroy_in_place(pthread_cond_t* raw_cond) noexcept {
    if (!raw_cond) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "raw_cond pointer cannot be null");
    }

    int rc = ::pthread_cond_destroy(raw_cond);
    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_cond_destroy() failed",
                rc));
    }

    return core::Result<void>::ok();
}

ProcessSharedConditionVariable::ProcessSharedConditionVariable(pthread_cond_t* raw_cond) noexcept
    : cond_(raw_cond) {}

ProcessSharedConditionVariable::ProcessSharedConditionVariable(ProcessSharedConditionVariable&& other) noexcept
    : cond_(other.cond_) {
    other.cond_ = nullptr;
}

ProcessSharedConditionVariable& ProcessSharedConditionVariable::operator=(
    ProcessSharedConditionVariable&& other) noexcept {
    if (this != &other) {
        cond_ = other.cond_;
        other.cond_ = nullptr;
    }
    return *this;
}

core::Result<void> ProcessSharedConditionVariable::notify_one() noexcept {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot notify_one: ProcessSharedConditionVariable is unattached or invalid");
    }

    int rc = ::pthread_cond_signal(cond_);
    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_cond_signal() failed",
                rc));
    }

    return core::Result<void>::ok();
}

core::Result<void> ProcessSharedConditionVariable::notify_all() noexcept {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot notify_all: ProcessSharedConditionVariable is unattached or invalid");
    }

    int rc = ::pthread_cond_broadcast(cond_);
    if (rc != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "pthread_cond_broadcast() failed",
                rc));
    }

    return core::Result<void>::ok();
}

core::Result<LockAcquireStatus> ProcessSharedConditionVariable::wait(ProcessSharedMutex& mutex) {
    if (!is_valid()) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot wait: ProcessSharedConditionVariable is unattached or invalid");
    }
    if (!mutex.is_valid()) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot wait: ProcessSharedMutex is unattached or invalid");
    }

    int rc = ::pthread_cond_wait(cond_, mutex.native_handle());
    if (rc == 0) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
    }
    if (rc == EOWNERDEAD) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::OwnerDead);
    }
    if (rc == ENOTRECOVERABLE) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Mutex is permanently unrecoverable (ENOTRECOVERABLE)",
            rc);
    }
    if (rc == EPERM) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot wait: calling thread does not hold the mutex (EPERM)",
            rc);
    }

    return core::Result<LockAcquireStatus>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "pthread_cond_wait() failed",
            rc));
}

core::Result<LockAcquireStatus> ProcessSharedConditionVariable::wait_for(
    ProcessSharedMutex& mutex,
    std::chrono::milliseconds timeout) {
    if (!is_valid()) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot wait_for: ProcessSharedConditionVariable is unattached or invalid");
    }
    if (!mutex.is_valid()) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot wait_for: ProcessSharedMutex is unattached or invalid");
    }
    if (timeout.count() < 0) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidArgument,
            "Timeout duration cannot be negative");
    }

    struct timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return core::Result<LockAcquireStatus>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "clock_gettime(CLOCK_MONOTONIC) failed",
                errno));
    }

    auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - secs);
    ts.tv_sec += static_cast<time_t>(secs.count());
    ts.tv_nsec += static_cast<long>(nsecs.count());
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }

    int rc = ::pthread_cond_timedwait(cond_, mutex.native_handle(), &ts);
    if (rc == 0) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::Acquired);
    }
    if (rc == ETIMEDOUT) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::TimedOut);
    }
    if (rc == EOWNERDEAD) {
        return core::Result<LockAcquireStatus>::ok(LockAcquireStatus::OwnerDead);
    }
    if (rc == ENOTRECOVERABLE) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Mutex is permanently unrecoverable (ENOTRECOVERABLE)",
            rc);
    }
    if (rc == EPERM) {
        return core::Result<LockAcquireStatus>::err(
            core::ErrorCode::InvalidState,
            "Cannot wait_for: calling thread does not hold the mutex (EPERM)",
            rc);
    }

    return core::Result<LockAcquireStatus>::err(
        core::Error::from_errno(
            core::ErrorCode::SystemError,
            "pthread_cond_timedwait() failed",
            rc));
}

} // namespace kernox::system
