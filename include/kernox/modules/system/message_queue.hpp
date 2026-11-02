#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"

#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <mqueue.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace kernox::modules::system {

/**
 * @brief Open access mode for POSIX message queues.
 */
enum class QueueAccess {
    ReadOnly = O_RDONLY,
    WriteOnly = O_WRONLY,
    ReadWrite = O_RDWR
};

/**
 * @brief Configuration and status attributes for POSIX message queues.
 */
struct QueueAttributes {
    long max_messages{10};      ///< mq_maxmsg: Maximum number of queued messages (Linux default limit is 10)
    long max_msg_size{1024};    ///< mq_msgsize: Maximum message payload size in bytes
    long current_messages{0};   ///< mq_curmsgs: Current number of messages in the queue
    bool non_blocking{false};   ///< Non-blocking I/O flag (O_NONBLOCK)
};

/**
 * @brief Move-only RAII abstraction managing POSIX Message Queues.
 *
 * Encapsulates mq_open, mq_close, mq_unlink, mq_send, mq_receive, mq_timedsend,
 * mq_timedreceive, mq_getattr, and mq_setattr.
 *
 * Ownership & Unlinking Semantics:
 *  - Creators can enable unlink_on_close (default: true for create()).
 *  - Unlink is only executed if owner_pid_ == ::getpid(), preventing forked
 *    or inherited child copies from removing the active queue.
 *  - Closed queues invalidate their mqd_t descriptor to static_cast<mqd_t>(-1).
 */
class PosixMessageQueue {
public:
    /**
     * @brief Creates a new named message queue exclusively (O_CREAT | O_EXCL).
     *
     * Fails with ErrorCode::AlreadyExists if the queue name already exists.
     * Sets unlink_on_close = true for the creating process.
     */
    static core::Result<PosixMessageQueue> create(
        const std::string& name,
        const QueueAttributes& attr = QueueAttributes{},
        mode_t mode = 0660);

    /**
     * @brief Opens an existing queue or creates it if it does not exist (O_CREAT).
     */
    static core::Result<PosixMessageQueue> open_or_create(
        const std::string& name,
        const QueueAttributes& attr = QueueAttributes{},
        mode_t mode = 0660);

    /**
     * @brief Opens an existing POSIX message queue.
     *
     * Fails with ErrorCode::NotFound if the queue does not exist.
     */
    static core::Result<PosixMessageQueue> open(
        const std::string& name,
        QueueAccess access = QueueAccess::ReadWrite,
        bool non_blocking = false);

    /**
     * @brief Explicitly unlinks a named message queue by name.
     */
    static core::Result<void> unlink(const std::string& name);

    // Lifecycle
    PosixMessageQueue() noexcept = default;
    ~PosixMessageQueue() noexcept;

    PosixMessageQueue(PosixMessageQueue&& other) noexcept;
    PosixMessageQueue& operator=(PosixMessageQueue&& other) noexcept;

    PosixMessageQueue(const PosixMessageQueue&) = delete;
    PosixMessageQueue& operator=(const PosixMessageQueue&) = delete;

    /**
     * @brief Closes the descriptor and unlinks if owner and unlink_on_close is set.
     */
    void close() noexcept;

    /**
     * @brief Explicitly unlinks this queue instance.
     */
    core::Result<void> unlink();

    // Data Transfer: Send
    core::Result<void> send(
        const void* data,
        size_t size,
        unsigned int priority = 0,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    core::Result<void> send_string(
        std::string_view msg,
        unsigned int priority = 0,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Data Transfer: Receive
    core::Result<size_t> receive(
        void* buffer,
        size_t buffer_size,
        unsigned int* priority = nullptr,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    core::Result<std::pair<std::string, unsigned int>> receive_string(
        size_t max_bytes = 0,
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Attributes
    core::Result<QueueAttributes> get_attributes() const;
    core::Result<void> set_non_blocking(bool non_blocking);

    // Metadata & Queries
    [[nodiscard]] bool is_valid() const noexcept { return mqd_ != static_cast<mqd_t>(-1); }
    [[nodiscard]] mqd_t native_handle() const noexcept { return mqd_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] bool is_owner() const noexcept;
    [[nodiscard]] bool unlink_on_close() const noexcept { return unlink_on_close_; }

    void set_unlink_on_close(bool unlink) noexcept { unlink_on_close_ = unlink; }

private:
    explicit PosixMessageQueue(mqd_t mqd, std::string name, bool is_owner) noexcept;

    mqd_t mqd_{static_cast<mqd_t>(-1)};
    std::string name_{};
    bool unlink_on_close_{false};
    pid_t owner_pid_{0};
};

} // namespace kernox::modules::system

namespace kernox::system {
    using PosixMessageQueue = kernox::modules::system::PosixMessageQueue;
    using QueueAccess = kernox::modules::system::QueueAccess;
    using QueueAttributes = kernox::modules::system::QueueAttributes;
}
