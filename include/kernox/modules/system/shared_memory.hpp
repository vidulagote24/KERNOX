#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"
#include "kernox/core/unique_fd.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace kernox::system {

/**
 * @brief Open mode configuration for shared memory objects.
 */
enum class ShmAccessMode {
    ReadWrite,
    ReadOnly
};

/**
 * @brief RAII abstraction for POSIX Shared Memory (shm_open, ftruncate, mmap, munmap, shm_unlink).
 *
 * Provides safe, robust process-shared memory mapping with strict ownership semantics:
 *  - Objects created via SharedMemory::create() are owned by the creating instance and
 *    will be automatically unlinked via shm_unlink() upon destruction.
 *  - Objects opened via SharedMemory::open_existing() are NOT owned and will NOT be unlinked
 *    upon destruction, allowing other processes to continue accessing the shared object.
 *  - File descriptors are managed via RAII (core::UniqueFd) with O_CLOEXEC.
 *  - Mappings are safely unmapped via munmap() on destruction or move.
 *
 * CONCURRENCY & SYNCHRONIZATION NOTICE:
 *  SharedMemory guarantees memory mapping and OS object lifecycle safety. However, concurrent
 *  read and write access to the underlying memory region across separate processes requires
 *  external synchronization (such as POSIX semaphores, process-shared mutexes, or atomic operations).
 *  Unsynchronized concurrent writes will produce data races and memory corruption.
 */
class SharedMemory {
public:
    /**
     * @brief Maximum length for a POSIX shared memory object name (including leading slash).
     */
    static constexpr size_t MAX_NAME_LENGTH = 255;

    /**
     * @brief Validates a POSIX shared memory name according to standard rules:
     *  - Must not be empty.
     *  - Must start with a single slash ('/').
     *  - Must not contain any subsequent slashes ('/').
     *  - Length must be at least 2 and at most MAX_NAME_LENGTH.
     */
    static core::Result<void> validate_name(std::string_view name);

    /**
     * @brief Creates a new POSIX shared memory object, sizes it with ftruncate, and maps it with mmap.
     *
     * Fails with ErrorCode::InvalidState if a shared memory object with the same name already exists (O_EXCL).
     * The resulting instance takes ownership of the object and will unlink it upon destruction.
     *
     * @param name Valid shared memory name starting with '/'.
     * @param size Size in bytes of the shared memory region (must be > 0).
     * @param mode POSIX file permissions (default: 0600, read/write for owner only).
     * @return SharedMemory instance on success, or Error on failure.
     */
    static core::Result<SharedMemory> create(
        const std::string& name,
        size_t size,
        mode_t mode = 0600);

    /**
     * @brief Opens an existing POSIX shared memory object and maps it into the process address space.
     *
     * If expected_size is 0, the size is determined automatically via fstat().
     * The instance does NOT take ownership of the shared memory object and will NOT unlink it on destruction.
     *
     * @param name Valid shared memory name starting with '/'.
     * @param expected_size Expected size in bytes (0 to auto-detect from existing object).
     * @param access ReadWrite or ReadOnly access.
     * @return SharedMemory instance on success, or Error on failure.
     */
    static core::Result<SharedMemory> open_existing(
        const std::string& name,
        size_t expected_size = 0,
        ShmAccessMode access = ShmAccessMode::ReadWrite);

    /**
     * @brief Destructor. Unmaps the memory region, closes the file descriptor, and unlinks the object
     *        if and only if this instance owns the object (is_owner() == true).
     */
    ~SharedMemory() noexcept;

    // Move-only semantics
    SharedMemory(SharedMemory&& other) noexcept;
    SharedMemory& operator=(SharedMemory&& other) noexcept;

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    /**
     * @brief Default constructor creating an unmapped/invalid instance.
     */
    SharedMemory() = default;

    /**
     * @brief Resizes the shared memory object using ftruncate and updates the memory mapping.
     *
     * Only valid on owned, read-write shared memory instances.
     *
     * @param new_size New size in bytes (must be > 0).
     * @return Result<void> indicating success or error.
     */
    core::Result<void> resize(size_t new_size);

    /**
     * @brief Explicitly unmaps the mapped memory region.
     *
     * Safe to call multiple times (idempotent).
     */
    core::Result<void> unmap() noexcept;

    /**
     * @brief Manually unlinks the shared memory object from the operating system.
     *
     * Revokes ownership so the destructor will not attempt a redundant unlink.
     * Safe to call multiple times (idempotent).
     */
    core::Result<void> unlink() noexcept;

    // Accessors
    [[nodiscard]] void* data() noexcept { return mapped_addr_; }
    [[nodiscard]] const void* data() const noexcept { return mapped_addr_; }

    template <typename T>
    [[nodiscard]] T* as() noexcept {
        return static_cast<T*>(mapped_addr_);
    }

    template <typename T>
    [[nodiscard]] const T* as() const noexcept {
        return static_cast<const T*>(mapped_addr_);
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] bool is_owner() const noexcept { return is_owner_; }
    [[nodiscard]] bool is_mapped() const noexcept { return mapped_addr_ != nullptr; }
    [[nodiscard]] bool is_valid() const noexcept { return fd_.is_valid() && is_mapped(); }
    [[nodiscard]] ShmAccessMode access_mode() const noexcept { return access_; }
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

private:
    SharedMemory(
        std::string name,
        core::UniqueFd fd,
        void* mapped_addr,
        size_t size,
        bool is_owner,
        ShmAccessMode access) noexcept
        : name_(std::move(name)),
          fd_(std::move(fd)),
          mapped_addr_(mapped_addr),
          size_(size),
          is_owner_(is_owner),
          access_(access) {}

    std::string name_{};
    core::UniqueFd fd_{};
    void* mapped_addr_{nullptr};
    size_t size_{0};
    bool is_owner_{false};
    ShmAccessMode access_{ShmAccessMode::ReadWrite};
};

} // namespace kernox::system

namespace kernox::sys {
    using SharedMemory = kernox::system::SharedMemory;
    using ShmAccessMode = kernox::system::ShmAccessMode;
}
