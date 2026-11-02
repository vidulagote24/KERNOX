#include "kernox/modules/system/ring_buffer.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>

namespace kernox::system {

core::Result<SharedRingBuffer> SharedRingBuffer::create(
    const std::string& name,
    const RingBufferConfig& config) {
    if (config.capacity == 0 || config.capacity > MAX_CAPACITY) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Invalid ring buffer capacity (must be between 1 and 65536)");
    }
    if (config.slot_size == 0 || config.slot_size > MAX_SLOT_SIZE) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Invalid ring buffer slot size (must be between 1 and 65536 bytes)");
    }

    size_t header_size = sizeof(RingBufferHeader);
    size_t header_padding = (64 - (header_size % 64)) % 64;
    size_t slots_offset = header_size + header_padding;
    size_t slot_stride = (sizeof(SlotHeader) + config.slot_size + 7) & ~7ULL;

    if (config.capacity > 0 && (SIZE_MAX - slots_offset) / slot_stride < config.capacity) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Ring buffer total size overflows size_t");
    }

    size_t total_size = slots_offset + (static_cast<size_t>(config.capacity) * slot_stride);

    auto shm_res = SharedMemory::create(name, total_size, config.permissions);
    if (!shm_res.is_ok()) {
        return core::Result<SharedRingBuffer>::err(shm_res.error());
    }

    SharedMemory shm = std::move(shm_res.value());
    auto* hdr = shm.as<RingBufferHeader>();
    std::memset(hdr, 0, total_size);

    hdr->magic = 0;
    hdr->version = 0;
    hdr->state = static_cast<uint32_t>(RingBufferState::Uninitialized);
    hdr->capacity = config.capacity;
    hdr->slot_size = config.slot_size;
    hdr->slot_stride = static_cast<uint32_t>(slot_stride);
    hdr->slots_offset = slots_offset;
    hdr->head = 0;
    hdr->tail = 0;
    hdr->count = 0;
    hdr->is_shutdown = 0;
    hdr->next_push_seq = 0;
    hdr->next_pop_seq = 0;

    auto mres = ProcessSharedMutex::init_in_place(&hdr->mutex);
    if (!mres.is_ok()) {
        return core::Result<SharedRingBuffer>::err(mres.error());
    }

    auto c1res = ProcessSharedConditionVariable::init_in_place(&hdr->not_empty);
    if (!c1res.is_ok()) {
        ProcessSharedMutex::destroy_in_place(&hdr->mutex);
        return core::Result<SharedRingBuffer>::err(c1res.error());
    }

    auto c2res = ProcessSharedConditionVariable::init_in_place(&hdr->not_full);
    if (!c2res.is_ok()) {
        ProcessSharedConditionVariable::destroy_in_place(&hdr->not_empty);
        ProcessSharedMutex::destroy_in_place(&hdr->mutex);
        return core::Result<SharedRingBuffer>::err(c2res.error());
    }

    for (size_t i = 0; i < config.capacity; ++i) {
        uint8_t* slot_addr = reinterpret_cast<uint8_t*>(hdr) + slots_offset + (i * slot_stride);
        auto* slot = reinterpret_cast<SlotHeader*>(slot_addr);
        slot->state = static_cast<uint32_t>(SlotState::Empty);
        slot->payload_size = 0;
        slot->sequence = 0;
    }

    // Atomic publication handshake: ensure all prior stores (mutex, condvars, slots)
    // are globally visible to other CPU cores before publishing MAGIC and Ready state.
#ifndef __SANITIZE_THREAD__
    std::atomic_thread_fence(std::memory_order_release);
#endif
    hdr->version = VERSION;
    hdr->magic = MAGIC;
    hdr->state = static_cast<uint32_t>(RingBufferState::Ready);
#ifndef __SANITIZE_THREAD__
    std::atomic_thread_fence(std::memory_order_release);
#endif

    return core::Result<SharedRingBuffer>::ok(
        SharedRingBuffer(std::move(shm), hdr));
}

core::Result<SharedRingBuffer> SharedRingBuffer::open_existing(
    const std::string& name) {
    auto shm_res = SharedMemory::open_existing(name);
    if (!shm_res.is_ok()) {
        return core::Result<SharedRingBuffer>::err(shm_res.error());
    }

    SharedMemory shm = std::move(shm_res.value());
    if (shm.size() < sizeof(RingBufferHeader)) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory size too small for RingBufferHeader");
    }

    auto* hdr = shm.as<RingBufferHeader>();
#ifndef __SANITIZE_THREAD__
    std::atomic_thread_fence(std::memory_order_acquire);
#endif
    if (hdr->magic != MAGIC) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Invalid ring buffer magic number");
    }
    if (hdr->version != VERSION) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Unsupported ring buffer layout version");
    }
    if (hdr->state == static_cast<uint32_t>(RingBufferState::Uninitialized)) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer shared memory is not fully initialized");
    }
    if (hdr->state == static_cast<uint32_t>(RingBufferState::Corrupted)) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer is permanently marked corrupted");
    }
    if (hdr->capacity == 0 || hdr->capacity > MAX_CAPACITY) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Invalid ring buffer capacity");
    }
    if (hdr->slot_size == 0 || hdr->slot_size > MAX_SLOT_SIZE) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Invalid ring buffer slot size");
    }

    size_t required_size = hdr->slots_offset + (static_cast<size_t>(hdr->capacity) * hdr->slot_stride);
    if (shm.size() < required_size) {
        return core::Result<SharedRingBuffer>::err(
            core::ErrorCode::InvalidArgument,
            "Shared memory size smaller than required ring buffer slots");
    }

    return core::Result<SharedRingBuffer>::ok(
        SharedRingBuffer(std::move(shm), hdr));
}

SharedRingBuffer::SharedRingBuffer(
    SharedMemory shm,
    RingBufferHeader* hdr) noexcept
    : shm_(std::move(shm)),
      mutex_(hdr ? &hdr->mutex : nullptr),
      not_empty_(hdr ? &hdr->not_empty : nullptr),
      not_full_(hdr ? &hdr->not_full : nullptr) {}

SharedRingBuffer::~SharedRingBuffer() noexcept {
    if (shm_.is_owner() && shm_.is_mapped()) {
        auto* hdr = header();
        if (hdr) {
            ProcessSharedMutex::destroy_in_place(&hdr->mutex);
            ProcessSharedConditionVariable::destroy_in_place(&hdr->not_empty);
            ProcessSharedConditionVariable::destroy_in_place(&hdr->not_full);
        }
    }
}

SharedRingBuffer::SharedRingBuffer(SharedRingBuffer&& other) noexcept
    : shm_(std::move(other.shm_)),
      mutex_(std::move(other.mutex_)),
      not_empty_(std::move(other.not_empty_)),
      not_full_(std::move(other.not_full_)) {}

SharedRingBuffer& SharedRingBuffer::operator=(SharedRingBuffer&& other) noexcept {
    if (this != &other) {
        if (shm_.is_owner() && shm_.is_mapped()) {
            auto* hdr = header();
            if (hdr) {
                ProcessSharedMutex::destroy_in_place(&hdr->mutex);
                ProcessSharedConditionVariable::destroy_in_place(&hdr->not_empty);
                ProcessSharedConditionVariable::destroy_in_place(&hdr->not_full);
            }
        }
        shm_ = std::move(other.shm_);
        mutex_ = std::move(other.mutex_);
        not_empty_ = std::move(other.not_empty_);
        not_full_ = std::move(other.not_full_);
    }
    return *this;
}

RingBufferHeader* SharedRingBuffer::header() noexcept {
    if (!shm_.is_mapped()) {
        return nullptr;
    }
    return shm_.as<RingBufferHeader>();
}

const RingBufferHeader* SharedRingBuffer::header() const noexcept {
    if (!shm_.is_mapped()) {
        return nullptr;
    }
    return shm_.as<const RingBufferHeader>();
}

SlotHeader* SharedRingBuffer::get_slot(size_t index) noexcept {
    auto* hdr = header();
    if (!hdr || index >= hdr->capacity) {
        return nullptr;
    }
    uint8_t* base = reinterpret_cast<uint8_t*>(hdr);
    return reinterpret_cast<SlotHeader*>(base + hdr->slots_offset + (index * hdr->slot_stride));
}

const SlotHeader* SharedRingBuffer::get_slot(size_t index) const noexcept {
    const auto* hdr = header();
    if (!hdr || index >= hdr->capacity) {
        return nullptr;
    }
    const uint8_t* base = reinterpret_cast<const uint8_t*>(hdr);
    return reinterpret_cast<const SlotHeader*>(base + hdr->slots_offset + (index * hdr->slot_stride));
}

uint8_t* SharedRingBuffer::get_payload(SlotHeader* slot) noexcept {
    if (!slot) return nullptr;
    return reinterpret_cast<uint8_t*>(slot) + sizeof(SlotHeader);
}

const uint8_t* SharedRingBuffer::get_payload(const SlotHeader* slot) const noexcept {
    if (!slot) return nullptr;
    return reinterpret_cast<const uint8_t*>(slot) + sizeof(SlotHeader);
}

core::Result<void> SharedRingBuffer::recover_after_owner_death() {
    auto* hdr = header();
    if (!hdr) {
        return core::Result<void>::err(core::ErrorCode::InvalidState, "Invalid header in recovery");
    }

    if (hdr->magic != MAGIC || hdr->version != VERSION ||
        hdr->capacity == 0 || hdr->capacity > MAX_CAPACITY ||
        hdr->slot_size == 0 || hdr->slot_size > MAX_SLOT_SIZE) {
        hdr->state = static_cast<uint32_t>(RingBufferState::Corrupted);
        return core::Result<void>::err(core::ErrorCode::InvalidState, "Ring buffer header corrupted");
    }

    if (hdr->tail < hdr->head) {
        hdr->state = static_cast<uint32_t>(RingBufferState::Corrupted);
        return core::Result<void>::err(core::ErrorCode::InvalidState, "Ring buffer corrupted: tail < head");
    }

    // Boundary Case 1: In-progress push died BEFORE tail++
    // Slot at hdr->tail % hdr->capacity is in Writing state.
    size_t tail_idx = hdr->tail % hdr->capacity;
    SlotHeader* cur_write_slot = get_slot(tail_idx);
    if (cur_write_slot && cur_write_slot->state == static_cast<uint32_t>(SlotState::Writing)) {
        cur_write_slot->state = static_cast<uint32_t>(SlotState::Empty);
        cur_write_slot->payload_size = 0;
        cur_write_slot->sequence = 0;
        std::memset(get_payload(cur_write_slot), 0, hdr->slot_size);
    }

    // Boundary Case 2: In-progress push died AFTER tail++ (and count++) before Committed
    // Slot at (hdr->tail - 1) % hdr->capacity is in Writing state.
    if (hdr->tail > hdr->head) {
        size_t prev_tail_idx = (hdr->tail - 1) % hdr->capacity;
        SlotHeader* prev_write_slot = get_slot(prev_tail_idx);
        if (prev_write_slot && prev_write_slot->state == static_cast<uint32_t>(SlotState::Writing)) {
            hdr->tail--;
            if (hdr->count > 0) {
                hdr->count--;
            }
            if (hdr->next_push_seq > 0) {
                hdr->next_push_seq--;
            }
            prev_write_slot->state = static_cast<uint32_t>(SlotState::Empty);
            prev_write_slot->payload_size = 0;
            prev_write_slot->sequence = 0;
            std::memset(get_payload(prev_write_slot), 0, hdr->slot_size);
        }
    }

    // Boundary Case 3: In-progress pop died BEFORE head++
    // Slot at hdr->head % hdr->capacity is in Reading state.
    // At-least-once guarantee: restore to Committed so another consumer receives message.
    size_t head_idx = hdr->head % hdr->capacity;
    SlotHeader* cur_read_slot = get_slot(head_idx);
    if (cur_read_slot && cur_read_slot->state == static_cast<uint32_t>(SlotState::Reading)) {
        cur_read_slot->state = static_cast<uint32_t>(SlotState::Committed);
    }

    // Boundary Case 4: In-progress pop died AFTER head++ (and count--) before Empty
    // Slot at (hdr->head - 1) % hdr->capacity is in Reading state.
    if (hdr->head > 0) {
        size_t prev_head_idx = (hdr->head - 1) % hdr->capacity;
        SlotHeader* prev_read_slot = get_slot(prev_head_idx);
        if (prev_read_slot && prev_read_slot->state == static_cast<uint32_t>(SlotState::Reading)) {
            prev_read_slot->state = static_cast<uint32_t>(SlotState::Empty);
            prev_read_slot->payload_size = 0;
            prev_read_slot->sequence = 0;
            std::memset(get_payload(prev_read_slot), 0, hdr->slot_size);
        }
    }

    // Invariant Verification
    uint64_t active_count = hdr->tail - hdr->head;
    if (active_count > hdr->capacity) {
        hdr->state = static_cast<uint32_t>(RingBufferState::Corrupted);
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer recovery failed: active slot count > capacity");
    }
    hdr->count = static_cast<uint32_t>(active_count);

    // Verify all slots in [head, tail) are strictly Committed
    for (uint64_t seq = hdr->head; seq < hdr->tail; ++seq) {
        size_t idx = seq % hdr->capacity;
        SlotHeader* s = get_slot(idx);
        if (!s || s->state != static_cast<uint32_t>(SlotState::Committed)) {
            hdr->state = static_cast<uint32_t>(RingBufferState::Corrupted);
            return core::Result<void>::err(
                core::ErrorCode::InvalidState,
                "Ring buffer recovery failed: active slot is not Committed");
        }
    }

    // Verify all slots outside [head, tail) are strictly Empty
    for (size_t i = 0; i < hdr->capacity; ++i) {
        bool in_active_range = false;
        for (uint64_t seq = hdr->head; seq < hdr->tail; ++seq) {
            if ((seq % hdr->capacity) == i) {
                in_active_range = true;
                break;
            }
        }
        if (!in_active_range) {
            SlotHeader* s = get_slot(i);
            if (!s || s->state != static_cast<uint32_t>(SlotState::Empty)) {
                hdr->state = static_cast<uint32_t>(RingBufferState::Corrupted);
                return core::Result<void>::err(
                    core::ErrorCode::InvalidState,
                    "Ring buffer recovery failed: inactive slot is not Empty");
            }
        }
    }

    auto c_res = mutex_.make_consistent();
    if (!c_res.is_ok()) {
        hdr->state = static_cast<uint32_t>(RingBufferState::Corrupted);
        return c_res;
    }

    not_empty_.notify_all();
    not_full_.notify_all();

    return core::Result<void>::ok();
}

core::Result<void> SharedRingBuffer::push(
    const void* data,
    size_t size,
    std::chrono::milliseconds timeout) {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer is not initialized or invalid");
    }
    if (!data && size > 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Push data pointer is null");
    }
    auto* hdr = header();
    if (size > hdr->slot_size) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Message size exceeds configured slot size");
    }

    auto start_time = std::chrono::steady_clock::now();
    bool has_timeout = (timeout.count() >= 0);

    core::Result<LockAcquireStatus> lock_res = has_timeout
        ? mutex_.timed_lock(timeout)
        : mutex_.lock();

    if (!lock_res.is_ok()) {
        return core::Result<void>::err(lock_res.error());
    }

    if (lock_res.value() == LockAcquireStatus::TimedOut) {
        return core::Result<void>::err(
            core::ErrorCode::Timeout,
            "Timed out waiting to acquire mutex for push");
    }

    if (lock_res.value() == LockAcquireStatus::OwnerDead) {
        auto rec = recover_after_owner_death();
        if (!rec.is_ok()) {
            mutex_.unlock();
            return core::Result<void>::err(rec.error());
        }
    }

    while (hdr->count == hdr->capacity && !hdr->is_shutdown &&
           hdr->state != static_cast<uint32_t>(RingBufferState::Corrupted)) {
        if (has_timeout) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            if (elapsed >= timeout) {
                mutex_.unlock();
                return core::Result<void>::err(
                    core::ErrorCode::Timeout,
                    "Ring buffer push timed out waiting for available slot");
            }
            auto remaining = timeout - elapsed;
            auto wait_res = not_full_.wait_for(mutex_, remaining);
            if (!wait_res.is_ok()) {
                mutex_.unlock();
                return core::Result<void>::err(wait_res.error());
            }
            if (wait_res.value() == LockAcquireStatus::OwnerDead) {
                auto rec = recover_after_owner_death();
                if (!rec.is_ok()) {
                    mutex_.unlock();
                    return core::Result<void>::err(rec.error());
                }
            }
        } else {
            auto wait_res = not_full_.wait(mutex_);
            if (!wait_res.is_ok()) {
                mutex_.unlock();
                return core::Result<void>::err(wait_res.error());
            }
            if (wait_res.value() == LockAcquireStatus::OwnerDead) {
                auto rec = recover_after_owner_death();
                if (!rec.is_ok()) {
                    mutex_.unlock();
                    return core::Result<void>::err(rec.error());
                }
            }
        }
    }

    if (hdr->is_shutdown) {
        mutex_.unlock();
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer has been shut down");
    }

    if (hdr->state == static_cast<uint32_t>(RingBufferState::Corrupted)) {
        mutex_.unlock();
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer is corrupted");
    }

    size_t slot_idx = hdr->tail % hdr->capacity;
    SlotHeader* slot = get_slot(slot_idx);
    slot->state = static_cast<uint32_t>(SlotState::Writing);

    if (size > 0) {
        std::memcpy(get_payload(slot), data, size);
    }
    slot->payload_size = static_cast<uint32_t>(size);
    slot->sequence = ++hdr->next_push_seq;

    hdr->tail++;
    hdr->count++;
    slot->state = static_cast<uint32_t>(SlotState::Committed);

    not_empty_.notify_one();
    mutex_.unlock();
    return core::Result<void>::ok();
}

core::Result<void> SharedRingBuffer::push(
    std::string_view message,
    std::chrono::milliseconds timeout) {
    return push(message.data(), message.size(), timeout);
}

core::Result<size_t> SharedRingBuffer::pop(
    void* buffer,
    size_t buffer_capacity,
    std::chrono::milliseconds timeout) {
    if (!is_valid()) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer is not initialized or invalid");
    }
    if (!buffer && buffer_capacity > 0) {
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidArgument,
            "Pop buffer pointer is null");
    }
    auto* hdr = header();

    auto start_time = std::chrono::steady_clock::now();
    bool has_timeout = (timeout.count() >= 0);

    core::Result<LockAcquireStatus> lock_res = has_timeout
        ? mutex_.timed_lock(timeout)
        : mutex_.lock();

    if (!lock_res.is_ok()) {
        return core::Result<size_t>::err(lock_res.error());
    }

    if (lock_res.value() == LockAcquireStatus::TimedOut) {
        return core::Result<size_t>::err(
            core::ErrorCode::Timeout,
            "Timed out waiting to acquire mutex for pop");
    }

    if (lock_res.value() == LockAcquireStatus::OwnerDead) {
        auto rec = recover_after_owner_death();
        if (!rec.is_ok()) {
            mutex_.unlock();
            return core::Result<size_t>::err(rec.error());
        }
    }

    while (hdr->count == 0 && !hdr->is_shutdown &&
           hdr->state != static_cast<uint32_t>(RingBufferState::Corrupted)) {
        if (has_timeout) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            if (elapsed >= timeout) {
                mutex_.unlock();
                return core::Result<size_t>::err(
                    core::ErrorCode::Timeout,
                    "Ring buffer pop timed out waiting for message");
            }
            auto remaining = timeout - elapsed;
            auto wait_res = not_empty_.wait_for(mutex_, remaining);
            if (!wait_res.is_ok()) {
                mutex_.unlock();
                return core::Result<size_t>::err(wait_res.error());
            }
            if (wait_res.value() == LockAcquireStatus::OwnerDead) {
                auto rec = recover_after_owner_death();
                if (!rec.is_ok()) {
                    mutex_.unlock();
                    return core::Result<size_t>::err(rec.error());
                }
            }
        } else {
            auto wait_res = not_empty_.wait(mutex_);
            if (!wait_res.is_ok()) {
                mutex_.unlock();
                return core::Result<size_t>::err(wait_res.error());
            }
            if (wait_res.value() == LockAcquireStatus::OwnerDead) {
                auto rec = recover_after_owner_death();
                if (!rec.is_ok()) {
                    mutex_.unlock();
                    return core::Result<size_t>::err(rec.error());
                }
            }
        }
    }

    if (hdr->count == 0 && hdr->is_shutdown) {
        mutex_.unlock();
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer has been shut down and is empty");
    }

    if (hdr->state == static_cast<uint32_t>(RingBufferState::Corrupted)) {
        mutex_.unlock();
        return core::Result<size_t>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer is corrupted");
    }

    size_t slot_idx = hdr->head % hdr->capacity;
    SlotHeader* slot = get_slot(slot_idx);
    slot->state = static_cast<uint32_t>(SlotState::Reading);

    size_t payload_len = slot->payload_size;
    size_t to_copy = std::min(buffer_capacity, payload_len);
    if (to_copy > 0 && buffer) {
        std::memcpy(buffer, get_payload(slot), to_copy);
    }

    hdr->head++;
    hdr->count--;

    slot->state = static_cast<uint32_t>(SlotState::Empty);
    slot->payload_size = 0;
    slot->sequence = 0;

    not_full_.notify_one();
    mutex_.unlock();
    return core::Result<size_t>::ok(payload_len);
}

core::Result<std::string> SharedRingBuffer::pop(
    std::chrono::milliseconds timeout) {
    auto* hdr = header();
    if (!hdr) {
        return core::Result<std::string>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer is not initialized or invalid");
    }

    std::string result;
    result.resize(hdr->slot_size);

    auto res = pop(result.data(), result.size(), timeout);
    if (!res.is_ok()) {
        return core::Result<std::string>::err(res.error());
    }

    result.resize(res.value());
    return core::Result<std::string>::ok(std::move(result));
}

core::Result<void> SharedRingBuffer::shutdown() {
    if (!is_valid()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Ring buffer is not initialized or invalid");
    }
    auto* hdr = header();
    auto lk = mutex_.lock();
    if (!lk.is_ok()) {
        return core::Result<void>::err(lk.error());
    }
    if (lk.value() == LockAcquireStatus::OwnerDead) {
        recover_after_owner_death();
    }

    hdr->is_shutdown = 1;
    if (hdr->state == static_cast<uint32_t>(RingBufferState::Ready)) {
        hdr->state = static_cast<uint32_t>(RingBufferState::Shutdown);
    }

    not_empty_.notify_all();
    not_full_.notify_all();

    mutex_.unlock();
    return core::Result<void>::ok();
}

bool SharedRingBuffer::is_valid() const noexcept {
    return shm_.is_valid() && header() != nullptr;
}

bool SharedRingBuffer::is_owner() const noexcept {
    return shm_.is_owner();
}

bool SharedRingBuffer::is_shutdown() const {
    const auto* hdr = header();
    return hdr ? (hdr->is_shutdown != 0) : false;
}

bool SharedRingBuffer::is_empty() const {
    return size() == 0;
}

bool SharedRingBuffer::is_full() const {
    return size() >= capacity();
}

size_t SharedRingBuffer::capacity() const {
    const auto* hdr = header();
    return hdr ? hdr->capacity : 0;
}

size_t SharedRingBuffer::size() const {
    const auto* hdr = header();
    return hdr ? hdr->count : 0;
}

size_t SharedRingBuffer::slot_size() const {
    const auto* hdr = header();
    return hdr ? hdr->slot_size : 0;
}

const std::string& SharedRingBuffer::name() const noexcept {
    return shm_.name();
}

core::Result<void> SharedRingBuffer::unlink() noexcept {
    return shm_.unlink();
}

} // namespace kernox::system
