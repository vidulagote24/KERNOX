#pragma once

#include "kernox/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::sim {

// ----------------------------------------------------------------------------
// Enums and Conversions
// ----------------------------------------------------------------------------
enum class ContiguousStrategy {
    FirstFit,
    BestFit,
    WorstFit,
    NextFit
};

[[nodiscard]] std::string_view to_string(ContiguousStrategy strategy) noexcept;
[[nodiscard]] std::optional<ContiguousStrategy> parse_contiguous_strategy(std::string_view str) noexcept;

enum class PageReplacementAlgo {
    Fifo,
    Lru,
    Optimal
};

[[nodiscard]] std::string_view to_string(PageReplacementAlgo algo) noexcept;
[[nodiscard]] std::optional<PageReplacementAlgo> parse_page_replacement_algo(std::string_view str) noexcept;

// ----------------------------------------------------------------------------
// Contiguous Memory Allocation Model
// ----------------------------------------------------------------------------
struct MemoryBlock {
    uint64_t start_address{0};
    uint64_t size{0};
    bool is_allocated{false};
    uint32_t process_id{0}; // Valid only if is_allocated == true

    [[nodiscard]] bool operator==(const MemoryBlock& other) const noexcept {
        return start_address == other.start_address &&
               size == other.size &&
               is_allocated == other.is_allocated &&
               process_id == other.process_id;
    }
};

struct AllocationResult {
    bool success{false};
    MemoryBlock allocated_block;
    size_t block_index{0};
    std::string message;
};

struct DeallocationResult {
    bool success{false};
    size_t blocks_freed{0};
    uint64_t bytes_freed{0};
    size_t merges_performed{0};
    std::string message;
};

class ContiguousMemoryManager {
public:
    explicit ContiguousMemoryManager(uint64_t total_size = 0);

    void reset(uint64_t total_size);

    [[nodiscard]] uint64_t total_size() const noexcept { return total_size_; }
    [[nodiscard]] uint64_t allocated_size() const noexcept;
    [[nodiscard]] uint64_t free_size() const noexcept;
    [[nodiscard]] const std::vector<MemoryBlock>& blocks() const noexcept { return blocks_; }
    [[nodiscard]] size_t next_fit_cursor() const noexcept { return next_fit_cursor_; }

    // Manually add/initialize block for custom benchmark workloads
    core::Result<void> set_blocks(std::vector<MemoryBlock> blocks, uint64_t total_size);

    core::Result<AllocationResult> allocate(ContiguousStrategy strategy, uint32_t process_id, uint64_t size);
    core::Result<DeallocationResult> deallocate(uint32_t process_id);
    core::Result<DeallocationResult> deallocate_address(uint64_t start_address);

    [[nodiscard]] core::Result<void> validate_invariants() const;

private:
    uint64_t total_size_{0};
    std::vector<MemoryBlock> blocks_;
    size_t next_fit_cursor_{0}; // Block index where Next Fit resumes search

    void coalesce_free_blocks();
};

// ----------------------------------------------------------------------------
// Paging and Address Translation Model
// ----------------------------------------------------------------------------
struct PageTableEntry {
    uint32_t frame_number{0};
    bool valid{false};        // Present bit: true if page is resident in physical frame
    bool modified{false};     // Dirty bit
    bool referenced{false};   // Access bit
};

struct PagingConfig {
    uint64_t logical_address_space_size{65536}; // 64 KB
    uint64_t page_size{4096};                  // 4 KB
    uint64_t physical_memory_size{32768};       // 32 KB

    [[nodiscard]] core::Result<void> validate() const;
    [[nodiscard]] uint64_t page_count() const noexcept {
        return (page_size > 0) ? (logical_address_space_size / page_size) : 0;
    }
    [[nodiscard]] uint64_t frame_count() const noexcept {
        return (page_size > 0) ? (physical_memory_size / page_size) : 0;
    }
};

struct TranslationResult {
    bool success{false};
    uint64_t logical_address{0};
    uint64_t page_number{0};
    uint64_t offset{0};
    uint32_t frame_number{0};
    uint64_t physical_address{0};
    bool is_page_fault{false};
    std::string status_message;
};

class PagingSystem {
public:
    explicit PagingSystem(PagingConfig config = {});

    core::Result<void> set_page_table(uint32_t process_id, std::vector<PageTableEntry> table);
    core::Result<void> set_page_entry(uint32_t process_id, uint64_t page_number, PageTableEntry entry);

    [[nodiscard]] core::Result<TranslationResult> translate(uint32_t process_id, uint64_t logical_address) const;
    [[nodiscard]] const PagingConfig& config() const noexcept { return config_; }
    [[nodiscard]] const std::vector<PageTableEntry>* get_page_table(uint32_t process_id) const;
    [[nodiscard]] const std::map<uint32_t, std::vector<PageTableEntry>>& page_tables() const noexcept {
        return page_tables_;
    }

private:
    PagingConfig config_{};
    std::map<uint32_t, std::vector<PageTableEntry>> page_tables_;
};

// ----------------------------------------------------------------------------
// Page Replacement Algorithms
// ----------------------------------------------------------------------------
struct ReplacementStep {
    size_t step_index{0};
    uint32_t page{0};
    std::vector<std::optional<uint32_t>> frames;
    bool is_hit{false};
    std::optional<uint32_t> evicted_page;
    size_t frame_index{0};
    std::string action; // "HIT", "FAULT (ALLOC)", "FAULT (REPLACE Px)"
};

struct ReplacementResult {
    PageReplacementAlgo algorithm{PageReplacementAlgo::Fifo};
    size_t frame_count{0};
    size_t total_references{0};
    size_t page_faults{0};
    size_t page_hits{0};
    double fault_rate{0.0};
    double hit_rate{0.0};
    std::vector<ReplacementStep> trace;
    std::string summary;
};

struct WorkingSetStep {
    size_t time_step{0};
    uint32_t referenced_page{0};
    std::vector<uint32_t> working_set;
    size_t working_set_size{0};
};

struct WorkingSetResult {
    size_t window_size{0};
    std::vector<WorkingSetStep> steps;
    double average_working_set_size{0.0};
    size_t peak_working_set_size{0};
};

class PageReplacementSimulator {
public:
    static core::Result<ReplacementResult> run(
        PageReplacementAlgo algo, const std::vector<uint32_t>& reference_string, size_t frame_count);

    static core::Result<WorkingSetResult> run_working_set(
        const std::vector<uint32_t>& reference_string, size_t window_size);
};

// ----------------------------------------------------------------------------
// Memory Management Engine API & Formatters
// ----------------------------------------------------------------------------
class MemoryEngine {
public:
    // Built-in Deterministic Workloads
    static ContiguousMemoryManager get_contiguous_workload();
    static PagingSystem get_paging_workload();
    static std::vector<uint32_t> get_canonical_reference_string();

    // ASCII Formatters
    static std::string format_contiguous_memory(const ContiguousMemoryManager& manager);
    static std::string format_allocation_result(const AllocationResult& res);
    static std::string format_deallocation_result(const DeallocationResult& res);
    static std::string format_page_table(uint32_t process_id, const std::vector<PageTableEntry>& table);
    static std::string format_translation(const TranslationResult& res);
    static std::string format_replacement_trace(const ReplacementResult& res);
    static std::string format_replacement_summary(const ReplacementResult& res);
    static std::string format_working_set(const WorkingSetResult& res);
};

} // namespace kernox::sim
