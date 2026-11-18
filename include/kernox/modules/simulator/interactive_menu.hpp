#pragma once

#include "kernox/modules/experiment/experiment_runner.hpp"
#include "kernox/modules/simulator/analytics_bridge.hpp"
#include "kernox/modules/simulator/ascii_visualizer.hpp"
#include "kernox/modules/simulator/custom_process.hpp"
#include "kernox/modules/simulator/terminal_reader.hpp"
#include "kernox/modules/simulator/workload_builder.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::simulator {

// Supported reclamation policies for interactive simulation.
enum class SelectedPolicy : uint32_t {
    Fifo = 1,
    Lru = 2,
    Priority = 3,
    ImportanceAware = 4
};

[[nodiscard]] std::string_view to_string(SelectedPolicy pol) noexcept;

// Tracks the most recent simulation run type to prevent silent stale export.
enum class LatestSimulationType {
    None,
    SingleResult,
    Comparison
};

// Session-level record of a single simulation or comparison run.
struct HistoryEntry {
    size_t sequence_id{0};
    std::string timestamp{};
    std::string policy_name{};
    std::string workload_fingerprint{};
    size_t total_processes{0};
    size_t processes_survived{0};
    double survival_rate{0.0};
    double foreground_survival_rate{0.0};
    uint64_t reclaimed_bytes{0};
    uint64_t peak_used_bytes{0};
    uint64_t capacity_bytes{0};
    double average_utilization_ratio{0.0};
    uint64_t wall_clock_elapsed_microseconds{0};
    bool is_comparison{false};
    std::string summary_text{};
    std::string visual_overview{};
    std::string timeline_chart{};
    std::string reclamation_log{};
    std::string comparison_matrix{};
};

// Full interactive terminal environment for the Android Resource Simulator.
class InteractiveSimulator {
public:
    explicit InteractiveSimulator(
        std::istream& in = std::cin,
        std::ostream& out = std::cout,
        bool use_color = true,
        bool enable_post_simulation_prompt = false
    );

    // Starts the main interactive loop. Returns exit status (0 on normal exit).
    int run();

    // Menu handlers (public for direct testing without running infinite loop)
    void handle_configure_device();
    void handle_add_process();
    void handle_edit_process();
    void handle_remove_process();
    void handle_view_processes();
    void handle_select_policy();
    void handle_run_simulation();
    void handle_compare_policies();
    void handle_view_history();
    void handle_export_and_analytics();

    // Accessors for state inspection and programmatic verification
    [[nodiscard]] const CustomDeviceConfiguration& device_config() const noexcept { return device_config_; }
    [[nodiscard]] const std::vector<CustomProcessInput>& processes() const noexcept { return processes_; }
    [[nodiscard]] SelectedPolicy selected_policy() const noexcept { return selected_policy_; }
    [[nodiscard]] const std::vector<HistoryEntry>& history() const noexcept { return history_; }
    [[nodiscard]] const std::optional<experiment::ExperimentResult>& last_result() const noexcept { return last_result_; }
    [[nodiscard]] const std::optional<experiment::ExperimentComparison>& last_comparison() const noexcept { return last_comparison_; }
    [[nodiscard]] LatestSimulationType latest_simulation_type() const noexcept { return latest_simulation_type_; }
    [[nodiscard]] bool config_modified_since_run() const noexcept { return config_modified_since_run_; }
    [[nodiscard]] bool enable_post_simulation_prompt() const noexcept { return enable_post_simulation_prompt_; }
    [[nodiscard]] std::shared_ptr<AnalyticsBridge> analytics_bridge() const noexcept { return analytics_bridge_; }

    void set_device_config(CustomDeviceConfiguration config) {
        device_config_ = config;
        config_modified_since_run_ = true;
    }
    void add_process(CustomProcessInput process) {
        processes_.push_back(std::move(process));
        config_modified_since_run_ = true;
    }
    void set_processes(std::vector<CustomProcessInput> processes) {
        processes_ = std::move(processes);
        config_modified_since_run_ = true;
    }
    void set_policy(SelectedPolicy pol) { selected_policy_ = pol; }
    void set_seed(uint64_t seed) { seed_ = seed; }
    void set_enable_post_simulation_prompt(bool enable) noexcept { enable_post_simulation_prompt_ = enable; }
    void set_analytics_bridge(std::shared_ptr<AnalyticsBridge> bridge) { analytics_bridge_ = std::move(bridge); }

private:
    void print_header();
    void print_main_menu();
    void print_process_table();
    [[nodiscard]] std::string format_time_now();

    TerminalReader reader_;
    std::ostream& out_;
    bool use_color_{false};
    bool enable_post_simulation_prompt_{false};

    CustomDeviceConfiguration device_config_{
        .total_ram_mb = 2048,
        .moderate_threshold_ratio = 0.75,
        .critical_threshold_ratio = 0.90
    };
    std::vector<CustomProcessInput> processes_{};
    SelectedPolicy selected_policy_{SelectedPolicy::ImportanceAware};
    uint64_t seed_{42};
    std::vector<HistoryEntry> history_{};
    size_t next_history_id_{1};
    std::optional<experiment::ExperimentResult> last_result_{std::nullopt};
    std::optional<experiment::ExperimentComparison> last_comparison_{std::nullopt};
    LatestSimulationType latest_simulation_type_{LatestSimulationType::None};
    bool config_modified_since_run_{false};
    std::shared_ptr<AnalyticsBridge> analytics_bridge_{nullptr};
};

} // namespace kernox::simulator
