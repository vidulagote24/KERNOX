#pragma once

#include <ostream>
#include <string>
#include <string_view>

namespace kernox::core {

enum class ErrorCode {
    Success = 0,
    InvalidArgument,
    SystemError,
    IoError,
    ProcessFailed,
    Timeout,
    PermissionDenied,
    NotFound,
    ResourceExhausted,
    InvalidState
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

inline std::ostream& operator<<(std::ostream& os, ErrorCode code) {
    return os << to_string(code);
}

struct Error {
    ErrorCode code{ErrorCode::Success};
    std::string message{};
    int system_errno{0};

    Error() = default;

    Error(ErrorCode c, std::string msg, int sys_errno = 0)
        : code(c), message(std::move(msg)), system_errno(sys_errno) {}

    [[nodiscard]] bool is_error() const noexcept {
        return code != ErrorCode::Success;
    }

    [[nodiscard]] std::string to_string() const;

    static Error from_errno(ErrorCode code, std::string_view context, int err_num);
};

} // namespace kernox::core
