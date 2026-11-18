#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/experiment/workload.hpp"
#include "kernox/modules/simulator/custom_process.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kernox::simulator {

// Builder class translating user-specified process inputs and device parameters
// into an immutable, deterministically hashed experiment::WorkloadDefinition.
class CustomWorkloadBuilder {
public:
    CustomWorkloadBuilder() = default;

    CustomWorkloadBuilder& set_device_config(CustomDeviceConfiguration config);
    CustomWorkloadBuilder& set_scenario_name(std::string name);
    CustomWorkloadBuilder& set_seed(uint64_t seed);
    CustomWorkloadBuilder& add_process(CustomProcessInput process);
    CustomWorkloadBuilder& set_processes(std::vector<CustomProcessInput> processes);

    [[nodiscard]] const CustomDeviceConfiguration& device_config() const noexcept { return config_.device_config; }
    [[nodiscard]] const std::vector<CustomProcessInput>& processes() const noexcept { return config_.processes; }
    [[nodiscard]] const CustomSimulationConfiguration& simulation_config() const noexcept { return config_; }

    // Validates inputs and translates to an experiment::WorkloadDefinition with WorkloadScenarioType::Custom.
    [[nodiscard]] core::Result<experiment::WorkloadDefinition> build() const;

    // Static convenience method translating from device configuration and process list.
    static core::Result<experiment::WorkloadDefinition> build_from(
        const CustomDeviceConfiguration& device_config,
        const std::vector<CustomProcessInput>& processes,
        uint64_t seed = 42,
        std::string scenario_name = "CustomInteractive"
    );

    // Static convenience method translating from full CustomSimulationConfiguration.
    static core::Result<experiment::WorkloadDefinition> build_from(
        const CustomSimulationConfiguration& config
    );

private:
    CustomSimulationConfiguration config_{};
};

} // namespace kernox::simulator
