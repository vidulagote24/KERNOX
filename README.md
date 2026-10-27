# KERNOX

**A Linux-Based Operating System Resource Management and Benchmarking Platform**

KERNOX is an educational and benchmarking operating system platform designed for academic and industry evaluation. The project bridges the gap between theoretical operating system algorithms and real-world Linux kernel system interactions.

---

## Architecture Overview

KERNOX is designed with a strict duality principle:
- **Real POSIX System Modules:** Hands-on Linux kernel interfaces (`fork`, `execve`, `pthreads`, `pipe`, `/proc`, `ptrace`).
- **Algorithmic Simulation Modules:** High-fidelity discrete-event simulators for theoretical OS concepts (CPU scheduling, Page replacement, Disk scheduling, Banker's safety algorithm, Memory partitioning).

For detailed architecture, interfaces, and module specifications, see [docs/TECHNICAL_DESIGN.md](docs/TECHNICAL_DESIGN.md).

---

## Directory Structure

```text
KERNOX/
├── CMakeLists.txt              # Top-level CMake configuration
├── CMakePresets.json           # Presets for Debug, Release, ASan, and TSan
├── README.md                   # Project overview & build instructions
├── .gitignore                  # Git ignore patterns
├── docs/
│   ├── TECHNICAL_DESIGN.md     # Engineering design and roadmap
│   ├── CPU_SCHEDULING.md       # CPU scheduling algorithms & metrics spec
│   ├── DISK_SCHEDULING.md      # Disk scheduling algorithms & trace spec
│   ├── DEADLOCK_MANAGEMENT.md  # Banker's algorithm, detection & RAG spec
│   └── MEMORY_MANAGEMENT.md    # Allocation, paging, translation & replacement spec
├── include/
│   └── kernox/
│       ├── core/               # Foundation utilities (Result, RAII, Timer, Logger)
│       ├── modules/sim/        # Simulation engines (CPU, Disk, Deadlock, Memory)
│       └── cli/                # CLI commands and dispatcher
├── src/
│   ├── main.cpp                # Executable entry point
│   ├── core/                   # Implementation of core primitives
│   ├── modules/sim/            # Implementation of simulation engines
│   └── cli/                    # CLI parsing and command execution
├── tests/                      # CTest automated test suite
├── workloads/                  # Workload definitions for benchmarks
└── scripts/
    └── test.sh                 # Test runner script
```

---

## Prerequisites

- **OS:** Linux (Ubuntu 24.04 LTS recommended) or WSL 2 on Windows
- **Compiler:** Modern C++20 compiler (`g++ >= 13` or `clang++ >= 16`)
- **Build System:** `cmake >= 3.25` and `ninja` (or `make`)

---

## Building and Running

### Using CMake Presets (Recommended)

```bash
# Configure and build Debug preset
cmake --preset dev-debug
cmake --build --preset dev-debug

# Run tests
ctest --preset dev-debug --output-on-failure

# Execute the application
./build/debug/bin/kernox --help
./build/debug/bin/kernox status
./build/debug/bin/kernox self-test

# Run CPU Scheduling Simulations
./build/debug/bin/kernox schedule --algorithm fcfs --workload basic
./build/debug/bin/kernox schedule --algorithm srtf --workload arrivals
./build/debug/bin/kernox schedule --algorithm rr --quantum 2 --workload basic
./build/debug/bin/kernox schedule --algorithm mlfq --workload arrivals

# Run Disk Scheduling Simulations
./build/debug/bin/kernox disk schedule --algorithm sstf --head 53 --cylinders 200 --requests 98,183,37,122,14,124,65,67
./build/debug/bin/kernox disk schedule --algorithm scan --direction up --workload canonical
./build/debug/bin/kernox disk schedule --algorithm cscan --direction down --workload secondary

# Run Deadlock Simulations
./build/debug/bin/kernox deadlock banker --workload safe
./build/debug/bin/kernox deadlock detect --workload detection-demo
./build/debug/bin/kernox deadlock graph --workload cycle-demo
./build/debug/bin/kernox deadlock request --workload safe --process 1 --request 1,0,2

# Run Memory Management Simulations
./build/debug/bin/kernox memory allocate --algorithm first-fit --workload allocation-demo
./build/debug/bin/kernox memory allocate --algorithm best-fit --workload allocation-demo
./build/debug/bin/kernox memory paging --workload paging-demo
./build/debug/bin/kernox memory translate --process 0 --address 4096 --workload paging-demo
./build/debug/bin/kernox memory replace --algorithm lru --workload page-reference-demo --frames 3
./build/debug/bin/kernox memory ws --workload page-reference-demo --window 4
```

### Manual Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/kernox status
```

---

## License

KERNOX is developed for academic and industry research. All rights reserved.
