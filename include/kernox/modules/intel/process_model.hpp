#pragma once

#include "kernox/core/result.hpp"

#include <atomic>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace kernox::intel {

// Distinct simulated PID type, isolated from host OS PIDs.
using SimulatedPid = uint32_t;

// Simulated lifecycle states.
enum class ProcessLifecycleState : uint32_t {
    Created = 0,
    Ready,
    Running,
    Blocked,
    Completed,
    Terminated
};

// Android-inspired importance classification.
// Higher numeric score indicates lower importance (more eligible for termination under pressure).
enum class ProcessImportance : uint32_t {
    Foreground = 100, // Active user interaction / top focused task
    Visible    = 200, // Visible to user but not focused
    Service    = 300, // Background execution with user awareness (e.g., media playback)
    Background = 400, // Invisible background process
    Cached     = 500  // Empty / cached process retained for rapid relaunch
};

// Numerical priority bounds for CPU scheduling (decoupled from mobile importance).
// 0 is the highest CPU scheduling priority; 99 is the lowest.
inline constexpr uint32_t HIGHEST_CPU_PRIORITY = 0;
inline constexpr uint32_t LOWEST_CPU_PRIORITY = 99;

// Default initial PID for simulated processes.
inline constexpr SimulatedPid DEFAULT_INITIAL_SIM_PID = 1000;

[[nodiscard]] std::string_view to_string(ProcessLifecycleState state) noexcept;
[[nodiscard]] std::string_view to_string(ProcessImportance importance) noexcept;

std::ostream& operator<<(std::ostream& os, ProcessLifecycleState state);
std::ostream& operator<<(std::ostream& os, ProcessImportance importance);

// Resident and peak memory metrics in bytes.
struct ProcessResourceFootprint {
    uint64_t memory_requirement_bytes{0}; // Declared total capacity required
    uint64_t memory_consumed_bytes{0};    // Current resident memory consumption
    uint64_t peak_memory_bytes{0};        // High-water mark of memory consumption
};

// Comprehensive simulated application process model for Android/Linux resource management.
class SimulatedProcess {
public:
    // Factory method with full parameter validation.
    static core::Result<SimulatedProcess> create(
        std::string name,
        uint32_t cpu_priority,
        ProcessImportance importance,
        uint64_t memory_requirement_bytes,
        uint64_t cpu_burst_ticks,
        uint64_t creation_time_ticks = 0
    );

    // Resets the simulated PID counter (primarily for deterministic unit testing).
    static void reset_pid_generator(SimulatedPid next_pid = DEFAULT_INITIAL_SIM_PID) noexcept;

    // --- State Machine Transitions ---

    // Transition: Created / Blocked / Running -> Ready
    core::Result<void> mark_ready();

    // Transition: Ready -> Running
    core::Result<void> dispatch_running(uint64_t current_time_ticks = 0);

    // Transition: Running -> Ready (CPU preemption)
    core::Result<void> preempt_to_ready();

    // Transition: Running -> Blocked (I/O or resource wait)
    core::Result<void> block(std::string reason = "");

    // Transition: Blocked -> Ready (I/O or resource fulfilled)
    core::Result<void> unblock_to_ready();

    // Transition: Running -> Completed (Normal task completion)
    core::Result<void> complete(uint64_t completion_time_ticks = 0);

    // Transition: Created / Ready / Running / Blocked -> Terminated (Signal, abort, or LMK reclaim)
    core::Result<void> terminate(
        std::string reason = "terminated",
        uint64_t termination_time_ticks = 0,
        bool killed_under_pressure = false
    );

    // --- Memory Accounting (Bytes) ---

    // Increments memory consumption and updates peak high-water mark.
    core::Result<void> allocate_memory(uint64_t bytes);

    // Decrements memory consumption with underflow protection.
    core::Result<void> release_memory(uint64_t bytes);

    // Directly updates current consumption and adjusts peak high-water mark.
    core::Result<void> update_memory_consumption(uint64_t total_bytes);

    // Reclaims all resident memory (sets consumption to 0, returns reclaimed bytes).
    uint64_t reclaim_all_memory() noexcept;

    // --- CPU Accounting ---

    // Consumes execution ticks while Running, decrementing remaining burst.
    core::Result<void> consume_cpu_time(uint64_t ticks, uint64_t current_time_ticks = 0);

    // --- Priority & Importance Mutation ---

    // Mutates Android-inspired lifecycle importance.
    void set_importance(ProcessImportance new_importance) noexcept;

    // Mutates CPU scheduling priority with [0, 99] bounds validation.
    core::Result<void> set_cpu_priority(uint32_t new_priority);


    // --- Query Accessors ---

    [[nodiscard]] SimulatedPid pid() const noexcept { return pid_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] ProcessLifecycleState state() const noexcept { return state_; }
    [[nodiscard]] uint32_t cpu_priority() const noexcept { return cpu_priority_; }
    [[nodiscard]] uint64_t cpu_burst_total() const noexcept { return cpu_burst_total_; }
    [[nodiscard]] uint64_t cpu_burst_remaining() const noexcept { return cpu_burst_remaining_; }
    [[nodiscard]] uint64_t cpu_time_executed() const noexcept { return cpu_time_executed_; }
    [[nodiscard]] ProcessImportance importance() const noexcept { return importance_; }
    [[nodiscard]] const ProcessResourceFootprint& memory() const noexcept { return memory_; }
    [[nodiscard]] uint64_t creation_time_ticks() const noexcept { return creation_time_ticks_; }
    [[nodiscard]] uint64_t last_active_time_ticks() const noexcept { return last_active_time_ticks_; }
    [[nodiscard]] uint64_t completion_time_ticks() const noexcept { return completion_time_ticks_; }
    [[nodiscard]] bool was_killed_under_pressure() const noexcept { return was_killed_under_pressure_; }
    [[nodiscard]] const std::string& termination_reason() const noexcept { return termination_reason_; }

    [[nodiscard]] bool is_alive() const noexcept {
        return state_ != ProcessLifecycleState::Completed &&
               state_ != ProcessLifecycleState::Terminated;
    }

    [[nodiscard]] bool is_terminal() const noexcept {
        return state_ == ProcessLifecycleState::Completed ||
               state_ == ProcessLifecycleState::Terminated;
    }

    [[nodiscard]] std::string to_string() const;

private:
    SimulatedProcess(
        SimulatedPid pid,
        std::string name,
        uint32_t cpu_priority,
        ProcessImportance importance,
        uint64_t memory_requirement_bytes,
        uint64_t cpu_burst_ticks,
        uint64_t creation_time_ticks
    );

    static std::atomic<SimulatedPid> s_next_pid_;

    SimulatedPid pid_{0};
    std::string name_{};
    ProcessLifecycleState state_{ProcessLifecycleState::Created};

    uint32_t cpu_priority_{HIGHEST_CPU_PRIORITY};
    uint64_t cpu_burst_total_{0};
    uint64_t cpu_burst_remaining_{0};
    uint64_t cpu_time_executed_{0};

    ProcessImportance importance_{ProcessImportance::Cached};
    ProcessResourceFootprint memory_{};

    uint64_t creation_time_ticks_{0};
    uint64_t last_active_time_ticks_{0};
    uint64_t completion_time_ticks_{0};

    bool was_killed_under_pressure_{false};
    std::string termination_reason_{};
};

} // namespace kernox::intel
