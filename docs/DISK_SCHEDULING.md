# Disk Scheduling Simulation Engine

KERNOX includes a modular, deterministic disk-scheduling simulation engine implemented in modern C++20. The module models classic secondary storage head-movement algorithms, computes step-by-step movement traces and Total Head Movement (THM), distinguishes service events from boundary traversals and circular wraparounds, and provides rich CLI formatting.

---

## 1. Domain Model & Principles

### 1.1 Physical Disk Geometry Abstraction
The simulation models disk cylinders as a discrete linear address space:
- **Cylinder Count ($N$)**: Total number of concentric physical cylinders on the platter ($N \ge 1$).
- **Valid Cylinder Range**: $[0, N - 1]$, where cylinder `0` is the innermost/outermost boundary and cylinder $N - 1$ is the opposite physical boundary.
- **Initial Head Position ($H_0$)**: Initial cylinder location of the read/write head, requiring $0 \le H_0 < N$.

### 1.2 Direction Model
Directional algorithms (SCAN, C-SCAN, LOOK, C-LOOK) utilize an explicit direction enum:
- `DiskDirection::Up`: Moving towards higher cylinder numbers (towards $N - 1$).
- `DiskDirection::Down`: Moving towards lower cylinder numbers (towards $0$).

### 1.3 Step Types and Accounting
The simulation distinguishes physical head movements into three explicit categories:
1. `MovementType::Service`: Head travels to a requested cylinder and services the pending I/O request.
2. `MovementType::Boundary`: Head travels to the physical platter boundary (`0` or $N - 1$) in SCAN or C-SCAN before reversing or wrapping.
3. `MovementType::Wrap`: Head performs a full-span wraparound traversal across the platter (in C-SCAN between boundaries `0` and $N - 1$, or in C-LOOK between the extreme pending requests).

Each movement step records:
- `from_cylinder`: Starting cylinder index.
- `to_cylinder`: Destination cylinder index.
- `distance`: Non-negative seek distance $| \text{to\_cylinder} - \text{from\_cylinder} |$.
- `type`: Movement classification (`SERVICE`, `BOUNDARY`, or `WRAP`).
- `serviced_request`: The cylinder request fulfilled (if applicable), or `std::nullopt` for pure travel.

### 1.4 Deterministic Tie-Breaking
When multiple pending requests are equidistant from the current head position (e.g., in SSTF):
1. **Earliest Arrival Index**: The request appearing earlier in the original input request sequence is selected first.
2. Every request is serviced exactly once, including duplicate cylinder requests.

### 1.5 Workload Validation
Prior to execution, configurations are validated through `DiskScheduler::validate_config`:
- Cylinder count must be strictly positive ($N > 0$).
- Initial head position must satisfy $0 \le H_0 < N$.
- Request queues cannot be empty.
- Every cylinder request $c_i$ must satisfy $0 \le c_i < N$.
- Invalid configurations reject execution with `core::ErrorCode::InvalidArgument`.

---

## 2. Supported Scheduling Algorithms

### 2.1 First-Come, First-Served (FCFS)
* **Policy**: Services pending cylinder requests in their exact arrival sequence.
* **Characteristics**: Fair and starvation-free, but suffers from wild head oscillations across the disk platter when requests are non-localized.

### 2.2 Shortest Seek Time First (SSTF)
* **Policy**: Greedily chooses the pending request with the minimal absolute cylinder distance from the current head position.
* **Tie-Breaking**: In case of equal distances, services the request with the earlier input vector index.
* **Characteristics**: Significantly reduces average seek distance compared to FCFS, but can cause starvation for requests situated far from heavily accessed clusters.

### 2.3 SCAN (Elevator Algorithm)
* **Policy**: The head moves in the configured direction (UP or DOWN), servicing all pending requests encountered along the way. Upon servicing the furthest request in the active direction, the head continues to the **physical disk boundary** (`N - 1` when moving UP, or `0` when moving DOWN) before reversing direction and servicing remaining requests.
* **Boundary Condition**: If no pending requests exist behind the initial head in the opposite direction, the head terminates upon servicing the last request in the current direction without unneeded boundary traversal.

### 2.4 C-SCAN (Circular SCAN)
* **Policy**: Unidirectional service model. The head travels in the configured direction servicing requests until it reaches the physical boundary (`N - 1` when UP, `0` when DOWN). It then wraps around to the opposite physical boundary (`0` or `N - 1`) via a fast traversal and resumes servicing requests in the original direction.
* **Wraparound Accounting**: The physical cylinder distance traversed during wraparound ($| (N - 1) - 0 | = N - 1$) is counted in the total head movement under standard physical seek accounting.
* **Characteristics**: Provides a more uniform waiting time distribution than standard SCAN.

### 2.5 LOOK
* **Policy**: Similar to SCAN, but avoids travelling to the physical disk boundaries. The head travels in the active direction only until the furthest pending request in that direction is reached. It then immediately reverses direction to service pending requests on the other side.
* **Characteristics**: Eliminates unnecessary seek travel to unused edge cylinders.

### 2.6 C-LOOK (Circular LOOK)
* **Policy**: Unidirectional variant of LOOK. The head travels in the active direction servicing requests until the furthest pending request in that direction is fulfilled. It then directly wraps around to the furthest pending request at the opposite end of the platter and resumes servicing in the initial direction.
* **Wraparound Accounting**: The physical cylinder distance between the two extreme requests is explicitly recorded as a `WRAP` step and added to total head movement.

---

## 3. Mathematical Trace & Canonical Workload Verification

### 3.1 Canonical Workload Specification
- **Platter Cylinders**: $200$ (Range: $0$ to $199$).
- **Initial Head**: $53$.
- **Pending Requests**: $[98, 183, 37, 122, 14, 124, 65, 67]$.
- **Initial Direction**: `UP`.

### 3.2 Algorithm Traces and Exact Head Movements

| Algorithm | Service Order Sequence | Movement Step Breakdown (From $\to$ To [Dist]) | Total Head Movement |
| :--- | :--- | :--- | :---: |
| **FCFS** | $98 \to 183 \to 37 \to 122 \to 14 \to 124 \to 65 \to 67$ | $53\to98 [45] \to 183 [85] \to 37 [146] \to 122 [85] \to 14 [108] \to 124 [110] \to 65 [59] \to 67 [2]$ | **640 cylinders** |
| **SSTF** | $65 \to 67 \to 37 \to 14 \to 98 \to 122 \to 124 \to 183$ | $53\to65 [12] \to 67 [2] \to 37 [30] \to 14 [23] \to 98 [84] \to 122 [24] \to 124 [2] \to 183 [59]$ | **236 cylinders** |
| **SCAN (UP)** | $65 \to 67 \to 98 \to 122 \to 124 \to 183 \to 37 \to 14$ | $53\to65 [12] \to 67 [2] \to 98 [31] \to 122 [24] \to 124 [2] \to 183 [59] \to 199 [16\text{ boundary}] \to 37 [162] \to 14 [23]$ | **331 cylinders** |
| **C-SCAN (UP)** | $65 \to 67 \to 98 \to 122 \to 124 \to 183 \to 14 \to 37$ | $53\to65 [12] \to 67 [2] \to 98 [31] \to 122 [24] \to 124 [2] \to 183 [59] \to 199 [16\text{ boundary}] \to 0 [199\text{ wrap}] \to 14 [14] \to 37 [23]$ | **382 cylinders** |
| **LOOK (UP)** | $65 \to 67 \to 98 \to 122 \to 124 \to 183 \to 37 \to 14$ | $53\to65 [12] \to 67 [2] \to 98 [31] \to 122 [24] \to 124 [2] \to 183 [59] \to 37 [146] \to 14 [23]$ | **299 cylinders** |
| **C-LOOK (UP)** | $65 \to 67 \to 98 \to 122 \to 124 \to 183 \to 14 \to 37$ | $53\to65 [12] \to 67 [2] \to 98 [31] \to 122 [24] \to 124 [2] \to 183 [59] \to 14 [169\text{ wrap}] \to 37 [23]$ | **322 cylinders** |

### 3.3 Total Head Movement Invariant
For all algorithms and workloads, the Total Head Movement (THM) strictly satisfies:
$$\text{THM} = \sum_{i=1}^{M} \text{step\_distance}_i$$
where each valid request is serviced exactly once ($M \ge \text{requests.size()}$).

---

## 4. CLI Usage Guide

The `kernox disk schedule` command provides interactive simulations:

```bash
# Run canonical SSTF workload
kernox disk schedule --algorithm sstf --head 53 --cylinders 200 --requests 98,183,37,122,14,124,65,67

# Run SCAN with direction UP on the built-in canonical workload
kernox disk schedule --algorithm scan --direction up --workload canonical

# Run C-SCAN with direction DOWN on the secondary workload
kernox disk schedule --algorithm cscan --direction down --workload secondary

# Run C-LOOK with custom request queue
kernox disk schedule --algorithm clook --head 100 --cylinders 250 --requests 45,190,12,88,220 --direction up
```

### Supported CLI Flags
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--algorithm`, `-a` | Algorithm name (`fcfs`, `sstf`, `scan`, `cscan`, `look`, `clook`) | Required |
| `--head`, `-h` | Initial head position ($0 \le \text{head} < \text{cylinders}$) | `53` (or workload default) |
| `--cylinders`, `-c` | Total disk cylinder count | `200` |
| `--requests`, `-r` | Comma-separated list of pending cylinder requests | None |
| `--direction`, `-d` | Initial head direction (`up` or `down`) | `up` |
| `--workload`, `-w` | Built-in workload name (`canonical` or `secondary`) | None |
| `--help` | Display command usage and option descriptions | — |

---

## 5. Physical Storage Architecture vs. Classical Simulation

> [!WARNING]
> **Educational Simulation Notice**:
> Classical disk-scheduling models calculate cylinder head travel assuming a uniform magnetic platter with constant mechanical seek latency. Real-world storage hardware behaves fundamentally differently.

### 5.1 Hard Disk Drives (HDDs)
1. **Rotational Latency**: Once the head arrives at the correct cylinder track, it must wait for the target sector to rotate underneath the read head ($\approx 2 - 4\,\text{ms}$ at 7200–15000 RPM). Seek distance alone does not determine total I/O latency.
2. **Track Skew & Variable Geometry**: Outer cylinders contain more sectors than inner cylinders (Zoned Bit Recording). Cylinder distances do not map linearly to physical track pitch.
3. **On-Disk Cache & TCQ/NCQ**: Modern SATA/SAS disks use Native Command Queuing (NCQ) to reorder requests inside the drive controller firmware based on rotational position and head trajectory simultaneously.

### 5.2 Solid-State Drives (SSDs & NVMe)
1. **Zero Mechanical Movement**: Flash memory cells have no platters, spindles, or physical actuator arms. Seek distance between logical block addresses (LBAs) is nonexistent.
2. **Flash Translation Layer (FTL)**: Logical block addresses are dynamically mapped to physical flash pages via wear-leveling algorithms. Two adjacent LBAs may reside on completely different flash dies or channels.
3. **Multi-Queue Parallelism**: NVMe drives utilize up to 64,000 hardware submission/completion queues with queue depths of 64,000 commands, executing I/O requests concurrently across parallel NAND flash channels.
