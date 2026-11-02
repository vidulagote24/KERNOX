#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"
#include "kernox/modules/system/shared_memory.hpp"
#include "kernox/modules/system/sync.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace kernox::system {

// ----------------------------------------------------------------------------
// Ring Buffer Enums and Layout Structures
// ----------------------------------------------------------------------------

enum class SlotState : uint32_t {
    Empty = 0x00000000,
    Writing = 0x57524954,    // 'WRIT'
    Committed = 0x434F4D4D,  // 'COMM'
    Reading = 0x52454144     // 'READ'
};

enum class RingBufferState : uint32_t {
    Uninitialized = 0,
    Ready = 1,
    Shutdown = 2,
    Corrupted = 3
};

struct alignas(8) SlotHeader {
    uint32_t state{static_cast<uint32_t>(SlotState::Empty)};
    uint32_t payload_size{0};
    uint64_t sequence{0};
};

struct alignas(64) RingBufferHeader {
    uint32_t magic;              // MAGIC: 0x4B524E58 ('KRNX')
    uint32_t version;            // VERSION: 1
    uint32_t state;              // RingBufferState: Ready, Shutdown, Corrupted
    uint32_t capacity;           // Total slot count
    uint32_t slot_size;          // Payload bytes per slot
    uint32_t slot_stride;        // sizeof(SlotHeader) + slot_size aligned to 8 bytes
    uint64_t head;               // Next slot index to pop (head % capacity)
    uint64_t tail;               // Next slot index to push (tail % capacity)
    uint32_t count;              // Committed items in buffer
    uint32_t is_shutdown;        // 1 if shutdown initiated, 0 otherwise
    uint64_t next_push_seq;      // Monotonic sequence counter for pushes
    uint64_t next_pop_seq;       // Monotonic sequence counter for pops
    uint64_t slots_offset;       // Byte offset from start of shm to slot 0

    alignas(64) pthread_mutex_t mutex;     // Process-shared robust mutex
    alignas(64) pthread_cond_t  not_empty; // Condition variable: buffer not empty
    alignas(64) pthread_cond_t  not_full;  // Condition variable: buffer not full
};

struct RingBufferConfig {
    uint32_t capacity{16};          // Number of message slots (must be > 0)
    uint32_t slot_size{512};        // Maximum payload size per slot in bytes
    mode_t permissions{0660};       // Shared memory file permissions
};

// ----------------------------------------------------------------------------
// SharedRingBuffer
// ----------------------------------------------------------------------------

/**
 * @brief Bounded, multi-producer multi-consumer ring buffer in POSIX shared memory.
 *
 * Architecture & Concurrency Contract:
 *  - Memory: Sits entirely inside a POSIX shared memory segment managed by SharedMemory.
 *  - Synchronization: ProcessSharedMutex (PTHREAD_MUTEX_ROBUST) + two ProcessSharedConditionVariable
 *    instances (not_empty and not_full with CLOCK_MONOTONIC).
 *  - Transaction Protocol: Per-slot state transitions (Empty -> Writing -> Committed -> Reading -> Empty).
 *  - Crash Recovery: If an owner dies while Writing, the uncommitted write is discarded and the slot
 *    is reverted to Empty. If an owner dies while Reading, the read is rolled back to Committed so
 *    another consumer can receive the message. Invariants are audited before make_consistent().
 *  - Shutdown: shutdown() wakes all waiting producers and consumers. push() immediately fails;
 *    pop() drains already committed messages until empty, then fails with InvalidState.
 */
class SharedRingBuffer {
public:
    static constexpr uint32_t MAGIC = 0x4B524E58; // 'KRNX'
    static constexpr uint32_t VERSION = 1;
    static constexpr uint32_t MAX_CAPACITY = 65536;
    static constexpr uint32_t MAX_SLOT_SIZE = 65536;

    /**
     * @brief Creates and initializes a new bounded ring buffer in POSIX shared memory.
     * The creating instance owns the shared memory segment and unlinks it on destruction.
     */
    static core::Result<SharedRingBuffer> create(
        const std::string& name,
        const RingBufferConfig& config = {});

    /**
     * @brief Opens and attaches to an existing bounded ring buffer in POSIX shared memory.
     * The attached instance does NOT own or unlink the shared memory segment on destruction.
     */
    static core::Result<SharedRingBuffer> open_existing(
        const std::string& name);

    /**
     * @brief Destructor. Owner instances cleanly destroy mutex and condition variables,
     * and unlink the shared memory segment.
     */
    ~SharedRingBuffer() noexcept;

    // Move-only semantics with explicit invalidation
    SharedRingBuffer(SharedRingBuffer&& other) noexcept;
    SharedRingBuffer& operator=(SharedRingBuffer&& other) noexcept;

    SharedRingBuffer(const SharedRingBuffer&) = delete;
    SharedRingBuffer& operator=(const SharedRingBuffer&) = delete;

    SharedRingBuffer() noexcept = default;

    /**
     * @brief Pushes a binary message into the ring buffer.
     * Blocks if the buffer is full until space is freed, timeout expires, or shutdown occurs.
     *
     * @param data Pointer to message payload.
     * @param size Size in bytes of message payload (must be <= slot_size).
     * @param timeout Maximum duration to wait if full (< 0 means infinite wait).
     * @return Result<void> indicating success, Timeout error, or InvalidState on shutdown/corruption.
     */
    core::Result<void> push(
        const void* data,
        size_t size,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));

    /**
     * @brief Pushes a string message into the ring buffer.
     */
    core::Result<void> push(
        std::string_view message,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));

    /**
     * @brief Pops a binary message from the ring buffer into caller's buffer.
     * Blocks if the buffer is empty until a message arrives, timeout expires, or shutdown occurs.
     *
     * @param buffer Output buffer to receive payload.
     * @param buffer_capacity Maximum bytes buffer can hold.
     * @param timeout Maximum duration to wait if empty (< 0 means infinite wait).
     * @return Result<size_t> containing actual bytes copied, Timeout error, or InvalidState on shutdown.
     */
    core::Result<size_t> pop(
        void* buffer,
        size_t buffer_capacity,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));

    /**
     * @brief Pops a string message from the ring buffer.
     */
    core::Result<std::string> pop(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));

    /**
     * @brief Initiates graceful shutdown of the ring buffer.
     * Wakes all waiting producers and consumers. Blocked producers wake and fail with InvalidState.
     * Blocked consumers wake; consumers can continue draining committed items until empty.
     */
    core::Result<void> shutdown();

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_owner() const noexcept;
    [[nodiscard]] bool is_shutdown() const;
    [[nodiscard]] bool is_empty() const;
    [[nodiscard]] bool is_full() const;
    [[nodiscard]] size_t capacity() const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t slot_size() const;
    [[nodiscard]] const std::string& name() const noexcept;

    /**
     * @brief Manually unlinks the shared memory segment if this instance is the owner.
     */
    core::Result<void> unlink() noexcept;

private:
    SharedRingBuffer(
        SharedMemory shm,
        RingBufferHeader* hdr) noexcept;

    core::Result<void> recover_after_owner_death();
    [[nodiscard]] RingBufferHeader* header() noexcept;
    [[nodiscard]] const RingBufferHeader* header() const noexcept;
    [[nodiscard]] SlotHeader* get_slot(size_t index) noexcept;
    [[nodiscard]] const SlotHeader* get_slot(size_t index) const noexcept;
    [[nodiscard]] uint8_t* get_payload(SlotHeader* slot) noexcept;
    [[nodiscard]] const uint8_t* get_payload(const SlotHeader* slot) const noexcept;

    SharedMemory shm_{};
    ProcessSharedMutex mutex_{};
    ProcessSharedConditionVariable not_empty_{};
    ProcessSharedConditionVariable not_full_{};
};

} // namespace kernox::system

namespace kernox::sys {
    using SlotState = kernox::system::SlotState;
    using RingBufferState = kernox::system::RingBufferState;
    using SlotHeader = kernox::system::SlotHeader;
    using RingBufferHeader = kernox::system::RingBufferHeader;
    using RingBufferConfig = kernox::system::RingBufferConfig;
    using SharedRingBuffer = kernox::system::SharedRingBuffer;
}
