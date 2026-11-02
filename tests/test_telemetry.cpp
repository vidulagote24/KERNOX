#include "kernox/modules/system/telemetry.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace kernox::system;
using namespace kernox::core;

namespace {

bool double_approx_eq(double a, double b, double epsilon = 0.001) {
    return std::fabs(a - b) <= epsilon;
}

// -----------------------------------------------------------------------------
// CPU Stat Parsing Tests
// -----------------------------------------------------------------------------

int test_cpu_stat_parsing_valid() {
    const std::string content =
        "cpu  487 0 1066 8177 227 0 129 0 0 0\n"
        "cpu0 25 0 155 591 30 0 64 0 0 0\n"
        "intr 123456\n";

    auto res = TelemetryParser::parse_proc_stat(content);
    KERNOX_ASSERT_TRUE(res.is_ok());

    const auto& stat = res.value();
    KERNOX_ASSERT_EQ(stat.user, 487ULL);
    KERNOX_ASSERT_EQ(stat.nice, 0ULL);
    KERNOX_ASSERT_EQ(stat.system, 1066ULL);
    KERNOX_ASSERT_EQ(stat.idle, 8177ULL);
    KERNOX_ASSERT_EQ(stat.iowait, 227ULL);
    KERNOX_ASSERT_EQ(stat.irq, 0ULL);
    KERNOX_ASSERT_EQ(stat.softirq, 129ULL);
    KERNOX_ASSERT_EQ(stat.steal, 0ULL);
    KERNOX_ASSERT_EQ(stat.guest, 0ULL);
    KERNOX_ASSERT_EQ(stat.guest_nice, 0ULL);

    KERNOX_ASSERT_EQ(stat.idle_ticks(), 8177ULL + 227ULL);
    KERNOX_ASSERT_EQ(stat.busy_ticks(), 487ULL + 1066ULL + 129ULL);
    KERNOX_ASSERT_EQ(stat.total_ticks(), stat.idle_ticks() + stat.busy_ticks());
    return 0;
}

int test_cpu_stat_parsing_minimal() {
    // Exactly 4 counters: user, nice, system, idle
    const std::string content = "cpu 100 20 50 830\n";
    auto res = TelemetryParser::parse_proc_stat(content);
    KERNOX_ASSERT_TRUE(res.is_ok());

    const auto& stat = res.value();
    KERNOX_ASSERT_EQ(stat.user, 100ULL);
    KERNOX_ASSERT_EQ(stat.nice, 20ULL);
    KERNOX_ASSERT_EQ(stat.system, 50ULL);
    KERNOX_ASSERT_EQ(stat.idle, 830ULL);
    KERNOX_ASSERT_EQ(stat.iowait, 0ULL);
    KERNOX_ASSERT_EQ(stat.total_ticks(), 1000ULL);
    return 0;
}

int test_cpu_stat_parsing_tab_delimited() {
    // Tab-delimited aggregate cpu line
    const std::string content = "cpu\t487\t0\t1066\t8177\t227\t0\t129\t0\t0\t0\n";
    auto res = TelemetryParser::parse_proc_stat(content);
    KERNOX_ASSERT_TRUE(res.is_ok());

    const auto& stat = res.value();
    KERNOX_ASSERT_EQ(stat.user, 487ULL);
    KERNOX_ASSERT_EQ(stat.idle, 8177ULL);
    KERNOX_ASSERT_EQ(stat.iowait, 227ULL);
    KERNOX_ASSERT_EQ(stat.softirq, 129ULL);
    return 0;
}

int test_cpu_stat_parsing_invalid() {
    // Missing aggregate cpu line
    {
        const std::string content = "cpu0 100 20 50 830\ncpu1 50 10 20 400\n";
        auto res = TelemetryParser::parse_proc_stat(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::NotFound);
    }
    // Insufficient counters
    {
        const std::string content = "cpu 100 20 50\n";
        auto res = TelemetryParser::parse_proc_stat(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Non-numeric token
    {
        const std::string content = "cpu 100 bad_token 50 830\n";
        auto res = TelemetryParser::parse_proc_stat(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// CPU Utilization Calculation Tests
// -----------------------------------------------------------------------------

int test_cpu_utilization_calculation() {
    CpuStat snap1{};
    snap1.user = 100;
    snap1.idle = 900; // total 1000, busy 100, idle 900

    CpuStat snap2{};
    snap2.user = 350;
    snap2.idle = 1650; // total 2000, busy 350, idle 1650

    // delta_total = 1000, delta_idle = 750, delta_busy = 250
    // utilization = 250 / 1000 * 100 = 25.0%
    auto res = TelemetryParser::calculate_cpu_utilization(snap1, snap2);
    KERNOX_ASSERT_TRUE(res.is_ok());
    KERNOX_ASSERT_TRUE(double_approx_eq(res.value(), 25.0));

    // 100% utilization: no idle increase
    CpuStat snap3 = snap2;
    snap3.user += 500; // delta_total = 500, delta_idle = 0 -> 100%
    auto res_100 = TelemetryParser::calculate_cpu_utilization(snap2, snap3);
    KERNOX_ASSERT_TRUE(res_100.is_ok());
    KERNOX_ASSERT_TRUE(double_approx_eq(res_100.value(), 100.0));

    // 0% utilization: all idle increase
    CpuStat snap4 = snap3;
    snap4.idle += 500; // delta_total = 500, delta_busy = 0 -> 0%
    auto res_0 = TelemetryParser::calculate_cpu_utilization(snap3, snap4);
    KERNOX_ASSERT_TRUE(res_0.is_ok());
    KERNOX_ASSERT_TRUE(double_approx_eq(res_0.value(), 0.0));

    return 0;
}

int test_cpu_utilization_rollback_and_zero_delta() {
    CpuStat snap1{};
    snap1.user = 100;
    snap1.idle = 900;

    // Zero delta (snapshots identical)
    auto res_zero = TelemetryParser::calculate_cpu_utilization(snap1, snap1);
    KERNOX_ASSERT_TRUE(res_zero.is_err());
    KERNOX_ASSERT_EQ(res_zero.error().code, ErrorCode::InvalidState);

    // Total counter rollback
    CpuStat snap_rollback_total{};
    snap_rollback_total.user = 50;
    snap_rollback_total.idle = 500; // total 550 < 1000
    auto res_rb_total = TelemetryParser::calculate_cpu_utilization(snap1, snap_rollback_total);
    KERNOX_ASSERT_TRUE(res_rb_total.is_err());
    KERNOX_ASSERT_EQ(res_rb_total.error().code, ErrorCode::InvalidState);

    // Idle counter rollback (total increases, but idle dropped)
    CpuStat snap_rollback_idle = snap1;
    snap_rollback_idle.user += 200;
    snap_rollback_idle.idle -= 50;
    auto res_rb_idle = TelemetryParser::calculate_cpu_utilization(snap1, snap_rollback_idle);
    KERNOX_ASSERT_TRUE(res_rb_idle.is_err());
    KERNOX_ASSERT_EQ(res_rb_idle.error().code, ErrorCode::InvalidState);

    // Busy counter rollback
    CpuStat snap_rollback_busy = snap1;
    snap_rollback_busy.user -= 50;
    snap_rollback_busy.idle += 200;
    auto res_rb_busy = TelemetryParser::calculate_cpu_utilization(snap1, snap_rollback_busy);
    KERNOX_ASSERT_TRUE(res_rb_busy.is_err());
    KERNOX_ASSERT_EQ(res_rb_busy.error().code, ErrorCode::InvalidState);

    return 0;
}

// -----------------------------------------------------------------------------
// Memory Parsing Tests
// -----------------------------------------------------------------------------

int test_memory_parsing_valid() {
    const std::string content =
        "MemTotal:        8000000 kB\n"
        "MemFree:         2000000 kB\n"
        "MemAvailable:    6000000 kB\n"
        "Buffers:          200000 kB\n"
        "Cached:          1800000 kB\n";

    auto res = TelemetryParser::parse_proc_meminfo(content);
    KERNOX_ASSERT_TRUE(res.is_ok());

    const auto& mem = res.value();
    KERNOX_ASSERT_EQ(mem.total_bytes, 8000000ULL * 1024ULL);
    KERNOX_ASSERT_EQ(mem.available_bytes, 6000000ULL * 1024ULL);
    KERNOX_ASSERT_EQ(mem.used_bytes, 2000000ULL * 1024ULL);
    KERNOX_ASSERT_EQ(mem.free_bytes, 2000000ULL * 1024ULL);
    KERNOX_ASSERT_EQ(mem.buffers_bytes, 200000ULL * 1024ULL);
    KERNOX_ASSERT_EQ(mem.cached_bytes, 1800000ULL * 1024ULL);
    KERNOX_ASSERT_FALSE(mem.used_fallback_available);
    KERNOX_ASSERT_TRUE(double_approx_eq(mem.usage_percent, 25.0));
    return 0;
}

int test_memory_parsing_fallback() {
    // Missing MemAvailable: should estimate available as Free + Buffers + Cached
    const std::string content =
        "MemTotal:       10000000 kB\n"
        "MemFree:         2000000 kB\n"
        "Buffers:         1000000 kB\n"
        "Cached:          1000000 kB\n";

    auto res = TelemetryParser::parse_proc_meminfo(content);
    KERNOX_ASSERT_TRUE(res.is_ok());

    const auto& mem = res.value();
    KERNOX_ASSERT_TRUE(mem.used_fallback_available);
    // Estimated available = 2000000 + 1000000 + 1000000 = 4000000 kB
    KERNOX_ASSERT_EQ(mem.available_bytes, 4000000ULL * 1024ULL);
    // Used = 10000000 - 4000000 = 6000000 kB
    KERNOX_ASSERT_EQ(mem.used_bytes, 6000000ULL * 1024ULL);
    KERNOX_ASSERT_TRUE(double_approx_eq(mem.usage_percent, 60.0));
    return 0;
}

int test_memory_parsing_clamping_available() {
    // MemAvailable greater than MemTotal: should clamp available to total
    const std::string content =
        "MemTotal:        4000000 kB\n"
        "MemAvailable:    6000000 kB\n";

    auto res = TelemetryParser::parse_proc_meminfo(content);
    KERNOX_ASSERT_TRUE(res.is_ok());

    const auto& mem = res.value();
    KERNOX_ASSERT_EQ(mem.available_bytes, 4000000ULL * 1024ULL);
    KERNOX_ASSERT_EQ(mem.used_bytes, 0ULL);
    KERNOX_ASSERT_TRUE(double_approx_eq(mem.usage_percent, 0.0));
    return 0;
}

int test_memory_parsing_overflow_hardening() {
    // Fallback addition overflow (free + buffers)
    {
        const std::string content =
            "MemTotal: 1000000 kB\n"
            "MemFree: 18446744073709551600 kB\n"
            "Buffers: 100 kB\n";
        auto res = TelemetryParser::parse_proc_meminfo(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Fallback addition overflow (free + buffers + cached)
    {
        const std::string content =
            "MemTotal: 1000000 kB\n"
            "MemFree: 9000000000000000000 kB\n"
            "Buffers: 9000000000000000000 kB\n"
            "Cached: 1000000000000000000 kB\n";
        auto res = TelemetryParser::parse_proc_meminfo(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Fallback byte multiplication overflow (sum exceeds max_kb_before_overflow)
    {
        const std::string content =
            "MemTotal: 10000000000000000 kB\n"
            "MemFree: 10000000000000000 kB\n"
            "Buffers: 10000000000000000 kB\n";
        auto res = TelemetryParser::parse_proc_meminfo(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    return 0;
}

int test_memory_parsing_invalid() {
    // Missing MemTotal
    {
        const std::string content = "MemFree: 2000000 kB\nMemAvailable: 3000000 kB\n";
        auto res = TelemetryParser::parse_proc_meminfo(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Zero MemTotal
    {
        const std::string content = "MemTotal: 0 kB\nMemAvailable: 0 kB\n";
        auto res = TelemetryParser::parse_proc_meminfo(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Missing both MemAvailable and MemFree
    {
        const std::string content = "MemTotal: 8000000 kB\nBuffers: 100 kB\n";
        auto res = TelemetryParser::parse_proc_meminfo(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidState);
    }
    // Arithmetic overflow in kB for MemTotal
    {
        const std::string content = "MemTotal: 18446744073709551615 kB\n";
        auto res = TelemetryParser::parse_proc_meminfo(content);
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Load Average & Uptime Parsing Tests
// -----------------------------------------------------------------------------

int test_load_average_parsing() {
    // Valid input
    {
        const std::string content = "0.88 0.18 0.06 1/302 412\n";
        auto res = TelemetryParser::parse_proc_loadavg(content);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& avg = res.value();
        KERNOX_ASSERT_TRUE(double_approx_eq(avg.load_1min, 0.88));
        KERNOX_ASSERT_TRUE(double_approx_eq(avg.load_5min, 0.18));
        KERNOX_ASSERT_TRUE(double_approx_eq(avg.load_15min, 0.06));
    }
    // Insufficient tokens
    {
        auto res = TelemetryParser::parse_proc_loadavg("0.88 0.18");
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Negative load average
    {
        auto res = TelemetryParser::parse_proc_loadavg("-0.50 0.18 0.06 1/100 5");
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Malformed float
    {
        auto res = TelemetryParser::parse_proc_loadavg("abc 0.18 0.06 1/100 5");
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Non-finite values: nan, inf, -inf
    {
        auto res_nan = TelemetryParser::parse_proc_loadavg("nan 0.18 0.06 1/100 5");
        KERNOX_ASSERT_TRUE(res_nan.is_err());
        KERNOX_ASSERT_EQ(res_nan.error().code, ErrorCode::InvalidArgument);

        auto res_inf = TelemetryParser::parse_proc_loadavg("0.88 inf 0.06 1/100 5");
        KERNOX_ASSERT_TRUE(res_inf.is_err());
        KERNOX_ASSERT_EQ(res_inf.error().code, ErrorCode::InvalidArgument);

        auto res_ninf = TelemetryParser::parse_proc_loadavg("0.88 0.18 -inf 1/100 5");
        KERNOX_ASSERT_TRUE(res_ninf.is_err());
        KERNOX_ASSERT_EQ(res_ninf.error().code, ErrorCode::InvalidArgument);
    }
    return 0;
}

int test_uptime_parsing() {
    // Valid uptime with idle
    {
        const std::string content = "12345.67 98765.43\n";
        auto res = TelemetryParser::parse_proc_uptime(content);
        KERNOX_ASSERT_TRUE(res.is_ok());
        const auto& up = res.value();
        KERNOX_ASSERT_TRUE(double_approx_eq(up.uptime_seconds, 12345.67));
        KERNOX_ASSERT_TRUE(double_approx_eq(up.idle_seconds, 98765.43));
    }
    // Valid uptime single token
    {
        auto res = TelemetryParser::parse_proc_uptime("42.0");
        KERNOX_ASSERT_TRUE(res.is_ok());
        KERNOX_ASSERT_TRUE(double_approx_eq(res.value().uptime_seconds, 42.0));
        KERNOX_ASSERT_TRUE(double_approx_eq(res.value().idle_seconds, 0.0));
    }
    // Negative uptime
    {
        auto res = TelemetryParser::parse_proc_uptime("-10.0 5.0");
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Empty uptime
    {
        auto res = TelemetryParser::parse_proc_uptime("   \n");
        KERNOX_ASSERT_TRUE(res.is_err());
        KERNOX_ASSERT_EQ(res.error().code, ErrorCode::InvalidArgument);
    }
    // Non-finite uptime values: nan, inf, -inf
    {
        auto res_nan = TelemetryParser::parse_proc_uptime("nan 10.0");
        KERNOX_ASSERT_TRUE(res_nan.is_err());
        KERNOX_ASSERT_EQ(res_nan.error().code, ErrorCode::InvalidArgument);

        auto res_inf = TelemetryParser::parse_proc_uptime("inf 10.0");
        KERNOX_ASSERT_TRUE(res_inf.is_err());
        KERNOX_ASSERT_EQ(res_inf.error().code, ErrorCode::InvalidArgument);

        auto res_idle_nan = TelemetryParser::parse_proc_uptime("10.0 nan");
        KERNOX_ASSERT_TRUE(res_idle_nan.is_ok()); // Valid uptime, idle clamped safely to 0.0
        KERNOX_ASSERT_TRUE(double_approx_eq(res_idle_nan.value().idle_seconds, 0.0));
    }
    return 0;
}

// -----------------------------------------------------------------------------
// SystemTelemetry File Reading & Live Host Telemetry
// -----------------------------------------------------------------------------

int test_telemetry_missing_files_error_handling() {
    TelemetryPaths paths{};
    paths.proc_stat = "/tmp/kernox_non_existent_stat_xyz";
    paths.proc_meminfo = "/tmp/kernox_non_existent_meminfo_xyz";
    paths.proc_loadavg = "/tmp/kernox_non_existent_loadavg_xyz";
    paths.proc_uptime = "/tmp/kernox_non_existent_uptime_xyz";

    SystemTelemetry telemetry{paths};

    // Every reader returns IoError gracefully without throwing or crashing
    auto cpu_res = telemetry.read_cpu_stat();
    KERNOX_ASSERT_TRUE(cpu_res.is_err());
    KERNOX_ASSERT_EQ(cpu_res.error().code, ErrorCode::IoError);

    auto mem_res = telemetry.read_memory_info();
    KERNOX_ASSERT_TRUE(mem_res.is_err());
    KERNOX_ASSERT_EQ(mem_res.error().code, ErrorCode::IoError);

    auto load_res = telemetry.read_load_average();
    KERNOX_ASSERT_TRUE(load_res.is_err());
    KERNOX_ASSERT_EQ(load_res.error().code, ErrorCode::IoError);

    auto up_res = telemetry.read_uptime();
    KERNOX_ASSERT_TRUE(up_res.is_err());
    KERNOX_ASSERT_EQ(up_res.error().code, ErrorCode::IoError);

    // Host telemetry collection aggregates errors independently
    auto host = telemetry.collect_telemetry(std::chrono::milliseconds(1));
    KERNOX_ASSERT_TRUE(host.cpu_utilization_percent.is_err());
    KERNOX_ASSERT_TRUE(host.memory.is_err());
    KERNOX_ASSERT_TRUE(host.load_avg.is_err());
    KERNOX_ASSERT_TRUE(host.uptime.is_err());

    return 0;
}

int test_bounded_proc_file_reading() {
    TelemetryPaths paths{};

    // 1. Normal small file
    const std::string small_file = "/tmp/kernox_test_small.txt";
    {
        std::ofstream out(small_file);
        out << "cpu 100 20 50 830\n";
    }
    paths.proc_stat = small_file;
    SystemTelemetry tel_small{paths};
    auto small_res = tel_small.read_cpu_stat();
    KERNOX_ASSERT_TRUE(small_res.is_ok());
    std::remove(small_file.c_str());

    // 2. Empty file
    const std::string empty_file = "/tmp/kernox_test_empty.txt";
    {
        std::ofstream out(empty_file);
    }
    paths.proc_stat = empty_file;
    SystemTelemetry tel_empty{paths};
    auto empty_res = tel_empty.read_cpu_stat();
    KERNOX_ASSERT_TRUE(empty_res.is_err()); // Empty content fails to parse
    std::remove(empty_file.c_str());

    // 3. File at boundary (exactly 1 MiB)
    const std::string boundary_file = "/tmp/kernox_test_1mb.txt";
    constexpr size_t one_mib = 1024 * 1024;
    {
        std::ofstream out(boundary_file, std::ios::binary);
        std::string chunk(4096, ' ');
        for (size_t i = 0; i < one_mib / 4096; ++i) {
            out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }
    paths.proc_stat = boundary_file;
    SystemTelemetry tel_boundary{paths};
    auto boundary_stat = tel_boundary.read_cpu_stat();
    // Rejects because it contains spaces and no cpu line, but did NOT exceed buffer size
    KERNOX_ASSERT_TRUE(boundary_stat.is_err());
    KERNOX_ASSERT_NE(boundary_stat.error().code, ErrorCode::ResourceExhausted);
    std::remove(boundary_file.c_str());

    // 4. File exceeding boundary (> 1 MiB)
    const std::string oversize_file = "/tmp/kernox_test_oversize.txt";
    {
        std::ofstream out(oversize_file, std::ios::binary);
        std::string chunk(4096, 'A');
        for (size_t i = 0; i < (one_mib / 4096) + 2; ++i) {
            out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }
    paths.proc_stat = oversize_file;
    SystemTelemetry tel_oversize{paths};
    auto over_res = tel_oversize.read_cpu_stat();
    KERNOX_ASSERT_TRUE(over_res.is_err());
    KERNOX_ASSERT_EQ(over_res.error().code, ErrorCode::ResourceExhausted);
    std::remove(oversize_file.c_str());

    return 0;
}

int test_telemetry_live_host_collection() {
    SystemTelemetry telemetry{};

    // When running under Linux / WSL2, real /proc files should be present
    auto mem_res = telemetry.read_memory_info();
    KERNOX_ASSERT_TRUE(mem_res.is_ok());
    KERNOX_ASSERT_TRUE(mem_res.value().total_bytes > 0);

    auto load_res = telemetry.read_load_average();
    KERNOX_ASSERT_TRUE(load_res.is_ok());

    auto up_res = telemetry.read_uptime();
    KERNOX_ASSERT_TRUE(up_res.is_ok());
    KERNOX_ASSERT_TRUE(up_res.value().uptime_seconds >= 0.0);

    // Sample CPU utilization with a 20ms delta
    auto cpu_res = telemetry.sample_cpu_utilization(std::chrono::milliseconds(20));
    KERNOX_ASSERT_TRUE(cpu_res.is_ok());
    KERNOX_ASSERT_TRUE(cpu_res.value() >= 0.0 && cpu_res.value() <= 100.0);

    return 0;
}

} // namespace

int main() {
    KERNOX_ASSERT_EQ(test_cpu_stat_parsing_valid(), 0);
    KERNOX_ASSERT_EQ(test_cpu_stat_parsing_minimal(), 0);
    KERNOX_ASSERT_EQ(test_cpu_stat_parsing_tab_delimited(), 0);
    KERNOX_ASSERT_EQ(test_cpu_stat_parsing_invalid(), 0);

    KERNOX_ASSERT_EQ(test_cpu_utilization_calculation(), 0);
    KERNOX_ASSERT_EQ(test_cpu_utilization_rollback_and_zero_delta(), 0);

    KERNOX_ASSERT_EQ(test_memory_parsing_valid(), 0);
    KERNOX_ASSERT_EQ(test_memory_parsing_fallback(), 0);
    KERNOX_ASSERT_EQ(test_memory_parsing_clamping_available(), 0);
    KERNOX_ASSERT_EQ(test_memory_parsing_overflow_hardening(), 0);
    KERNOX_ASSERT_EQ(test_memory_parsing_invalid(), 0);

    KERNOX_ASSERT_EQ(test_load_average_parsing(), 0);
    KERNOX_ASSERT_EQ(test_uptime_parsing(), 0);

    KERNOX_ASSERT_EQ(test_telemetry_missing_files_error_handling(), 0);
    KERNOX_ASSERT_EQ(test_bounded_proc_file_reading(), 0);
    KERNOX_ASSERT_EQ(test_telemetry_live_host_collection(), 0);

    std::cout << "All telemetry module unit and regression tests passed successfully.\n";
    return 0;
}
