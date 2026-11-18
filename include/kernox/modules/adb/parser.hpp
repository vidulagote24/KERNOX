#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/adb/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kernox::adb {

// Robust, pure parsing routines for Android Debug Bridge inspection outputs.
// Completely testable offline using static fixtures without hardware.
class AdbParser {
public:
    // Parses the output of 'adb devices -l' or 'adb devices'.
    // Handles 0, 1, multiple, unauthorized, and offline devices.
    // Tolerates adb daemon startup logs and arbitrary whitespace.
    [[nodiscard]] static core::Result<std::vector<AdbDeviceInfo>> parse_devices(std::string_view output);

    // Cleans and normalizes getprop output (trims CR, LF, whitespace).
    [[nodiscard]] static std::string parse_getprop(std::string_view output);

    // Parses Linux /proc/meminfo output into structured AdbMemorySnapshot.
    // Tolerates missing fields (e.g. MemAvailable on old kernels), irregular spacing,
    // and converts all quantities (kB, mB, etc.) to uniform bytes.
    [[nodiscard]] static core::Result<AdbMemorySnapshot> parse_proc_meminfo(std::string_view content);

    // Parses the summary RAM block from 'dumpsys meminfo'.
    // Handles formatted numbers with commas (e.g. "7,856,128K"), Free RAM breakdowns,
    // Used RAM, Lost RAM, and ZRAM physical usage.
    [[nodiscard]] static core::Result<AdbMemorySnapshot> parse_dumpsys_meminfo_summary(std::string_view content);

    // Parses process PSS listings and OOM classifications from 'dumpsys meminfo'.
    // Extracts real Android PID, process name, PSS bytes, and category.
    [[nodiscard]] static core::Result<std::vector<AdbProcessObservation>> parse_dumpsys_meminfo_processes(
        std::string_view content
    );

    // Parses 'dumpsys activity processes' output for process records,
    // extracting real Android PID, process name, user ID, and oom_score_adj.
    [[nodiscard]] static core::Result<std::vector<AdbProcessObservation>> parse_dumpsys_activity_processes(
        std::string_view content
    );

    // Helper: Parses formatted byte quantities with units (e.g., "1024 kB", "7,856,128K", "512M")
    // into integer byte counts with overflow detection.
    [[nodiscard]] static core::Result<uint64_t> parse_formatted_bytes(std::string_view raw_value);
};

} // namespace kernox::adb
