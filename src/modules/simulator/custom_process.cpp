#include "kernox/modules/simulator/custom_process.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace kernox::simulator {

core::Result<void> validate_device_config(const CustomDeviceConfiguration& config) {
    if (config.total_ram_mb == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Device RAM capacity must be greater than zero"
        );
    }

    if (config.moderate_threshold_ratio <= 0.0 || config.moderate_threshold_ratio >= 1.0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Moderate threshold ratio must be in range (0.0, 1.0)"
        );
    }

    if (config.critical_threshold_ratio <= 0.0 || config.critical_threshold_ratio > 1.0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Critical threshold ratio must be in range (0.0, 1.0]"
        );
    }

    if (config.moderate_threshold_ratio >= config.critical_threshold_ratio) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Moderate threshold ratio must be strictly less than critical threshold ratio"
        );
    }

    return core::Result<void>::ok();
}

core::Result<void> validate_process_input(const CustomProcessInput& input) {
    bool has_non_space = false;
    for (char ch : input.name) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            has_non_space = true;
            break;
        }
    }
    if (!has_non_space) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Process name cannot be empty"
        );
    }

    if (input.cpu_priority > intel::LOWEST_CPU_PRIORITY) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Process CPU priority must be in range [0, 99]"
        );
    }

    switch (input.importance) {
        case intel::ProcessImportance::Foreground:
        case intel::ProcessImportance::Visible:
        case intel::ProcessImportance::Service:
        case intel::ProcessImportance::Background:
        case intel::ProcessImportance::Cached:
            break;
        default:
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Invalid process importance tier"
            );
    }

    if (input.initial_memory_mb == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Initial memory must be greater than 0 MB"
        );
    }

    if (input.requested_memory_mb < input.initial_memory_mb) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Requested memory must be greater than or equal to initial memory"
        );
    }

    if (input.cpu_burst_ticks == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "CPU burst ticks must be greater than 0"
        );
    }

    for (const auto& ev : input.dynamic_events) {
        if (ev.tick < input.arrival_tick) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Dynamic memory event tick (" + std::to_string(ev.tick) +
                ") cannot occur before process arrival tick (" + std::to_string(input.arrival_tick) + ")"
            );
        }
    }

    return core::Result<void>::ok();
}

core::Result<void> validate_process_collection(
    const std::vector<CustomProcessInput>& processes,
    const CustomDeviceConfiguration& device_config
) {
    auto dev_res = validate_device_config(device_config);
    if (dev_res.is_err()) {
        return dev_res;
    }

    if (processes.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Process collection cannot be empty"
        );
    }

    std::set<std::string> names;
    uint64_t tick0_total_ram_mb = 0;

    for (const auto& proc : processes) {
        auto val_res = validate_process_input(proc);
        if (val_res.is_err()) {
            return val_res;
        }

        if (!names.insert(proc.name).second) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Duplicate process name detected: '" + proc.name + "'"
            );
        }

        if (proc.initial_memory_mb > device_config.total_ram_mb) {
            return core::Result<void>::err(
                core::ErrorCode::InvalidArgument,
                "Process '" + proc.name + "' initial memory (" + std::to_string(proc.initial_memory_mb) +
                " MB) exceeds total device RAM capacity (" + std::to_string(device_config.total_ram_mb) + " MB)"
            );
        }

        if (proc.arrival_tick == 0) {
            tick0_total_ram_mb += proc.initial_memory_mb;
        }
    }

    if (tick0_total_ram_mb > device_config.total_ram_mb) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Total initial process memory at tick 0 (" + std::to_string(tick0_total_ram_mb) +
            " MB) exceeds total device RAM capacity (" + std::to_string(device_config.total_ram_mb) + " MB)"
        );
    }

    return core::Result<void>::ok();
}

core::Result<void> validate_simulation_config(const CustomSimulationConfiguration& config) {
    bool has_non_space = false;
    for (char ch : config.scenario_name) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            has_non_space = true;
            break;
        }
    }
    if (!has_non_space) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Scenario name cannot be empty"
        );
    }

    return validate_process_collection(config.processes, config.device_config);
}

} // namespace kernox::simulator
