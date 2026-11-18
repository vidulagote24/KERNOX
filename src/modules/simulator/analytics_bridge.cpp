#include "kernox/modules/simulator/analytics_bridge.hpp"
#include "kernox/modules/experiment/exporter.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace kernox::simulator {

namespace {

// Helper: Ensure parent directories exist
bool ensure_parent_directory(const std::string& file_or_dir_path) {
    if (file_or_dir_path.empty()) return false;

    std::string path_copy = file_or_dir_path;
    // If it looks like a file path, strip filename
    size_t last_slash = path_copy.rfind('/');
    if (last_slash == std::string::npos) {
        return true; // Current directory
    }
    std::string dir = path_copy.substr(0, last_slash);

    // Iteratively create directories
    std::string curr;
    size_t start = 0;
    if (dir[0] == '/') {
        curr = "/";
        start = 1;
    }
    while (start < dir.size()) {
        size_t next_slash = dir.find('/', start);
        if (next_slash == std::string::npos) next_slash = dir.size();
        curr += dir.substr(start, next_slash - start);
        if (!curr.empty() && curr != "/") {
            ::mkdir(curr.c_str(), 0755);
        }
        curr += "/";
        start = next_slash + 1;
    }
    return true;
}

} // namespace

AnalyticsBridge::AnalyticsBridge(std::shared_ptr<adb::ICommandRunner> runner)
    : runner_(runner ? runner : std::make_shared<adb::SystemCommandRunner>()) {}

std::string AnalyticsBridge::discover_project_root() const {
    if (!project_root_override_.empty()) {
        return project_root_override_;
    }

    // 1. Resolve executable location via /proc/self/exe on Linux
    char buf[1024];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string exe_path(buf);

        // Iteratively search parent directories for KERNOX repository markers
        size_t last_slash = exe_path.rfind('/');
        while (last_slash != std::string::npos && last_slash > 0) {
            exe_path = exe_path.substr(0, last_slash);
            std::string marker_cmake = exe_path + "/CMakeLists.txt";
            std::string marker_analytics = exe_path + "/tools/analytics/src";
            if (::access(marker_cmake.c_str(), R_OK) == 0 &&
                ::access(marker_analytics.c_str(), R_OK) == 0) {
                return exe_path;
            }
            last_slash = exe_path.rfind('/');
        }
    }

    // 2. Relative traversal fallback from current working directory
    const std::vector<std::string> relative_candidates = {
        ".",
        "..",
        "../..",
        "../../.."
    };

    for (const auto& rel : relative_candidates) {
        std::string marker_cmake = rel + "/CMakeLists.txt";
        std::string marker_analytics = rel + "/tools/analytics/src";
        if (::access(marker_cmake.c_str(), R_OK) == 0 &&
            ::access(marker_analytics.c_str(), R_OK) == 0) {
            char real_buf[1024];
            if (::realpath(rel.c_str(), real_buf) != nullptr) {
                return std::string(real_buf);
            }
            return rel;
        }
    }

    return "";
}

void AnalyticsBridge::ensure_pythonpath() const {
    const char* cur = ::getenv("PYTHONPATH");
    std::string root = discover_project_root();
    std::string needed;
    if (!root.empty()) {
        needed = root + "/tools/analytics/src";
    } else {
        needed = "tools/analytics/src:../tools/analytics/src:../../tools/analytics/src";
    }

    if (cur && cur[0] != '\0') {
        std::string cur_str(cur);
        if (cur_str.find(needed) == std::string::npos && cur_str.find("tools/analytics/src") == std::string::npos) {
            std::string combined = needed + ":" + cur_str;
            ::setenv("PYTHONPATH", combined.c_str(), 1);
        }
    } else {
        ::setenv("PYTHONPATH", needed.c_str(), 1);
    }
}

std::string AnalyticsBridge::detect_python_interpreter() const {
    if (!python_override_.empty()) {
        if (runner_->is_executable_available(python_override_)) {
            return python_override_;
        }
    }

    // 1. Root-derived virtualenv paths
    std::string root = discover_project_root();
    if (!root.empty()) {
        std::string root_venv = root + "/.venv/bin/python";
        if (runner_->is_executable_available(root_venv)) {
            return root_venv;
        }
        std::string sub_venv = root + "/tools/analytics/.venv/bin/python";
        if (runner_->is_executable_available(sub_venv)) {
            return sub_venv;
        }
    }

    // 2. Relative candidates
    const std::vector<std::string> candidates = {
        ".venv/bin/python",
        "./.venv/bin/python",
        "tools/analytics/.venv/bin/python",
        "../.venv/bin/python",
        "../../.venv/bin/python",
    };

    for (const auto& path : candidates) {
        if (runner_->is_executable_available(path)) {
            return path;
        }
    }

    // 3. VIRTUAL_ENV environment variable
    const char* venv_env = std::getenv("VIRTUAL_ENV");
    if (venv_env && venv_env[0] != '\0') {
        std::string venv_bin = std::string(venv_env) + "/bin/python";
        if (runner_->is_executable_available(venv_bin)) {
            return venv_bin;
        }
    }

    // 4. PATH fallbacks
    if (runner_->is_executable_available("python3")) {
        return "python3";
    }
    if (runner_->is_executable_available("python")) {
        return "python";
    }

    return "";
}

core::Result<void> AnalyticsBridge::check_environment(std::chrono::milliseconds timeout) const {
    std::string python_bin = detect_python_interpreter();
    if (python_bin.empty()) {
        return core::Result<void>::err(
            core::ErrorCode::NotFound,
            "Python interpreter not detected. Ensure .venv or python3 is available in PATH."
        );
    }

    ensure_pythonpath();

    auto out_res = runner_->execute(
        python_bin,
        {"-c", "import kernox_analytics, pandas, matplotlib"},
        timeout
    );

    if (!out_res.is_ok()) {
        return core::Result<void>::err(out_res.error());
    }

    const auto& out = out_res.value();
    if (!out.is_success()) {
        std::string err_msg = "Python analytics dependencies check failed (exit code " +
                              std::to_string(out.exit_code) + ")";
        if (!out.stderr_content.empty()) {
            err_msg += ": " + out.stderr_content;
        }
        return core::Result<void>::err(core::ErrorCode::ProcessFailed, err_msg);
    }

    return core::Result<void>::ok();
}

core::Result<AnalyticsInvocationResult> AnalyticsBridge::generate_analytics(
    const std::string& input_json_path,
    const std::string& output_dir,
    std::chrono::milliseconds timeout
) {
    AnalyticsInvocationResult result;
    result.exported_json_path = input_json_path;
    result.output_directory = output_dir;
    result.report_markdown_path = output_dir + "/experiment_summary.md";

    if (input_json_path.empty()) {
        result.error_message = "Input JSON path cannot be empty.";
        return core::Result<AnalyticsInvocationResult>::err(
            core::ErrorCode::InvalidArgument, result.error_message
        );
    }

    // Verify input file exists
    if (::access(input_json_path.c_str(), R_OK) != 0) {
        result.error_message = "Input experiment JSON file does not exist: " + input_json_path;
        return core::Result<AnalyticsInvocationResult>::err(
            core::ErrorCode::NotFound, result.error_message
        );
    }

    std::string python_bin = detect_python_interpreter();
    if (python_bin.empty()) {
        result.error_message = "Python interpreter not found. Ensure .venv or python3 is available in PATH.";
        return core::Result<AnalyticsInvocationResult>::err(
            core::ErrorCode::NotFound, result.error_message
        );
    }

    ensure_pythonpath();
    ensure_parent_directory(output_dir + "/dummy");

    // Execute python -m kernox_analytics.cli report -i <input> -o <output>
    // Notice: Safely separated argument vector, zero shell escaping vulnerability
    const std::vector<std::string> args = {
        "-m", "kernox_analytics.cli",
        "report",
        "-i", input_json_path,
        "-o", output_dir
    };

    auto run_res = runner_->execute(python_bin, args, timeout);
    if (!run_res.is_ok()) {
        result.error_message = "Failed to launch Python subprocess: " + run_res.error().message;
        return core::Result<AnalyticsInvocationResult>::err(
            run_res.error().code, result.error_message
        );
    }

    const auto& out = run_res.value();
    result.exit_code = out.exit_code;
    result.stdout_output = out.stdout_content;
    result.stderr_output = out.stderr_content;

    if (out.timed_out) {
        result.success = false;
        result.error_message = "Python analytics generation timed out after " +
                              std::to_string(timeout.count()) + " ms.";
        return core::Result<AnalyticsInvocationResult>::err(
            core::ErrorCode::Timeout, result.error_message
        );
    }

    if (!out.is_success()) {
        result.success = false;
        result.error_message = "Python analytics CLI failed with exit code " +
                              std::to_string(out.exit_code);
        if (!out.stderr_content.empty()) {
            result.error_message += ": " + out.stderr_content;
        }
        return core::Result<AnalyticsInvocationResult>::err(
            core::ErrorCode::ProcessFailed, result.error_message
        );
    }

    result.success = true;
    result.generated_charts = {
        output_dir + "/policy_comparison.png",
        output_dir + "/process_survival.png",
        output_dir + "/memory_pressure.png",
        output_dir + "/execution_performance.png"
    };

    return core::Result<AnalyticsInvocationResult>::ok(std::move(result));
}

core::Result<AnalyticsInvocationResult> AnalyticsBridge::export_and_analyze(
    const experiment::ExperimentResult& result,
    const std::string& json_path,
    const std::string& output_dir,
    bool run_analytics
) {
    ensure_parent_directory(json_path);

    std::string json_payload = experiment::ExperimentExporter::to_json(result);
    auto write_res = experiment::ExperimentExporter::write_to_file(json_path, json_payload);
    if (!write_res.is_ok()) {
        return core::Result<AnalyticsInvocationResult>::err(write_res.error());
    }

    AnalyticsInvocationResult inv_res;
    inv_res.json_exported = true;
    inv_res.exported_json_path = json_path;
    inv_res.output_directory = output_dir;
    inv_res.report_markdown_path = output_dir + "/experiment_summary.md";

    if (!run_analytics) {
        inv_res.success = true;
        return core::Result<AnalyticsInvocationResult>::ok(std::move(inv_res));
    }

    // Run analytics. If analytics fails, JSON export remains saved and success=false with diagnostic.
    auto ana_res = generate_analytics(json_path, output_dir);
    if (!ana_res.is_ok()) {
        inv_res.success = false;
        inv_res.error_message = ana_res.error().message;
        // Return ok with success=false so caller receives json_exported=true + error diagnostic
        return core::Result<AnalyticsInvocationResult>::ok(std::move(inv_res));
    }

    auto full_res = std::move(ana_res).value();
    full_res.json_exported = true;
    return core::Result<AnalyticsInvocationResult>::ok(std::move(full_res));
}

core::Result<AnalyticsInvocationResult> AnalyticsBridge::export_and_analyze(
    const experiment::ExperimentComparison& comparison,
    const std::string& json_path,
    const std::string& output_dir,
    bool run_analytics
) {
    ensure_parent_directory(json_path);

    std::string json_payload = experiment::ExperimentExporter::to_json(comparison);
    auto write_res = experiment::ExperimentExporter::write_to_file(json_path, json_payload);
    if (!write_res.is_ok()) {
        return core::Result<AnalyticsInvocationResult>::err(write_res.error());
    }

    AnalyticsInvocationResult inv_res;
    inv_res.json_exported = true;
    inv_res.exported_json_path = json_path;
    inv_res.output_directory = output_dir;
    inv_res.report_markdown_path = output_dir + "/experiment_summary.md";

    if (!run_analytics) {
        inv_res.success = true;
        return core::Result<AnalyticsInvocationResult>::ok(std::move(inv_res));
    }

    // Run analytics. If analytics fails, JSON export remains saved and success=false with diagnostic.
    auto ana_res = generate_analytics(json_path, output_dir);
    if (!ana_res.is_ok()) {
        inv_res.success = false;
        inv_res.error_message = ana_res.error().message;
        // Return ok with success=false so caller receives json_exported=true + error diagnostic
        return core::Result<AnalyticsInvocationResult>::ok(std::move(inv_res));
    }

    auto full_res = std::move(ana_res).value();
    full_res.json_exported = true;
    return core::Result<AnalyticsInvocationResult>::ok(std::move(full_res));
}

} // namespace kernox::simulator
