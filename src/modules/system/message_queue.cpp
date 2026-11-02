#include "kernox/modules/system/message_queue.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <unistd.h>

namespace kernox::modules::system {

namespace {

core::Result<void> validate_queue_name(std::string_view name) {
    if (name.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Message queue name cannot be empty");
    }
    if (name.front() != '/') {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Message queue name must start with a leading slash ('/'): " + std::string(name));
    }
    if (name.size() == 1) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Message queue name cannot be just a single slash");
    }
    if (name.size() > 255) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Message queue name exceeds maximum length (255 chars)");
    }
    if (name.find('/', 1) != std::string_view::npos) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Message queue name cannot contain additional slashes: " + std::string(name));
    }
    if (name.find('\0') != std::string_view::npos) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Message queue name cannot contain embedded null characters");
    }
    return core::Result<void>::ok();
}

struct timespec to_abs_timespec(std::chrono::milliseconds timeout) {
    struct timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    int64_t ms = timeout.count();
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1'000'000;
    if (ts.tv_nsec >= 1'000'000'000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1'000'000'000;
    }
    return ts;
}

core::Error translate_mq_errno(std::string_view action, int err) {
    switch (err) {
        case EEXIST:
            return core::Error(core::ErrorCode::InvalidState, std::string(action) + ": queue already exists");
        case ENOENT:
            return core::Error(core::ErrorCode::NotFound, std::string(action) + ": queue does not exist");
        case ETIMEDOUT:
            return core::Error(core::ErrorCode::Timeout, std::string(action) + ": operation timed out");
        case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
        case EWOULDBLOCK:
#endif
            return core::Error(core::ErrorCode::Timeout, std::string(action) + ": queue would block (non-blocking / full / empty)");
        case EMSGSIZE:
            return core::Error(core::ErrorCode::InvalidArgument, std::string(action) + ": message size exceeds queue limit or buffer is too small");
        case EACCES:
            return core::Error(core::ErrorCode::PermissionDenied, std::string(action) + ": permission denied");
        case EINVAL:
            return core::Error(core::ErrorCode::InvalidArgument, std::string(action) + ": invalid argument or attribute value");
        case EBADF:
            return core::Error(core::ErrorCode::InvalidState, std::string(action) + ": invalid message queue descriptor");
        case EMFILE:
        case ENFILE:
            return core::Error(core::ErrorCode::IoError, std::string(action) + ": file descriptor table full");
        default:
            return core::Error::from_errno(core::ErrorCode::SystemError, std::string(action) + " failed", err);
    }
}

} // namespace

PosixMessageQueue::PosixMessageQueue(mqd_t mqd, std::string name, bool is_owner) noexcept
    : mqd_(mqd), name_(std::move(name)), unlink_on_close_(is_owner), owner_pid_(::getpid()) {}

PosixMessageQueue::~PosixMessageQueue() noexcept {
    close();
}

PosixMessageQueue::PosixMessageQueue(PosixMessageQueue&& other) noexcept
    : mqd_(other.mqd_),
      name_(std::move(other.name_)),
      unlink_on_close_(other.unlink_on_close_),
      owner_pid_(other.owner_pid_) {
    other.mqd_ = static_cast<mqd_t>(-1);
    other.unlink_on_close_ = false;
    other.owner_pid_ = 0;
}

PosixMessageQueue& PosixMessageQueue::operator=(PosixMessageQueue&& other) noexcept {
    if (this != &other) {
        close();
        mqd_ = other.mqd_;
        name_ = std::move(other.name_);
        unlink_on_close_ = other.unlink_on_close_;
        owner_pid_ = other.owner_pid_;

        other.mqd_ = static_cast<mqd_t>(-1);
        other.unlink_on_close_ = false;
        other.owner_pid_ = 0;
    }
    return *this;
}

void PosixMessageQueue::close() noexcept {
    if (mqd_ != static_cast<mqd_t>(-1)) {
        ::mq_close(mqd_);
        mqd_ = static_cast<mqd_t>(-1);
    }
    if (unlink_on_close_ && owner_pid_ == ::getpid() && !name_.empty()) {
        ::mq_unlink(name_.c_str());
        unlink_on_close_ = false;
    }
}

core::Result<void> PosixMessageQueue::unlink() {
    if (name_.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot unlink message queue: instance has no associated name");
    }
    auto res = unlink(name_);
    if (res.is_ok()) {
        unlink_on_close_ = false;
    }
    return res;
}

bool PosixMessageQueue::is_owner() const noexcept {
    return owner_pid_ == ::getpid();
}

// ----------------------------------------------------------------------------
// Static Factory Methods
// ----------------------------------------------------------------------------

core::Result<PosixMessageQueue> PosixMessageQueue::create(
    const std::string& name,
    const QueueAttributes& attr,
    mode_t mode) {
    auto val_res = validate_queue_name(name);
    if (val_res.is_err()) {
        return core::Result<PosixMessageQueue>::err(val_res.error());
    }

    if (attr.max_messages <= 0 || attr.max_msg_size <= 0) {
        return core::Result<PosixMessageQueue>::err(
            core::ErrorCode::InvalidArgument,
            "Queue max_messages and max_msg_size must be positive");
    }

    struct mq_attr mqattr{};
    mqattr.mq_flags = attr.non_blocking ? O_NONBLOCK : 0;
    mqattr.mq_maxmsg = attr.max_messages;
    mqattr.mq_msgsize = attr.max_msg_size;
    mqattr.mq_curmsgs = 0;

    int oflags = O_CREAT | O_EXCL | O_RDWR;
    if (attr.non_blocking) {
        oflags |= O_NONBLOCK;
    }

    mqd_t mqd = ::mq_open(name.c_str(), oflags, mode, &mqattr);
    if (mqd == static_cast<mqd_t>(-1)) {
        return core::Result<PosixMessageQueue>::err(
            translate_mq_errno("mq_open(create)", errno));
    }

    PosixMessageQueue queue(mqd, name, true);
    return core::Result<PosixMessageQueue>::ok(std::move(queue));
}

core::Result<PosixMessageQueue> PosixMessageQueue::open_or_create(
    const std::string& name,
    const QueueAttributes& attr,
    mode_t mode) {
    auto val_res = validate_queue_name(name);
    if (val_res.is_err()) {
        return core::Result<PosixMessageQueue>::err(val_res.error());
    }

    if (attr.max_messages <= 0 || attr.max_msg_size <= 0) {
        return core::Result<PosixMessageQueue>::err(
            core::ErrorCode::InvalidArgument,
            "Queue max_messages and max_msg_size must be positive");
    }

    struct mq_attr mqattr{};
    mqattr.mq_flags = attr.non_blocking ? O_NONBLOCK : 0;
    mqattr.mq_maxmsg = attr.max_messages;
    mqattr.mq_msgsize = attr.max_msg_size;
    mqattr.mq_curmsgs = 0;

    int oflags = O_CREAT | O_RDWR;
    if (attr.non_blocking) {
        oflags |= O_NONBLOCK;
    }

    mqd_t mqd = ::mq_open(name.c_str(), oflags, mode, &mqattr);
    if (mqd == static_cast<mqd_t>(-1)) {
        return core::Result<PosixMessageQueue>::err(
            translate_mq_errno("mq_open(open_or_create)", errno));
    }

    // Existing queue or newly created without exclusive ownership
    PosixMessageQueue queue(mqd, name, false);
    return core::Result<PosixMessageQueue>::ok(std::move(queue));
}

core::Result<PosixMessageQueue> PosixMessageQueue::open(
    const std::string& name,
    QueueAccess access,
    bool non_blocking) {
    auto val_res = validate_queue_name(name);
    if (val_res.is_err()) {
        return core::Result<PosixMessageQueue>::err(val_res.error());
    }

    int oflags = static_cast<int>(access);
    if (non_blocking) {
        oflags |= O_NONBLOCK;
    }

    mqd_t mqd = ::mq_open(name.c_str(), oflags);
    if (mqd == static_cast<mqd_t>(-1)) {
        return core::Result<PosixMessageQueue>::err(
            translate_mq_errno("mq_open", errno));
    }

    PosixMessageQueue queue(mqd, name, false);
    return core::Result<PosixMessageQueue>::ok(std::move(queue));
}

core::Result<void> PosixMessageQueue::unlink(const std::string& name) {
    auto val_res = validate_queue_name(name);
    if (val_res.is_err()) {
        return val_res;
    }

    if (::mq_unlink(name.c_str()) != 0) {
        return core::Result<void>::err(
            translate_mq_errno("mq_unlink", errno));
    }
    return core::Result<void>::ok();
}

// ----------------------------------------------------------------------------
// Data Transfer: Send
// ----------------------------------------------------------------------------

core::Result<void> PosixMessageQueue::send(
    const void* data,
    size_t size,
    unsigned int priority,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot send to message queue: queue is closed or invalid");
    }

    int rc = 0;
    if (timeout.has_value()) {
        struct timespec abs_ts = to_abs_timespec(*timeout);
        do {
            rc = ::mq_timedsend(
                mqd_,
                reinterpret_cast<const char*>(data),
                size,
                priority,
                &abs_ts);
        } while (rc < 0 && errno == EINTR);
    } else {
        do {
            rc = ::mq_send(
                mqd_,
                reinterpret_cast<const char*>(data),
                size,
                priority);
        } while (rc < 0 && errno == EINTR);
    }

    if (rc < 0) {
        return core::Result<void>::err(
            translate_mq_errno("mq_send", errno));
    }

    return core::Result<void>::ok();
}

core::Result<void> PosixMessageQueue::send_string(
    std::string_view msg,
    unsigned int priority,
    std::optional<std::chrono::milliseconds> timeout) {
    return send(msg.data(), msg.size(), priority, timeout);
}

// ----------------------------------------------------------------------------
// Data Transfer: Receive
// ----------------------------------------------------------------------------

core::Result<size_t> PosixMessageQueue::receive(
    void* buffer,
    size_t buffer_size,
    unsigned int* priority,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!is_valid()) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Cannot receive from message queue: queue is closed or invalid");
    }
    if (buffer == nullptr || buffer_size == 0) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument,
            "Receive buffer cannot be null or zero-sized");
    }

    ssize_t rc = 0;
    if (timeout.has_value()) {
        struct timespec abs_ts = to_abs_timespec(*timeout);
        do {
            rc = ::mq_timedreceive(
                mqd_,
                reinterpret_cast<char*>(buffer),
                buffer_size,
                priority,
                &abs_ts);
        } while (rc < 0 && errno == EINTR);
    } else {
        do {
            rc = ::mq_receive(
                mqd_,
                reinterpret_cast<char*>(buffer),
                buffer_size,
                priority);
        } while (rc < 0 && errno == EINTR);
    }

    if (rc < 0) {
        return core::Result<size_t>::err(
            translate_mq_errno("mq_receive", errno));
    }

    return core::Result<size_t>::ok(static_cast<size_t>(rc));
}

core::Result<std::pair<std::string, unsigned int>> PosixMessageQueue::receive_string(
    size_t max_bytes,
    std::optional<std::chrono::milliseconds> timeout) {
    // Determine required receive buffer size (must be >= mq_msgsize)
    size_t buf_size = max_bytes;
    auto attr_res = get_attributes();
    if (attr_res.is_ok()) {
        size_t q_msgsize = static_cast<size_t>(attr_res.value().max_msg_size);
        if (buf_size < q_msgsize) {
            buf_size = q_msgsize;
        }
    } else if (buf_size == 0) {
        buf_size = 8192; // POSIX default fallback
    }

    std::string buf;
    buf.resize(buf_size);
    unsigned int prio = 0;

    auto rec_res = receive(buf.data(), buf.size(), &prio, timeout);
    if (rec_res.is_err()) {
        return core::Result<std::pair<std::string, unsigned int>>::err(rec_res.error());
    }

    buf.resize(rec_res.value());
    return core::Result<std::pair<std::string, unsigned int>>::ok(
        std::make_pair(std::move(buf), prio));
}

// ----------------------------------------------------------------------------
// Attributes & Settings
// ----------------------------------------------------------------------------

core::Result<QueueAttributes> PosixMessageQueue::get_attributes() const {
    if (!is_valid()) {
        return core::Result<QueueAttributes>::err(
            core::ErrorCode::InvalidState,
            "Cannot get attributes: message queue is closed or invalid");
    }

    struct mq_attr mqattr{};
    if (::mq_getattr(mqd_, &mqattr) != 0) {
        return core::Result<QueueAttributes>::err(
            translate_mq_errno("mq_getattr", errno));
    }

    QueueAttributes attr{};
    attr.max_messages = mqattr.mq_maxmsg;
    attr.max_msg_size = mqattr.mq_msgsize;
    attr.current_messages = mqattr.mq_curmsgs;
    attr.non_blocking = (mqattr.mq_flags & O_NONBLOCK) != 0;

    return core::Result<QueueAttributes>::ok(attr);
}

core::Result<void> PosixMessageQueue::set_non_blocking(bool non_blocking) {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot set attributes: message queue is closed or invalid");
    }

    struct mq_attr new_attr{};
    new_attr.mq_flags = non_blocking ? O_NONBLOCK : 0;

    if (::mq_setattr(mqd_, &new_attr, nullptr) != 0) {
        return core::Result<void>::err(
            translate_mq_errno("mq_setattr", errno));
    }

    return core::Result<void>::ok();
}

} // namespace kernox::modules::system
