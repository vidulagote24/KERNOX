#include "kernox/modules/simulator/ascii_visualizer.hpp"

#include "kernox/modules/intel/process_model.hpp"
#include "kernox/modules/reclaim/pressure_detector.hpp"
#include "kernox/modules/reclaim/reclamation_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace kernox::simulator {

std::string AsciiVisualizer::color_code(std::string_view name, bool use_color) {
    if (!use_color) {
        return "";
    }
    if (name == "reset")   return "\033[0m";
    if (name == "bold")    return "\033[1m";
    if (name == "red")     return "\033[1;31m";
    if (name == "green")   return "\033[1;32m";
    if (name == "yellow")  return "\033[1;33m";
    if (name == "blue")    return "\033[1;34m";
    if (name == "magenta") return "\033[1;35m";
    if (name == "cyan")    return "\033[1;36m";
    if (name == "white")   return "\033[1;37m";
    if (name == "gray")    return "\033[0;90m";
    return "";
}

size_t AsciiVisualizer::get_terminal_width() noexcept {
    struct winsize w{};
    if (::isatty(STDOUT_FILENO) != 0 && ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col >= 40) {
        return static_cast<size_t>(w.ws_col);
    }
    return 80;
}

std::string AsciiVisualizer::render_utilization_bar(
    uint64_t used_bytes,
    uint64_t total_bytes,
    double moderate_ratio,
    double critical_ratio,
    size_t bar_width,
    bool use_color
) {
    if (bar_width == 0) {
        bar_width = 30;
    }
    double ratio = (total_bytes > 0)
        ? (static_cast<double>(used_bytes) / static_cast<double>(total_bytes))
        : 0.0;
    double clamped_ratio = std::clamp(ratio, 0.0, 1.0);
    size_t filled = static_cast<size_t>(std::round(clamped_ratio * static_cast<double>(bar_width)));
    if (ratio >= 1.0) {
        filled = bar_width;
    }
    size_t empty = (bar_width >= filled) ? (bar_width - filled) : 0;

    std::string bar_color = "green";
    if (ratio >= critical_ratio) {
        bar_color = "red";
    } else if (ratio >= moderate_ratio) {
        bar_color = "yellow";
    }

    std::ostringstream oss;
    oss << "[";
    oss << color_code(bar_color, use_color);
    for (size_t i = 0; i < filled; ++i) {
        oss << "\u2588"; // █
    }
    oss << color_code("reset", use_color);
    oss << color_code("gray", use_color);
    for (size_t i = 0; i < empty; ++i) {
        oss << "\u2591"; // ░
    }
    oss << color_code("reset", use_color);
    oss << "] ";

    oss << color_code(bar_color, use_color) << color_code("bold", use_color)
        << std::fixed << std::setprecision(1) << (ratio * 100.0) << "%"
        << color_code("reset", use_color);

    double used_mb = static_cast<double>(used_bytes) / (1024.0 * 1024.0);
    double total_mb = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    oss << " (" << std::fixed << std::setprecision(1) << used_mb << " MB / "
        << total_mb << " MB)";

    return oss.str();
}

std::string AsciiVisualizer::render_memory_overview(
    uint64_t used_bytes,
    uint64_t total_bytes,
    const reclaim::PressureThresholds& thresholds,
    bool use_color
) {
    double ratio = (total_bytes > 0)
        ? (static_cast<double>(used_bytes) / static_cast<double>(total_bytes))
        : 0.0;

    uint64_t avail_bytes = (total_bytes > used_bytes) ? (total_bytes - used_bytes) : 0;
    double avail_ratio = (total_bytes > 0)
        ? (static_cast<double>(avail_bytes) / static_cast<double>(total_bytes))
        : 0.0;

    std::string pressure_name = "NORMAL";
    std::string pressure_color = "green";
    if (ratio >= thresholds.critical_threshold_ratio) {
        pressure_name = "CRITICAL";
        pressure_color = "red";
    } else if (ratio >= thresholds.moderate_threshold_ratio) {
        pressure_name = "MODERATE";
        pressure_color = "yellow";
    }

    double total_mb = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    double used_mb = static_cast<double>(used_bytes) / (1024.0 * 1024.0);
    double avail_mb = static_cast<double>(avail_bytes) / (1024.0 * 1024.0);
    double mod_mb = static_cast<double>(thresholds.moderate_limit_bytes()) / (1024.0 * 1024.0);
    double crit_mb = static_cast<double>(thresholds.critical_limit_bytes()) / (1024.0 * 1024.0);

    std::string bar_str = render_utilization_bar(
        used_bytes,
        total_bytes,
        thresholds.moderate_threshold_ratio,
        thresholds.critical_threshold_ratio,
        28,
        use_color
    );

    std::ostringstream oss;
    oss << "+------------------------------------------------------------------------------+\n";
    oss << "|                           DEVICE MEMORY OVERVIEW                             |\n";
    oss << "+------------------------------------------------------------------------------+\n";
    oss << "  Total RAM Capacity   : " << std::right << std::setw(8) << std::fixed << std::setprecision(1) << total_mb << " MB\n";
    oss << "  Used Resident RAM    : " << std::right << std::setw(8) << used_mb << " MB ("
        << std::setw(5) << (ratio * 100.0) << "%)\n";
    oss << "  Available Free RAM   : " << std::right << std::setw(8) << avail_mb << " MB ("
        << std::setw(5) << (avail_ratio * 100.0) << "%)\n";
    oss << "  Memory Pressure      : " << color_code(pressure_color, use_color) << color_code("bold", use_color)
        << pressure_name << color_code("reset", use_color) << "\n";
    oss << "  Utilization Bar      : " << bar_str << "\n";
    oss << "  Moderate Threshold   : " << std::right << std::setw(8) << mod_mb << " MB ("
        << std::setw(5) << (thresholds.moderate_threshold_ratio * 100.0) << "%)\n";
    oss << "  Critical Threshold   : " << std::right << std::setw(8) << crit_mb << " MB ("
        << std::setw(5) << (thresholds.critical_threshold_ratio * 100.0) << "%)\n";
    oss << "+------------------------------------------------------------------------------+\n";

    return oss.str();
}

std::string AsciiVisualizer::render_process_input_distribution(
    const std::vector<CustomProcessInput>& processes,
    uint64_t total_capacity_bytes,
    size_t max_bar_width,
    bool use_color
) {
    if (processes.empty()) {
        return "  [No processes configured in current workload]\n";
    }
    if (max_bar_width == 0) {
        max_bar_width = 20;
    }

    std::ostringstream oss;
    oss << "+------------------------------------------------------------------------------------------------+\n"
        << "|                                  PROCESS MEMORY DISTRIBUTION                                   |\n"
        << "+-----+--------------------------------+------------+-----------+---------+----------------------+\n"
        << "| ID  | Process Name                   | Importance | Memory    | % Total | Memory Share Bar     |\n"
        << "+-----+--------------------------------+------------+-----------+---------+----------------------+\n";

    for (size_t i = 0; i < processes.size(); ++i) {
        const auto& p = processes[i];
        uint64_t proc_bytes = p.initial_memory_mb * 1024ULL * 1024ULL;
        double ratio = (total_capacity_bytes > 0)
            ? (static_cast<double>(proc_bytes) / static_cast<double>(total_capacity_bytes))
            : 0.0;
        double clamped_ratio = std::clamp(ratio, 0.0, 1.0);
        size_t filled = static_cast<size_t>(std::round(clamped_ratio * static_cast<double>(max_bar_width)));
        size_t empty = (max_bar_width >= filled) ? (max_bar_width - filled) : 0;

        std::string imp_str = std::string(intel::to_string(p.importance));
        std::string imp_color = "gray";
        switch (p.importance) {
            case intel::ProcessImportance::Foreground: imp_color = "green"; break;
            case intel::ProcessImportance::Visible:    imp_color = "cyan"; break;
            case intel::ProcessImportance::Service:    imp_color = "blue"; break;
            case intel::ProcessImportance::Background: imp_color = "yellow"; break;
            case intel::ProcessImportance::Cached:     imp_color = "magenta"; break;
        }

        oss << "| " << std::left << std::setw(4) << (i + 1)
            << "| " << std::left << std::setw(31) << p.name.substr(0, 31)
            << "| " << color_code(imp_color, use_color) << std::left << std::setw(11) << imp_str << color_code("reset", use_color)
            << "| " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << static_cast<double>(p.initial_memory_mb) << " MB "
            << "| " << std::right << std::setw(6) << std::fixed << std::setprecision(1) << (ratio * 100.0) << "% "
            << "| [";

        oss << color_code(imp_color, use_color);
        for (size_t b = 0; b < filled; ++b) {
            oss << "\u2588";
        }
        oss << color_code("reset", use_color) << color_code("gray", use_color);
        for (size_t b = 0; b < empty; ++b) {
            oss << "\u2591";
        }
        oss << color_code("reset", use_color) << "] |\n";
    }

    oss << "+-----+--------------------------------+------------+-----------+---------+----------------------+\n";
    return oss.str();
}

std::string AsciiVisualizer::render_process_memory_distribution(
    const std::vector<experiment::ProcessExecutionRecord>& records,
    uint64_t total_capacity_bytes,
    size_t max_bar_width,
    bool use_color
) {
    if (records.empty()) {
        return "  [No process execution records recorded]\n";
    }
    if (max_bar_width == 0) {
        max_bar_width = 20;
    }

    std::ostringstream oss;
    oss << "+-----------------------------------------------------------------------------------------------------------------------+\n"
        << "|                                             PROCESS EXECUTION DISTRIBUTION                                            |\n"
        << "+------+--------------------------------+------------+-----------+-----------+-----------+---------+----------------------+\n"
        << "| PID  | Process Name                   | Importance | Final RAM | Peak RAM  | Status    | % Total | Memory Share Bar     |\n"
        << "+------+--------------------------------+------------+-----------+-----------+-----------+---------+----------------------+\n";

    for (const auto& rec : records) {
        double final_mb = static_cast<double>(rec.final_memory_bytes) / (1024.0 * 1024.0);
        double peak_mb = static_cast<double>(rec.peak_memory_bytes) / (1024.0 * 1024.0);
        double ratio = (total_capacity_bytes > 0)
            ? (static_cast<double>(rec.final_memory_bytes) / static_cast<double>(total_capacity_bytes))
            : 0.0;
        double clamped_ratio = std::clamp(ratio, 0.0, 1.0);
        size_t filled = static_cast<size_t>(std::round(clamped_ratio * static_cast<double>(max_bar_width)));
        size_t empty = (max_bar_width >= filled) ? (max_bar_width - filled) : 0;

        std::string imp_str = std::string(intel::to_string(rec.importance));
        std::string status_str = rec.killed_under_pressure ? "Reclaimed" : "Survived";
        std::string status_color = rec.killed_under_pressure ? "red" : "green";

        oss << "| " << std::left << std::setw(5) << rec.pid
            << "| " << std::left << std::setw(31) << rec.name.substr(0, 31)
            << "| " << std::left << std::setw(11) << imp_str
            << "| " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << final_mb << " MB "
            << "| " << std::right << std::setw(7) << peak_mb << " MB "
            << "| " << color_code(status_color, use_color) << std::left << std::setw(10) << status_str << color_code("reset", use_color)
            << "| " << std::right << std::setw(6) << std::fixed << std::setprecision(1) << (ratio * 100.0) << "% "
            << "| [";

        oss << color_code(status_color, use_color);
        for (size_t b = 0; b < filled; ++b) {
            oss << "\u2588";
        }
        oss << color_code("reset", use_color) << color_code("gray", use_color);
        for (size_t b = 0; b < empty; ++b) {
            oss << "\u2591";
        }
        oss << color_code("reset", use_color) << "] |\n";
    }

    oss << "+------+--------------------------------+------------+-----------+-----------+-----------+---------+----------------------+\n";
    return oss.str();
}

std::string AsciiVisualizer::render_timeline_chart(
    const std::vector<experiment::MemoryTimelinePoint>& timeline,
    uint64_t total_capacity_bytes,
    const reclaim::PressureThresholds& thresholds,
    size_t chart_height,
    size_t chart_width,
    bool use_color
) {
    if (timeline.empty()) {
        return "  [No timeline samples recorded for this simulation run]\n";
    }
    if (total_capacity_bytes == 0) {
        return "  [Invalid memory capacity for timeline chart]\n";
    }
    if (chart_height < 4) chart_height = 8;
    if (chart_width < 10) chart_width = 50;

    uint64_t min_tick = timeline.front().tick;
    uint64_t max_tick = timeline.back().tick;
    if (max_tick <= min_tick) {
        max_tick = min_tick + 1;
    }

    uint64_t peak_consumed = 0;
    uint64_t min_consumed = total_capacity_bytes;
    for (const auto& pt : timeline) {
        if (pt.consumed_bytes > peak_consumed) peak_consumed = pt.consumed_bytes;
        if (pt.consumed_bytes < min_consumed) min_consumed = pt.consumed_bytes;
    }

    // Bucket timeline points across chart_width columns
    std::vector<double> column_ratios(chart_width, 0.0);
    std::vector<bool> column_has_data(chart_width, false);

    for (size_t col = 0; col < chart_width; ++col) {
        uint64_t target_tick = min_tick + static_cast<uint64_t>(
            std::round((static_cast<double>(col) / static_cast<double>(chart_width - 1)) * static_cast<double>(max_tick - min_tick))
        );

        // Find nearest timeline point to target_tick
        size_t best_idx = 0;
        uint64_t best_diff = UINT64_MAX;
        for (size_t i = 0; i < timeline.size(); ++i) {
            uint64_t diff = (timeline[i].tick > target_tick)
                ? (timeline[i].tick - target_tick)
                : (target_tick - timeline[i].tick);
            if (diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }
        double r = static_cast<double>(timeline[best_idx].consumed_bytes) / static_cast<double>(total_capacity_bytes);
        column_ratios[col] = std::clamp(r, 0.0, 1.0);
        column_has_data[col] = true;
    }

    // Determine row indices for thresholds
    size_t crit_row = static_cast<size_t>(std::round(thresholds.critical_threshold_ratio * static_cast<double>(chart_height - 1)));
    size_t mod_row = static_cast<size_t>(std::round(thresholds.moderate_threshold_ratio * static_cast<double>(chart_height - 1)));

    std::ostringstream oss;
    oss << "\n  MEMORY CONSUMPTION TIMELINE ACROSS SIMULATION TICKS\n";
    oss << "  ------------------------------------------------------------------------------\n";

    for (int r = static_cast<int>(chart_height) - 1; r >= 0; --r) {
        size_t row_idx = static_cast<size_t>(r);

        // Left axis label
        if (row_idx == chart_height - 1) {
            oss << " 100% | ";
        } else if (row_idx == crit_row) {
            oss << color_code("red", use_color) << "[CRIT]| " << color_code("reset", use_color);
        } else if (row_idx == mod_row) {
            oss << color_code("yellow", use_color) << "[MOD] | " << color_code("reset", use_color);
        } else if (row_idx == (chart_height - 1) / 2) {
            oss << "  50% | ";
        } else if (row_idx == 0) {
            oss << "   0% | ";
        } else {
            oss << "      | ";
        }

        // Render columns
        for (size_t col = 0; col < chart_width; ++col) {
            size_t pt_row = static_cast<size_t>(std::round(column_ratios[col] * static_cast<double>(chart_height - 1)));
            if (pt_row == row_idx) {
                std::string pt_color = "green";
                if (column_ratios[col] >= thresholds.critical_threshold_ratio) {
                    pt_color = "red";
                } else if (column_ratios[col] >= thresholds.moderate_threshold_ratio) {
                    pt_color = "yellow";
                }
                oss << color_code(pt_color, use_color) << "\u2588" << color_code("reset", use_color);
            } else if (row_idx == crit_row) {
                oss << color_code("red", use_color) << "-" << color_code("reset", use_color);
            } else if (row_idx == mod_row) {
                oss << color_code("yellow", use_color) << "-" << color_code("reset", use_color);
            } else {
                oss << " ";
            }
        }
        oss << "\n";
    }

    // Bottom horizontal axis
    oss << "      +-";
    for (size_t col = 0; col < chart_width; ++col) {
        oss << "-";
    }
    oss << "\n";

    // Tick markers
    oss << "Tick:  0";
    size_t pad_mid = (chart_width > 12) ? ((chart_width / 2) - 4) : 2;
    for (size_t i = 0; i < pad_mid; ++i) oss << " ";
    oss << (max_tick / 2);
    size_t pad_end = (chart_width > 12) ? (chart_width - pad_mid - 8) : 2;
    for (size_t i = 0; i < pad_end; ++i) oss << " ";
    oss << max_tick << "\n";

    // Legend & stats
    double peak_mb = static_cast<double>(peak_consumed) / (1024.0 * 1024.0);
    double peak_pct = (static_cast<double>(peak_consumed) / static_cast<double>(total_capacity_bytes)) * 100.0;
    double min_mb = static_cast<double>(min_consumed) / (1024.0 * 1024.0);

    oss << "  Legend: "
        << color_code("green", use_color) << "\u2588 Normal (<"
        << std::fixed << std::setprecision(0) << (thresholds.moderate_threshold_ratio * 100.0) << "%)"
        << color_code("reset", use_color) << " | "
        << color_code("yellow", use_color) << "\u2588 Moderate ("
        << (thresholds.moderate_threshold_ratio * 100.0) << "-" << (thresholds.critical_threshold_ratio * 100.0) << "%)"
        << color_code("reset", use_color) << " | "
        << color_code("red", use_color) << "\u2588 Critical (>="
        << (thresholds.critical_threshold_ratio * 100.0) << "%)"
        << color_code("reset", use_color) << "\n";
    oss << "  Stats : Peak Used: " << std::fixed << std::setprecision(1) << peak_mb << " MB ("
        << peak_pct << "%) | Min Used: " << min_mb << " MB | Samples: " << timeline.size() << "\n";
    oss << "  ------------------------------------------------------------------------------\n";

    return oss.str();
}

std::string AsciiVisualizer::render_reclamation_log(
    const std::vector<reclaim::ReclamationDecision>& log,
    bool use_color
) {
    if (log.empty()) {
        return "  [No reclamation events executed — Memory remained within safe operating limits]\n";
    }

    std::ostringstream oss;
    oss << "+-----------------------------------------------------------------------------------------------------------------------+\n"
        << "|                                              RECLAMATION AUDIT TRAIL                                                  |\n"
        << "+------+------+--------+--------------------------------+------------+-----------+--------------------------------------+\n"
        << "| Tick | PID  | Press  | Victim Process Name            | Importance | Reclaimed | Policy Rationale / Selection Reason  |\n"
        << "+------+------+--------+--------------------------------+------------+-----------+--------------------------------------+\n";

    uint64_t total_reclaimed_bytes = 0;

    for (const auto& dec : log) {
        total_reclaimed_bytes += dec.memory_reclaimed_bytes;
        double rec_mb = static_cast<double>(dec.memory_reclaimed_bytes) / (1024.0 * 1024.0);
        std::string imp_str = std::string(intel::to_string(dec.importance));

        std::string press_str = "NORM";
        std::string press_color = "green";
        if (dec.pressure_level == reclaim::MemoryPressureLevel::Critical) {
            press_str = "CRIT";
            press_color = "red";
        } else if (dec.pressure_level == reclaim::MemoryPressureLevel::Moderate) {
            press_str = "MOD ";
            press_color = "yellow";
        }

        oss << "| " << std::right << std::setw(4) << dec.timestamp_ticks << " "
            << "| " << std::left << std::setw(5) << dec.victim_pid
            << "| " << color_code(press_color, use_color) << std::left << std::setw(6) << press_str << color_code("reset", use_color) << " "
            << "| " << std::left << std::setw(31) << dec.victim_name.substr(0, 31)
            << "| " << std::left << std::setw(11) << imp_str
            << "| " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << rec_mb << " MB "
            << "| " << std::left << std::setw(37) << dec.reason.substr(0, 37) << "|\n";
    }

    double total_mb = static_cast<double>(total_reclaimed_bytes) / (1024.0 * 1024.0);
    oss << "+------+------+--------+--------------------------------+------------+-----------+--------------------------------------+\n"
        << "  Total Reclaimed: " << color_code("bold", use_color) << std::fixed << std::setprecision(1) << total_mb << " MB"
        << color_code("reset", use_color) << " across " << log.size() << " process termination(s).\n";

    return oss.str();
}

std::string AsciiVisualizer::render_comparison_matrix(
    const experiment::ExperimentComparison& comparison,
    bool use_color
) {
    std::ostringstream oss;
    oss << "+-----------------------------------------------------------------------------------------------------------------------------------------+\n"
        << "|                                                  4-POLICY COMPARATIVE BENCHMARK MATRIX                                                  |\n"
        << "+--------------------+------------+---------+----------+-----------+---------+-------------+-------------+--------------------------------+\n"
        << "| Policy             | Reclaimed  | Victims | Survived | Surv Rate | FG Surv | Peak Util % | Fail Allocs | Survival Ratio Visual Bar      |\n"
        << "+--------------------+------------+---------+----------+-----------+---------+-------------+-------------+--------------------------------+\n";

    for (const auto& res : comparison.policy_results) {
        double rec_mb = static_cast<double>(res.metrics.total_reclaimed_bytes) / (1024.0 * 1024.0);
        double peak_pct = (res.metrics.total_capacity_bytes > 0)
            ? (static_cast<double>(res.metrics.peak_used_bytes) / static_cast<double>(res.metrics.total_capacity_bytes)) * 100.0
            : 0.0;
        double surv_rate = res.metrics.survival_rate;
        double fg_surv = res.metrics.foreground_survival_rate;

        // Visual bar of survival rate (width: 16)
        size_t bar_width = 16;
        size_t filled = static_cast<size_t>(std::round(std::clamp(surv_rate, 0.0, 1.0) * static_cast<double>(bar_width)));
        size_t empty = (bar_width >= filled) ? (bar_width - filled) : 0;

        std::string policy_color = (res.policy_name.find("Importance") != std::string::npos) ? "green" : "white";

        oss << "| " << color_code(policy_color, use_color) << std::left << std::setw(19) << res.policy_name.substr(0, 19) << color_code("reset", use_color)
            << "| " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << rec_mb << " MB "
            << "| " << std::right << std::setw(7) << res.metrics.processes_reclaimed << " "
            << "| " << std::right << std::setw(4) << res.metrics.processes_survived << " / " << std::left << std::setw(3) << res.metrics.total_processes
            << "| " << std::right << std::setw(8) << std::fixed << std::setprecision(1) << (surv_rate * 100.0) << "% "
            << "| " << std::right << std::setw(6) << std::fixed << std::setprecision(1) << (fg_surv * 100.0) << "% "
            << "| " << std::right << std::setw(10) << std::fixed << std::setprecision(1) << peak_pct << "% "
            << "| " << std::right << std::setw(11) << res.metrics.failed_allocations << " "
            << "| [";

        oss << color_code("green", use_color);
        for (size_t b = 0; b < filled; ++b) {
            oss << "\u2588";
        }
        oss << color_code("reset", use_color) << color_code("gray", use_color);
        for (size_t b = 0; b < empty; ++b) {
            oss << "\u2591";
        }
        oss << color_code("reset", use_color) << "] "
            << std::right << std::setw(5) << std::fixed << std::setprecision(1) << (surv_rate * 100.0) << "% |\n";
    }

    oss << "+--------------------+------------+---------+----------+-----------+---------+-------------+-------------+--------------------------------+\n";

    // Summary key takeaway
    const auto* ia_res = comparison.get_result("ImportanceAware");
    if (ia_res == nullptr) {
        ia_res = comparison.get_result("Importance-Aware");
    }
    const auto* fifo_res = comparison.get_result("FIFO");
    if (ia_res != nullptr && fifo_res != nullptr) {
        double ia_fg = ia_res->metrics.foreground_survival_rate * 100.0;
        double fifo_fg = fifo_res->metrics.foreground_survival_rate * 100.0;
        oss << "\n  Key Insight: " << ia_res->policy_name << " retains " << color_code("green", use_color) << color_code("bold", use_color)
            << std::fixed << std::setprecision(1) << ia_fg << "%" << color_code("reset", use_color)
            << " of Foreground/Visible processes vs "
            << color_code("yellow", use_color) << fifo_fg << "%" << color_code("reset", use_color)
            << " under FIFO.\n";
    }

    return oss.str();
}

} // namespace kernox::simulator
