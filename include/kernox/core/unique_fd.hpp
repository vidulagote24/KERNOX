#pragma once

#include "kernox/core/result.hpp"

#include <utility>

namespace kernox::core {

class UniqueFd {
public:
    // Default constructor creates an invalid descriptor (-1)
    constexpr UniqueFd() noexcept : fd_(-1) {}

    // Explicit constructor taking a raw file descriptor
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    // Destructor closes the descriptor if valid
    ~UniqueFd() noexcept;

    // Non-copyable
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    // Move constructible
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

    // Move assignable
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    // Get the underlying raw descriptor
    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    // Check if the descriptor is valid (0 is a valid descriptor for stdin)
    [[nodiscard]] bool is_valid() const noexcept {
        return fd_ >= 0;
    }

    // Explicit boolean conversion
    explicit operator bool() const noexcept {
        return is_valid();
    }

    // Release ownership of the descriptor without closing it
    [[nodiscard]] int release() noexcept {
        int temp = fd_;
        fd_ = -1;
        return temp;
    }

    // Close the current descriptor (if valid) and take ownership of a new one
    void reset(int new_fd = -1) noexcept;

    // Helper to create a POSIX pipe returning (read_fd, write_fd)
    static Result<std::pair<UniqueFd, UniqueFd>> create_pipe();

private:
    int fd_{-1};
};

} // namespace kernox::core
