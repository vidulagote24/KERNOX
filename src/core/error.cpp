#include "kernox/core/error.hpp"

#include <cstring>
#include <sstream>

namespace kernox::core {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Success:
            return "Success";
        case ErrorCode::InvalidArgument:
            return "InvalidArgument";
        case ErrorCode::SystemError:
            return "SystemError";
        case ErrorCode::IoError:
            return "IoError";
        case ErrorCode::ProcessFailed:
            return "ProcessFailed";
        case ErrorCode::Timeout:
            return "Timeout";
        case ErrorCode::PermissionDenied:
            return "PermissionDenied";
        case ErrorCode::NotFound:
            return "NotFound";
        case ErrorCode::ResourceExhausted:
            return "ResourceExhausted";
        case ErrorCode::InvalidState:
            return "InvalidState";
    }
    return "UnknownError";
}

std::string Error::to_string() const {
    std::ostringstream oss;
    oss << "[" << kernox::core::to_string(code) << "] " << message;
    if (system_errno != 0) {
        oss << " (errno " << system_errno << ": " << std::strerror(system_errno) << ")";
    }
    return oss.str();
}

Error Error::from_errno(ErrorCode code, std::string_view context, int err_num) {
    std::string msg(context);
    msg += ": ";
    msg += std::strerror(err_num);
    return Error(code, std::move(msg), err_num);
}

} // namespace kernox::core
