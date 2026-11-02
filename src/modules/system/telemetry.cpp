#include "kernox/modules/system/telemetry.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>

namespace kernox::system {

namespace {

// Trims leading and trailing ASCII whitespace
std::string_view trim_sv(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

// Parses an unsigned 64-bit integer from string_view safely
bool parse_u64(std::string_view sv, uint64_t& out) {
    sv = trim_sv(sv);
    if (sv.empty()) {
        return false;
    }
    uint64_t result = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
    if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
        return false;
    }
    out = result;
    return true;
}

// Parses a double from string_view safely, rejecting NaN, Inf, and malformed tokens
bool parse_double(std::string_view sv, double& out) {
    sv = trim_sv(sv);
    if (sv.empty()) {
        return false;
    }
    std::string str(sv);
    char* endptr = nullptr;
    errno = 0;
    double val = std::strtod(str.c_str(), &endptr);
    if (errno != 0 || endptr != str.c_str() + str.size()) {
        return false;
    }
    if (!std::isfinite(val)) {
        return false;
    }
    out = val;
    return true;
}

} // namespace

core::Result<CpuStat> TelemetryParser::parse_proc_stat(std::string_view content) {
    std::istringstream stream{std::string(content)};
    std::string line;

    while (std::getline(stream, line)) {
        std::string_view sv = trim_sv(line);
        // The aggregate line starts with "cpu " (not "cpu0", "cpu1", etc.)
        if (sv.starts_with("cpu ") || sv.starts_with("cpu\t")) {
            std::istringstream line_stream{line};
            std::string cpu_label;
            line_stream >> cpu_label; // Consume "cpu"

            std::vector<uint64_t> counters;
            std::string token;
            while (line_stream >> token) {
                uint64_t val = 0;
                if (!parse_u64(token, val)) {
                    return core::Result<CpuStat>::err(
                        core::ErrorCode::InvalidArgument,
                        "Malformed /proc/stat: invalid non-numeric counter: '" + token + "'");
                }
                counters.push_back(val);
            }

            // Linux aggregate line has at least: user, nice, system, idle
            if (counters.size() < 4) {
                return core::Result<CpuStat>::err(
                    core::ErrorCode::InvalidArgument,
                    "Malformed /proc/stat: expected at least 4 counters for aggregate CPU line");
            }

            CpuStat stat{};
            stat.user = counters[0];
            stat.nice = counters[1];
            stat.system = counters[2];
            stat.idle = counters[3];
            if (counters.size() > 4) stat.iowait = counters[4];
            if (counters.size() > 5) stat.irq = counters[5];
            if (counters.size() > 6) stat.softirq = counters[6];
            if (counters.size() > 7) stat.steal = counters[7];
            if (counters.size() > 8) stat.guest = counters[8];
            if (counters.size() > 9) stat.guest_nice = counters[9];

            return core::Result<CpuStat>::ok(stat);
        }
    }

    return core::Result<CpuStat>::err(
        core::ErrorCode::NotFound, "Malformed /proc/stat: aggregate 'cpu' line not found");
}

core::Result<double> TelemetryParser::calculate_cpu_utilization(const CpuStat& first,
                                                              const CpuStat& second) {
    const uint64_t total1 = first.total_ticks();
    const uint64_t total2 = second.total_ticks();
    const uint64_t idle1 = first.idle_ticks();
    const uint64_t idle2 = second.idle_ticks();
    const uint64_t busy1 = first.busy_ticks();
    const uint64_t busy2 = second.busy_ticks();

    // Check for counter rollback or non-monotonicity across system restarts/resets
    if (total2 < total1) {
        return core::Result<double>::err(
            core::ErrorCode::InvalidState,
            "Non-monotonic CPU counters detected: total2 (" + std::to_string(total2) +
            ") < total1 (" + std::to_string(total1) + ")");
    }
    if (idle2 < idle1) {
        return core::Result<double>::err(
            core::ErrorCode::InvalidState,
            "Non-monotonic CPU counters detected: idle2 (" + std::to_string(idle2) +
            ") < idle1 (" + std::to_string(idle1) + ")");
    }
    if (busy2 < busy1) {
        return core::Result<double>::err(
            core::ErrorCode::InvalidState,
            "Non-monotonic CPU counters detected: busy2 (" + std::to_string(busy2) +
            ") < busy1 (" + std::to_string(busy1) + ")");
    }

    const uint64_t delta_total = total2 - total1;
    if (delta_total == 0) {
        return core::Result<double>::err(
            core::ErrorCode::InvalidState,
            "Zero CPU time delta elapsed between snapshots; cannot compute utilization");
    }

    const uint64_t delta_idle = idle2 - idle1;
    if (delta_idle > delta_total) {
        return core::Result<double>::err(
            core::ErrorCode::InvalidState,
            "Invalid CPU counter delta: idle delta (" + std::to_string(delta_idle) +
            ") exceeds total delta (" + std::to_string(delta_total) + ")");
    }

    const uint64_t delta_busy = delta_total - delta_idle;
    double percentage = (100.0 * static_cast<double>(delta_busy)) / static_cast<double>(delta_total);

    // Clamp to valid range [0.0, 100.0]
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;

    return core::Result<double>::ok(percentage);
}

core::Result<MemoryInfo> TelemetryParser::parse_proc_meminfo(std::string_view content) {
    std::istringstream stream{std::string(content)};
    std::string line;

    bool has_total = false;
    bool has_available = false;
    bool has_free = false;
    bool has_buffers = false;
    bool has_cached = false;

    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
    uint64_t free_kb = 0;
    uint64_t buffers_kb = 0;
    uint64_t cached_kb = 0;

    constexpr uint64_t max_kb_before_overflow = std::numeric_limits<uint64_t>::max() / 1024ULL;

    while (std::getline(stream, line)) {
        auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        std::string_view key = trim_sv(std::string_view(line).substr(0, colon_pos));
        std::string_view rest = trim_sv(std::string_view(line).substr(colon_pos + 1));

        // Format is: <value> [kB]
        auto space_pos = rest.find_first_of(" \t");
        std::string_view val_sv = (space_pos != std::string_view::npos)
                                      ? rest.substr(0, space_pos)
                                      : rest;

        uint64_t val = 0;
        if (!parse_u64(val_sv, val)) {
            continue;
        }

        if (val > max_kb_before_overflow) {
            return core::Result<MemoryInfo>::err(
                core::ErrorCode::InvalidArgument,
                "Memory counter in /proc/meminfo exceeds 64-bit byte conversion capacity");
        }

        if (key == "MemTotal") {
            has_total = true;
            total_kb = val;
        } else if (key == "MemAvailable") {
            has_available = true;
            available_kb = val;
        } else if (key == "MemFree") {
            has_free = true;
            free_kb = val;
        } else if (key == "Buffers") {
            has_buffers = true;
            buffers_kb = val;
        } else if (key == "Cached") {
            has_cached = true;
            cached_kb = val;
        }
    }

    if (!has_total || total_kb == 0) {
        return core::Result<MemoryInfo>::err(
            core::ErrorCode::InvalidArgument,
            "Missing or zero MemTotal in /proc/meminfo content");
    }

    MemoryInfo info{};
    info.total_bytes = total_kb * 1024ULL;
    info.free_bytes = free_kb * 1024ULL;
    info.buffers_bytes = buffers_kb * 1024ULL;
    info.cached_bytes = cached_kb * 1024ULL;

    if (has_available) {
        info.available_bytes = available_kb * 1024ULL;
        info.used_fallback_available = false;
    } else if (has_free) {
        // Fallback calculation for Linux kernels < 3.14 without MemAvailable: MemFree + Buffers + Cached
        uint64_t est_avail_kb = free_kb;
        if (has_buffers) {
            if (buffers_kb > std::numeric_limits<uint64_t>::max() - est_avail_kb) {
                return core::Result<MemoryInfo>::err(
                    core::ErrorCode::InvalidArgument,
                    "Arithmetic overflow calculating fallback available memory (free + buffers)");
            }
            est_avail_kb += buffers_kb;
        }
        if (has_cached) {
            if (cached_kb > std::numeric_limits<uint64_t>::max() - est_avail_kb) {
                return core::Result<MemoryInfo>::err(
                    core::ErrorCode::InvalidArgument,
                    "Arithmetic overflow calculating fallback available memory (free + buffers + cached)");
            }
            est_avail_kb += cached_kb;
        }

        if (est_avail_kb > max_kb_before_overflow) {
            return core::Result<MemoryInfo>::err(
                core::ErrorCode::InvalidArgument,
                "Fallback available memory exceeds 64-bit byte conversion capacity");
        }

        info.available_bytes = est_avail_kb * 1024ULL;
        info.used_fallback_available = true;
    } else {
        return core::Result<MemoryInfo>::err(
            core::ErrorCode::InvalidState,
            "Neither MemAvailable nor MemFree found in /proc/meminfo");
    }

    // Guard against edge cases where available exceeds total
    if (info.available_bytes > info.total_bytes) {
        info.available_bytes = info.total_bytes;
    }

    info.used_bytes = info.total_bytes - info.available_bytes;
    info.usage_percent = (100.0 * static_cast<double>(info.used_bytes)) / static_cast<double>(info.total_bytes);

    return core::Result<MemoryInfo>::ok(info);
}

core::Result<LoadAverage> TelemetryParser::parse_proc_loadavg(std::string_view content) {
    std::istringstream stream{std::string(content)};
    std::string tok1;
    std::string tok2;
    std::string tok3;

    if (!(stream >> tok1 >> tok2 >> tok3)) {
        return core::Result<LoadAverage>::err(
            core::ErrorCode::InvalidArgument,
            "Malformed /proc/loadavg: expected at least 3 load average tokens");
    }

    double l1 = 0.0;
    double l5 = 0.0;
    double l15 = 0.0;

    if (!parse_double(tok1, l1) || !parse_double(tok2, l5) || !parse_double(tok3, l15)) {
        return core::Result<LoadAverage>::err(
            core::ErrorCode::InvalidArgument,
            "Malformed /proc/loadavg: invalid floating-point load average value");
    }

    if (l1 < 0.0 || l5 < 0.0 || l15 < 0.0) {
        return core::Result<LoadAverage>::err(
            core::ErrorCode::InvalidArgument,
            "Malformed /proc/loadavg: negative load average values are invalid");
    }

    LoadAverage avg{};
    avg.load_1min = l1;
    avg.load_5min = l5;
    avg.load_15min = l15;
    return core::Result<LoadAverage>::ok(avg);
}

core::Result<SystemUptime> TelemetryParser::parse_proc_uptime(std::string_view content) {
    std::istringstream stream{std::string(content)};
    std::string tok_up;
    if (!(stream >> tok_up)) {
        return core::Result<SystemUptime>::err(
            core::ErrorCode::InvalidArgument,
            "Malformed /proc/uptime: empty content");
    }

    double up = 0.0;
    if (!parse_double(tok_up, up) || up < 0.0) {
        return core::Result<SystemUptime>::err(
            core::ErrorCode::InvalidArgument,
            "Malformed /proc/uptime: invalid or negative uptime value: '" + tok_up + "'");
    }

    double idle = 0.0;
    std::string tok_idle;
    if (stream >> tok_idle) {
        parse_double(tok_idle, idle);
        if (idle < 0.0) idle = 0.0;
    }

    SystemUptime res{};
    res.uptime_seconds = up;
    res.idle_seconds = idle;
    return core::Result<SystemUptime>::ok(res);
}

core::Result<ProcessStatusInfo> TelemetryParser::parse_proc_status(std::string_view content, pid_t target_pid) {
    if (content.empty()) {
        return core::Result<ProcessStatusInfo>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot parse empty /proc/<pid>/status content");
    }

    ProcessStatusInfo info{};
    info.pid = target_pid;

    std::istringstream stream{std::string(content)};
    std::string line;

    while (std::getline(stream, line)) {
        auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        std::string_view key = trim_sv(std::string_view(line).substr(0, colon_pos));
        std::string_view val = trim_sv(std::string_view(line).substr(colon_pos + 1));

        if (key == "Name") {
            info.name = std::string(val);
        } else if (key == "State") {
            if (!val.empty()) {
                info.state = val.front();
                auto paren_start = val.find('(');
                auto paren_end = val.find(')', paren_start);
                if (paren_start != std::string_view::npos && paren_end != std::string_view::npos && paren_end > paren_start) {
                    info.state_description = std::string(val.substr(paren_start + 1, paren_end - paren_start - 1));
                }
            }
        } else if (key == "Pid") {
            uint64_t v = 0;
            if (parse_u64(val, v)) {
                info.pid = static_cast<pid_t>(v);
            }
        } else if (key == "PPid") {
            uint64_t v = 0;
            if (parse_u64(val, v)) {
                info.ppid = static_cast<pid_t>(v);
            }
        } else if (key == "TracerPid") {
            uint64_t v = 0;
            if (parse_u64(val, v)) {
                info.tracer_pid = static_cast<pid_t>(v);
            }
        } else if (key == "Threads") {
            uint64_t v = 0;
            if (parse_u64(val, v)) {
                info.threads = static_cast<uint32_t>(v);
            }
        } else if (key == "Uid") {
            std::istringstream uid_stream{std::string(val)};
            std::string t1, t2, t3, t4;
            if (uid_stream >> t1 >> t2 >> t3 >> t4) {
                uint64_t u1 = 0, u2 = 0, u3 = 0, u4 = 0;
                if (parse_u64(t1, u1)) info.uid_real = static_cast<uid_t>(u1);
                if (parse_u64(t2, u2)) info.uid_effective = static_cast<uid_t>(u2);
                if (parse_u64(t3, u3)) info.uid_saved = static_cast<uid_t>(u3);
                if (parse_u64(t4, u4)) info.uid_fs = static_cast<uid_t>(u4);
            }
        } else if (key == "Gid") {
            std::istringstream gid_stream{std::string(val)};
            std::string t1, t2, t3, t4;
            if (gid_stream >> t1 >> t2 >> t3 >> t4) {
                uint64_t g1 = 0, g2 = 0, g3 = 0, g4 = 0;
                if (parse_u64(t1, g1)) info.gid_real = static_cast<gid_t>(g1);
                if (parse_u64(t2, g2)) info.gid_effective = static_cast<gid_t>(g2);
                if (parse_u64(t3, g3)) info.gid_saved = static_cast<gid_t>(g3);
                if (parse_u64(t4, g4)) info.gid_fs = static_cast<gid_t>(g4);
            }
        } else if (key == "VmSize") {
            auto kb_pos = val.find(" kB");
            std::string_view num_part = (kb_pos != std::string_view::npos) ? val.substr(0, kb_pos) : val;
            uint64_t kb = 0;
            if (parse_u64(num_part, kb)) {
                info.vm_size_bytes = kb * 1024ULL;
            }
        } else if (key == "VmRSS") {
            auto kb_pos = val.find(" kB");
            std::string_view num_part = (kb_pos != std::string_view::npos) ? val.substr(0, kb_pos) : val;
            uint64_t kb = 0;
            if (parse_u64(num_part, kb)) {
                info.vm_rss_bytes = kb * 1024ULL;
            }
        } else if (key == "VmPeak") {
            auto kb_pos = val.find(" kB");
            std::string_view num_part = (kb_pos != std::string_view::npos) ? val.substr(0, kb_pos) : val;
            uint64_t kb = 0;
            if (parse_u64(num_part, kb)) {
                info.vm_peak_bytes = kb * 1024ULL;
            }
        } else if (key == "VmSwap") {
            auto kb_pos = val.find(" kB");
            std::string_view num_part = (kb_pos != std::string_view::npos) ? val.substr(0, kb_pos) : val;
            uint64_t kb = 0;
            if (parse_u64(num_part, kb)) {
                info.vm_swap_bytes = kb * 1024ULL;
            }
        } else if (key == "voluntary_ctxt_switches") {
            parse_u64(val, info.voluntary_context_switches);
        } else if (key == "nonvoluntary_ctxt_switches") {
            parse_u64(val, info.nonvoluntary_context_switches);
        }
    }

    return core::Result<ProcessStatusInfo>::ok(std::move(info));
}

core::Result<ProcessIoStat> TelemetryParser::parse_proc_io(std::string_view content, pid_t target_pid) {
    if (content.empty()) {
        return core::Result<ProcessIoStat>::err(
            core::ErrorCode::InvalidArgument,
            "Cannot parse empty /proc/<pid>/io content");
    }

    ProcessIoStat stat{};
    stat.pid = target_pid;

    std::istringstream stream{std::string(content)};
    std::string line;

    while (std::getline(stream, line)) {
        auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        std::string_view key = trim_sv(std::string_view(line).substr(0, colon_pos));
        std::string_view val = trim_sv(std::string_view(line).substr(colon_pos + 1));

        if (key == "rchar") {
            parse_u64(val, stat.rchar);
        } else if (key == "wchar") {
            parse_u64(val, stat.wchar);
        } else if (key == "syscr") {
            parse_u64(val, stat.syscr);
        } else if (key == "syscw") {
            parse_u64(val, stat.syscw);
        } else if (key == "read_bytes") {
            parse_u64(val, stat.read_bytes);
        } else if (key == "write_bytes") {
            parse_u64(val, stat.write_bytes);
        } else if (key == "cancelled_write_bytes") {
            parse_u64(val, stat.cancelled_write_bytes);
        }
    }

    return core::Result<ProcessIoStat>::ok(stat);
}

// -----------------------------------------------------------------------------
// SystemTelemetry
// -----------------------------------------------------------------------------

SystemTelemetry::SystemTelemetry(TelemetryPaths paths)
    : paths_(std::move(paths)) {}

core::Result<std::string> SystemTelemetry::read_proc_file(const std::string& path) {
    constexpr size_t max_proc_read_bytes = 1024 * 1024; // 1 MiB upper bound to prevent unbounded buffer growth

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return core::Result<std::string>::err(
            core::ErrorCode::IoError,
            "Failed to open proc file: " + path,
            errno);
    }

    // Read virtual pseudo-file safely in chunks (since /proc reports st_size == 0)
    std::string content;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        const auto read_bytes = static_cast<size_t>(file.gcount());
        if (content.size() + read_bytes > max_proc_read_bytes) {
            return core::Result<std::string>::err(
                core::ErrorCode::ResourceExhausted,
                "Proc file content exceeded maximum permitted buffer size (1 MiB): " + path);
        }
        content.append(buffer, read_bytes);
    }

    if (file.bad()) {
        return core::Result<std::string>::err(
            core::ErrorCode::IoError,
            "I/O error encountered while reading proc file: " + path,
            errno);
    }

    if (content.empty() && !file.eof()) {
        return core::Result<std::string>::err(
            core::ErrorCode::IoError,
            "Failed to read content from proc file: " + path);
    }

    return core::Result<std::string>::ok(content);
}

core::Result<CpuStat> SystemTelemetry::read_cpu_stat() const {
    auto file_res = read_proc_file(paths_.proc_stat);
    if (!file_res.is_ok()) {
        return core::Result<CpuStat>::err(file_res.error());
    }
    return TelemetryParser::parse_proc_stat(file_res.value());
}

core::Result<double> SystemTelemetry::sample_cpu_utilization(std::chrono::milliseconds interval) const {
    auto first_res = read_cpu_stat();
    if (!first_res.is_ok()) {
        return core::Result<double>::err(first_res.error());
    }

    std::this_thread::sleep_for(interval);

    auto second_res = read_cpu_stat();
    if (!second_res.is_ok()) {
        return core::Result<double>::err(second_res.error());
    }

    return TelemetryParser::calculate_cpu_utilization(first_res.value(), second_res.value());
}

core::Result<MemoryInfo> SystemTelemetry::read_memory_info() const {
    auto file_res = read_proc_file(paths_.proc_meminfo);
    if (!file_res.is_ok()) {
        return core::Result<MemoryInfo>::err(file_res.error());
    }
    return TelemetryParser::parse_proc_meminfo(file_res.value());
}

core::Result<LoadAverage> SystemTelemetry::read_load_average() const {
    auto file_res = read_proc_file(paths_.proc_loadavg);
    if (!file_res.is_ok()) {
        return core::Result<LoadAverage>::err(file_res.error());
    }
    return TelemetryParser::parse_proc_loadavg(file_res.value());
}

core::Result<SystemUptime> SystemTelemetry::read_uptime() const {
    auto file_res = read_proc_file(paths_.proc_uptime);
    if (!file_res.is_ok()) {
        return core::Result<SystemUptime>::err(file_res.error());
    }
    return TelemetryParser::parse_proc_uptime(file_res.value());
}

core::Result<ProcessStatusInfo> SystemTelemetry::read_process_status(pid_t pid, const std::string& proc_dir) const {
    if (pid < 0) {
        return core::Result<ProcessStatusInfo>::err(
            core::ErrorCode::InvalidArgument,
            "Target process ID must be non-negative: " + std::to_string(pid));
    }
    std::string path = proc_dir + "/" + std::to_string(pid) + "/status";
    auto file_res = read_proc_file(path);
    if (!file_res.is_ok()) {
        return core::Result<ProcessStatusInfo>::err(file_res.error());
    }
    return TelemetryParser::parse_proc_status(file_res.value(), pid);
}

core::Result<ProcessIoStat> SystemTelemetry::read_process_io(pid_t pid, const std::string& proc_dir) const {
    if (pid < 0) {
        return core::Result<ProcessIoStat>::err(
            core::ErrorCode::InvalidArgument,
            "Target process ID must be non-negative: " + std::to_string(pid));
    }
    std::string path = proc_dir + "/" + std::to_string(pid) + "/io";
    auto file_res = read_proc_file(path);
    if (!file_res.is_ok()) {
        return core::Result<ProcessIoStat>::err(file_res.error());
    }
    return TelemetryParser::parse_proc_io(file_res.value(), pid);
}

core::Result<ProcessStatusInfo> SystemTelemetry::get_process_status(pid_t pid, const std::string& proc_dir) {
    SystemTelemetry t;
    return t.read_process_status(pid, proc_dir);
}

core::Result<ProcessIoStat> SystemTelemetry::get_process_io(pid_t pid, const std::string& proc_dir) {
    SystemTelemetry t;
    return t.read_process_io(pid, proc_dir);
}

HostTelemetry SystemTelemetry::collect_telemetry(std::chrono::milliseconds cpu_sample_interval) const {
    HostTelemetry telemetry{};

    struct utsname sys_info{};
    if (::uname(&sys_info) == 0) {
        telemetry.os_name = sys_info.sysname;
        telemetry.node_name = sys_info.nodename;
        telemetry.kernel_release = sys_info.release;
        telemetry.kernel_version = sys_info.version;
        telemetry.architecture = sys_info.machine;
    }

    telemetry.cpu_utilization_percent = sample_cpu_utilization(cpu_sample_interval);
    telemetry.memory = read_memory_info();
    telemetry.load_avg = read_load_average();
    telemetry.uptime = read_uptime();

    return telemetry;
}

} // namespace kernox::system
