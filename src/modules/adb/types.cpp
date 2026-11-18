#include "kernox/modules/adb/types.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace kernox::adb {

namespace {

std::string escape_json(std::string_view input) {
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

std::string bytes_to_mb_string(uint64_t bytes) {
    std::ostringstream oss;
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    oss << std::fixed << std::setprecision(1) << mb << " MB";
    return oss.str();
}

} // namespace

std::string_view to_string(AdbConnectionState state) noexcept {
    switch (state) {
        case AdbConnectionState::Device:       return "device";
        case AdbConnectionState::Unauthorized: return "unauthorized";
        case AdbConnectionState::Offline:      return "offline";
        case AdbConnectionState::Authorizing:  return "authorizing";
        case AdbConnectionState::Recovery:     return "recovery";
        case AdbConnectionState::Sideload:     return "sideload";
        case AdbConnectionState::Bootloader:   return "bootloader";
        case AdbConnectionState::NoDevice:     return "no_device";
        case AdbConnectionState::Unknown:      return "unknown";
    }
    return "unknown";
}

std::optional<AdbConnectionState> parse_connection_state(std::string_view str) noexcept {
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lower == "device")       return AdbConnectionState::Device;
    if (lower == "unauthorized") return AdbConnectionState::Unauthorized;
    if (lower == "offline")      return AdbConnectionState::Offline;
    if (lower == "authorizing")  return AdbConnectionState::Authorizing;
    if (lower == "recovery")     return AdbConnectionState::Recovery;
    if (lower == "sideload")     return AdbConnectionState::Sideload;
    if (lower == "bootloader")   return AdbConnectionState::Bootloader;
    if (lower == "no_device" || lower == "no devices") return AdbConnectionState::NoDevice;
    return AdbConnectionState::Unknown;
}

std::string_view to_string(ObservationConfidence confidence) noexcept {
    switch (confidence) {
        case ObservationConfidence::High:     return "high";
        case ObservationConfidence::Moderate: return "moderate";
        case ObservationConfidence::Low:      return "low";
        case ObservationConfidence::RawOnly:  return "raw_only";
    }
    return "unknown";
}

std::string AdbDeviceInfo::to_string() const {
    std::ostringstream oss;
    oss << "Serial: " << serial << " [" << adb::to_string(state) << "]";
    if (!model.empty() || !manufacturer.empty()) {
        oss << " - ";
        if (!manufacturer.empty()) oss << manufacturer << " ";
        if (!model.empty()) oss << model;
    }
    if (!android_release.empty() || sdk_version.has_value()) {
        oss << " (Android ";
        if (!android_release.empty()) oss << android_release;
        if (sdk_version.has_value()) oss << ", API " << *sdk_version;
        oss << ")";
    }
    return oss.str();
}

std::string AdbDeviceInfo::to_json() const {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"serial\": \"" << escape_json(serial) << "\",\n"
        << "  \"state\": \"" << adb::to_string(state) << "\",\n"
        << "  \"manufacturer\": \"" << escape_json(manufacturer) << "\",\n"
        << "  \"model\": \"" << escape_json(model) << "\",\n"
        << "  \"product\": \"" << escape_json(product) << "\",\n"
        << "  \"device\": \"" << escape_json(device) << "\",\n"
        << "  \"android_release\": \"" << escape_json(android_release) << "\",\n"
        << "  \"sdk_version\": ";
    if (sdk_version.has_value()) {
        oss << *sdk_version;
    } else {
        oss << "null";
    }
    oss << "\n}";
    return oss.str();
}

std::string AdbMemorySnapshot::to_string() const {
    std::ostringstream oss;
    oss << "================================================================================\n"
        << "ANDROID DEVICE MEMORY OBSERVATION (Source: " << source << ")\n"
        << "--------------------------------------------------------------------------------\n";

    if (total_ram_bytes.has_value()) {
        oss << "Total RAM       : " << bytes_to_mb_string(*total_ram_bytes)
            << " (" << *total_ram_bytes << " bytes)\n";
    } else {
        oss << "Total RAM       : [Unavailable]\n";
    }

    if (available_ram_bytes.has_value()) {
        oss << "Available RAM   : " << bytes_to_mb_string(*available_ram_bytes) << "\n";
    } else {
        oss << "Available RAM   : [Unavailable on this kernel]\n";
    }

    if (free_ram_bytes.has_value()) {
        oss << "Free RAM        : " << bytes_to_mb_string(*free_ram_bytes) << "\n";
    }

    if (used_ram_bytes.has_value()) {
        oss << "Used RAM        : " << bytes_to_mb_string(*used_ram_bytes) << "\n";
    }

    if (cached_bytes.has_value()) {
        oss << "Cached Memory   : " << bytes_to_mb_string(*cached_bytes) << "\n";
    }

    if (buffers_bytes.has_value()) {
        oss << "Buffers         : " << bytes_to_mb_string(*buffers_bytes) << "\n";
    }

    if (swap_total_bytes.has_value() && *swap_total_bytes > 0) {
        oss << "Swap Total      : " << bytes_to_mb_string(*swap_total_bytes) << "\n";
        if (swap_free_bytes.has_value()) {
            oss << "Swap Free       : " << bytes_to_mb_string(*swap_free_bytes) << "\n";
        }
    }

    if (zram_physical_used_bytes.has_value()) {
        oss << "ZRAM Physical   : " << bytes_to_mb_string(*zram_physical_used_bytes) << "\n";
    }

    if (lost_ram_bytes.has_value()) {
        oss << "Lost RAM        : " << bytes_to_mb_string(*lost_ram_bytes) << "\n";
    }

    if (!warnings.empty()) {
        oss << "Parser Warnings :\n";
        for (const auto& w : warnings) {
            oss << "  - " << w << "\n";
        }
    }
    oss << "================================================================================";
    return oss.str();
}

std::string AdbMemorySnapshot::to_json() const {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"source\": \"" << escape_json(source) << "\",\n"
        << "  \"timestamp_epoch_ms\": " << timestamp_epoch_ms << ",\n"
        << "  \"command_executed\": \"" << escape_json(command_executed) << "\",\n"
        << "  \"metrics\": {\n";

    auto write_opt = [&](std::string_view key, const std::optional<uint64_t>& val, bool comma) {
        oss << "    \"" << key << "\": ";
        if (val.has_value()) {
            oss << *val;
        } else {
            oss << "null";
        }
        if (comma) oss << ",";
        oss << "\n";
    };

    write_opt("total_ram_bytes", total_ram_bytes, true);
    write_opt("free_ram_bytes", free_ram_bytes, true);
    write_opt("available_ram_bytes", available_ram_bytes, true);
    write_opt("used_ram_bytes", used_ram_bytes, true);
    write_opt("cached_bytes", cached_bytes, true);
    write_opt("buffers_bytes", buffers_bytes, true);
    write_opt("swap_total_bytes", swap_total_bytes, true);
    write_opt("swap_free_bytes", swap_free_bytes, true);
    write_opt("zram_physical_used_bytes", zram_physical_used_bytes, true);
    write_opt("lost_ram_bytes", lost_ram_bytes, false);

    oss << "  },\n"
        << "  \"warnings\": [";
    for (size_t i = 0; i < warnings.size(); ++i) {
        oss << "\"" << escape_json(warnings[i]) << "\"" << (i + 1 < warnings.size() ? ", " : "");
    }
    oss << "]\n"
        << "}";
    return oss.str();
}

std::string AdbProcessObservation::to_string() const {
    std::ostringstream oss;
    oss << "PID: " << std::setw(6) << pid << " | "
        << std::left << std::setw(32) << process_name;
    if (pss_bytes.has_value()) {
        oss << " | PSS: " << std::right << std::setw(9) << bytes_to_mb_string(*pss_bytes);
    }
    if (!raw_importance_or_state.empty()) {
        oss << " | State: " << raw_importance_or_state;
    }
    if (oom_score_adj.has_value()) {
        oss << " (oom_adj: " << *oom_score_adj << ")";
    }
    return oss.str();
}

std::string AdbProcessObservation::to_json() const {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"source\": \"" << escape_json(source) << "\",\n"
        << "  \"pid\": " << pid << ",\n"
        << "  \"process_name\": \"" << escape_json(process_name) << "\",\n"
        << "  \"package_name\": \"" << escape_json(package_name) << "\",\n"
        << "  \"pss_bytes\": " << (pss_bytes.has_value() ? std::to_string(*pss_bytes) : "null") << ",\n"
        << "  \"rss_bytes\": " << (rss_bytes.has_value() ? std::to_string(*rss_bytes) : "null") << ",\n"
        << "  \"raw_importance\": \"" << escape_json(raw_importance_or_state) << "\",\n"
        << "  \"normalized_importance\": "
        << (normalized_importance.has_value() ? ("\"" + escape_json(*normalized_importance) + "\"") : "null") << ",\n"
        << "  \"oom_score_adj\": " << (oom_score_adj.has_value() ? std::to_string(*oom_score_adj) : "null") << ",\n"
        << "  \"confidence\": \"" << adb::to_string(confidence) << "\"\n"
        << "}";
    return oss.str();
}

std::string AdbObservationReport::to_string() const {
    std::ostringstream oss;
    oss << "================================================================================\n"
        << "KERNOX 2.0 ANDROID ADB OBSERVATION REPORT\n"
        << "Provenance: " << source << "\n"
        << "Device    : " << device_info.to_string() << "\n"
        << "Duration  : " << (static_cast<double>(collection_duration_us) / 1000.0) << " ms\n"
        << "--------------------------------------------------------------------------------\n";

    if (memory_snapshot.has_value()) {
        oss << "MEMORY SUMMARY:\n";
        if (memory_snapshot->total_ram_bytes.has_value()) {
            oss << "  Total RAM    : " << bytes_to_mb_string(*memory_snapshot->total_ram_bytes) << "\n";
        }
        if (memory_snapshot->available_ram_bytes.has_value()) {
            oss << "  Available RAM: " << bytes_to_mb_string(*memory_snapshot->available_ram_bytes) << "\n";
        }
        if (memory_snapshot->free_ram_bytes.has_value()) {
            oss << "  Free RAM     : " << bytes_to_mb_string(*memory_snapshot->free_ram_bytes) << "\n";
        }
        if (memory_snapshot->used_ram_bytes.has_value()) {
            oss << "  Used RAM     : " << bytes_to_mb_string(*memory_snapshot->used_ram_bytes) << "\n";
        }
    } else {
        oss << "MEMORY SUMMARY: [Observation Failed or Inaccessible]\n";
    }

    oss << "--------------------------------------------------------------------------------\n"
        << "PROCESS OBSERVATIONS (" << processes.size() << " monitored processes):\n";

    size_t display_limit = std::min<size_t>(processes.size(), 20);
    for (size_t i = 0; i < display_limit; ++i) {
        oss << "  " << processes[i].to_string() << "\n";
    }
    if (processes.size() > display_limit) {
        oss << "  ... (" << (processes.size() - display_limit) << " more processes omitted from summary)\n";
    }

    if (!failed_commands.empty() || !warnings.empty() || !permission_restrictions.empty()) {
        oss << "--------------------------------------------------------------------------------\n"
            << "DIAGNOSTICS & SAFETY AUDIT:\n";
        for (const auto& fc : failed_commands) {
            oss << "  [Command Failed] " << fc.first << ": " << fc.second << "\n";
        }
        for (const auto& pr : permission_restrictions) {
            oss << "  [Permission Restricted] " << pr << "\n";
        }
        for (const auto& w : warnings) {
            oss << "  [Warning] " << w << "\n";
        }
    }

    oss << "================================================================================";
    return oss.str();
}

std::string AdbObservationReport::to_json() const {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"source\": \"" << escape_json(source) << "\",\n"
        << "  \"collection_timestamp_epoch_ms\": " << collection_timestamp_epoch_ms << ",\n"
        << "  \"collection_duration_us\": " << collection_duration_us << ",\n"
        << "  \"device_info\": " << device_info.to_json() << ",\n"
        << "  \"memory_snapshot\": "
        << (memory_snapshot.has_value() ? memory_snapshot->to_json() : "null") << ",\n"
        << "  \"process_count\": " << processes.size() << ",\n"
        << "  \"processes\": [\n";

    for (size_t i = 0; i < processes.size(); ++i) {
        std::string p_json = processes[i].to_json();
        std::istringstream iss(p_json);
        std::string line;
        bool first = true;
        while (std::getline(iss, line)) {
            if (!first) oss << "\n";
            oss << "    " << line;
            first = false;
        }
        if (i + 1 < processes.size()) oss << ",";
        oss << "\n";
    }

    oss << "  ],\n"
        << "  \"diagnostics\": {\n"
        << "    \"failed_commands\": [\n";
    for (size_t i = 0; i < failed_commands.size(); ++i) {
        oss << "      {\"command\": \"" << escape_json(failed_commands[i].first)
            << "\", \"error\": \"" << escape_json(failed_commands[i].second) << "\"}"
            << (i + 1 < failed_commands.size() ? "," : "") << "\n";
    }
    oss << "    ],\n"
        << "    \"permission_restrictions\": [";
    for (size_t i = 0; i < permission_restrictions.size(); ++i) {
        oss << "\"" << escape_json(permission_restrictions[i]) << "\""
            << (i + 1 < permission_restrictions.size() ? ", " : "");
    }
    oss << "],\n"
        << "    \"warnings\": [";
    for (size_t i = 0; i < warnings.size(); ++i) {
        oss << "\"" << escape_json(warnings[i]) << "\""
            << (i + 1 < warnings.size() ? ", " : "");
    }
    oss << "]\n"
        << "  }\n"
        << "}";
    return oss.str();
}

} // namespace kernox::adb
