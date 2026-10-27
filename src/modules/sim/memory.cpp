#include "kernox/modules/sim/memory.hpp"

#include <algorithm>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>

namespace kernox::sim {

std::string_view to_string(ContiguousStrategy strategy) noexcept {
    switch (strategy) {
        case ContiguousStrategy::FirstFit:
            return "First Fit";
        case ContiguousStrategy::BestFit:
            return "Best Fit";
        case ContiguousStrategy::WorstFit:
            return "Worst Fit";
        case ContiguousStrategy::NextFit:
            return "Next Fit";
    }
    return "Unknown";
}

std::optional<ContiguousStrategy> parse_contiguous_strategy(std::string_view str) noexcept {
    if (str == "first-fit" || str == "firstfit" || str == "ff" || str == "first") {
        return ContiguousStrategy::FirstFit;
    }
    if (str == "best-fit" || str == "bestfit" || str == "bf" || str == "best") {
        return ContiguousStrategy::BestFit;
    }
    if (str == "worst-fit" || str == "worstfit" || str == "wf" || str == "worst") {
        return ContiguousStrategy::WorstFit;
    }
    if (str == "next-fit" || str == "nextfit" || str == "nf" || str == "next") {
        return ContiguousStrategy::NextFit;
    }
    return std::nullopt;
}

std::string_view to_string(PageReplacementAlgo algo) noexcept {
    switch (algo) {
        case PageReplacementAlgo::Fifo:
            return "FIFO (First-In, First-Out)";
        case PageReplacementAlgo::Lru:
            return "LRU (Least Recently Used)";
        case PageReplacementAlgo::Optimal:
            return "Optimal (Bélady's MIN / Offline)";
    }
    return "Unknown";
}

std::optional<PageReplacementAlgo> parse_page_replacement_algo(std::string_view str) noexcept {
    if (str == "fifo" || str == "FIFO") {
        return PageReplacementAlgo::Fifo;
    }
    if (str == "lru" || str == "LRU") {
        return PageReplacementAlgo::Lru;
    }
    if (str == "optimal" || str == "opt" || str == "OPTIMAL" || str == "OPT") {
        return PageReplacementAlgo::Optimal;
    }
    return std::nullopt;
}

// ----------------------------------------------------------------------------
// Contiguous Memory Allocation
// ----------------------------------------------------------------------------
ContiguousMemoryManager::ContiguousMemoryManager(uint64_t total_size) {
    reset(total_size);
}

void ContiguousMemoryManager::reset(uint64_t total_size) {
    total_size_ = total_size;
    next_fit_cursor_ = 0;
    blocks_.clear();
    if (total_size_ > 0) {
        blocks_.push_back(MemoryBlock{0, total_size_, false, 0});
    }
}

uint64_t ContiguousMemoryManager::allocated_size() const noexcept {
    uint64_t sum = 0;
    for (const auto& block : blocks_) {
        if (block.is_allocated) {
            sum += block.size;
        }
    }
    return sum;
}

uint64_t ContiguousMemoryManager::free_size() const noexcept {
    uint64_t sum = 0;
    for (const auto& block : blocks_) {
        if (!block.is_allocated) {
            sum += block.size;
        }
    }
    return sum;
}

core::Result<void> ContiguousMemoryManager::set_blocks(std::vector<MemoryBlock> blocks, uint64_t total_size) {
    if (total_size == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Total memory size must be positive (> 0)");
    }
    total_size_ = total_size;
    blocks_ = std::move(blocks);
    next_fit_cursor_ = 0;
    return validate_invariants();
}

core::Result<void> ContiguousMemoryManager::validate_invariants() const {
    if (total_size_ == 0) {
        if (!blocks_.empty()) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument, "Memory blocks exist but total memory size is 0");
        }
        return core::Result<void>::ok();
    }
    if (blocks_.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Non-zero memory has an empty block list");
    }

    if (blocks_[0].start_address != 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Initial memory block must start at address 0 (got " + std::to_string(blocks_[0].start_address) + ")");
    }

    uint64_t running_total = 0;
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& block = blocks_[i];
        if (block.size == 0) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument, "Block at index " + std::to_string(i) + " has zero size");
        }
        if (UINT64_MAX - block.start_address < block.size) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Arithmetic overflow in address space for block at index " + std::to_string(i));
        }
        if (block.is_allocated && block.process_id == 0) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Block at index " + std::to_string(i) + " is marked allocated but has PID 0");
        }
        if (i + 1 < blocks_.size()) {
            const auto& next = blocks_[i + 1];
            if (block.start_address + block.size != next.start_address) {
                return core::Result<void>::err(
                    core::ErrorCode::InvalidArgument,
                    "Discontiguous gap or overlap between block " + std::to_string(i) + " (ends at " +
                    std::to_string(block.start_address + block.size) + ") and block " + std::to_string(i + 1) +
                    " (starts at " + std::to_string(next.start_address) + ")");
            }
            if (!block.is_allocated && !next.is_allocated) {
                return core::Result<void>::err(
                    core::ErrorCode::InvalidArgument,
                    "Uncoalesced adjacent free blocks detected at indices " + std::to_string(i) + " and " +
                    std::to_string(i + 1));
            }
        }
        running_total += block.size;
    }

    if (running_total != total_size_) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Total sum of block sizes (" + std::to_string(running_total) +
            ") does not match configured memory capacity (" + std::to_string(total_size_) + ")");
    }

    return core::Result<void>::ok();
}

void ContiguousMemoryManager::coalesce_free_blocks() {
    if (blocks_.size() < 2) return;

    size_t i = 0;
    while (i + 1 < blocks_.size()) {
        if (!blocks_[i].is_allocated && !blocks_[i + 1].is_allocated) {
            blocks_[i].size += blocks_[i + 1].size;
            blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(i + 1));
            if (next_fit_cursor_ > i + 1) {
                next_fit_cursor_--;
            } else if (next_fit_cursor_ == i + 1) {
                next_fit_cursor_ = i;
            }
        } else {
            i++;
        }
    }

    if (next_fit_cursor_ >= blocks_.size()) {
        next_fit_cursor_ = 0;
    }
}

core::Result<AllocationResult> ContiguousMemoryManager::allocate(
    ContiguousStrategy strategy, uint32_t process_id, uint64_t size) {
    if (size == 0) {
        return core::Result<AllocationResult>::err(
            core::ErrorCode::InvalidArgument, "Allocation size must be positive (> 0)");
    }
    if (process_id == 0) {
        return core::Result<AllocationResult>::err(
            core::ErrorCode::InvalidArgument, "Process ID must be non-zero (> 0)");
    }
    auto val_res = validate_invariants();
    if (val_res.is_err()) {
        return core::Result<AllocationResult>::err(val_res.error());
    }

    size_t candidate_idx = SIZE_MAX;
    uint64_t best_diff = UINT64_MAX;
    uint64_t largest_free = 0;

    switch (strategy) {
        case ContiguousStrategy::FirstFit: {
            for (size_t i = 0; i < blocks_.size(); ++i) {
                if (!blocks_[i].is_allocated) {
                    if (blocks_[i].size > largest_free) largest_free = blocks_[i].size;
                    if (blocks_[i].size >= size) {
                        candidate_idx = i;
                        break;
                    }
                }
            }
            break;
        }
        case ContiguousStrategy::BestFit: {
            for (size_t i = 0; i < blocks_.size(); ++i) {
                if (!blocks_[i].is_allocated) {
                    if (blocks_[i].size > largest_free) largest_free = blocks_[i].size;
                    if (blocks_[i].size >= size) {
                        uint64_t diff = blocks_[i].size - size;
                        if (diff < best_diff) {
                            best_diff = diff;
                            candidate_idx = i;
                        }
                    }
                }
            }
            break;
        }
        case ContiguousStrategy::WorstFit: {
            uint64_t worst_diff = 0;
            bool found = false;
            for (size_t i = 0; i < blocks_.size(); ++i) {
                if (!blocks_[i].is_allocated) {
                    if (blocks_[i].size > largest_free) largest_free = blocks_[i].size;
                    if (blocks_[i].size >= size) {
                        uint64_t diff = blocks_[i].size - size;
                        if (!found || diff > worst_diff) {
                            worst_diff = diff;
                            candidate_idx = i;
                            found = true;
                        }
                    }
                }
            }
            break;
        }
        case ContiguousStrategy::NextFit: {
            if (next_fit_cursor_ >= blocks_.size()) {
                next_fit_cursor_ = 0;
            }
            size_t n = blocks_.size();
            for (size_t step = 0; step < n; ++step) {
                size_t i = (next_fit_cursor_ + step) % n;
                if (!blocks_[i].is_allocated) {
                    if (blocks_[i].size > largest_free) largest_free = blocks_[i].size;
                    if (blocks_[i].size >= size) {
                        candidate_idx = i;
                        break;
                    }
                }
            }
            break;
        }
    }

    AllocationResult result{};
    if (candidate_idx == SIZE_MAX) {
        result.success = false;
        result.message = "Out of Memory: Cannot allocate " + std::to_string(size) +
                         " bytes for Process P" + std::to_string(process_id) +
                         " (largest available free block: " + std::to_string(largest_free) + " bytes)";
        return core::Result<AllocationResult>::ok(result);
    }

    MemoryBlock chosen = blocks_[candidate_idx];
    if (chosen.size == size) {
        blocks_[candidate_idx].is_allocated = true;
        blocks_[candidate_idx].process_id = process_id;
        result.allocated_block = blocks_[candidate_idx];
        result.block_index = candidate_idx;
        if (strategy == ContiguousStrategy::NextFit) {
            next_fit_cursor_ = (candidate_idx + 1) % blocks_.size();
        }
    } else {
        // Split block
        MemoryBlock alloc_block{chosen.start_address, size, true, process_id};
        MemoryBlock free_block{chosen.start_address + size, chosen.size - size, false, 0};
        blocks_[candidate_idx] = alloc_block;
        blocks_.insert(blocks_.begin() + static_cast<std::ptrdiff_t>(candidate_idx + 1), free_block);
        result.allocated_block = alloc_block;
        result.block_index = candidate_idx;
        if (strategy == ContiguousStrategy::NextFit) {
            // Next Fit cursor advances to the remainder free block
            next_fit_cursor_ = (candidate_idx + 1) % blocks_.size();
        } else {
            // A new block was inserted at candidate_idx + 1. If cursor was past this, shift it.
            if (next_fit_cursor_ > candidate_idx) {
                next_fit_cursor_ = (next_fit_cursor_ + 1) % blocks_.size();
            }
        }
    }

    result.success = true;
    result.message = "Allocated " + std::to_string(size) + " bytes to Process P" +
                     std::to_string(process_id) + " at address [0x" +
                     [&]() {
                         std::ostringstream ss;
                         ss << std::hex << result.allocated_block.start_address;
                         return ss.str();
                     }() + ", 0x" +
                     [&]() {
                         std::ostringstream ss;
                         ss << std::hex << (result.allocated_block.start_address + result.allocated_block.size);
                         return ss.str();
                     }() + ") via " + std::string(to_string(strategy));

    return core::Result<AllocationResult>::ok(result);
}

core::Result<DeallocationResult> ContiguousMemoryManager::deallocate(uint32_t process_id) {
    if (process_id == 0) {
        return core::Result<DeallocationResult>::err(
            core::ErrorCode::InvalidArgument, "Cannot deallocate process ID 0 (reserved for free blocks)");
    }

    size_t blocks_freed = 0;
    uint64_t bytes_freed = 0;
    size_t before_count = blocks_.size();

    for (auto& block : blocks_) {
        if (block.is_allocated && block.process_id == process_id) {
            block.is_allocated = false;
            block.process_id = 0;
            bytes_freed += block.size;
            blocks_freed++;
        }
    }

    DeallocationResult res{};
    if (blocks_freed == 0) {
        res.success = false;
        res.message = "Process P" + std::to_string(process_id) + " holds no allocated memory blocks.";
        return core::Result<DeallocationResult>::ok(res);
    }

    coalesce_free_blocks();
    size_t after_count = blocks_.size();
    size_t merges = (before_count > after_count) ? (before_count - after_count) : 0;

    res.success = true;
    res.blocks_freed = blocks_freed;
    res.bytes_freed = bytes_freed;
    res.merges_performed = merges;
    res.message = "Deallocated " + std::to_string(blocks_freed) + " block(s) (" +
                  std::to_string(bytes_freed) + " bytes) from Process P" +
                  std::to_string(process_id) + "; performed " + std::to_string(merges) +
                  " coalescing merge(s).";

    return core::Result<DeallocationResult>::ok(res);
}

core::Result<DeallocationResult> ContiguousMemoryManager::deallocate_address(uint64_t start_address) {
    size_t before_count = blocks_.size();
    bool found = false;
    uint32_t pid = 0;
    uint64_t bytes = 0;

    for (auto& block : blocks_) {
        if (block.start_address == start_address) {
            if (!block.is_allocated) {
                DeallocationResult res{};
                res.success = false;
                res.message = "Memory block at address " + std::to_string(start_address) + " is already free.";
                return core::Result<DeallocationResult>::ok(res);
            }
            pid = block.process_id;
            bytes = block.size;
            block.is_allocated = false;
            block.process_id = 0;
            found = true;
            break;
        }
    }

    DeallocationResult res{};
    if (!found) {
        res.success = false;
        res.message = "No memory block found starting at address " + std::to_string(start_address);
        return core::Result<DeallocationResult>::ok(res);
    }

    coalesce_free_blocks();
    size_t after_count = blocks_.size();
    size_t merges = (before_count > after_count) ? (before_count - after_count) : 0;

    res.success = true;
    res.blocks_freed = 1;
    res.bytes_freed = bytes;
    res.merges_performed = merges;
    res.message = "Deallocated block at address " + std::to_string(start_address) +
                  " (Process P" + std::to_string(pid) + ", " + std::to_string(bytes) +
                  " bytes); performed " + std::to_string(merges) + " coalescing merge(s).";

    return core::Result<DeallocationResult>::ok(res);
}

// ----------------------------------------------------------------------------
// Paging and Address Translation
// ----------------------------------------------------------------------------
core::Result<void> PagingConfig::validate() const {
    if (page_size == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Page size must be positive (> 0)");
    }
    // Check power of 2
    if ((page_size & (page_size - 1)) != 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Page size must be a power of 2 (e.g. 4096)");
    }
    if (logical_address_space_size == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Logical address space size must be positive (> 0)");
    }
    if (physical_memory_size == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Physical memory size must be positive (> 0)");
    }
    if (logical_address_space_size % page_size != 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Logical address space size must be a multiple of page size");
    }
    if (physical_memory_size % page_size != 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Physical memory size must be a multiple of page size");
    }
    if (logical_address_space_size / page_size > UINT32_MAX) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Logical page count exceeds 32-bit addressing capacity");
    }
    if (physical_memory_size / page_size > UINT32_MAX) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Physical frame count exceeds 32-bit addressing capacity");
    }
    return core::Result<void>::ok();
}

PagingSystem::PagingSystem(PagingConfig config) : config_(config) {}

core::Result<void> PagingSystem::set_page_table(uint32_t process_id, std::vector<PageTableEntry> table) {
    auto val_res = config_.validate();
    if (val_res.is_err()) {
        return val_res;
    }
    if (table.size() != config_.page_count()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Page table size (" + std::to_string(table.size()) +
            ") must match total page count (" + std::to_string(config_.page_count()) + ")");
    }
    for (size_t p = 0; p < table.size(); ++p) {
        if (table[p].valid && table[p].frame_number >= config_.frame_count()) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Page " + std::to_string(p) + " references invalid frame " +
                std::to_string(table[p].frame_number) + " (physical frames available: " +
                std::to_string(config_.frame_count()) + ")");
        }
    }
    page_tables_[process_id] = std::move(table);
    return core::Result<void>::ok();
}

core::Result<void> PagingSystem::set_page_entry(
    uint32_t process_id, uint64_t page_number, PageTableEntry entry) {
    auto val_res = config_.validate();
    if (val_res.is_err()) {
        return val_res;
    }
    if (page_number >= config_.page_count()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Page number out of logical address space range");
    }
    if (entry.valid && entry.frame_number >= config_.frame_count()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument, "Page table entry references frame out of physical memory range");
    }

    auto it = page_tables_.find(process_id);
    if (it == page_tables_.end()) {
        std::vector<PageTableEntry> default_table(config_.page_count(), PageTableEntry{});
        default_table[page_number] = entry;
        page_tables_[process_id] = std::move(default_table);
    } else {
        it->second[page_number] = entry;
    }
    return core::Result<void>::ok();
}

const std::vector<PageTableEntry>* PagingSystem::get_page_table(uint32_t process_id) const {
    auto it = page_tables_.find(process_id);
    if (it != page_tables_.end()) {
        return &it->second;
    }
    return nullptr;
}

core::Result<TranslationResult> PagingSystem::translate(
    uint32_t process_id, uint64_t logical_address) const {
    auto val_res = config_.validate();
    if (val_res.is_err()) {
        return core::Result<TranslationResult>::err(val_res.error());
    }

    TranslationResult res{};
    res.logical_address = logical_address;

    // Check logical boundary
    if (logical_address >= config_.logical_address_space_size) {
        res.success = false;
        res.is_page_fault = false;
        std::ostringstream ss;
        ss << "Segmentation Fault: Logical address 0x" << std::hex << logical_address
           << " exceeds configured address space limit 0x" << config_.logical_address_space_size;
        res.status_message = ss.str();
        return core::Result<TranslationResult>::ok(res);
    }

    res.page_number = logical_address / config_.page_size;
    res.offset = logical_address % config_.page_size;

    auto it = page_tables_.find(process_id);
    if (it == page_tables_.end() || res.page_number >= it->second.size()) {
        res.success = false;
        res.is_page_fault = true;
        res.status_message = "Page Fault: No page table exists for Process P" + std::to_string(process_id);
        return core::Result<TranslationResult>::ok(res);
    }

    const auto& pte = it->second[res.page_number];
    if (!pte.valid) {
        res.success = false;
        res.is_page_fault = true;
        res.status_message = "Page Fault: Page " + std::to_string(res.page_number) +
                             " is not resident in physical memory (Valid bit = 0)";
        return core::Result<TranslationResult>::ok(res);
    }

    if (pte.frame_number >= config_.frame_count()) {
        res.success = false;
        res.is_page_fault = false;
        res.status_message = "Invalid Frame: Page " + std::to_string(res.page_number) +
                             " mapped to frame " + std::to_string(pte.frame_number) +
                             " which exceeds total physical frames (" +
                             std::to_string(config_.frame_count()) + ")";
        return core::Result<TranslationResult>::ok(res);
    }

    uint64_t frame_base = static_cast<uint64_t>(pte.frame_number) * config_.page_size;
    if (UINT64_MAX - frame_base < res.offset) {
        res.success = false;
        res.is_page_fault = false;
        res.status_message = "Address Overflow: Physical address calculation overflows 64-bit address space";
        return core::Result<TranslationResult>::ok(res);
    }

    res.frame_number = pte.frame_number;
    res.physical_address = frame_base + res.offset;
    res.success = true;
    res.is_page_fault = false;

    std::ostringstream ss;
    ss << "Translation Successful: Logical address 0x" << std::hex << logical_address
       << " [Page " << std::dec << res.page_number << ", Offset 0x" << std::hex << res.offset
       << "] -> Physical address 0x" << res.physical_address << " [Frame " << std::dec
       << res.frame_number << "]";
    res.status_message = ss.str();

    return core::Result<TranslationResult>::ok(res);
}

// ----------------------------------------------------------------------------
// Page Replacement Algorithms
// ----------------------------------------------------------------------------
core::Result<ReplacementResult> PageReplacementSimulator::run(
    PageReplacementAlgo algo, const std::vector<uint32_t>& reference_string, size_t frame_count) {
    if (frame_count == 0) {
        return core::Result<ReplacementResult>::err(
            core::ErrorCode::InvalidArgument, "Physical frame count must be positive (> 0)");
    }
    if (reference_string.empty()) {
        return core::Result<ReplacementResult>::err(
            core::ErrorCode::InvalidArgument, "Page reference string cannot be empty");
    }

    ReplacementResult result{};
    result.algorithm = algo;
    result.frame_count = frame_count;
    result.total_references = reference_string.size();
    result.trace.reserve(reference_string.size());

    std::vector<std::optional<uint32_t>> frames(frame_count, std::nullopt);

    // FIFO bookkeeping: queue of frame indices in arrival order
    std::queue<size_t> fifo_queue;

    // LRU bookkeeping: last access step per frame index
    std::vector<size_t> last_access(frame_count, 0);

    for (size_t step = 0; step < reference_string.size(); ++step) {
        uint32_t page = reference_string[step];
        ReplacementStep r_step{};
        r_step.step_index = step + 1;
        r_step.page = page;

        // Check for page hit
        bool hit = false;
        size_t hit_frame = 0;
        for (size_t f = 0; f < frame_count; ++f) {
            if (frames[f].has_value() && *frames[f] == page) {
                hit = true;
                hit_frame = f;
                break;
            }
        }

        if (hit) {
            result.page_hits++;
            r_step.is_hit = true;
            r_step.frame_index = hit_frame;
            r_step.action = "HIT";
            if (algo == PageReplacementAlgo::Lru) {
                last_access[hit_frame] = step;
            }
        } else {
            // Page fault
            result.page_faults++;
            r_step.is_hit = false;

            // Check if there is an empty frame
            size_t empty_frame = SIZE_MAX;
            for (size_t f = 0; f < frame_count; ++f) {
                if (!frames[f].has_value()) {
                    empty_frame = f;
                    break;
                }
            }

            if (empty_frame != SIZE_MAX) {
                // Cold/Compulsory Miss: place in empty frame
                frames[empty_frame] = page;
                r_step.frame_index = empty_frame;
                r_step.action = "FAULT (ALLOC)";
                if (algo == PageReplacementAlgo::Fifo) {
                    fifo_queue.push(empty_frame);
                } else if (algo == PageReplacementAlgo::Lru) {
                    last_access[empty_frame] = step;
                }
            } else {
                // Replacement needed
                size_t victim_frame = 0;
                switch (algo) {
                    case PageReplacementAlgo::Fifo: {
                        victim_frame = fifo_queue.front();
                        fifo_queue.pop();
                        fifo_queue.push(victim_frame);
                        break;
                    }
                    case PageReplacementAlgo::Lru: {
                        size_t oldest_time = SIZE_MAX;
                        for (size_t f = 0; f < frame_count; ++f) {
                            if (last_access[f] < oldest_time) {
                                oldest_time = last_access[f];
                                victim_frame = f;
                            }
                        }
                        last_access[victim_frame] = step;
                        break;
                    }
                    case PageReplacementAlgo::Optimal: {
                        size_t farthest_next_use = 0;
                        victim_frame = 0;

                        for (size_t f = 0; f < frame_count; ++f) {
                            uint32_t resident_page = *frames[f];
                            size_t next_use = SIZE_MAX;

                            for (size_t future = step + 1; future < reference_string.size(); ++future) {
                                if (reference_string[future] == resident_page) {
                                    next_use = future;
                                    break;
                                }
                            }

                            if (next_use == SIZE_MAX) {
                                // Page is never referenced again: prime victim
                                victim_frame = f;
                                break; // Lowest index tie-breaking
                            }

                            if (next_use > farthest_next_use) {
                                farthest_next_use = next_use;
                                victim_frame = f;
                            }
                        }
                        break;
                    }
                }

                r_step.evicted_page = frames[victim_frame];
                frames[victim_frame] = page;
                r_step.frame_index = victim_frame;
                r_step.action = "FAULT (REPLACE P" + std::to_string(*r_step.evicted_page) + ")";
            }
        }

        r_step.frames = frames;
        result.trace.push_back(std::move(r_step));
    }

    result.fault_rate = static_cast<double>(result.page_faults) / static_cast<double>(result.total_references);
    result.hit_rate = static_cast<double>(result.page_hits) / static_cast<double>(result.total_references);

    std::ostringstream ss;
    ss << to_string(algo) << ": " << result.page_faults << " faults, " << result.page_hits
       << " hits over " << result.total_references << " references (Fault Rate: "
       << std::fixed << std::setprecision(2) << (result.fault_rate * 100.0) << "%, Hit Rate: "
       << (result.hit_rate * 100.0) << "%)";
    result.summary = ss.str();

    return core::Result<ReplacementResult>::ok(std::move(result));
}

core::Result<WorkingSetResult> PageReplacementSimulator::run_working_set(
    const std::vector<uint32_t>& reference_string, size_t window_size) {
    if (window_size == 0) {
        return core::Result<WorkingSetResult>::err(
            core::ErrorCode::InvalidArgument, "Working set window size must be positive (> 0)");
    }
    if (reference_string.empty()) {
        return core::Result<WorkingSetResult>::err(
            core::ErrorCode::InvalidArgument, "Page reference string cannot be empty");
    }

    WorkingSetResult res{};
    res.window_size = window_size;
    res.steps.reserve(reference_string.size());

    uint64_t total_size = 0;
    size_t peak_size = 0;

    for (size_t t = 0; t < reference_string.size(); ++t) {
        WorkingSetStep step{};
        step.time_step = t + 1;
        step.referenced_page = reference_string[t];

        size_t start = (t >= window_size) ? (t - window_size + 1) : 0;
        std::set<uint32_t> unique_pages;
        for (size_t i = start; i <= t; ++i) {
            unique_pages.insert(reference_string[i]);
        }

        step.working_set.assign(unique_pages.begin(), unique_pages.end());
        step.working_set_size = unique_pages.size();
        total_size += step.working_set_size;
        if (step.working_set_size > peak_size) {
            peak_size = step.working_set_size;
        }

        res.steps.push_back(std::move(step));
    }

    res.peak_working_set_size = peak_size;
    res.average_working_set_size =
        static_cast<double>(total_size) / static_cast<double>(reference_string.size());

    return core::Result<WorkingSetResult>::ok(std::move(res));
}

// ----------------------------------------------------------------------------
// Built-in Deterministic Workloads
// ----------------------------------------------------------------------------
ContiguousMemoryManager MemoryEngine::get_contiguous_workload() {
    // Canonical textbook memory layout: 1000 KB total
    // Initial blocks:
    // [0, 100): Free (100 KB)
    // [100, 300): P1 (200 KB)
    // [300, 600): Free (300 KB)
    // [600, 700): P2 (100 KB)
    // [700, 900): Free (200 KB)
    // [900, 1000): P3 (100 KB)
    ContiguousMemoryManager mgr(1000);
    std::vector<MemoryBlock> blocks = {
        {0, 100, false, 0},
        {100, 200, true, 1},
        {300, 300, false, 0},
        {600, 100, true, 2},
        {700, 200, false, 0},
        {900, 100, true, 3}
    };
    static_cast<void>(mgr.set_blocks(std::move(blocks), 1000));
    return mgr;
}

PagingSystem MemoryEngine::get_paging_workload() {
    // 64 KB logical address space, 4 KB page size (16 pages)
    // 32 KB physical memory (8 frames)
    PagingConfig cfg{65536, 4096, 32768};
    PagingSystem sys(cfg);

    // Per-process page table for P0:
    // Page 0 -> Frame 5
    // Page 1 -> Frame 2
    // Page 2 -> Frame 7
    // Page 3 -> Frame 1
    // Page 4 -> Invalid (not present)
    // Page 5 -> Frame 0
    std::vector<PageTableEntry> p0_table(16);
    p0_table[0] = {5, true, false, true};
    p0_table[1] = {2, true, true, true};
    p0_table[2] = {7, true, false, true};
    p0_table[3] = {1, true, false, false};
    p0_table[4] = {0, false, false, false}; // Not in memory -> Page Fault
    p0_table[5] = {0, true, false, true};
    for (size_t p = 6; p < 16; ++p) {
        p0_table[p] = {0, false, false, false};
    }

    static_cast<void>(sys.set_page_table(0, std::move(p0_table)));
    return sys;
}

std::vector<uint32_t> MemoryEngine::get_canonical_reference_string() {
    // Bélady's canonical 20-reference string (Silberschatz Chapter 10)
    // 7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1
    return {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
}

// ----------------------------------------------------------------------------
// ASCII Formatters
// ----------------------------------------------------------------------------
std::string MemoryEngine::format_contiguous_memory(const ContiguousMemoryManager& manager) {
    std::ostringstream oss;
    oss << "Contiguous Memory Map (Total Capacity: " << manager.total_size() << " bytes):\n"
        << "  Allocated Memory : " << manager.allocated_size() << " bytes\n"
        << "  Free Memory      : " << manager.free_size() << " bytes\n"
        << "  Next-Fit Cursor  : Block Index " << manager.next_fit_cursor() << "\n\n";

    oss << "+-------+---------------------+---------------------+----------+------------+------------+\n"
        << "| Block |    Start Address    |     End Address     |   Size   |   Status   |  Process   |\n"
        << "+-------+---------------------+---------------------+----------+------------+------------+\n";

    const auto& blocks = manager.blocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& b = blocks[i];
        std::ostringstream start_ss, end_ss, status_ss, proc_ss;
        start_ss << "0x" << std::hex << b.start_address;
        end_ss << "0x" << std::hex << (b.start_address + b.size);
        status_ss << (b.is_allocated ? "ALLOCATED" : "FREE");
        if (b.is_allocated) {
            proc_ss << "P" << b.process_id;
        } else {
            proc_ss << "-";
        }

        oss << "| " << std::left << std::setw(5) << i
            << " | " << std::right << std::setw(19) << start_ss.str()
            << " | " << std::setw(19) << end_ss.str()
            << " | " << std::setw(8) << b.size
            << " | " << std::setw(10) << status_ss.str()
            << " | " << std::setw(10) << proc_ss.str()
            << " |\n";
    }
    oss << "+-------+---------------------+---------------------+----------+------------+------------+\n";
    return oss.str();
}

std::string MemoryEngine::format_allocation_result(const AllocationResult& res) {
    std::ostringstream oss;
    oss << "Allocation Result:\n"
        << "  Status               : " << (res.success ? "SUCCESS" : "FAILED") << "\n"
        << "  Message              : " << res.message << "\n";
    if (res.success) {
        oss << "  Allocated Block      : Start 0x" << std::hex << res.allocated_block.start_address
            << ", Size " << std::dec << res.allocated_block.size << " bytes, Process P"
            << res.allocated_block.process_id << "\n";
    }
    return oss.str();
}

std::string MemoryEngine::format_deallocation_result(const DeallocationResult& res) {
    std::ostringstream oss;
    oss << "Deallocation Result:\n"
        << "  Status               : " << (res.success ? "SUCCESS" : "FAILED") << "\n"
        << "  Blocks Freed         : " << res.blocks_freed << "\n"
        << "  Bytes Reclaimed      : " << res.bytes_freed << " bytes\n"
        << "  Adjacent Merges      : " << res.merges_performed << "\n"
        << "  Message              : " << res.message << "\n";
    return oss.str();
}

std::string MemoryEngine::format_page_table(uint32_t process_id, const std::vector<PageTableEntry>& table) {
    std::ostringstream oss;
    oss << "Page Table for Process P" << process_id << " (" << table.size() << " logical pages):\n"
        << "+------+-------+-------+----------+------------+\n"
        << "| Page | Frame | Valid | Modified | Referenced |\n"
        << "+------+-------+-------+----------+------------+\n";

    for (size_t p = 0; p < table.size(); ++p) {
        const auto& pte = table[p];
        std::ostringstream frame_ss;
        if (pte.valid) {
            frame_ss << pte.frame_number;
        } else {
            frame_ss << "-";
        }

        oss << "| " << std::left << std::setw(4) << p
            << " | " << std::right << std::setw(5) << frame_ss.str()
            << " | " << std::setw(5) << (pte.valid ? "1" : "0")
            << " | " << std::setw(8) << (pte.modified ? "1" : "0")
            << " | " << std::setw(10) << (pte.referenced ? "1" : "0")
            << " |\n";
    }
    oss << "+------+-------+-------+----------+------------+\n";
    return oss.str();
}

std::string MemoryEngine::format_translation(const TranslationResult& res) {
    std::ostringstream oss;
    oss << "Address Translation Analysis:\n"
        << "  Logical Address      : 0x" << std::hex << res.logical_address << " (" << std::dec << res.logical_address << ")\n"
        << "  Page Number          : " << res.page_number << "\n"
        << "  Page Offset          : 0x" << std::hex << res.offset << " (" << std::dec << res.offset << ")\n"
        << "  Status               : " << (res.success ? "TRANSLATED" : (res.is_page_fault ? "PAGE FAULT" : "SEGMENTATION FAULT")) << "\n";
    if (res.success) {
        oss << "  Mapped Frame         : " << res.frame_number << "\n"
            << "  Physical Address     : 0x" << std::hex << res.physical_address << " (" << std::dec << res.physical_address << ")\n";
    }
    oss << "  Diagnostic           : " << res.status_message << "\n";
    return oss.str();
}

std::string MemoryEngine::format_replacement_trace(const ReplacementResult& res) {
    std::ostringstream oss;
    oss << "Page Replacement Trace (" << to_string(res.algorithm) << ", " << res.frame_count << " Frames):\n"
        << "+------+------+";
    for (size_t f = 0; f < res.frame_count; ++f) {
        oss << "--------+";
    }
    oss << "---------+-----------------------+\n"
        << "| Step | Page |";
    for (size_t f = 0; f < res.frame_count; ++f) {
        oss << " Frame " << f << "|";
    }
    oss << " Result  | Action                |\n"
        << "+------+------+";
    for (size_t f = 0; f < res.frame_count; ++f) {
        oss << "--------+";
    }
    oss << "---------+-----------------------+\n";

    for (const auto& step : res.trace) {
        oss << "| " << std::left << std::setw(4) << step.step_index
            << " | " << std::right << std::setw(4) << step.page << " |";

        for (size_t f = 0; f < res.frame_count; ++f) {
            std::ostringstream frame_ss;
            if (f < step.frames.size() && step.frames[f].has_value()) {
                frame_ss << *step.frames[f];
            } else {
                frame_ss << "-";
            }
            oss << " " << std::setw(6) << frame_ss.str() << " |";
        }

        oss << " " << std::setw(7) << (step.is_hit ? "HIT" : "FAULT")
            << " | " << std::left << std::setw(21) << step.action
            << " |\n";
    }

    oss << "+------+------+";
    for (size_t f = 0; f < res.frame_count; ++f) {
        oss << "--------+";
    }
    oss << "---------+-----------------------+\n";
    return oss.str();
}

std::string MemoryEngine::format_replacement_summary(const ReplacementResult& res) {
    std::ostringstream oss;
    oss << "Page Replacement Performance Summary:\n"
        << "  Algorithm            : " << to_string(res.algorithm) << "\n"
        << "  Total References     : " << res.total_references << "\n"
        << "  Allocated Frames     : " << res.frame_count << "\n"
        << "  Page Faults          : " << res.page_faults << "\n"
        << "  Page Hits            : " << res.page_hits << "\n"
        << "  Fault Ratio          : " << std::fixed << std::setprecision(4) << res.fault_rate
        << " (" << std::setprecision(2) << (res.fault_rate * 100.0) << "%)\n"
        << "  Hit Ratio            : " << std::fixed << std::setprecision(4) << res.hit_rate
        << " (" << std::setprecision(2) << (res.hit_rate * 100.0) << "%)\n";
    return oss.str();
}

std::string MemoryEngine::format_working_set(const WorkingSetResult& res) {
    std::ostringstream oss;
    oss << "Working-Set Analysis (Window Size Delta = " << res.window_size << "):\n"
        << "+------+------+--------------------+---------+\n"
        << "| Time | Page | Working Set W(t,d) | |W(t,d)| |\n"
        << "+------+------+--------------------+---------+\n";

    for (const auto& step : res.steps) {
        std::ostringstream ws_ss;
        ws_ss << "{";
        for (size_t k = 0; k < step.working_set.size(); ++k) {
            ws_ss << step.working_set[k] << (k + 1 < step.working_set.size() ? ", " : "");
        }
        ws_ss << "}";

        oss << "| " << std::left << std::setw(4) << step.time_step
            << " | " << std::right << std::setw(4) << step.referenced_page
            << " | " << std::setw(18) << ws_ss.str()
            << " | " << std::setw(7) << step.working_set_size
            << " |\n";
    }
    oss << "+------+------+--------------------+---------+\n"
        << "  Average Working Set Size : " << std::fixed << std::setprecision(2) << res.average_working_set_size << " pages\n"
        << "  Peak Working Set Size    : " << res.peak_working_set_size << " pages\n";
    return oss.str();
}

} // namespace kernox::sim
