#include "kernox/modules/sim/memory.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <iostream>
#include <vector>

using namespace kernox::sim;
using namespace kernox::core;

static int test_contiguous_allocation_strategies() {
    std::cout << "Running test_contiguous_allocation_strategies...\n";

    // Layout: 1000 KB total
    // [0, 100): Free (100 KB)
    // [100, 300): P1 (200 KB)
    // [300, 600): Free (300 KB)
    // [600, 700): P2 (100 KB)
    // [700, 900): Free (200 KB)
    // [900, 1000): P3 (100 KB)

    // 1. First Fit: allocates 120 KB for P4 in the first hole with size >= 120 (hole [300, 600))
    {
        auto mgr = MemoryEngine::get_contiguous_workload();
        auto alloc_res = mgr.allocate(ContiguousStrategy::FirstFit, 4, 120);
        KERNOX_ASSERT_TRUE(alloc_res.is_ok());
        const auto& r = alloc_res.value();
        KERNOX_ASSERT_TRUE(r.success);
        KERNOX_ASSERT_EQ(r.allocated_block.start_address, 300u);
        KERNOX_ASSERT_EQ(r.allocated_block.size, 120u);
        KERNOX_ASSERT_EQ(r.allocated_block.process_id, 4u);
        KERNOX_ASSERT_TRUE(mgr.validate_invariants().is_ok());
    }

    // 2. Best Fit: allocates 120 KB for P4 in the smallest hole with size >= 120 (hole [700, 900), size 200)
    {
        auto mgr = MemoryEngine::get_contiguous_workload();
        auto alloc_res = mgr.allocate(ContiguousStrategy::BestFit, 4, 120);
        KERNOX_ASSERT_TRUE(alloc_res.is_ok());
        const auto& r = alloc_res.value();
        KERNOX_ASSERT_TRUE(r.success);
        KERNOX_ASSERT_EQ(r.allocated_block.start_address, 700u);
        KERNOX_ASSERT_EQ(r.allocated_block.size, 120u);
        KERNOX_ASSERT_EQ(r.allocated_block.process_id, 4u);
        KERNOX_ASSERT_TRUE(mgr.validate_invariants().is_ok());
    }

    // 3. Worst Fit: allocates 120 KB for P4 in the largest hole (hole [300, 600), size 300)
    {
        auto mgr = MemoryEngine::get_contiguous_workload();
        auto alloc_res = mgr.allocate(ContiguousStrategy::WorstFit, 4, 120);
        KERNOX_ASSERT_TRUE(alloc_res.is_ok());
        const auto& r = alloc_res.value();
        KERNOX_ASSERT_TRUE(r.success);
        KERNOX_ASSERT_EQ(r.allocated_block.start_address, 300u);
        KERNOX_ASSERT_EQ(r.allocated_block.size, 120u);
        KERNOX_ASSERT_EQ(r.allocated_block.process_id, 4u);
        KERNOX_ASSERT_TRUE(mgr.validate_invariants().is_ok());
    }

    // 4. Next Fit: sequential search preserving cursor across requests
    {
        auto mgr = MemoryEngine::get_contiguous_workload();

        // Req 1: P4 requests 120 KB -> finds [300, 600) -> splits at 420. Cursor at remainder [420, 600) (size 180)
        auto r1 = mgr.allocate(ContiguousStrategy::NextFit, 4, 120);
        KERNOX_ASSERT_TRUE(r1.is_ok() && r1.value().success);
        KERNOX_ASSERT_EQ(r1.value().allocated_block.start_address, 300u);

        // Req 2: P5 requests 150 KB -> searches from cursor at [420, 600) (size 180 >= 150)!
        auto r2 = mgr.allocate(ContiguousStrategy::NextFit, 5, 150);
        KERNOX_ASSERT_TRUE(r2.is_ok() && r2.value().success);
        KERNOX_ASSERT_EQ(r2.value().allocated_block.start_address, 420u);

        // Req 3: P6 requests 190 KB -> searches from cursor [570, 600) (30 KB < 190) -> moves to [700, 900) (200 KB)!
        auto r3 = mgr.allocate(ContiguousStrategy::NextFit, 6, 190);
        KERNOX_ASSERT_TRUE(r3.is_ok() && r3.value().success);
        KERNOX_ASSERT_EQ(r3.value().allocated_block.start_address, 700u);

        // Req 4: P7 requests 80 KB -> searches from remainder [890, 900) (10 KB < 80) -> wraps around to [0, 100) (100 KB)!
        auto r4 = mgr.allocate(ContiguousStrategy::NextFit, 7, 80);
        KERNOX_ASSERT_TRUE(r4.is_ok() && r4.value().success);
        KERNOX_ASSERT_EQ(r4.value().allocated_block.start_address, 0u);

        KERNOX_ASSERT_TRUE(mgr.validate_invariants().is_ok());
    }

    return 0;
}

static int test_contiguous_splitting_coalescing_and_invariants() {
    std::cout << "Running test_contiguous_splitting_coalescing_and_invariants...\n";

    ContiguousMemoryManager mgr(1000);
    KERNOX_ASSERT_EQ(mgr.total_size(), 1000u);
    KERNOX_ASSERT_EQ(mgr.free_size(), 1000u);
    KERNOX_ASSERT_EQ(mgr.allocated_size(), 0u);
    KERNOX_ASSERT_EQ(mgr.blocks().size(), 1u);

    // Allocate 300 KB for P1
    auto r1 = mgr.allocate(ContiguousStrategy::FirstFit, 1, 300);
    KERNOX_ASSERT_TRUE(r1.is_ok() && r1.value().success);
    KERNOX_ASSERT_EQ(mgr.blocks().size(), 2u);
    KERNOX_ASSERT_EQ(mgr.allocated_size(), 300u);
    KERNOX_ASSERT_EQ(mgr.free_size(), 700u);

    // Allocate 400 KB for P2
    auto r2 = mgr.allocate(ContiguousStrategy::FirstFit, 2, 400);
    KERNOX_ASSERT_TRUE(r2.is_ok() && r2.value().success);
    KERNOX_ASSERT_EQ(mgr.blocks().size(), 3u); // [0, 300) P1, [300, 700) P2, [700, 1000) Free

    // Allocate exact remainder 300 KB for P3 (no split!)
    auto r3 = mgr.allocate(ContiguousStrategy::FirstFit, 3, 300);
    KERNOX_ASSERT_TRUE(r3.is_ok() && r3.value().success);
    KERNOX_ASSERT_EQ(mgr.blocks().size(), 3u); // All 3 allocated
    KERNOX_ASSERT_EQ(mgr.allocated_size(), 1000u);
    KERNOX_ASSERT_EQ(mgr.free_size(), 0u);

    // Attempt allocation when memory is 100% full: Out of Memory
    auto r_full = mgr.allocate(ContiguousStrategy::FirstFit, 4, 50);
    KERNOX_ASSERT_TRUE(r_full.is_ok());
    KERNOX_ASSERT_FALSE(r_full.value().success);

    // Deallocate P2 in the middle -> creates 1 hole [300, 700)
    auto d2 = mgr.deallocate(2);
    KERNOX_ASSERT_TRUE(d2.is_ok() && d2.value().success);
    KERNOX_ASSERT_EQ(d2.value().blocks_freed, 1u);
    KERNOX_ASSERT_EQ(d2.value().bytes_freed, 400u);
    KERNOX_ASSERT_EQ(d2.value().merges_performed, 0u); // Neither neighbor is free

    // Deallocate P1 -> should merge with P2's hole: [0, 300) + [300, 700) = [0, 700)
    auto d1 = mgr.deallocate(1);
    KERNOX_ASSERT_TRUE(d1.is_ok() && d1.value().success);
    KERNOX_ASSERT_EQ(d1.value().merges_performed, 1u);
    KERNOX_ASSERT_EQ(mgr.blocks().size(), 2u); // [0, 700) Free, [700, 1000) P3

    // Deallocate P3 -> merges [0, 700) + [700, 1000) = [0, 1000) Free
    auto d3 = mgr.deallocate(3);
    KERNOX_ASSERT_TRUE(d3.is_ok() && d3.value().success);
    KERNOX_ASSERT_EQ(d3.value().merges_performed, 1u);
    KERNOX_ASSERT_EQ(mgr.blocks().size(), 1u);
    KERNOX_ASSERT_EQ(mgr.free_size(), 1000u);

    // Deallocate non-existent process
    auto d_none = mgr.deallocate(99);
    KERNOX_ASSERT_TRUE(d_none.is_ok());
    KERNOX_ASSERT_FALSE(d_none.value().success);

    // Error checks
    auto err_size0 = mgr.allocate(ContiguousStrategy::FirstFit, 1, 0);
    KERNOX_ASSERT_TRUE(err_size0.is_err());
    KERNOX_ASSERT_TRUE(err_size0.error().code == ErrorCode::InvalidArgument);

    auto err_pid0 = mgr.allocate(ContiguousStrategy::FirstFit, 0, 100);
    KERNOX_ASSERT_TRUE(err_pid0.is_err());
    KERNOX_ASSERT_TRUE(err_pid0.error().code == ErrorCode::InvalidArgument);

    KERNOX_ASSERT_TRUE(mgr.validate_invariants().is_ok());
    return 0;
}

static int test_paging_and_address_translation() {
    std::cout << "Running test_paging_and_address_translation...\n";

    // 64 KB logical address space, 4 KB page size, 32 KB physical memory
    auto sys = MemoryEngine::get_paging_workload();

    // 1. Valid translation: Page 0 (Frame 5)
    {
        auto res = sys.translate(0, 0);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& t = res.value();
        KERNOX_ASSERT_TRUE(t.success);
        KERNOX_ASSERT_FALSE(t.is_page_fault);
        KERNOX_ASSERT_EQ(t.page_number, 0u);
        KERNOX_ASSERT_EQ(t.offset, 0u);
        KERNOX_ASSERT_EQ(t.frame_number, 5u);
        KERNOX_ASSERT_EQ(t.physical_address, 5u * 4096u); // 20480
    }

    // 2. Valid translation with offset: Page 1, Address 5000 (4096 + 904) -> Frame 2
    {
        auto res = sys.translate(0, 5000);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& t = res.value();
        KERNOX_ASSERT_TRUE(t.success);
        KERNOX_ASSERT_EQ(t.page_number, 1u);
        KERNOX_ASSERT_EQ(t.offset, 904u);
        KERNOX_ASSERT_EQ(t.frame_number, 2u);
        KERNOX_ASSERT_EQ(t.physical_address, 2u * 4096u + 904u); // 9096
    }

    // 3. Page Fault: Page 4 (Valid bit = 0) at address 16384 (4 * 4096)
    {
        auto res = sys.translate(0, 16384);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& t = res.value();
        KERNOX_ASSERT_FALSE(t.success);
        KERNOX_ASSERT_TRUE(t.is_page_fault);
        KERNOX_ASSERT_EQ(t.page_number, 4u);
        KERNOX_ASSERT_EQ(t.offset, 0u);
    }

    // 4. Segmentation Fault: Address 70000 exceeds 64 KB (65536)
    {
        auto res = sys.translate(0, 70000);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& t = res.value();
        KERNOX_ASSERT_FALSE(t.success);
        KERNOX_ASSERT_FALSE(t.is_page_fault);
        KERNOX_ASSERT_TRUE(t.status_message.find("Segmentation Fault") != std::string::npos);
    }

    // 5. Config validation
    {
        PagingConfig bad_cfg{65536, 3000, 32768}; // 3000 is not power of 2
        KERNOX_ASSERT_TRUE(bad_cfg.validate().is_err());
        KERNOX_ASSERT_TRUE(bad_cfg.validate().error().code == ErrorCode::InvalidArgument);

        PagingConfig bad_cfg2{65536, 0, 32768}; // page_size = 0
        KERNOX_ASSERT_TRUE(bad_cfg2.validate().is_err());
    }

    return 0;
}

static int test_page_replacement_fifo() {
    std::cout << "Running test_page_replacement_fifo...\n";

    // Bélady's canonical 20-reference string: 7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1
    auto ref_str = MemoryEngine::get_canonical_reference_string();
    auto res = PageReplacementSimulator::run(PageReplacementAlgo::Fifo, ref_str, 3);
    KERNOX_ASSERT_TRUE(res.is_ok());
    const auto& r = res.value();

    // Standard theoretical FIFO results with 3 frames: 15 faults, 5 hits
    KERNOX_ASSERT_EQ(r.total_references, 20u);
    KERNOX_ASSERT_EQ(r.page_faults, 15u);
    KERNOX_ASSERT_EQ(r.page_hits, 5u);
    KERNOX_ASSERT_EQ(r.trace.size(), 20u);

    // Verify Bélady's Anomaly (1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5)
    // 3 frames: 9 faults; 4 frames: 10 faults!
    std::vector<uint32_t> anomaly_str = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    auto res_3 = PageReplacementSimulator::run(PageReplacementAlgo::Fifo, anomaly_str, 3);
    auto res_4 = PageReplacementSimulator::run(PageReplacementAlgo::Fifo, anomaly_str, 4);
    KERNOX_ASSERT_TRUE(res_3.is_ok() && res_4.is_ok());
    KERNOX_ASSERT_EQ(res_3.value().page_faults, 9u);
    KERNOX_ASSERT_EQ(res_4.value().page_faults, 10u); // Proof of Belady's Anomaly!

    return 0;
}

static int test_page_replacement_lru() {
    std::cout << "Running test_page_replacement_lru...\n";

    // Canonical reference string with 3 frames
    auto ref_str = MemoryEngine::get_canonical_reference_string();
    auto res = PageReplacementSimulator::run(PageReplacementAlgo::Lru, ref_str, 3);
    KERNOX_ASSERT_TRUE(res.is_ok());
    const auto& r = res.value();

    // Standard theoretical LRU results with 3 frames: 12 faults, 8 hits
    KERNOX_ASSERT_EQ(r.total_references, 20u);
    KERNOX_ASSERT_EQ(r.page_faults, 12u);
    KERNOX_ASSERT_EQ(r.page_hits, 8u);

    return 0;
}

static int test_page_replacement_optimal() {
    std::cout << "Running test_page_replacement_optimal...\n";

    // Canonical reference string with 3 frames
    auto ref_str = MemoryEngine::get_canonical_reference_string();
    auto res = PageReplacementSimulator::run(PageReplacementAlgo::Optimal, ref_str, 3);
    KERNOX_ASSERT_TRUE(res.is_ok());
    const auto& r = res.value();

    // Standard theoretical Optimal results with 3 frames: 9 faults, 11 hits
    KERNOX_ASSERT_EQ(r.total_references, 20u);
    KERNOX_ASSERT_EQ(r.page_faults, 9u);
    KERNOX_ASSERT_EQ(r.page_hits, 11u);

    // Optimal page fault count must be <= LRU and <= FIFO
    KERNOX_ASSERT_TRUE(r.page_faults <= 12u);
    KERNOX_ASSERT_TRUE(r.page_faults <= 15u);

    return 0;
}

static int test_page_replacement_edge_cases() {
    std::cout << "Running test_page_replacement_edge_cases...\n";

    // 1. Single frame
    {
        std::vector<uint32_t> single = {1, 2, 1, 2, 3};
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Fifo, single, 1);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(res.value().page_faults, 5u);
        KERNOX_ASSERT_EQ(res.value().page_hits, 0u);
    }

    // 2. Repeated references to same page
    {
        std::vector<uint32_t> repeated = {3, 3, 3, 3, 3};
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Lru, repeated, 2);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(res.value().page_faults, 1u);
        KERNOX_ASSERT_EQ(res.value().page_hits, 4u);
    }

    // 3. Error handling: 0 frames
    {
        std::vector<uint32_t> v = {1, 2};
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Optimal, v, 0);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_TRUE(res.error().code == ErrorCode::InvalidArgument);
    }

    // 4. Error handling: empty reference string
    {
        std::vector<uint32_t> empty;
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Fifo, empty, 3);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_TRUE(res.error().code == ErrorCode::InvalidArgument);
    }

    return 0;
}

static int test_working_set_analysis() {
    std::cout << "Running test_working_set_analysis...\n";

    // References: 1, 2, 3, 2, 1, 4 with window Delta = 3
    std::vector<uint32_t> refs = {1, 2, 3, 2, 1, 4};
    auto res = PageReplacementSimulator::run_working_set(refs, 3);
    KERNOX_ASSERT_TRUE(res.is_ok());
    const auto& ws = res.value();

    KERNOX_ASSERT_EQ(ws.steps.size(), 6u);
    // t=0: [1] -> size 1
    KERNOX_ASSERT_EQ(ws.steps[0].working_set_size, 1u);
    // t=1: [1, 2] -> size 2
    KERNOX_ASSERT_EQ(ws.steps[1].working_set_size, 2u);
    // t=2: [1, 2, 3] -> size 3
    KERNOX_ASSERT_EQ(ws.steps[2].working_set_size, 3u);
    // t=3: [2, 3, 2] -> {2, 3} -> size 2
    KERNOX_ASSERT_EQ(ws.steps[3].working_set_size, 2u);
    // t=4: [3, 2, 1] -> {1, 2, 3} -> size 3
    KERNOX_ASSERT_EQ(ws.steps[4].working_set_size, 3u);
    // t=5: [2, 1, 4] -> {1, 2, 4} -> size 3
    KERNOX_ASSERT_EQ(ws.steps[5].working_set_size, 3u);

    KERNOX_ASSERT_EQ(ws.peak_working_set_size, 3u);

    // Invalid window size
    auto err = PageReplacementSimulator::run_working_set(refs, 0);
    KERNOX_ASSERT_TRUE(err.is_err());
    KERNOX_ASSERT_TRUE(err.error().code == ErrorCode::InvalidArgument);

    return 0;
}

static int test_contiguous_adversarial_and_cursor_isolation() {
    std::cout << "Running test_contiguous_adversarial_and_cursor_isolation...\n";

    // 1. Adversarial equal-sized holes: tie-breaking to lowest address
    {
        std::vector<MemoryBlock> blocks = {
            {0, 200, false, 0},
            {200, 100, true, 1},
            {300, 200, false, 0},
            {500, 100, true, 2},
            {600, 200, false, 0},
            {800, 200, true, 3}
        };

        // First Fit: chooses lowest address (Hole 0 at address 0)
        {
            ContiguousMemoryManager mgr(1000);
            KERNOX_ASSERT_TRUE(mgr.set_blocks(blocks, 1000).is_ok());
            auto r = mgr.allocate(ContiguousStrategy::FirstFit, 4, 150);
            KERNOX_ASSERT_TRUE(r.is_ok() && r.value().success);
            KERNOX_ASSERT_EQ(r.value().allocated_block.start_address, 0u);
        }

        // Best Fit: all 3 holes have equal size 200 (diff 50). Must pick first candidate (address 0)
        {
            ContiguousMemoryManager mgr(1000);
            KERNOX_ASSERT_TRUE(mgr.set_blocks(blocks, 1000).is_ok());
            auto r = mgr.allocate(ContiguousStrategy::BestFit, 4, 150);
            KERNOX_ASSERT_TRUE(r.is_ok() && r.value().success);
            KERNOX_ASSERT_EQ(r.value().allocated_block.start_address, 0u);
        }

        // Worst Fit: all 3 holes are the largest (200). Must pick first candidate (address 0)
        {
            ContiguousMemoryManager mgr(1000);
            KERNOX_ASSERT_TRUE(mgr.set_blocks(blocks, 1000).is_ok());
            auto r = mgr.allocate(ContiguousStrategy::WorstFit, 4, 150);
            KERNOX_ASSERT_TRUE(r.is_ok() && r.value().success);
            KERNOX_ASSERT_EQ(r.value().allocated_block.start_address, 0u);
        }
    }

    // 2. Next Fit cursor isolation across interspersed allocations (Defect 1 regression)
    {
        ContiguousMemoryManager mgr(700);
        std::vector<MemoryBlock> blocks = {
            {0, 40, false, 0},     // Hole 0: 40 KB
            {40, 160, true, 1},    // P1: 160 KB
            {200, 200, false, 0},  // Hole 1: 200 KB
            {400, 100, true, 2},   // P2: 100 KB
            {500, 200, false, 0}   // Hole 2: 200 KB
        };
        KERNOX_ASSERT_TRUE(mgr.set_blocks(blocks, 700).is_ok());

        // Step 1: Next Fit allocate 50 KB for P10.
        // Hole 0 (40 KB) is too small, so Next Fit selects Hole 1 [200, 400).
        // Allocated: [200, 250), Remainder: [250, 400) (size 150).
        // Cursor points to remainder block.
        auto r1 = mgr.allocate(ContiguousStrategy::NextFit, 10, 50);
        KERNOX_ASSERT_TRUE(r1.is_ok() && r1.value().success);
        KERNOX_ASSERT_EQ(r1.value().allocated_block.start_address, 200u);

        // Step 2: Interspersed First Fit allocate 20 KB for P20.
        // First Fit chooses Hole 0 [0, 40), splitting it into [0, 20) and [20, 40).
        // This First Fit call MUST NOT reset or corrupt Next Fit's cursor!
        auto r2 = mgr.allocate(ContiguousStrategy::FirstFit, 20, 20);
        KERNOX_ASSERT_TRUE(r2.is_ok() && r2.value().success);
        KERNOX_ASSERT_EQ(r2.value().allocated_block.start_address, 0u);

        // Step 3: Next Fit allocate 10 KB for P30.
        // If the cursor was corrupted to candidate_idx+1 (1), it would allocate in [20, 40) at address 20!
        // With correct cursor preservation, Next Fit searches from [250, 400), allocating at address 250!
        auto r3 = mgr.allocate(ContiguousStrategy::NextFit, 30, 10);
        KERNOX_ASSERT_TRUE(r3.is_ok() && r3.value().success);
        KERNOX_ASSERT_EQ(r3.value().allocated_block.start_address, 250u);
    }

    // 3. Next Fit cursor at final block and wrap-around
    {
        ContiguousMemoryManager mgr(300);
        std::vector<MemoryBlock> blocks = {
            {0, 40, false, 0},
            {40, 160, true, 1},
            {200, 100, false, 0}
        };
        KERNOX_ASSERT_TRUE(mgr.set_blocks(blocks, 300).is_ok());

        // Allocate 80 KB via Next Fit: Hole 0 (40 KB) is too small, selects [200, 300).
        auto r1 = mgr.allocate(ContiguousStrategy::NextFit, 10, 80);
        KERNOX_ASSERT_TRUE(r1.is_ok() && r1.value().success);
        KERNOX_ASSERT_EQ(r1.value().allocated_block.start_address, 200u);
        // Remainder is [280, 300) size 20. Cursor points to this last block.

        // Now allocate 30 KB -> 30 cannot fit in 20 KB -> wraps around to [0, 40)!
        auto r2 = mgr.allocate(ContiguousStrategy::NextFit, 20, 30);
        KERNOX_ASSERT_TRUE(r2.is_ok() && r2.value().success);
        KERNOX_ASSERT_EQ(r2.value().allocated_block.start_address, 0u);
    }

    // 4. Deallocation by address and repeated deallocation
    {
        ContiguousMemoryManager mgr(500);
        auto a1 = mgr.allocate(ContiguousStrategy::FirstFit, 1, 100);
        auto a2 = mgr.allocate(ContiguousStrategy::FirstFit, 2, 200);
        KERNOX_ASSERT_TRUE(a1.is_ok() && a2.is_ok());

        // Deallocate by address
        auto d1 = mgr.deallocate_address(0);
        KERNOX_ASSERT_TRUE(d1.is_ok() && d1.value().success);
        KERNOX_ASSERT_EQ(d1.value().bytes_freed, 100u);

        // Repeated deallocation of already-free block
        auto d1_repeat = mgr.deallocate_address(0);
        KERNOX_ASSERT_TRUE(d1_repeat.is_ok());
        KERNOX_ASSERT_FALSE(d1_repeat.value().success);

        // Non-existent address
        auto d_invalid = mgr.deallocate_address(9999);
        KERNOX_ASSERT_TRUE(d_invalid.is_ok());
        KERNOX_ASSERT_FALSE(d_invalid.value().success);
    }

    return 0;
}

static int test_paging_isolation_and_bounds() {
    std::cout << "Running test_paging_isolation_and_bounds...\n";

    PagingConfig cfg{65536, 4096, 32768}; // 16 pages, 8 frames
    PagingSystem sys(cfg);

    // Process 10 table
    std::vector<PageTableEntry> p10(16);
    p10[0] = {1, true, false, true};
    p10[1] = {3, true, true, true};
    KERNOX_ASSERT_TRUE(sys.set_page_table(10, std::move(p10)).is_ok());

    // Process 20 table
    std::vector<PageTableEntry> p20(16);
    p20[0] = {4, true, false, true};
    p20[1] = {6, true, false, true};
    KERNOX_ASSERT_TRUE(sys.set_page_table(20, std::move(p20)).is_ok());

    // 1. Process isolation: P10 and P20 have different frames for logical address 100
    auto t10 = sys.translate(10, 100);
    KERNOX_ASSERT_TRUE(t10.is_ok() && t10.value().success);
    KERNOX_ASSERT_EQ(t10.value().frame_number, 1u);
    KERNOX_ASSERT_EQ(t10.value().physical_address, 1u * 4096u + 100u);

    auto t20 = sys.translate(20, 100);
    KERNOX_ASSERT_TRUE(t20.is_ok() && t20.value().success);
    KERNOX_ASSERT_EQ(t20.value().frame_number, 4u);
    KERNOX_ASSERT_EQ(t20.value().physical_address, 4u * 4096u + 100u);

    // Unmapped process P99 cannot access any frames -> Page Fault
    auto t99 = sys.translate(99, 100);
    KERNOX_ASSERT_TRUE(t99.is_ok());
    KERNOX_ASSERT_FALSE(t99.value().success);
    KERNOX_ASSERT_TRUE(t99.value().is_page_fault);

    // 2. Differing logical and physical sizes:
    // Case A: logical < physical
    {
        PagingConfig small_log{16384, 4096, 65536}; // 4 pages, 16 frames
        KERNOX_ASSERT_TRUE(small_log.validate().is_ok());
        KERNOX_ASSERT_EQ(small_log.page_count(), 4u);
        KERNOX_ASSERT_EQ(small_log.frame_count(), 16u);

        PagingSystem ps(small_log);
        std::vector<PageTableEntry> pt(4);
        pt[0] = {15, true, false, true};
        KERNOX_ASSERT_TRUE(ps.set_page_table(1, std::move(pt)).is_ok());

        auto tr = ps.translate(1, 0);
        KERNOX_ASSERT_TRUE(tr.is_ok() && tr.value().success);
        KERNOX_ASSERT_EQ(tr.value().frame_number, 15u);
    }

    // Case B: logical > physical
    {
        PagingConfig large_log{65536, 4096, 16384}; // 16 pages, 4 frames
        KERNOX_ASSERT_TRUE(large_log.validate().is_ok());
        KERNOX_ASSERT_EQ(large_log.page_count(), 16u);
        KERNOX_ASSERT_EQ(large_log.frame_count(), 4u);

        PagingSystem ps(large_log);
        std::vector<PageTableEntry> pt(16);
        pt[0] = {3, true, false, true};
        KERNOX_ASSERT_TRUE(ps.set_page_table(1, std::move(pt)).is_ok());

        // Reject invalid frame reference
        std::vector<PageTableEntry> pt_bad(16);
        pt_bad[0] = {4, true, false, true}; // Frame 4 >= frame_count 4
        KERNOX_ASSERT_TRUE(ps.set_page_table(2, std::move(pt_bad)).is_err());
    }

    return 0;
}

static int test_page_replacement_exact_traces_and_deterministic_ties() {
    std::cout << "Running test_page_replacement_exact_traces_and_deterministic_ties...\n";

    // 1. Exact step-by-step trace assertions for short workload
    // References: 1, 2, 3, 2, 1, 4 with 3 frames
    std::vector<uint32_t> refs = {1, 2, 3, 2, 1, 4};

    // FIFO simulation
    {
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Fifo, refs, 3);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(r.page_faults, 4u);
        KERNOX_ASSERT_EQ(r.page_hits, 2u);

        // Step 1: 1 -> Fault, frames: [1, -, -]
        KERNOX_ASSERT_FALSE(r.trace[0].is_hit);
        KERNOX_ASSERT_EQ(*r.trace[0].frames[0], 1u);
        KERNOX_ASSERT_FALSE(r.trace[0].frames[1].has_value());

        // Step 2: 2 -> Fault, frames: [1, 2, -]
        KERNOX_ASSERT_FALSE(r.trace[1].is_hit);
        KERNOX_ASSERT_EQ(*r.trace[1].frames[1], 2u);

        // Step 3: 3 -> Fault, frames: [1, 2, 3]
        KERNOX_ASSERT_FALSE(r.trace[2].is_hit);
        KERNOX_ASSERT_EQ(*r.trace[2].frames[2], 3u);

        // Step 4: 2 -> Hit!
        KERNOX_ASSERT_TRUE(r.trace[3].is_hit);
        KERNOX_ASSERT_EQ(r.trace[3].frame_index, 1u);

        // Step 5: 1 -> Hit!
        KERNOX_ASSERT_TRUE(r.trace[4].is_hit);
        KERNOX_ASSERT_EQ(r.trace[4].frame_index, 0u);

        // Step 6: 4 -> Fault! FIFO victim is frame 0 (Page 1)
        KERNOX_ASSERT_FALSE(r.trace[5].is_hit);
        KERNOX_ASSERT_EQ(r.trace[5].frame_index, 0u);
        KERNOX_ASSERT_TRUE(r.trace[5].evicted_page.has_value());
        KERNOX_ASSERT_EQ(*r.trace[5].evicted_page, 1u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[0], 4u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[1], 2u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[2], 3u);
    }

    // LRU simulation
    {
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Lru, refs, 3);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(r.page_faults, 4u);
        KERNOX_ASSERT_EQ(r.page_hits, 2u);

        // Step 6: 4 -> Fault! LRU victim: Page 3 (frame 2)
        KERNOX_ASSERT_FALSE(r.trace[5].is_hit);
        KERNOX_ASSERT_EQ(r.trace[5].frame_index, 2u);
        KERNOX_ASSERT_TRUE(r.trace[5].evicted_page.has_value());
        KERNOX_ASSERT_EQ(*r.trace[5].evicted_page, 3u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[0], 1u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[1], 2u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[2], 4u);
    }

    // Optimal simulation
    {
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Optimal, refs, 3);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& r = res.value();
        KERNOX_ASSERT_EQ(r.page_faults, 4u);
        KERNOX_ASSERT_EQ(r.page_hits, 2u);

        // Step 6: 4 -> Fault! None of resident pages {1, 2, 3} are referenced in future.
        // Deterministic lowest frame index tie-breaking evicts frame 0 (Page 1)!
        KERNOX_ASSERT_FALSE(r.trace[5].is_hit);
        KERNOX_ASSERT_EQ(r.trace[5].frame_index, 0u);
        KERNOX_ASSERT_TRUE(r.trace[5].evicted_page.has_value());
        KERNOX_ASSERT_EQ(*r.trace[5].evicted_page, 1u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[0], 4u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[1], 2u);
        KERNOX_ASSERT_EQ(*r.trace[5].frames[2], 3u);
    }

    // 2. More frames than distinct pages
    {
        std::vector<uint32_t> short_refs = {1, 2, 1, 2, 1, 2};
        auto res = PageReplacementSimulator::run(PageReplacementAlgo::Fifo, short_refs, 5);
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_EQ(res.value().page_faults, 2u);
        KERNOX_ASSERT_EQ(res.value().page_hits, 4u);
    }

    return 0;
}

static int test_working_set_window_boundaries() {
    std::cout << "Running test_working_set_window_boundaries...\n";

    std::vector<uint32_t> refs = {1, 2, 3, 2, 1, 4};

    // 1. Window size Delta = 1: each window has exactly 1 element
    {
        auto res = PageReplacementSimulator::run_working_set(refs, 1);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& ws = res.value();
        KERNOX_ASSERT_EQ(ws.peak_working_set_size, 1u);
        KERNOX_ASSERT_TRUE(std::abs(ws.average_working_set_size - 1.0) < 1e-6);
        for (size_t t = 0; t < refs.size(); ++t) {
            KERNOX_ASSERT_EQ(ws.steps[t].working_set_size, 1u);
            KERNOX_ASSERT_EQ(ws.steps[t].working_set[0], refs[t]);
        }
    }

    // 2. Window size Delta = N (6): window expands up to all distinct pages
    {
        auto res = PageReplacementSimulator::run_working_set(refs, 6);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& ws = res.value();
        // At t=5, all 6 refs [1, 2, 3, 2, 1, 4] -> 4 unique pages {1, 2, 3, 4}
        KERNOX_ASSERT_EQ(ws.steps[5].working_set_size, 4u);
        KERNOX_ASSERT_EQ(ws.peak_working_set_size, 4u);
        KERNOX_ASSERT_TRUE(std::abs(ws.average_working_set_size - (16.0 / 6.0)) < 1e-5);
    }

    // 3. Window size Delta > N (50): capped naturally at t elements
    {
        auto res = PageReplacementSimulator::run_working_set(refs, 50);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& ws = res.value();
        KERNOX_ASSERT_EQ(ws.peak_working_set_size, 4u);
        KERNOX_ASSERT_TRUE(std::abs(ws.average_working_set_size - (16.0 / 6.0)) < 1e-5);
    }

    return 0;
}

int main() {
    std::cout << "=== KERNOX Memory Management & Virtual Memory Tests ===\n";

    if (test_contiguous_allocation_strategies() != 0) return 1;
    if (test_contiguous_splitting_coalescing_and_invariants() != 0) return 1;
    if (test_contiguous_adversarial_and_cursor_isolation() != 0) return 1;
    if (test_paging_and_address_translation() != 0) return 1;
    if (test_paging_isolation_and_bounds() != 0) return 1;
    if (test_page_replacement_fifo() != 0) return 1;
    if (test_page_replacement_lru() != 0) return 1;
    if (test_page_replacement_optimal() != 0) return 1;
    if (test_page_replacement_exact_traces_and_deterministic_ties() != 0) return 1;
    if (test_page_replacement_edge_cases() != 0) return 1;
    if (test_working_set_analysis() != 0) return 1;
    if (test_working_set_window_boundaries() != 0) return 1;

    std::cout << "=== All Memory Management Tests Passed Successfully ===\n";
    return 0;
}

