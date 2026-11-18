#pragma once

#include "kernox/modules/experiment/experiment_runner.hpp"
#include "kernox/modules/reclaim/pressure_detector.hpp"
#include "kernox/modules/simulator/custom_process.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::simulator {

// Reusable ASCII visualization engine rendering terminal charts,
// utilization bars, memory distribution tables, timeline graphs, and comparison matrices.
class AsciiVisualizer {
public:
    // Formats a single horizontal progress/utilization bar.
    // e.g.: "[████████████████████░░░░░░░░░░░░░░░░░░░░] 50.0%"
    [[nodiscard]] static std::string render_utilization_bar(
        uint64_t used_bytes,
        uint64_t total_bytes,
        double moderate_ratio = 0.75,
        double critical_ratio = 0.90,
        size_t bar_width = 30,
        bool use_color = false
    );

    // Formats a complete device memory overview card.
    [[nodiscard]] static std::string render_memory_overview(
        uint64_t used_bytes,
        uint64_t total_bytes,
        const reclaim::PressureThresholds& thresholds,
        bool use_color = false
    );

    // Formats horizontal memory share bars for post-simulation process records.
    [[nodiscard]] static std::string render_process_memory_distribution(
        const std::vector<experiment::ProcessExecutionRecord>& records,
        uint64_t total_capacity_bytes,
        size_t max_bar_width = 20,
        bool use_color = false
    );

    // Formats horizontal memory share bars for pre-simulation custom process inputs.
    [[nodiscard]] static std::string render_process_input_distribution(
        const std::vector<CustomProcessInput>& processes,
        uint64_t total_capacity_bytes,
        size_t max_bar_width = 20,
        bool use_color = false
    );

    // Formats a multi-line ASCII timeline graph of memory utilization over simulation ticks.
    [[nodiscard]] static std::string render_timeline_chart(
        const std::vector<experiment::MemoryTimelinePoint>& timeline,
        uint64_t total_capacity_bytes,
        const reclaim::PressureThresholds& thresholds,
        size_t chart_height = 8,
        size_t chart_width = 50,
        bool use_color = false
    );

    // Formats a structured table of reclamation decisions and victim selection rationales.
    [[nodiscard]] static std::string render_reclamation_log(
        const std::vector<reclaim::ReclamationDecision>& log,
        bool use_color = false
    );

    // Formats a 4-policy comparison matrix with visual survival & reclamation bars.
    [[nodiscard]] static std::string render_comparison_matrix(
        const experiment::ExperimentComparison& comparison,
        bool use_color = false
    );

    // Returns default terminal width for text alignment (80-100 columns).
    [[nodiscard]] static size_t get_terminal_width() noexcept;

private:
    static std::string color_code(std::string_view name, bool use_color);
};

} // namespace kernox::simulator
