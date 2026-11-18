#include "kernox/modules/experiment/exporter.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace kernox::experiment {

std::string ExperimentExporter::escape_json_string(std::string_view input) {
    std::ostringstream oss;
    for (char ch : input) {
        switch (ch) {
            case '\"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch));
                } else {
                    oss << ch;
                }
                break;
        }
    }
    return oss.str();
}

std::string ExperimentExporter::to_json(const ExperimentResult& result) {
    std::ostringstream oss;
    const auto& m = result.metrics;

    oss << "{\n"
        << "  \"policy_name\": \"" << escape_json_string(result.policy_name) << "\",\n"
        << "  \"scenario_name\": \"" << escape_json_string(result.scenario_name) << "\",\n"
        << "  \"seed\": " << result.seed << ",\n"
        << "  \"workload_fingerprint\": \"" << escape_json_string(result.workload_fingerprint) << "\",\n"
        << "  \"metrics\": {\n"
        << "    \"memory\": {\n"
        << "      \"total_capacity_bytes\": " << m.total_capacity_bytes << ",\n"
        << "      \"initial_used_bytes\": " << m.initial_used_bytes << ",\n"
        << "      \"peak_used_bytes\": " << m.peak_used_bytes << ",\n"
        << "      \"final_used_bytes\": " << m.final_used_bytes << ",\n"
        << "      \"average_utilization_ratio\": " << std::fixed << std::setprecision(4) << m.average_utilization_ratio << ",\n"
        << "      \"total_reclaimed_bytes\": " << m.total_reclaimed_bytes << ",\n"
        << "      \"pressure_events_moderate\": " << m.pressure_events_moderate << ",\n"
        << "      \"pressure_events_critical\": " << m.pressure_events_critical << "\n"
        << "    },\n"
        << "    \"processes\": {\n"
        << "      \"total_processes\": " << m.total_processes << ",\n"
        << "      \"processes_completed\": " << m.processes_completed << ",\n"
        << "      \"processes_terminated_pressure\": " << m.processes_terminated_pressure << ",\n"
        << "      \"processes_survived\": " << m.processes_survived << ",\n"
        << "      \"survival_rate\": " << std::fixed << std::setprecision(4) << m.survival_rate << ",\n"
        << "      \"foreground_survival_rate\": " << m.foreground_survival_rate << ",\n"
        << "      \"visible_survival_rate\": " << m.visible_survival_rate << ",\n"
        << "      \"service_survival_rate\": " << m.service_survival_rate << ",\n"
        << "      \"background_survival_rate\": " << m.background_survival_rate << ",\n"
        << "      \"cached_survival_rate\": " << m.cached_survival_rate << "\n"
        << "    },\n"
        << "    \"reclamation\": {\n"
        << "      \"reclamation_invocations\": " << m.reclamation_invocations << ",\n"
        << "      \"reclamation_successes\": " << m.reclamation_successes << ",\n"
        << "      \"processes_reclaimed\": " << m.processes_reclaimed << ",\n"
        << "      \"average_reclaimed_per_victim_bytes\": " << std::fixed << std::setprecision(1) << m.average_reclaimed_per_victim_bytes << ",\n"
        << "      \"failed_allocations\": " << m.failed_allocations << ",\n"
        << "      \"no_eligible_victim_events\": " << m.no_eligible_victim_events << ",\n"
        << "      \"target_achievement_rate\": " << std::fixed << std::setprecision(4) << m.target_achievement_rate << "\n"
        << "    },\n"
        << "    \"timing\": {\n"
        << "      \"simulated_duration_ticks\": " << m.simulated_duration_ticks << ",\n"
        << "      \"wall_clock_elapsed_microseconds\": " << m.wall_clock_elapsed_microseconds << "\n"
        << "    }\n"
        << "  },\n"
        << "  \"reclamation_log\": [\n";

    for (size_t i = 0; i < result.reclamation_log.size(); ++i) {
        const auto& d = result.reclamation_log[i];
        oss << "    {\n"
            << "      \"victim_pid\": " << d.victim_pid << ",\n"
            << "      \"victim_name\": \"" << escape_json_string(d.victim_name) << "\",\n"
            << "      \"importance_tier\": \"" << intel::to_string(d.importance) << "\",\n"
            << "      \"importance_score\": " << static_cast<uint32_t>(d.importance) << ",\n"
            << "      \"cpu_priority\": " << d.cpu_priority << ",\n"
            << "      \"memory_reclaimed_bytes\": " << d.memory_reclaimed_bytes << ",\n"
            << "      \"policy_name\": \"" << escape_json_string(d.policy_name) << "\",\n"
            << "      \"reason\": \"" << escape_json_string(d.reason) << "\",\n"
            << "      \"timestamp_ticks\": " << d.timestamp_ticks << "\n"
            << "    }" << (i + 1 < result.reclamation_log.size() ? "," : "") << "\n";
    }

    oss << "  ]\n"
        << "}";
    return oss.str();
}

std::string ExperimentExporter::to_json(const ExperimentComparison& comparison) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"scenario_name\": \"" << escape_json_string(comparison.scenario_name) << "\",\n"
        << "  \"seed\": " << comparison.seed << ",\n"
        << "  \"workload_fingerprint\": \"" << escape_json_string(comparison.workload_fingerprint) << "\",\n"
        << "  \"total_capacity_bytes\": " << comparison.total_capacity_bytes << ",\n"
        << "  \"policy_count\": " << comparison.policy_results.size() << ",\n"
        << "  \"policy_results\": [\n";

    for (size_t i = 0; i < comparison.policy_results.size(); ++i) {
        std::string pol_json = to_json(comparison.policy_results[i]);
        // Indent sub-json
        std::istringstream iss(pol_json);
        std::string line;
        bool first_line = true;
        while (std::getline(iss, line)) {
            if (!first_line) oss << "\n";
            oss << "    " << line;
            first_line = false;
        }
        if (i + 1 < comparison.policy_results.size()) {
            oss << ",";
        }
        oss << "\n";
    }

    oss << "  ]\n"
        << "}";
    return oss.str();
}

std::string ExperimentExporter::to_csv(const ExperimentComparison& comparison) {
    std::ostringstream oss;
    oss << "scenario,seed,fingerprint,capacity_bytes,policy,reclaimed_bytes,processes_reclaimed,"
        << "processes_survived,total_processes,survival_rate,fg_survival_rate,vis_survival_rate,"
        << "svc_survival_rate,bg_survival_rate,cached_survival_rate,peak_used_bytes,final_used_bytes,"
        << "failed_allocations,simulated_ticks,wall_clock_us\n";

    for (const auto& r : comparison.policy_results) {
        const auto& m = r.metrics;
        oss << comparison.scenario_name << ","
            << comparison.seed << ","
            << comparison.workload_fingerprint << ","
            << comparison.total_capacity_bytes << ","
            << r.policy_name << ","
            << m.total_reclaimed_bytes << ","
            << m.processes_reclaimed << ","
            << m.processes_survived << ","
            << m.total_processes << ","
            << std::fixed << std::setprecision(4) << m.survival_rate << ","
            << m.foreground_survival_rate << ","
            << m.visible_survival_rate << ","
            << m.service_survival_rate << ","
            << m.background_survival_rate << ","
            << m.cached_survival_rate << ","
            << m.peak_used_bytes << ","
            << m.final_used_bytes << ","
            << m.failed_allocations << ","
            << m.simulated_duration_ticks << ","
            << m.wall_clock_elapsed_microseconds << "\n";
    }

    return oss.str();
}

std::string ExperimentExporter::to_decisions_csv(const ExperimentResult& result) {
    std::ostringstream oss;
    oss << "policy,scenario,seed,victim_pid,victim_name,importance_tier,importance_score,"
        << "cpu_priority,memory_reclaimed_bytes,timestamp_ticks,reason\n";

    for (const auto& d : result.reclamation_log) {
        oss << d.policy_name << ","
            << result.scenario_name << ","
            << result.seed << ","
            << d.victim_pid << ",\""
            << d.victim_name << "\","
            << intel::to_string(d.importance) << ","
            << static_cast<uint32_t>(d.importance) << ","
            << d.cpu_priority << ","
            << d.memory_reclaimed_bytes << ","
            << d.timestamp_ticks << ",\""
            << d.reason << "\"\n";
    }

    return oss.str();
}

core::Result<void> ExperimentExporter::write_to_file(const std::string& path, std::string_view content) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return core::Result<void>::err(
            core::ErrorCode::IoError,
            "Failed to open output file for writing: " + path
        );
    }
    out << content;
    if (!out.good()) {
        return core::Result<void>::err(
            core::ErrorCode::IoError,
            "Error occurred while writing output to file: " + path
        );
    }
    return core::Result<void>::ok();
}

} // namespace kernox::experiment
