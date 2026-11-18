#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/adb/bridge.hpp"
#include "kernox/modules/experiment/experiment_runner.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace kernox::simulator {

// Result of invoking the Python analytics generation pipeline.
struct AnalyticsInvocationResult {
    bool success{false};
    bool json_exported{false};
    int exit_code{-1};
    std::string exported_json_path{};
    std::string output_directory{};
    std::string report_markdown_path{};
    std::vector<std::string> generated_charts{};
    std::string stdout_output{};
    std::string stderr_output{};
    std::string error_message{};
};

// Integrates C++ interactive simulator exports with the Python analytics package.
// Safely executes python subprocesses via ICommandRunner with argument separation.
class AnalyticsBridge {
public:
    explicit AnalyticsBridge(std::shared_ptr<adb::ICommandRunner> runner = nullptr);

    // Discovers the KERNOX repository root independently of CWD (e.g. via /proc/self/exe).
    [[nodiscard]] std::string discover_project_root() const;

    // Discovers Python interpreter. Checks project-root .venv/bin/python first, then PATH fallbacks.
    [[nodiscard]] std::string detect_python_interpreter() const;

    // Validates python availability and required analytics dependencies (kernox_analytics, pandas, matplotlib).
    [[nodiscard]] core::Result<void> check_environment(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(20000)
    ) const;

    // Invokes Python analytics CLI on an already-exported experiment JSON file.
    [[nodiscard]] core::Result<AnalyticsInvocationResult> generate_analytics(
        const std::string& input_json_path,
        const std::string& output_dir = "artifacts/analytics/interactive_simulation",
        std::chrono::milliseconds timeout = std::chrono::milliseconds(60000)
    );

    // Exports an ExperimentResult to JSON, then runs analytics if requested.
    // If analytics fails, JSON export remains saved and Result contains non-fatal diagnostic.
    [[nodiscard]] core::Result<AnalyticsInvocationResult> export_and_analyze(
        const experiment::ExperimentResult& result,
        const std::string& json_path = "artifacts/experiments/interactive_simulation.json",
        const std::string& output_dir = "artifacts/analytics/interactive_simulation",
        bool run_analytics = true
    );

    // Exports an ExperimentComparison to JSON, then runs analytics if requested.
    // If analytics fails, JSON export remains saved and Result contains non-fatal diagnostic.
    [[nodiscard]] core::Result<AnalyticsInvocationResult> export_and_analyze(
        const experiment::ExperimentComparison& comparison,
        const std::string& json_path = "artifacts/experiments/interactive_comparison.json",
        const std::string& output_dir = "artifacts/analytics/interactive_comparison",
        bool run_analytics = true
    );

    // Overrides discovered project root path (useful for testing or custom layouts)
    void set_project_root_override(std::string root_path) {
        project_root_override_ = std::move(root_path);
    }

    [[nodiscard]] const std::string& project_root_override() const noexcept {
        return project_root_override_;
    }

    // Overrides discovered python interpreter path (useful for testing or specific envs)
    void set_python_override(std::string python_path) {
        python_override_ = std::move(python_path);
    }

    [[nodiscard]] const std::string& python_override() const noexcept {
        return python_override_;
    }

    [[nodiscard]] std::shared_ptr<adb::ICommandRunner> command_runner() const noexcept {
        return runner_;
    }

private:
    void ensure_pythonpath() const;

    std::shared_ptr<adb::ICommandRunner> runner_;
    std::string project_root_override_{};
    std::string python_override_{};
};

} // namespace kernox::simulator
