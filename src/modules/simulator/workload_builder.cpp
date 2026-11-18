#include "kernox/modules/simulator/workload_builder.hpp"

#include <utility>

namespace kernox::simulator {

CustomWorkloadBuilder& CustomWorkloadBuilder::set_device_config(CustomDeviceConfiguration config) {
    config_.device_config = config;
    return *this;
}

CustomWorkloadBuilder& CustomWorkloadBuilder::set_scenario_name(std::string name) {
    config_.scenario_name = std::move(name);
    return *this;
}

CustomWorkloadBuilder& CustomWorkloadBuilder::set_seed(uint64_t seed) {
    config_.seed = seed;
    return *this;
}

CustomWorkloadBuilder& CustomWorkloadBuilder::add_process(CustomProcessInput process) {
    config_.processes.push_back(std::move(process));
    return *this;
}

CustomWorkloadBuilder& CustomWorkloadBuilder::set_processes(std::vector<CustomProcessInput> processes) {
    config_.processes = std::move(processes);
    return *this;
}

core::Result<experiment::WorkloadDefinition> CustomWorkloadBuilder::build() const {
    auto val_res = validate_simulation_config(config_);
    if (val_res.is_err()) {
        return core::Result<experiment::WorkloadDefinition>::err(val_res.error());
    }

    std::vector<experiment::ProcessWorkloadSpec> specs;
    specs.reserve(config_.processes.size());

    for (const auto& proc : config_.processes) {
        experiment::ProcessWorkloadSpec spec;
        spec.name = proc.name;
        spec.cpu_priority = proc.cpu_priority;
        spec.importance = proc.importance;
        spec.initial_memory_bytes = proc.initial_memory_mb * 1024ULL * 1024ULL;
        spec.requested_memory_bytes = proc.requested_memory_mb * 1024ULL * 1024ULL;
        spec.cpu_burst_ticks = proc.cpu_burst_ticks;
        spec.arrival_tick = proc.arrival_tick;

        spec.dynamic_events.reserve(proc.dynamic_events.size());
        for (const auto& ev : proc.dynamic_events) {
            experiment::MemoryDemandEvent dev;
            dev.tick = ev.tick;
            dev.delta_bytes = static_cast<int64_t>(ev.delta_mb) * 1024LL * 1024LL;
            dev.reason = ev.reason;
            spec.dynamic_events.push_back(std::move(dev));
        }

        specs.push_back(std::move(spec));
    }

    experiment::WorkloadDefinition def;
    def.scenario_name = config_.scenario_name;
    def.scenario_type = experiment::WorkloadScenarioType::Custom;
    def.seed = config_.seed;
    def.total_memory_bytes = config_.device_config.total_ram_mb * 1024ULL * 1024ULL;
    def.process_specs = std::move(specs);
    def.fingerprint = def.compute_fingerprint();

    return core::Result<experiment::WorkloadDefinition>::ok(std::move(def));
}

core::Result<experiment::WorkloadDefinition> CustomWorkloadBuilder::build_from(
    const CustomDeviceConfiguration& device_config,
    const std::vector<CustomProcessInput>& processes,
    uint64_t seed,
    std::string scenario_name
) {
    CustomSimulationConfiguration cfg{
        .device_config = device_config,
        .processes = processes,
        .seed = seed,
        .scenario_name = std::move(scenario_name)
    };
    return build_from(cfg);
}

core::Result<experiment::WorkloadDefinition> CustomWorkloadBuilder::build_from(
    const CustomSimulationConfiguration& config
) {
    CustomWorkloadBuilder builder;
    builder.set_device_config(config.device_config);
    builder.set_scenario_name(config.scenario_name);
    builder.set_seed(config.seed);
    builder.set_processes(config.processes);
    return builder.build();
}

} // namespace kernox::simulator
