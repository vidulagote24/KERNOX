#include "kernox/modules/adb/parser.hpp"

#include <cctype>
#include <chrono>
#include <climits>
#include <sstream>

namespace kernox::adb {

namespace {

std::string trim(std::string_view str) {
    size_t start = 0;
    while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
        start++;
    }
    size_t end = str.size();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        end--;
    }
    return std::string(str.substr(start, end - start));
}

std::vector<std::string> split_lines(std::string_view content) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();

        std::string_view line_view = content.substr(start, end - start);
        if (!line_view.empty() && line_view.back() == '\r') {
            line_view.remove_suffix(1);
        }
        lines.emplace_back(line_view);
        start = end + 1;
    }
    return lines;
}

std::vector<std::string> tokenize_whitespace(std::string_view line) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            i++;
        }
        if (i >= line.size()) break;

        size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
            i++;
        }
        tokens.emplace_back(line.substr(start, i - start));
    }
    return tokens;
}

uint64_t current_time_ms() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

} // namespace

core::Result<uint64_t> AdbParser::parse_formatted_bytes(std::string_view raw_value) {
    std::string clean;
    clean.reserve(raw_value.size());
    for (char c : raw_value) {
        if (c != ',' && c != '(' && c != ')' && c != ':') {
            clean.push_back(c);
        }
    }

    std::string s = trim(clean);
    if (s.empty()) {
        return core::Result<uint64_t>::err(core::ErrorCode::InvalidArgument, "Empty byte value string");
    }

    // Split numeric prefix and unit suffix
    size_t num_end = 0;
    while (num_end < s.size() && (std::isdigit(static_cast<unsigned char>(s[num_end])) || s[num_end] == '.')) {
        num_end++;
    }

    if (num_end == 0) {
        return core::Result<uint64_t>::err(core::ErrorCode::InvalidArgument, "No numeric digits found in value");
    }

    std::string num_str = s.substr(0, num_end);
    std::string unit = trim(s.substr(num_end));

    double num = 0.0;
    try {
        num = std::stod(num_str);
    } catch (...) {
        return core::Result<uint64_t>::err(core::ErrorCode::InvalidArgument, "Malformed number: " + num_str);
    }

    if (num < 0.0) {
        return core::Result<uint64_t>::err(core::ErrorCode::InvalidArgument, "Negative byte quantity");
    }

    // Convert unit to lower case
    for (char& c : unit) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    double multiplier = 1.0;
    if (unit.empty() || unit == "b" || unit == "bytes") {
        multiplier = 1.0;
    } else if (unit == "k" || unit == "kb" || unit == "kib") {
        multiplier = 1024.0;
    } else if (unit == "m" || unit == "mb" || unit == "mib") {
        multiplier = 1024.0 * 1024.0;
    } else if (unit == "g" || unit == "gb" || unit == "gib") {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    } else {
        // Unknown unit
        return core::Result<uint64_t>::err(core::ErrorCode::InvalidArgument, "Unrecognized unit: " + unit);
    }

    double total = num * multiplier;
    if (total > static_cast<double>(ULLONG_MAX)) {
        return core::Result<uint64_t>::err(core::ErrorCode::ResourceExhausted, "Integer overflow in byte conversion");
    }

    return core::Result<uint64_t>::ok(static_cast<uint64_t>(total));
}

std::string AdbParser::parse_getprop(std::string_view output) {
    std::string val = trim(output);
    if (val.size() >= 2 && val.front() == '[' && val.back() == ']') {
        val = val.substr(1, val.size() - 2);
    }
    return trim(val);
}

core::Result<std::vector<AdbDeviceInfo>> AdbParser::parse_devices(std::string_view output) {
    std::vector<AdbDeviceInfo> devices;
    auto lines = split_lines(output);

    for (const auto& raw_line : lines) {
        std::string line = trim(raw_line);
        if (line.empty()) continue;

        // Skip daemon notifications
        if (line.rfind("* daemon", 0) == 0) continue;

        // Skip header
        std::string lower = line;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("list of devices attached") != std::string::npos) continue;

        auto tokens = tokenize_whitespace(line);
        if (tokens.size() < 2) continue;

        AdbDeviceInfo dev{};
        dev.serial = tokens[0];
        auto state_opt = parse_connection_state(tokens[1]);
        dev.state = state_opt.value_or(AdbConnectionState::Unknown);

        // Parse key-value attributes (e.g., product:xyz model:Pixel_7 device:panther transport_id:1)
        for (size_t i = 2; i < tokens.size(); ++i) {
            const auto& tok = tokens[i];
            size_t colon = tok.find(':');
            if (colon != std::string::npos && colon + 1 < tok.size()) {
                std::string k = tok.substr(0, colon);
                std::string v = tok.substr(colon + 1);
                if (k == "product")      dev.product = v;
                else if (k == "model")   dev.model = v;
                else if (k == "device")  dev.device = v;
                else if (k == "transport_id") dev.transport_id = v;
            }
        }

        devices.push_back(std::move(dev));
    }

    return core::Result<std::vector<AdbDeviceInfo>>::ok(std::move(devices));
}

core::Result<AdbMemorySnapshot> AdbParser::parse_proc_meminfo(std::string_view content) {
    auto lines = split_lines(content);
    if (lines.empty()) {
        return core::Result<AdbMemorySnapshot>::err(
            core::ErrorCode::InvalidArgument, "Empty /proc/meminfo content"
        );
    }

    AdbMemorySnapshot snapshot{};
    snapshot.command_executed = "cat /proc/meminfo";
    snapshot.timestamp_epoch_ms = current_time_ms();
    snapshot.raw_output = std::string(content);

    for (const auto& raw_line : lines) {
        std::string line = trim(raw_line);
        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val_str = trim(line.substr(colon + 1));

        auto byte_res = parse_formatted_bytes(val_str);
        if (!byte_res.is_ok()) {
            snapshot.warnings.push_back("Failed parsing field '" + key + "': " + byte_res.error().message);
            continue;
        }

        uint64_t bytes = byte_res.value();
        if (key == "MemTotal") {
            snapshot.total_ram_bytes = bytes;
        } else if (key == "MemFree") {
            snapshot.free_ram_bytes = bytes;
        } else if (key == "MemAvailable") {
            snapshot.available_ram_bytes = bytes;
        } else if (key == "Buffers") {
            snapshot.buffers_bytes = bytes;
        } else if (key == "Cached") {
            snapshot.cached_bytes = bytes;
        } else if (key == "SwapTotal") {
            snapshot.swap_total_bytes = bytes;
        } else if (key == "SwapFree") {
            snapshot.swap_free_bytes = bytes;
        }
    }

    if (!snapshot.total_ram_bytes.has_value()) {
        return core::Result<AdbMemorySnapshot>::err(
            core::ErrorCode::InvalidArgument, "Invalid /proc/meminfo: missing MemTotal"
        );
    }

    // Compute derived used_ram_bytes
    if (snapshot.available_ram_bytes.has_value() && *snapshot.total_ram_bytes >= *snapshot.available_ram_bytes) {
        snapshot.used_ram_bytes = *snapshot.total_ram_bytes - *snapshot.available_ram_bytes;
    } else if (snapshot.free_ram_bytes.has_value()) {
        uint64_t non_used = *snapshot.free_ram_bytes + snapshot.cached_bytes.value_or(0) + snapshot.buffers_bytes.value_or(0);
        if (*snapshot.total_ram_bytes >= non_used) {
            snapshot.used_ram_bytes = *snapshot.total_ram_bytes - non_used;
        }
    }

    return core::Result<AdbMemorySnapshot>::ok(std::move(snapshot));
}

core::Result<AdbMemorySnapshot> AdbParser::parse_dumpsys_meminfo_summary(std::string_view content) {
    auto lines = split_lines(content);
    if (lines.empty()) {
        return core::Result<AdbMemorySnapshot>::err(
            core::ErrorCode::InvalidArgument, "Empty dumpsys meminfo content"
        );
    }

    AdbMemorySnapshot snapshot{};
    snapshot.command_executed = "dumpsys meminfo";
    snapshot.timestamp_epoch_ms = current_time_ms();
    snapshot.raw_output = std::string(content);

    for (const auto& raw_line : lines) {
        std::string line = trim(raw_line);
        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val_part = trim(line.substr(colon + 1));

        // Extract first token before parentheses (e.g. "7,856,128K" from "7,856,128K (status normal)")
        size_t paren = val_part.find('(');
        std::string first_token = (paren != std::string::npos) ? val_part.substr(0, paren) : val_part;

        if (key == "Total RAM") {
            auto b = parse_formatted_bytes(first_token);
            if (b.is_ok()) snapshot.total_ram_bytes = b.value();
        } else if (key == "Free RAM") {
            auto b = parse_formatted_bytes(first_token);
            if (b.is_ok()) snapshot.free_ram_bytes = b.value();
        } else if (key == "Used RAM") {
            auto b = parse_formatted_bytes(first_token);
            if (b.is_ok()) snapshot.used_ram_bytes = b.value();
        } else if (key == "Lost RAM") {
            auto b = parse_formatted_bytes(first_token);
            if (b.is_ok()) snapshot.lost_ram_bytes = b.value();
        } else if (key == "ZRAM") {
            // e.g. "340,120K physical used for 980,400K in swap ( 2,097,148K total swap)"
            auto tokens = tokenize_whitespace(val_part);
            if (!tokens.empty()) {
                auto b = parse_formatted_bytes(tokens[0]);
                if (b.is_ok()) snapshot.zram_physical_used_bytes = b.value();
            }
            size_t for_pos = val_part.find("for");
            if (for_pos != std::string::npos) {
                size_t in_swap = val_part.find("in swap", for_pos);
                if (in_swap != std::string::npos) {
                    std::string swap_used = val_part.substr(for_pos + 3, in_swap - (for_pos + 3));
                    (void)swap_used;
                }
            }
            size_t total_swap_pos = val_part.find("total swap");
            if (total_swap_pos != std::string::npos) {
                // Find token preceding "total swap"
                size_t p = val_part.rfind('(', total_swap_pos);
                if (p != std::string::npos) {
                    std::string swap_tot_str = val_part.substr(p + 1, total_swap_pos - (p + 1));
                    auto b = parse_formatted_bytes(swap_tot_str);
                    if (b.is_ok()) snapshot.swap_total_bytes = b.value();
                }
            }
        }
    }

    if (!snapshot.total_ram_bytes.has_value()) {
        return core::Result<AdbMemorySnapshot>::err(
            core::ErrorCode::InvalidArgument, "Could not find 'Total RAM:' section in dumpsys meminfo output"
        );
    }

    return core::Result<AdbMemorySnapshot>::ok(std::move(snapshot));
}

core::Result<std::vector<AdbProcessObservation>> AdbParser::parse_dumpsys_meminfo_processes(
    std::string_view content
) {
    auto lines = split_lines(content);
    std::vector<AdbProcessObservation> processes;

    std::string current_category;
    bool in_process_section = false;

    for (const auto& raw_line : lines) {
        std::string line = trim(raw_line);
        if (line.empty()) continue;

        if (line.find("Total PSS by process:") != std::string::npos ||
            line.find("Total PSS by OOM classification:") != std::string::npos) {
            in_process_section = true;
            continue;
        }

        if (in_process_section) {
            // End of process section check
            if (line.find("Total RAM:") != std::string::npos ||
                line.find("Total PSS by category:") != std::string::npos) {
                in_process_section = false;
                break;
            }

            // Check if this line is an OOM classification header (e.g., "Native", "System", "Foreground", "Cached")
            if (line.find(':') == std::string::npos && !std::isdigit(static_cast<unsigned char>(line[0]))) {
                current_category = line;
                continue;
            }

            // Look for pattern: "<PSS>K: <process_name> (pid <pid> ...)"
            size_t colon = line.find(':');
            size_t pid_pos = line.find("(pid ");
            if (colon != std::string::npos && pid_pos != std::string::npos && colon < pid_pos) {
                std::string pss_str = line.substr(0, colon);
                std::string proc_name = trim(line.substr(colon + 1, pid_pos - (colon + 1)));

                size_t pid_num_start = pid_pos + 5;
                size_t pid_num_end = line.find_first_of(" )/", pid_num_start);
                if (pid_num_end == std::string::npos) pid_num_end = line.size();

                std::string pid_str = line.substr(pid_num_start, pid_num_end - pid_num_start);

                AndroidPid pid = 0;
                try {
                    pid = static_cast<AndroidPid>(std::stoul(pid_str));
                } catch (...) {
                    continue;
                }

                auto pss_res = parse_formatted_bytes(pss_str);

                AdbProcessObservation obs{};
                obs.pid = pid;
                obs.process_name = proc_name;
                obs.package_name = proc_name;
                if (pss_res.is_ok()) {
                    obs.pss_bytes = pss_res.value();
                }
                obs.raw_importance_or_state = current_category;

                // Normalize category if standard
                std::string lower_cat = current_category;
                for (char& c : lower_cat) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                if (lower_cat.find("foreground") != std::string::npos || lower_cat.find("top") != std::string::npos) {
                    obs.normalized_importance = "Foreground";
                } else if (lower_cat.find("visible") != std::string::npos) {
                    obs.normalized_importance = "Visible";
                } else if (lower_cat.find("service") != std::string::npos) {
                    obs.normalized_importance = "Service";
                } else if (lower_cat.find("background") != std::string::npos || lower_cat.find("perceptible") != std::string::npos) {
                    obs.normalized_importance = "Background";
                } else if (lower_cat.find("cached") != std::string::npos) {
                    obs.normalized_importance = "Cached";
                }

                obs.confidence = ObservationConfidence::High;
                processes.push_back(std::move(obs));
            }
        }
    }

    return core::Result<std::vector<AdbProcessObservation>>::ok(std::move(processes));
}

core::Result<std::vector<AdbProcessObservation>> AdbParser::parse_dumpsys_activity_processes(
    std::string_view content
) {
    auto lines = split_lines(content);
    std::vector<AdbProcessObservation> processes;

    AdbProcessObservation current_obs{};
    bool in_record = false;

    for (const auto& raw_line : lines) {
        std::string line = trim(raw_line);
        if (line.empty()) continue;

        // ProcessRecord pattern: "* ProcessRecord{42a1b90 1420:com.android.systemui/u0a45}"
        size_t pr_pos = line.find("ProcessRecord{");
        if (pr_pos != std::string::npos) {
            if (in_record && current_obs.pid > 0) {
                processes.push_back(std::move(current_obs));
                current_obs = AdbProcessObservation{};
            }

            size_t start_token = pr_pos + 14;
            size_t end_token = line.find('}', start_token);
            if (end_token != std::string::npos) {
                std::string record = line.substr(start_token, end_token - start_token);
                auto tokens = tokenize_whitespace(record);
                // Last token is typically "<pid>:<name>/<user>"
                std::string proc_tok = tokens.empty() ? "" : tokens.back();
                size_t colon = proc_tok.find(':');
                if (colon != std::string::npos) {
                    std::string pid_str = proc_tok.substr(0, colon);
                    std::string rest = proc_tok.substr(colon + 1);
                    size_t slash = rest.find('/');
                    std::string name = (slash != std::string::npos) ? rest.substr(0, slash) : rest;

                    try {
                        current_obs.pid = static_cast<AndroidPid>(std::stoul(pid_str));
                        current_obs.process_name = name;
                        current_obs.package_name = name;
                        current_obs.confidence = ObservationConfidence::Moderate;
                        in_record = true;
                    } catch (...) {}
                }
            }
            continue;
        }

        if (in_record) {
            // Check for oom_adj / oom_score_adj
            size_t oom_pos = line.find("oom_score_adj=");
            if (oom_pos != std::string::npos) {
                size_t end = line.find_first_of(" \t\r\n", oom_pos + 14);
                std::string adj_str = line.substr(oom_pos + 14, end - (oom_pos + 14));
                try {
                    current_obs.oom_score_adj = std::stoi(adj_str);
                } catch (...) {}
            }

            size_t state_pos = line.find("curProcState=");
            if (state_pos != std::string::npos) {
                current_obs.raw_importance_or_state = line.substr(state_pos);
            }
        }
    }

    if (in_record && current_obs.pid > 0) {
        processes.push_back(std::move(current_obs));
    }

    return core::Result<std::vector<AdbProcessObservation>>::ok(std::move(processes));
}

} // namespace kernox::adb
