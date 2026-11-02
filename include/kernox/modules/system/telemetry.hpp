#pragma once

#include "kernox/core/error.hpp"
#include "kernox/core/result.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace kernox::system {

/**
 * @brief Aggregate CPU tick counters from Linux /proc/stat.
 *
 * Ticks are measured in USER_HZ (typically 1/100ths of a second).
 * In modern Linux (2.6+):
 *  - 'guest' is already included in 'user'.
 *  - 'guest_nice' is already included in 'nice'.
 *
 * Idle time is defined as: idle + iowait.
 * Busy time is defined as: user + nice + system + irq + softirq + steal.
 * Total time is defined as: idle_time + busy_time.
 */
struct CpuStat {
    uint64_t user{0};
    uint64_t nice{0};
    uint64_t system{0};
    uint64_t idle{0};
    uint64_t iowait{0};
    uint64_t irq{0};
    uint64_t softirq{0};
    uint64_t steal{0};
    uint64_t guest{0};
    uint64_t guest_nice{0};

    [[nodiscard]] constexpr uint64_t idle_ticks() const noexcept {
        return idle + iowait;
    }

    [[nodiscard]] constexpr uint64_t busy_ticks() const noexcept {
        return user + nice + system + irq + softirq + steal;
    }

    [[nodiscard]] constexpr uint64_t total_ticks() const noexcept {
        return idle_ticks() + busy_ticks();
    }
};

/**
 * @brief Memory usage statistics parsed from Linux /proc/meminfo.
 *
 * All values are stored in bytes.
 * Usage percentage is computed as: (used_bytes / total_bytes) * 100.0.
 */
struct MemoryInfo {
    uint64_t total_bytes{0};
    uint64_t available_bytes{0};
    uint64_t used_bytes{0};
    uint64_t free_bytes{0};
    uint64_t buffers_bytes{0};
    uint64_t cached_bytes{0};
    double usage_percent{0.0};
    bool used_fallback_available{false}; // True if MemAvailable was absent and estimated from Free+Buffers+Cached
};

/**
 * @brief System load averages over 1, 5, and 15 minutes parsed from /proc/loadavg.
 */
struct LoadAverage {
    double load_1min{0.0};
    double load_5min{0.0};
    double load_15min{0.0};
};

/**
 * @brief System uptime and total idle time parsed from /proc/uptime.
 */
struct SystemUptime {
    double uptime_seconds{0.0};
    double idle_seconds{0.0};
};

/**
 * @brief Aggregated host telemetry report.
 *
 * Each metric is stored as an independent Result<T> to guarantee that failure
 * to collect one metric does not prevent reporting of others.
 */
struct HostTelemetry {
    std::string os_name{};
    std::string node_name{};
    std::string kernel_release{};
    std::string kernel_version{};
    std::string architecture{};

    core::Result<double> cpu_utilization_percent{core::Error(core::ErrorCode::InvalidState, "Not collected")};
    core::Result<MemoryInfo> memory{core::Error(core::ErrorCode::InvalidState, "Not collected")};
    core::Result<LoadAverage> load_avg{core::Error(core::ErrorCode::InvalidState, "Not collected")};
    core::Result<SystemUptime> uptime{core::Error(core::ErrorCode::InvalidState, "Not collected")};
};

/**
 * @brief Pure string parsing routines for /proc files.
 *
 * All functions are deterministic and take input strings directly,
 * enabling exhaustive unit testing without filesystem access.
 */
class TelemetryParser {
public:
    /**
     * @brief Parses the aggregate 'cpu ' line from /proc/stat.
     * @param content Full content or first line of /proc/stat.
     * @return CpuStat on success, or Error on malformed data.
     */
    static core::Result<CpuStat> parse_proc_stat(std::string_view content);

    /**
     * @brief Computes CPU utilization percentage across two sequential snapshots.
     *
     * Invariants & Safety:
     * - Checks for non-monotonic counters and counter rollbacks.
     * - Returns an error if delta_total == 0 (zero elapsed time).
     * - Returns an error if delta_idle > delta_total.
     * - Clamps resulting percentage to [0.0, 100.0].
     *
     * @param first Initial snapshot.
     * @param second Subsequent snapshot.
     * @return Utilization percentage [0.0, 100.0] on success, or Error.
     */
    static core::Result<double> calculate_cpu_utilization(const CpuStat& first, const CpuStat& second);

    /**
     * @brief Parses /proc/meminfo into MemoryInfo.
     *
     * Converts kB to bytes with arithmetic overflow protection.
     * Requires MemTotal > 0. Prefers MemAvailable, falling back to
     * MemFree + Buffers + Cached if MemAvailable is absent.
     *
     * @param content Full content of /proc/meminfo.
     * @return MemoryInfo on success, or Error on missing required fields.
     */
    static core::Result<MemoryInfo> parse_proc_meminfo(std::string_view content);

    /**
     * @brief Parses 1-, 5-, and 15-minute load averages from /proc/loadavg.
     * @param content Content of /proc/loadavg.
     * @return LoadAverage on success, or Error on malformed tokens.
     */
    static core::Result<LoadAverage> parse_proc_loadavg(std::string_view content);

    /**
     * @brief Parses uptime and idle seconds from /proc/uptime.
     * @param content Content of /proc/uptime.
     * @return SystemUptime on success, or Error on malformed tokens.
     */
    static core::Result<SystemUptime> parse_proc_uptime(std::string_view content);
};

/**
 * @brief Configurable paths for /proc files to allow dependency injection.
 */
struct TelemetryPaths {
    std::string proc_stat{"/proc/stat"};
    std::string proc_meminfo{"/proc/meminfo"};
    std::string proc_loadavg{"/proc/loadavg"};
    std::string proc_uptime{"/proc/uptime"};
};

/**
 * @brief System telemetry collector coordinating filesystem reads and parsing.
 */
class SystemTelemetry {
public:
    explicit SystemTelemetry(TelemetryPaths paths = TelemetryPaths{});

    [[nodiscard]] core::Result<CpuStat> read_cpu_stat() const;
    [[nodiscard]] core::Result<double> sample_cpu_utilization(std::chrono::milliseconds interval) const;
    [[nodiscard]] core::Result<MemoryInfo> read_memory_info() const;
    [[nodiscard]] core::Result<LoadAverage> read_load_average() const;
    [[nodiscard]] core::Result<SystemUptime> read_uptime() const;

    [[nodiscard]] HostTelemetry collect_telemetry(std::chrono::milliseconds cpu_sample_interval = std::chrono::milliseconds{100}) const;

    [[nodiscard]] const TelemetryPaths& paths() const noexcept { return paths_; }

private:
    TelemetryPaths paths_;
    static core::Result<std::string> read_proc_file(const std::string& path);
};

} // namespace kernox::system

namespace kernox::sys {
    using namespace kernox::system;
}
