#pragma once

#include "kernox/core/result.hpp"
#include "kernox/modules/adb/bridge.hpp"
#include "kernox/modules/adb/parser.hpp"
#include "kernox/modules/adb/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kernox::adb {

// High-level service orchestrating safe read-only inspection of connected Android devices.
// Enforces single-device disambiguation, authorization checks, and data provenance.
class AdbObservationService {
public:
    explicit AdbObservationService(std::shared_ptr<AdbBridge> bridge = nullptr);

    // Queries ADB and returns all discoverable devices with metadata.
    // Automatically enriches authorized devices with getprop telemetry.
    core::Result<std::vector<AdbDeviceInfo>> discover_devices();

    // Resolves a target device by serial, or identifies the single active device.
    // STRICT CONTRACT: If multiple devices are connected and no serial was requested,
    // returns ErrorCode::InvalidArgument and NEVER silently selects an arbitrary device.
    core::Result<AdbDeviceInfo> resolve_target_device(
        std::optional<std::string_view> requested_serial = std::nullopt
    );

    // Collects a live memory snapshot from the target device.
    // Attempts 'dumpsys meminfo' for Android-level metrics; gracefully falls back to '/proc/meminfo'.
    core::Result<AdbMemorySnapshot> get_memory_snapshot(
        std::optional<std::string_view> serial = std::nullopt
    );

    // Collects live process observations from the target device.
    // Extracts real Android PIDs, process names, and memory footprints.
    core::Result<std::vector<AdbProcessObservation>> get_process_observations(
        std::optional<std::string_view> serial = std::nullopt
    );

    // Generates a comprehensive observation report containing device information,
    // memory status, process telemetry, and diagnostic audit logs.
    core::Result<AdbObservationReport> generate_snapshot_report(
        std::optional<std::string_view> serial = std::nullopt
    );

    [[nodiscard]] std::shared_ptr<AdbBridge> bridge() const noexcept {
        return bridge_;
    }

private:
    std::shared_ptr<AdbBridge> bridge_;
};

} // namespace kernox::adb
