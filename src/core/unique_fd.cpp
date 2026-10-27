#include "kernox/core/unique_fd.hpp"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace kernox::core {

UniqueFd::~UniqueFd() noexcept {
    reset(-1);
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

void UniqueFd::reset(int new_fd) noexcept {
    if (fd_ >= 0) {
        // POSIX close: on Linux, close() always closes the descriptor even if interrupted by EINTR.
        // Retrying close() on Linux is dangerous and can close a descriptor reused by another thread.
        ::close(fd_);
    }
    fd_ = new_fd;
}

Result<std::pair<UniqueFd, UniqueFd>> UniqueFd::create_pipe() {
    int fds[2] = {-1, -1};
#if defined(__linux__) && defined(O_CLOEXEC)
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        return Result<std::pair<UniqueFd, UniqueFd>>::err(
            Error::from_errno(ErrorCode::IoError, "pipe2 failed", errno));
    }
#else
    if (::pipe(fds) != 0) {
        return Result<std::pair<UniqueFd, UniqueFd>>::err(
            Error::from_errno(ErrorCode::IoError, "pipe failed", errno));
    }
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
    return Result<std::pair<UniqueFd, UniqueFd>>::ok(
        std::make_pair(UniqueFd(fds[0]), UniqueFd(fds[1])));
}

} // namespace kernox::core
