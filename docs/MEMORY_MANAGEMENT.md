# Memory Management and Virtual Memory Simulation Engine

KERNOX provides an academic, discrete-event Memory Management and Virtual Memory simulation engine implemented in modern C++20. The module models Contiguous Memory Allocation strategies (First Fit, Best Fit, Worst Fit, Next Fit), Paging and Logical-to-Physical Address Translation with an MMU model, Page Replacement algorithms (FIFO, LRU, and Bélady's Optimal MIN algorithm), and Working-Set locality analysis.

---

## 1. Theoretical Foundations & Architecture

### 1.1 Contiguous Memory Allocation
In contiguous allocation, each process is loaded into a single contiguous section of memory. As processes enter and exit, free memory is fragmented into variable-sized holes.
- **First Fit**: Allocates the first free hole that is large enough ($\text{size} \ge \text{request}$). Search begins from the start of memory.
- **Best Fit**: Allocates the smallest free hole that is large enough. Leaves the smallest remaining fragment, but minimizes wasted memory per allocation. Requires examining all holes unless ordered by size.
- **Worst Fit**: Allocates the largest free hole. Leaves the largest remaining fragment, which may be more useful than tiny fragments produced by Best Fit.
- **Next Fit**: Begins searching from the location of the previous allocation (tracked via a persistent cursor) and selects the next hole that fits, wrapping around to the beginning if necessary.

#### Coalescing and Invariant Maintenance:
When a block is deallocated, adjacent free blocks are immediately merged (**coalesced**) into a single contiguous hole to combat external fragmentation. The engine maintains strict memory invariants:
$$\text{Allocated Memory} + \text{Free Memory} = \text{Total Configured Memory Capacity}$$
$$\text{Block}[i].\text{start} + \text{Block}[i].\text{size} = \text{Block}[i+1].\text{start} \quad \forall i < N-1$$

---

### 1.2 Paging and Address Translation Unit (MMU Model)
Paging eliminates external fragmentation by partitioning physical memory into fixed-sized blocks called **frames** and logical memory into blocks of the same size called **pages**.

```
Logical Address (A)
+-----------------------+-----------------------+
|  Page Number (p)      |  Page Offset (d)      |
+-----------------------+-----------------------+
       |                           |
       v                           |
+--------------+                   |
|  Page Table  |                   |
|  p -> Frame f|                   |
+--------------+                   |
       |                           |
       v                           v
+-----------------------+-----------------------+
|  Frame Number (f)     |  Frame Offset (d)     |
+-----------------------+-----------------------+
Physical Address = (f * PageSize) + d
```

#### Translation Logic:
Given a page size $S$ (must be a power of 2):
1. **Boundary Validation**: If $\text{Address} \ge \text{LogicalAddressSpaceSize}$, trigger a **Segmentation Fault**.
2. **Page & Offset Calculation**:
   $$p = \lfloor \text{Address} / S \rfloor, \quad d = \text{Address} \pmod S$$
3. **Page Table Lookup**:
   - If Page Table Entry $\text{PTE}[p]$ has $\text{Valid} == \text{false}$, trigger a **Page Fault** (page not resident in physical memory).
   - If $\text{PTE}[p].\text{Frame} \ge \text{TotalPhysicalFrames}$, trigger an **Invalid Frame** fault.
4. **Physical Address Computation**:
   $$\text{Physical Address} = (\text{PTE}[p].\text{Frame} \times S) + d$$

---

### 1.3 Page Replacement Algorithms
When a page fault occurs and all physical frames are occupied, an existing page in memory must be selected as a **victim** and replaced.

1. **FIFO (First-In, First-Out)**:
   - Associates each page with the timestamp or order when it entered memory.
   - Replaces the oldest resident page.
   - Susceptible to **Bélady's Anomaly** (allocating more physical frames can increase page faults).
2. **LRU (Least Recently Used)**:
   - Uses the recent past as an approximation of the near future.
   - Replaces the page that has not been referenced for the longest period of time.
   - Stack algorithm (strictly immune to Bélady's Anomaly).
3. **Optimal (Bélady's MIN / Offline Algorithm)**:
   - Replaces the page that will not be used for the longest period of time in the future.
   - Requires perfect future knowledge; serves as an offline theoretical benchmark against which all practical online algorithms are measured.
   - Guarantees the absolute lowest page-fault frequency for any fixed frame count.

#### Deterministic Tie-Breaking:
If multiple candidate frames share identical replacement criteria (e.g., in Optimal when multiple resident pages are never referenced again), the simulator tie-breaks deterministically by selecting the **lowest physical frame index**.

---

### 1.4 Working-Set Model Locality Analysis
Based on Peter Denning's principle of locality, a process's **Working Set** $W(t, \Delta)$ at virtual time $t$ is the set of distinct pages referenced during the preceding window interval:
$$W(t, \Delta) = \{ p \in \mathbb{N} \mid \text{Page } p \text{ was referenced in time interval } [t - \Delta + 1, t] \}$$
- $|W(t, \Delta)|$ represents the instantaneous working-set size.
- If total working-set demand exceeds available physical frames ($\sum |W_i| > \text{TotalFrames}$), the system enters **Thrashing**.

---

## 2. Worked Numerical Benchmarks for OS Examiners

### 2.1 Contiguous Allocation Benchmark
**Initial Memory Configuration (1000 KB total)**:
- Hole 0: $[0, 100)$ KB (100 KB)
- $P_1$: $[100, 300)$ KB (200 KB)
- Hole 1: $[300, 600)$ KB (300 KB)
- $P_2$: $[600, 700)$ KB (100 KB)
- Hole 2: $[700, 900)$ KB (200 KB)
- $P_3$: $[900, 1000)$ KB (100 KB)

**Request**: Process $P_4$ requests $120$ KB:
| Strategy | Selected Hole | Allocated Address Range | Remainder Hole |
| :--- | :--- | :--- | :--- |
| **First Fit** | Hole 1 ($300$ KB) | $[300, 420)$ KB | $[420, 600)$ KB ($180$ KB) |
| **Best Fit** | Hole 2 ($200$ KB) | $[700, 820)$ KB | $[820, 900)$ KB ($80$ KB) |
| **Worst Fit** | Hole 1 ($300$ KB) | $[300, 420)$ KB | $[420, 600)$ KB ($180$ KB) |
| **Next Fit** | Hole 1 ($300$ KB) | $[300, 420)$ KB | Advances cursor to index 3 |

---

### 2.2 Address Translation Benchmark
**Configuration**:
- Logical Address Space: $64$ KB ($65536$ bytes) $\implies 16$ pages (Pages $0 \dots 15$)
- Page Size: $4$ KB ($4096$ bytes)
- Physical Memory: $32$ KB ($32768$ bytes) $\implies 8$ frames (Frames $0 \dots 7$)
- Page Table Mapping for $P_0$:
  - Page $0 \to$ Frame $5$ (Valid)
  - Page $1 \to$ Frame $2$ (Valid)
  - Page $2 \to$ Frame $7$ (Valid)
  - Page $3 \to$ Frame $1$ (Valid)
  - Page $4 \to$ Frame $0$ (**Invalid / Valid bit = 0**)
  - Page $5 \to$ Frame $0$ (Valid)

**Translation Results**:
1. Address $5000$:
   - $p = 5000 / 4096 = 1$
   - $d = 5000 \pmod{4096} = 904$
   - Page $1 \to$ Frame $2$
   - Physical Address $= 2 \times 4096 + 904 = \mathbf{9096}$ (`0x2388`)
2. Address $16384$ ($4 \times 4096$):
   - $p = 4, d = 0$
   - Page $4$ is marked **Invalid** $\implies$ **Page Fault** triggered.
3. Address $70000$:
   - $70000 \ge 65536 \implies$ **Segmentation Fault** triggered.

---

### 2.3 Page Replacement Benchmark (Bélady's Canonical Workload)
**Reference String (20 references, 3 frames)**:
`7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1`

| Algorithm | Page Faults | Page Hits | Fault Rate | Hit Rate |
| :--- | :---: | :---: | :---: | :---: |
| **Optimal (MIN)** | **9** | **11** | **45.00%** | **55.00%** |
| **LRU** | **12** | **8** | **60.00%** | **40.00%** |
| **FIFO** | **15** | **5** | **75.00%** | **25.00%** |

#### Demonstration of Bélady's Anomaly (FIFO):
Reference String: `1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5`
- With **3 Frames**: **9 page faults**
- With **4 Frames**: **10 page faults** ($\uparrow$ faults despite more frames!)

---

## 3. CLI Command Guide

```bash
# 1. Contiguous Memory Allocation
kernox memory allocate --algorithm first-fit --workload allocation-demo
kernox memory allocate --algorithm best-fit --workload allocation-demo
kernox memory allocate --algorithm worst-fit --workload allocation-demo
kernox memory allocate --algorithm next-fit --workload allocation-demo

# 2. Paging System Inspection
kernox memory paging --workload paging-demo

# 3. Logical-to-Physical Address Translation
kernox memory translate --process 0 --address 4096 --workload paging-demo
kernox memory translate --process 0 --address 5000 --workload paging-demo
kernox memory translate --process 0 --address 16384 --workload paging-demo # Triggers Page Fault

# 4. Page Replacement Algorithms
kernox memory replace --algorithm fifo --workload page-reference-demo --frames 3
kernox memory replace --algorithm lru --workload page-reference-demo --frames 3
kernox memory replace --algorithm optimal --workload page-reference-demo --frames 3

# 5. Working-Set Analysis
kernox memory ws --workload page-reference-demo --window 4
```

---

## 4. Contrast with Real Linux Virtual Memory

> [!IMPORTANT]
> **Academic Simulation vs. Linux Kernel Architecture**:
> - **Pure Reference-String vs. Hardware MMU**: KERNOX simulates discrete algorithmic page references. Linux uses multi-level page tables (x86-64 4-level/5-level PML4/PML5), hardware Translation Lookaside Buffers (TLB), and speculative page faults.
> - **Two-Handed Clock vs. Strict LRU**: True LRU has prohibitive hardware overhead. Linux uses an active/inactive dual-list Clock page eviction algorithm approximated via CPU hardware accessed/dirty bits (`_PAGE_ACCESSED`, `_PAGE_DIRTY`).
> - **Kernel Allocator Hierarchy**: Linux implements buddy allocator for physical page frame allocations, SLUB/SLAB caches for kernel objects, and `vmalloc`/`kmalloc` APIs, rather than textbook variable-partition contiguous allocation.

---

## 5. Phase 3D.1 Correctness Audit & Invariant Verification

During the Phase 3D.1 independent correctness audit, the memory management subsystem was subjected to adversarial boundary stress testing, cross-strategy interaction analysis, and arithmetic overflow auditing.

### 5.1 Confirmed Defects, Root Causes, and Targeted Fixes

#### Defect 1: Next Fit Cursor Overwrite During Non-NextFit Allocations
- **Root Cause**: In `ContiguousMemoryManager::allocate`, `next_fit_cursor_` was unconditionally updated to `candidate_idx + 1` for every strategy (including `FirstFit`, `BestFit`, and `WorstFit`). When an application interleaved `FirstFit` or `BestFit` requests with `NextFit` requests, the persistent search cursor for `NextFit` was clobbered by the candidate index of the preceding non-NextFit allocation.
- **Minimal Fix**: Guard cursor advancement to execute strictly when `strategy == ContiguousStrategy::NextFit`. Furthermore, when a block split occurs at `candidate_idx` during non-NextFit allocations prior to the cursor (`next_fit_cursor_ > candidate_idx`), shift the cursor by `+1` (`(next_fit_cursor_ + 1) % blocks_.size()`) to preserve the identity of the tracked block.
- **Regression Test**: `test_contiguous_adversarial_and_cursor_isolation()` in `tests/test_memory.cpp` verifies that an interspersed `FirstFit` allocation does not corrupt the `NextFit` search cursor.

#### Defect 2: 32-bit Frame/Page Count Truncation in Paging Configuration
- **Root Cause**: `PagingConfig::page_count()` and `frame_count()` cast 64-bit address capacities to `uint32_t`. If a user configured a 64-bit logical or physical memory space exceeding $2^{32} \times \text{PageSize}$, the computed counts truncated silently, leading to incorrect page table dimensioning and undetected memory corruption.
- **Minimal Fix**: Added explicit validation in `PagingConfig::validate()` to ensure `logical_address_space_size / page_size <= UINT32_MAX` and `physical_memory_size / page_size <= UINT32_MAX`, returning `ErrorCode::InvalidArgument` when exceeded.
- **Regression Test**: Verified via `test_paging_isolation_and_bounds()`.

#### Defect 3: Physical Address Arithmetic Overflow Protection
- **Root Cause**: In `PagingSystem::translate`, physical address computation `frame_base + res.offset` where `frame_base = pte.frame_number * config_.page_size` assumed `frame_base + res.offset` would not exceed `UINT64_MAX`. In extreme configurations with large frame numbers or offsets, this could wrap around silently.
- **Minimal Fix**: Added `if (UINT64_MAX - frame_base < res.offset)` check returning an explicit `Address Overflow` error with `res.success = false`.
- **Regression Test**: Verified in `test_paging_isolation_and_bounds()`.

### 5.2 Adversarial Invariant Challenges Verified
1. **Equal-Sized Holes**: When multiple candidate holes have identical sizes, `BestFit` and `WorstFit` guarantee deterministic tie-breaking to the hole with the lowest starting address / index.
2. **Next Fit Boundary Wrap-Around**: When the cursor points to the final block and the request exceeds the remaining capacity of that block, `NextFit` cleanly wraps around to index 0.
3. **Process Isolation in Paging**: Multiple processes ($P_{10}$ and $P_{20}$) maintain completely disjoint page tables. Queries for unmapped processes or invalid entries reliably report `Page Fault` without leaking mappings.
4. **Distinction Between Page Fault and Segmentation Fault**:
   - $\text{Address} \ge \text{LogicalAddressSpaceSize} \implies$ **Segmentation Fault** (`is_page_fault = false`).
   - $\text{Address} < \text{LogicalAddressSpaceSize}$ but $\text{PTE}[p].\text{valid} == \text{false} \implies$ **Page Fault** (`is_page_fault = true`).
5. **Exact Step-by-Step Page Replacement Traces**:
   - Evaluated step-by-step residency for `1, 2, 3, 2, 1, 4` on FIFO, LRU, and Optimal with 3 frames.
   - FIFO preserves queue order after hits and replaces oldest frame.
   - LRU updates recency timestamps on every hit.
   - Optimal implements deterministic lowest-frame-index tie-breaking when multiple resident pages are never referenced in the future.
6. **Working-Set Window Invariants**:
   - $\Delta = 1$: $|W(t, 1)| = 1$ for all $t$, peak size = 1, average size = 1.0.
   - $\Delta = N$: $|W(N, N)| = \text{DistinctPages}$, peak size = 4, average size = $2.6667$.
   - $\Delta > N$: Window cleanly clamps to available historical references without index overflow or division by zero.
