#pragma once

#include "kernox/core/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kernox::adb {

// Dedicated strong type for real Android OS process IDs.
// STRICT ARCHITECTURAL INVARIANT:
// Real Android PIDs belong exclusively to the ADB observation subsystem.
// They must NEVER be converted to or confused with kernox::intel::SimulatedPid.
using AndroidPid = uint32_t;

// Standard Android ADB connection states reported by 'adb devices -l'.
enum class AdbConnectionState : uint32_t {
    Device = 0,     // Online and fully authorized
    Unauthorized,   // Connected via USB/TCP but RSA key not accepted on device screen
    Offline,        // Connected but communication failed or kernel suspended
    Authorizing,    // Handshake in progress
    Recovery,       // Device booted into Android Recovery
    Sideload,       // Device in sideload mode
    Bootloader,     // Fastboot / bootloader mode
    NoDevice,       // Explicit indicator: no devices present
    Unknown         // Unparseable state
};

[[nodiscard]] std::string_view to_string(AdbConnectionState state) noexcept;
[[nodiscard]] std::optional<AdbConnectionState> parse_connection_state(std::string_view str) noexcept;

// Confidence classification for parsed process or system observations.
enum class ObservationConfidence : uint32_t {
    High = 0,       // Parsed directly from structured, validated fields
    Moderate,       // Extracted with validated heuristics
    Low,            // Ambiguous field formatting
    RawOnly         // Preserved raw string without normalized interpretation
};

[[nodiscard]] std::string_view to_string(ObservationConfidence confidence) noexcept;

// Structural metadata describing an inspected Android device.
struct AdbDeviceInfo {
    std::string serial{};
    AdbConnectionState state{AdbConnectionState::Unknown};
    std::string product{};
    std::string model{};
    std::string device{};
    std::string transport_id{};
    std::string manufacturer{};
    std::string android_release{};
    std::optional<uint32_t> sdk_version{std::nullopt};

    [[nodiscard]] bool is_usable() const noexcept {
        return state == AdbConnectionState::Device;
    }

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_json() const;
};

// Snapshot of real device memory telemetry collected via read-only inspection.
// Note: All measurements are uniformly represented in bytes internally.
// Fields not exposed or accessible on specific Android builds are explicitly std::nullopt.
struct AdbMemorySnapshot {
    static constexpr std::string_view DATA_PROVENANCE = "android_adb_observation";

    std::string source{DATA_PROVENANCE};
    uint64_t timestamp_epoch_ms{0};
    std::string command_executed{};

    // Global RAM Metrics (bytes)
    std::optional<uint64_t> total_ram_bytes{std::nullopt};
    std::optional<uint64_t> free_ram_bytes{std::nullopt};
    std::optional<uint64_t> available_ram_bytes{std::nullopt}; // Not present on Linux < 3.14
    std::optional<uint64_t> used_ram_bytes{std::nullopt};
    std::optional<uint64_t> cached_bytes{std::nullopt};
    std::optional<uint64_t> buffers_bytes{std::nullopt};

    // Swap and Compressed Memory (bytes)
    std::optional<uint64_t> swap_total_bytes{std::nullopt};
    std::optional<uint64_t> swap_free_bytes{std::nullopt};
    std::optional<uint64_t> zram_physical_used_bytes{std::nullopt};

    // Android Dumpsys-specific Metrics (bytes)
    std::optional<uint64_t> lost_ram_bytes{std::nullopt};
    std::optional<uint64_t> kernel_ram_bytes{std::nullopt};
    std::optional<uint64_t> native_heap_bytes{std::nullopt};
    std::optional<uint64_t> dalvik_heap_bytes{std::nullopt};

    // Diagnostics & Raw Text
    std::vector<std::string> warnings{};
    std::string raw_output{};

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_json() const;
};

// Read-only observation of an active real Android process.
struct AdbProcessObservation {
    static constexpr std::string_view DATA_PROVENANCE = "android_adb_observation";

    std::string source{DATA_PROVENANCE};
    AndroidPid pid{0};
    std::string process_name{};
    std::string package_name{};
    std::optional<uint32_t> user_id{std::nullopt};

    // Process Memory Metrics (bytes)
    std::optional<uint64_t> pss_bytes{std::nullopt};           // Proportional Set Size
    std::optional<uint64_t> rss_bytes{std::nullopt};           // Resident Set Size
    std::optional<uint64_t> private_dirty_bytes{std::nullopt}; // App-exclusive dirty pages
    std::optional<uint64_t> swap_pss_bytes{std::nullopt};      // Swap proportional share

    // Process Lifecycle & Importance
    std::string raw_importance_or_state{};                     // Exact token from dumpsys
    std::optional<std::string> normalized_importance{std::nullopt}; // e.g. "Foreground", "Cached"
    std::optional<int32_t> oom_score_adj{std::nullopt};        // Linux oom_score_adj (-1000..1000)

    ObservationConfidence confidence{ObservationConfidence::High};

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_json() const;
};

// Consolidated inspection report combining device metadata, memory, processes, and diagnostics.
struct AdbObservationReport {
    static constexpr std::string_view DATA_PROVENANCE = "android_adb_observation";

    std::string source{DATA_PROVENANCE};
    uint64_t collection_timestamp_epoch_ms{0};
    AdbDeviceInfo device_info{};
    std::optional<AdbMemorySnapshot> memory_snapshot{std::nullopt};
    std::vector<AdbProcessObservation> processes{};

    // Diagnostic & Safety Audit Section
    uint64_t collection_duration_us{0};
    std::vector<std::pair<std::string, std::string>> failed_commands{};
    std::vector<std::string> warnings{};
    std::vector<std::string> permission_restrictions{};

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_json() const;
};

} // namespace kernox::adb
