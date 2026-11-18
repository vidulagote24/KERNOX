#include "kernox/modules/intel/process_model.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace kernox::intel {

std::atomic<SimulatedPid> SimulatedProcess::s_next_pid_{DEFAULT_INITIAL_SIM_PID};

std::string_view to_string(ProcessLifecycleState state) noexcept {
    switch (state) {
        case ProcessLifecycleState::Created:
            return "Created";
        case ProcessLifecycleState::Ready:
            return "Ready";
        case ProcessLifecycleState::Running:
            return "Running";
        case ProcessLifecycleState::Blocked:
            return "Blocked";
        case ProcessLifecycleState::Completed:
            return "Completed";
        case ProcessLifecycleState::Terminated:
            return "Terminated";
    }
    return "Unknown";
}

std::string_view to_string(ProcessImportance importance) noexcept {
    switch (importance) {
        case ProcessImportance::Foreground:
            return "Foreground";
        case ProcessImportance::Visible:
            return "Visible";
        case ProcessImportance::Service:
            return "Service";
        case ProcessImportance::Background:
            return "Background";
        case ProcessImportance::Cached:
            return "Cached";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& os, ProcessLifecycleState state) {
    return os << to_string(state);
}

std::ostream& operator<<(std::ostream& os, ProcessImportance importance) {
    return os << to_string(importance);
}

void SimulatedProcess::reset_pid_generator(SimulatedPid next_pid) noexcept {
    s_next_pid_.store(next_pid, std::memory_order_relaxed);
}

core::Result<SimulatedProcess> SimulatedProcess::create(
    std::string name,
    uint32_t cpu_priority,
    ProcessImportance importance,
    uint64_t memory_requirement_bytes,
    uint64_t cpu_burst_ticks,
    uint64_t creation_time_ticks
) {
    auto is_not_space = [](unsigned char ch) { return !std::isspace(ch); };
    auto first = std::find_if(name.begin(), name.end(), is_not_space);
    auto last = std::find_if(name.rbegin(), name.rend(), is_not_space).base();

    if (first >= last) {
        return core::Result<SimulatedProcess>::err(
            core::ErrorCode::InvalidArgument,
            "Process name cannot be empty or contain only whitespace"
        );
    }
    std::string trimmed_name(first, last);

    if (cpu_priority > LOWEST_CPU_PRIORITY) {
        return core::Result<SimulatedProcess>::err(
            core::ErrorCode::InvalidArgument,
            "CPU priority must be between 0 and 99 (provided: " + std::to_string(cpu_priority) + ")"
        );
    }

    if (cpu_burst_ticks == 0) {
        return core::Result<SimulatedProcess>::err(
            core::ErrorCode::InvalidArgument,
            "CPU burst ticks must be greater than zero"
        );
    }

    if (memory_requirement_bytes == 0) {
        return core::Result<SimulatedProcess>::err(
            core::ErrorCode::InvalidArgument,
            "Memory requirement must be greater than zero bytes"
        );
    }

    SimulatedPid pid = s_next_pid_.fetch_add(1, std::memory_order_relaxed);

    SimulatedProcess proc(
        pid,
        std::move(trimmed_name),
        cpu_priority,
        importance,
        memory_requirement_bytes,
        cpu_burst_ticks,
        creation_time_ticks
    );

    return core::Result<SimulatedProcess>::ok(std::move(proc));
}

SimulatedProcess::SimulatedProcess(
    SimulatedPid pid,
    std::string name,
    uint32_t cpu_priority,
    ProcessImportance importance,
    uint64_t memory_requirement_bytes,
    uint64_t cpu_burst_ticks,
    uint64_t creation_time_ticks
)
    : pid_(pid),
      name_(std::move(name)),
      state_(ProcessLifecycleState::Created),
      cpu_priority_(cpu_priority),
      cpu_burst_total_(cpu_burst_ticks),
      cpu_burst_remaining_(cpu_burst_ticks),
      cpu_time_executed_(0),
      importance_(importance),
      memory_{memory_requirement_bytes, 0, 0},
      creation_time_ticks_(creation_time_ticks),
      last_active_time_ticks_(creation_time_ticks),
      completion_time_ticks_(0),
      was_killed_under_pressure_(false),
      termination_reason_("") {}

core::Result<void> SimulatedProcess::mark_ready() {
    if (state_ == ProcessLifecycleState::Ready) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Process PID " + std::to_string(pid_) + " is already in Ready state"
        );
    }

    if (is_terminal()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot transition to Ready from terminal state: " + std::string(intel::to_string(state_))
        );
    }

    state_ = ProcessLifecycleState::Ready;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::dispatch_running(uint64_t current_time_ticks) {
    if (state_ == ProcessLifecycleState::Running) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Process PID " + std::to_string(pid_) + " is already in Running state"
        );
    }

    if (state_ != ProcessLifecycleState::Ready) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot dispatch to Running from non-Ready state: " + std::string(intel::to_string(state_))
        );
    }

    state_ = ProcessLifecycleState::Running;
    last_active_time_ticks_ = current_time_ticks;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::preempt_to_ready() {
    if (state_ != ProcessLifecycleState::Running) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot preempt non-running process in state: " + std::string(intel::to_string(state_))
        );
    }

    state_ = ProcessLifecycleState::Ready;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::block(std::string reason) {
    static_cast<void>(reason);
    if (state_ != ProcessLifecycleState::Running) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot block non-running process in state: " + std::string(intel::to_string(state_))
        );
    }

    state_ = ProcessLifecycleState::Blocked;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::unblock_to_ready() {
    if (state_ != ProcessLifecycleState::Blocked) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot unblock process not in Blocked state (current state: " + std::string(intel::to_string(state_)) + ")"
        );
    }

    state_ = ProcessLifecycleState::Ready;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::complete(uint64_t completion_time_ticks) {
    if (state_ != ProcessLifecycleState::Running) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot complete process not in Running state (current state: " + std::string(intel::to_string(state_)) + ")"
        );
    }

    state_ = ProcessLifecycleState::Completed;
    completion_time_ticks_ = completion_time_ticks;
    last_active_time_ticks_ = completion_time_ticks;
    cpu_burst_remaining_ = 0;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::terminate(
    std::string reason,
    uint64_t termination_time_ticks,
    bool killed_under_pressure
) {
    if (is_terminal()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot terminate process in terminal state: " + std::string(intel::to_string(state_))
        );
    }

    state_ = ProcessLifecycleState::Terminated;
    termination_reason_ = reason.empty() ? "terminated" : std::move(reason);
    completion_time_ticks_ = termination_time_ticks;
    last_active_time_ticks_ = termination_time_ticks;
    was_killed_under_pressure_ = killed_under_pressure;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::allocate_memory(uint64_t bytes) {
    if (is_terminal()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot allocate memory for terminated or completed process"
        );
    }

    if (bytes == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Memory allocation amount must be greater than zero"
        );
    }

    if (memory_.memory_consumed_bytes > std::numeric_limits<uint64_t>::max() - bytes) {
        return core::Result<void>::err(
            core::ErrorCode::ResourceExhausted,
            "Memory allocation causes 64-bit integer overflow"
        );
    }

    memory_.memory_consumed_bytes += bytes;
    if (memory_.memory_consumed_bytes > memory_.peak_memory_bytes) {
        memory_.peak_memory_bytes = memory_.memory_consumed_bytes;
    }

    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::release_memory(uint64_t bytes) {
    if (bytes == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Memory release amount must be greater than zero"
        );
    }

    if (bytes > memory_.memory_consumed_bytes) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot release " + std::to_string(bytes) + " bytes; only " +
            std::to_string(memory_.memory_consumed_bytes) + " bytes currently consumed"
        );
    }

    memory_.memory_consumed_bytes -= bytes;
    return core::Result<void>::ok();
}

core::Result<void> SimulatedProcess::update_memory_consumption(uint64_t total_bytes) {
    if (is_terminal()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot update memory consumption for terminated or completed process"
        );
    }

    memory_.memory_consumed_bytes = total_bytes;
    if (memory_.memory_consumed_bytes > memory_.peak_memory_bytes) {
        memory_.peak_memory_bytes = memory_.memory_consumed_bytes;
    }

    return core::Result<void>::ok();
}

uint64_t SimulatedProcess::reclaim_all_memory() noexcept {
    uint64_t reclaimed = memory_.memory_consumed_bytes;
    memory_.memory_consumed_bytes = 0;
    return reclaimed;
}

core::Result<void> SimulatedProcess::consume_cpu_time(uint64_t ticks, uint64_t current_time_ticks) {
    if (state_ != ProcessLifecycleState::Running) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidState,
            "Cannot consume CPU time when process is not in Running state (current state: " +
            std::string(intel::to_string(state_)) + ")"
        );
    }

    if (ticks == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "CPU ticks to consume must be greater than zero"
        );
    }

    if (ticks >= cpu_burst_remaining_) {
        cpu_time_executed_ += cpu_burst_remaining_;
        cpu_burst_remaining_ = 0;
    } else {
        cpu_burst_remaining_ -= ticks;
        cpu_time_executed_ += ticks;
    }

    last_active_time_ticks_ = current_time_ticks;
    return core::Result<void>::ok();
}

void SimulatedProcess::set_importance(ProcessImportance new_importance) noexcept {
    importance_ = new_importance;
}

core::Result<void> SimulatedProcess::set_cpu_priority(uint32_t new_priority) {
    if (new_priority > LOWEST_CPU_PRIORITY) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "CPU priority must be between 0 and 99 (provided: " + std::to_string(new_priority) + ")"
        );
    }

    cpu_priority_ = new_priority;
    return core::Result<void>::ok();
}

sim::ProcessTask SimulatedProcess::to_task() const noexcept {
    return sim::ProcessTask{
        .pid = pid_,
        .arrival_time = creation_time_ticks_,
        .burst_time = cpu_burst_remaining_,
        .priority = cpu_priority_
    };
}

std::string SimulatedProcess::to_string() const {
    std::ostringstream oss;
    oss << "SimulatedProcess[PID: " << pid_
        << ", Name: \"" << name_ << "\""
        << ", State: " << intel::to_string(state_)
        << ", Importance: " << intel::to_string(importance_)
        << " (" << static_cast<uint32_t>(importance_) << ")"
        << ", CPU Prio: " << cpu_priority_
        << ", Burst: " << cpu_burst_remaining_ << "/" << cpu_burst_total_
        << ", Executed: " << cpu_time_executed_
        << ", Resident Mem: " << memory_.memory_consumed_bytes << " B"
        << ", Peak Mem: " << memory_.peak_memory_bytes << " B"
        << ", Required Mem: " << memory_.memory_requirement_bytes << " B";

    if (was_killed_under_pressure_) {
        oss << ", Killed Under Pressure: YES (" << termination_reason_ << ")";
    } else if (state_ == ProcessLifecycleState::Terminated) {
        oss << ", Terminated Reason: " << termination_reason_;
    } else if (state_ == ProcessLifecycleState::Completed) {
        oss << ", Completed At: " << completion_time_ticks_ << " ticks";
    }

    oss << "]";
    return oss.str();
}

} // namespace kernox::intel
