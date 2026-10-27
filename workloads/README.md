# KERNOX Workloads

This directory contains standardized workloads for deterministic algorithm simulations and benchmarks.

## Workload Categories

1. **CPU Scheduling (`cpu_workload_*.json`):**
   - Task lists with Arrival Times ($T_a$), Burst Times ($T_b$), and Priorities.
2. **Virtual Memory (`vmem_reference_*.json`):**
   - Reference strings (sequence of page numbers) and test frame counts.
3. **Disk Scheduling (`disk_requests_*.json`):**
   - Cylinder request tracks, initial head location, and seek boundaries.
4. **Deadlock Management (`deadlock_state_*.json`):**
   - Resource allocation matrices: Allocation, Max, and Available vectors.

Standardized sample workloads will be populated alongside simulation modules in subsequent milestones.
