# KERNOX — POSIX Message Queue Subsystem (`PosixMessageQueue`)

## 1. Overview & Architectural Principles

The KERNOX POSIX Message Queue subsystem provides structured, priority-aware, and type-safe Inter-Process Communication (IPC) for Linux systems. Operating under the POSIX realtime extensions (`<mqueue.h>`, `-lrt`), message queues allow processes to exchange discrete messages rather than raw unformatted byte streams.

Key architectural features in KERNOX:
- **Move-Only RAII Semantics**: Message queue descriptors (`mqd_t`) are managed with strict move semantics. Copying is deleted. Closing occurs automatically upon destruction.
- **Priority-Driven Delivery**: Messages are assigned integer priorities ($0 \dots \text{sys\_mq\_prio\_max}-1$). Higher priority messages are dequeued strictly ahead of lower priority messages. Messages of equal priority preserve FIFO order.
- **Lifecycle & Ownership Protection**: Creators set `unlink_on_close = true`. Destructors unlink the named queue from the kernel VFS strictly if `owner_pid_ == ::getpid()`, preventing forked workers or client processes from disrupting the queue.
- **Type-Safe Result Abstractions**: All operations return `core::Result<T>` with rich `core::Error` contexts, avoiding C++ exceptions and mapping POSIX `errno` codes deterministically.
- **Bounded Real-Time Deadlines**: Both `send()` and `receive()` support optional millisecond timeouts using POSIX absolute time specifications (`mq_timedsend` / `mq_timedreceive`).

---

## 2. API Reference

Located in `include/kernox/modules/system/message_queue.hpp` and `src/modules/system/message_queue.cpp`:

```cpp
namespace kernox::modules::system {

enum class QueueAccess {
    ReadOnly = O_RDONLY,
    WriteOnly = O_WRONLY,
    ReadWrite = O_RDWR
};

struct QueueAttributes {
    long max_messages{10};      // mq_maxmsg: max queued messages
    long max_msg_size{1024};    // mq_msgsize: max payload bytes per message
    long current_messages{0};   // mq_curmsgs: currently queued messages
    bool non_blocking{false};   // O_NONBLOCK flag
};

class PosixMessageQueue {
public:
    // Creation and Opening
    static core::Result<PosixMessageQueue> create(const std::string& name, const QueueAttributes& attr = QueueAttributes{}, mode_t mode = 0660);
    static core::Result<PosixMessageQueue> open_or_create(const std::string& name, const QueueAttributes& attr = QueueAttributes{}, mode_t mode = 0660);
    static core::Result<PosixMessageQueue> open(const std::string& name, QueueAccess access = QueueAccess::ReadWrite, bool non_blocking = false);
    static core::Result<void> unlink(const std::string& name);

    // Lifecycle
    PosixMessageQueue() noexcept = default;
    ~PosixMessageQueue() noexcept;
    PosixMessageQueue(PosixMessageQueue&& other) noexcept;
    PosixMessageQueue& operator=(PosixMessageQueue&& other) noexcept;

    PosixMessageQueue(const PosixMessageQueue&) = delete;
    PosixMessageQueue& operator=(const PosixMessageQueue&) = delete;

    void close() noexcept;
    core::Result<void> unlink();

    // Data Transfer
    core::Result<void> send(const void* data, size_t size, unsigned int priority = 0, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<void> send_string(std::string_view msg, unsigned int priority = 0, std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    core::Result<size_t> receive(void* buffer, size_t buffer_size, unsigned int* priority = nullptr, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    core::Result<std::pair<std::string, unsigned int>> receive_string(size_t max_bytes = 0, std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Attributes & Flags
    core::Result<QueueAttributes> get_attributes() const;
    core::Result<void> set_non_blocking(bool non_blocking);

    // Queries
    bool is_valid() const noexcept;
    mqd_t native_handle() const noexcept;
    const std::string& name() const noexcept;
    bool is_owner() const noexcept;
    bool unlink_on_close() const noexcept;
    void set_unlink_on_close(bool unlink) noexcept;
};

} // namespace kernox::modules::system
```

---

## 3. Key Behavioral Invariants

### 3.1. Priority Ordering & Equal-Priority FIFO
POSIX guarantees that messages with higher numerical priority values are placed ahead of messages with lower priorities in the queue. Messages enqueued with the same priority maintain strict FIFO order.

### 3.2. Buffer Sizing Requirement
In POSIX message queues, calls to `mq_receive()` or `mq_timedreceive()` **must** supply a receive buffer whose capacity is at least equal to the queue's `mq_msgsize`. Supplying a smaller buffer immediately fails with `EMSGSIZE`. The KERNOX `receive_string()` method transparently queries `get_attributes()` to guarantee the internal buffer accommodates `mq_msgsize`, eliminating caller-side buffer allocation bugs.

### 3.3. Unlinking vs. Closing Semantics
- `mq_close()` closes the calling process's file descriptor table entry.
- `mq_unlink()` removes the queue name from `/dev/mqueue`. Existing open references remain valid until the last process closes the queue.
- KERNOX separates `close()` from `unlink()`. The creating process tracks ownership via `owner_pid_`. When the owning process exits, the destructor performs `mq_unlink()`. Child or client processes that open the queue never unlink it during destruction.

---

## 4. Automated Test Verification

Automated test coverage is provided in `tests/test_message_queue.cpp` across 12 distinct test suites:

1. **Test 1**: Queue Creation and Attribute Inspection (`max_messages`, `max_msg_size`, `current_messages`, `non_blocking`).
2. **Test 2**: Basic Send and Receive data integrity.
3. **Test 3**: Message Priority Ordering (Priority 20 received before 10 and 1).
4. **Test 4**: Strict FIFO Ordering for Equal Priorities.
5. **Test 5**: Timed Receive on an Empty Queue (returns `ErrorCode::Timeout`).
6. **Test 6**: Timed Send on a Full Queue (returns `ErrorCode::Timeout`).
7. **Test 7**: Cross-Process Communication (forked child request/response synchronization).
8. **Test 8**: Non-Blocking Behavior (`set_non_blocking(true)` immediate return).
9. **Test 9**: Move Semantics and Invalidation of Moved-From Descriptors.
10. **Test 10**: Ownership and Unlink Lifecycle (`owner_pid_` safety).
11. **Test 11**: Invalid Queue Names (missing slash, nested slashes) and Oversized Payloads.
12. **Test 12**: Error Handling for Missing and Duplicate Queues (`NotFound` and `InvalidState`).

All 12 suites pass under Debug, Release, and AddressSanitizer/UBSanitizer modes.
