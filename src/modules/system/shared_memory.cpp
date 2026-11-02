#include "kernox/modules/system/shared_memory.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kernox::system {

core::Result<void> SharedMemory::validate_name(std::string_view name) {
    if (name.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory name cannot be empty");
    }

    if (name.front() != '/') {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory name must start with a leading slash ('/'): " + std::string(name));
    }

    if (name.size() < 2) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory name cannot be just a single slash ('/')");
    }

    if (name.size() > MAX_NAME_LENGTH) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory name exceeds maximum length (" + std::to_string(MAX_NAME_LENGTH) +
            " bytes): " + std::string(name));
    }

    // POSIX shm_open requirement: at most one slash at the start
    if (name.substr(1).find('/') != std::string_view::npos) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory name must not contain interior or trailing slashes: " + std::string(name));
    }

    if (name.find('\0') != std::string_view::npos) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory name must not contain null characters");
    }

    return core::Result<void>::ok();
}

core::Result<SharedMemory> SharedMemory::create(
    const std::string& name,
    size_t size,
    mode_t mode) {
    auto name_val = validate_name(name);
    if (name_val.is_err()) {
        return core::Result<SharedMemory>::err(name_val.error());
    }

    if (size == 0) {
        return core::Result<SharedMemory>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory size must be strictly positive (> 0)");
    }

    if (size > static_cast<size_t>(std::numeric_limits<off_t>::max())) {
        return core::Result<SharedMemory>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory size exceeds operating system file offset limit");
    }

    int fd = -1;
    do {
        fd = ::shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        if (errno == EEXIST) {
            return core::Result<SharedMemory>::err(
                core::ErrorCode::InvalidState,
                "Shared memory object already exists: " + name);
        }
        if (errno == EACCES) {
            return core::Result<SharedMemory>::err(
                core::ErrorCode::PermissionDenied,
                "Permission denied creating shared memory: " + name);
        }
        return core::Result<SharedMemory>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "shm_open() failed creating: " + name,
                errno));
    }

    core::UniqueFd ufd(fd);

    // Size the shared memory object
    int tr_res = 0;
    do {
        tr_res = ::ftruncate(ufd.get(), static_cast<off_t>(size));
    } while (tr_res < 0 && errno == EINTR);

    if (tr_res != 0) {
        int err = errno;
        ::shm_unlink(name.c_str());
        return core::Result<SharedMemory>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "ftruncate() failed sizing: " + name,
                err));
    }

    // Map into memory
    void* addr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, ufd.get(), 0);
    if (addr == MAP_FAILED) {
        int err = errno;
        ::shm_unlink(name.c_str());
        return core::Result<SharedMemory>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "mmap() failed mapping: " + name,
                err));
    }

    return core::Result<SharedMemory>::ok(SharedMemory(
        name,
        std::move(ufd),
        addr,
        size,
        true /* is_owner */,
        ShmAccessMode::ReadWrite));
}

core::Result<SharedMemory> SharedMemory::open_existing(
    const std::string& name,
    size_t expected_size,
    ShmAccessMode access) {
    auto name_val = validate_name(name);
    if (name_val.is_err()) {
        return core::Result<SharedMemory>::err(name_val.error());
    }

    int flags = (access == ShmAccessMode::ReadOnly ? O_RDONLY : O_RDWR) | O_CLOEXEC;
    int fd = -1;
    do {
        fd = ::shm_open(name.c_str(), flags, 0);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        if (errno == ENOENT) {
            return core::Result<SharedMemory>::err(
                core::ErrorCode::NotFound,
                "Shared memory object does not exist: " + name);
        }
        if (errno == EACCES) {
            return core::Result<SharedMemory>::err(
                core::ErrorCode::PermissionDenied,
                "Permission denied opening shared memory: " + name);
        }
        return core::Result<SharedMemory>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "shm_open() failed opening: " + name,
                errno));
    }

    core::UniqueFd ufd(fd);

    struct stat st{};
    if (::fstat(ufd.get(), &st) != 0) {
        return core::Result<SharedMemory>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "fstat() failed on shared memory: " + name,
                errno));
    }

    size_t actual_size = static_cast<size_t>(st.st_size);
    if (actual_size == 0) {
        return core::Result<SharedMemory>::err(
            core::ErrorCode::InvalidState,
            "Shared memory object has zero size: " + name);
    }

    size_t map_size = (expected_size > 0) ? expected_size : actual_size;
    if (map_size > actual_size) {
        return core::Result<SharedMemory>::err(
            core::ErrorCode::InvalidArgument,
            "Requested mapping size (" + std::to_string(map_size) +
            ") exceeds shared memory object size (" + std::to_string(actual_size) + "): " + name);
    }

    int prot = PROT_READ | (access == ShmAccessMode::ReadWrite ? PROT_WRITE : 0);
    void* addr = ::mmap(nullptr, map_size, prot, MAP_SHARED, ufd.get(), 0);
    if (addr == MAP_FAILED) {
        return core::Result<SharedMemory>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "mmap() failed mapping existing: " + name,
                errno));
    }

    return core::Result<SharedMemory>::ok(SharedMemory(
        name,
        std::move(ufd),
        addr,
        map_size,
        false /* is_owner */,
        access));
}

SharedMemory::~SharedMemory() noexcept {
    static_cast<void>(unmap());
    fd_.reset();
    if (is_owner_ && !name_.empty()) {
        ::shm_unlink(name_.c_str());
        is_owner_ = false;
    }
}

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : name_(std::move(other.name_)),
      fd_(std::move(other.fd_)),
      mapped_addr_(other.mapped_addr_),
      size_(other.size_),
      is_owner_(other.is_owner_),
      access_(other.access_) {
    other.mapped_addr_ = nullptr;
    other.size_ = 0;
    other.is_owner_ = false;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept {
    if (this != &other) {
        static_cast<void>(unmap());
        fd_.reset();
        if (is_owner_ && !name_.empty()) {
            ::shm_unlink(name_.c_str());
            is_owner_ = false;
        }

        name_ = std::move(other.name_);
        fd_ = std::move(other.fd_);
        mapped_addr_ = other.mapped_addr_;
        size_ = other.size_;
        is_owner_ = other.is_owner_;
        access_ = other.access_;

        other.mapped_addr_ = nullptr;
        other.size_ = 0;
        other.is_owner_ = false;
    }
    return *this;
}

core::Result<void> SharedMemory::resize(size_t new_size) {
    if (!is_owner_) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot resize shared memory object: instance is not the owner (" + name_ + ")");
    }

    if (access_ != ShmAccessMode::ReadWrite) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot resize read-only shared memory mapping (" + name_ + ")");
    }

    if (new_size == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "New shared memory size must be strictly positive (> 0)");
    }

    if (new_size > static_cast<size_t>(std::numeric_limits<off_t>::max())) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "New shared memory size exceeds operating system file offset limit");
    }

    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot resize invalid or unmapped shared memory instance");
    }

    if (new_size == size_) {
        return core::Result<void>::ok();
    }

    int tr_res = 0;
    do {
        tr_res = ::ftruncate(fd_.get(), static_cast<off_t>(new_size));
    } while (tr_res < 0 && errno == EINTR);

    if (tr_res != 0) {
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "ftruncate() failed during resize: " + name_,
                errno));
    }

    // Unmap existing mapping before creating new mapping
    if (mapped_addr_ != nullptr) {
        ::munmap(mapped_addr_, size_);
        mapped_addr_ = nullptr;
    }

    void* new_addr = ::mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_.get(), 0);
    if (new_addr == MAP_FAILED) {
        size_ = 0;
        return core::Result<void>::err(
            core::Error::from_errno(
                core::ErrorCode::SystemError,
                "mmap() failed after resize: " + name_,
                errno));
    }

    mapped_addr_ = new_addr;
    size_ = new_size;

    return core::Result<void>::ok();
}

core::Result<void> SharedMemory::unmap() noexcept {
    if (mapped_addr_ != nullptr && size_ > 0) {
        if (::munmap(mapped_addr_, size_) != 0) {
            int err = errno;
            mapped_addr_ = nullptr;
            size_ = 0;
            return core::Result<void>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    "munmap() failed on: " + name_,
                    err));
        }
        mapped_addr_ = nullptr;
        size_ = 0;
    }
    return core::Result<void>::ok();
}

core::Result<void> SharedMemory::unlink() noexcept {
    if (is_owner_ && !name_.empty()) {
        if (::shm_unlink(name_.c_str()) != 0 && errno != ENOENT) {
            return core::Result<void>::err(
                core::Error::from_errno(
                    core::ErrorCode::SystemError,
                    "shm_unlink() failed for: " + name_,
                    errno));
        }
        is_owner_ = false;
    }
    return core::Result<void>::ok();
}

} // namespace kernox::system
