#include "kernox/modules/adb/service.hpp"
#include "kernox/core/timer.hpp"

#include <chrono>

namespace kernox::adb {

namespace {

uint64_t current_time_ms() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

} // namespace

AdbObservationService::AdbObservationService(std::shared_ptr<AdbBridge> bridge)
    : bridge_(bridge ? std::move(bridge) : std::make_shared<AdbBridge>()) {}

core::Result<std::vector<AdbDeviceInfo>> AdbObservationService::discover_devices() {
    if (!bridge_->is_available()) {
        return core::Result<std::vector<AdbDeviceInfo>>::err(
            core::ErrorCode::NotFound,
            "Android Debug Bridge ('" + bridge_->adb_path() + "') is not available in PATH.\n"
            "To enable Android observation, please install Android Platform Tools:\n"
            "  Ubuntu / Debian : sudo apt install adb android-tools-adb\n"
            "  Arch Linux      : sudo pacman -S android-tools\n"
            "  Fedora          : sudo dnf install android-tools"
        );
    }

    auto out_res = bridge_->list_devices();
    if (!out_res.is_ok()) {
        return core::Result<std::vector<AdbDeviceInfo>>::err(out_res.error());
    }

    const auto& out = out_res.value();
    if (!out.is_success()) {
        return core::Result<std::vector<AdbDeviceInfo>>::err(
            core::ErrorCode::ProcessFailed,
            "Failed listing ADB devices: " + (out.stderr_content.empty() ? out.stdout_content : out.stderr_content)
        );
    }

    auto parse_res = AdbParser::parse_devices(out.stdout_content);
    if (!parse_res.is_ok()) {
        return parse_res;
    }

    auto devices = parse_res.value();

    // Enrich authorized online devices with getprop information
    for (auto& dev : devices) {
        if (dev.is_usable()) {
            auto mfg = bridge_->getprop(dev.serial, "ro.product.manufacturer");
            if (mfg.is_ok() && mfg.value().is_success()) {
                dev.manufacturer = AdbParser::parse_getprop(mfg.value().stdout_content);
            }

            auto mdl = bridge_->getprop(dev.serial, "ro.product.model");
            if (mdl.is_ok() && mdl.value().is_success()) {
                dev.model = AdbParser::parse_getprop(mdl.value().stdout_content);
            }

            auto rel = bridge_->getprop(dev.serial, "ro.build.version.release");
            if (rel.is_ok() && rel.value().is_success()) {
                dev.android_release = AdbParser::parse_getprop(rel.value().stdout_content);
            }

            auto sdk = bridge_->getprop(dev.serial, "ro.build.version.sdk");
            if (sdk.is_ok() && sdk.value().is_success()) {
                std::string sdk_str = AdbParser::parse_getprop(sdk.value().stdout_content);
                try {
                    dev.sdk_version = static_cast<uint32_t>(std::stoul(sdk_str));
                } catch (...) {}
            }
        }
    }

    return core::Result<std::vector<AdbDeviceInfo>>::ok(std::move(devices));
}

core::Result<AdbDeviceInfo> AdbObservationService::resolve_target_device(
    std::optional<std::string_view> requested_serial
) {
    auto disc_res = discover_devices();
    if (!disc_res.is_ok()) {
        return core::Result<AdbDeviceInfo>::err(disc_res.error());
    }

    const auto& devices = disc_res.value();

    if (requested_serial.has_value() && !requested_serial->empty()) {
        for (const auto& d : devices) {
            if (d.serial == *requested_serial) {
                if (d.state == AdbConnectionState::Unauthorized) {
                    return core::Result<AdbDeviceInfo>::err(
                        core::ErrorCode::PermissionDenied,
                        "Device '" + std::string(*requested_serial) +
                        "' is UNAUTHORIZED. Please unlock device screen and accept USB debugging prompt."
                    );
                }
                if (d.state == AdbConnectionState::Offline) {
                    return core::Result<AdbDeviceInfo>::err(
                        core::ErrorCode::InvalidState,
                        "Device '" + std::string(*requested_serial) + "' is OFFLINE."
                    );
                }
                if (!d.is_usable()) {
                    return core::Result<AdbDeviceInfo>::err(
                        core::ErrorCode::InvalidState,
                        "Device '" + std::string(*requested_serial) +
                        "' is not online (State: " + std::string(to_string(d.state)) + ")."
                    );
                }
                return core::Result<AdbDeviceInfo>::ok(d);
            }
        }
        return core::Result<AdbDeviceInfo>::err(
            core::ErrorCode::NotFound,
            "Target Android device with serial '" + std::string(*requested_serial) + "' was not found."
        );
    }

    // No explicit serial specified
    if (devices.empty()) {
        return core::Result<AdbDeviceInfo>::err(
            core::ErrorCode::NotFound,
            "No connected Android devices detected via ADB.\n"
            "Please attach an Android device via USB/Wi-Fi and ensure Developer Options -> USB Debugging is ON."
        );
    }

    if (devices.size() > 1) {
        std::string err_msg = "Multiple Android devices connected (" + std::to_string(devices.size()) + "):\n";
        for (const auto& d : devices) {
            err_msg += "  - " + d.serial + " [" + std::string(to_string(d.state)) + "]\n";
        }
        err_msg += "Ambiguity Safety Policy: You must explicitly select target device using '--serial <serial>'.";
        return core::Result<AdbDeviceInfo>::err(core::ErrorCode::InvalidArgument, err_msg);
    }

    // Exactly one device connected
    const auto& single_dev = devices[0];
    if (single_dev.state == AdbConnectionState::Unauthorized) {
        return core::Result<AdbDeviceInfo>::err(
            core::ErrorCode::PermissionDenied,
            "Device '" + single_dev.serial +
            "' is UNAUTHORIZED. Unlock the device screen and tap 'Allow USB debugging'."
        );
    }
    if (single_dev.state == AdbConnectionState::Offline) {
        return core::Result<AdbDeviceInfo>::err(
            core::ErrorCode::InvalidState,
            "Device '" + single_dev.serial + "' is OFFLINE. Reconnect cable or restart adb server."
        );
    }
    if (!single_dev.is_usable()) {
        return core::Result<AdbDeviceInfo>::err(
            core::ErrorCode::InvalidState,
            "Device '" + single_dev.serial + "' is in unusable state: " + std::string(to_string(single_dev.state))
        );
    }

    return core::Result<AdbDeviceInfo>::ok(single_dev);
}

core::Result<AdbMemorySnapshot> AdbObservationService::get_memory_snapshot(
    std::optional<std::string_view> serial
) {
    auto dev_res = resolve_target_device(serial);
    if (!dev_res.is_ok()) {
        return core::Result<AdbMemorySnapshot>::err(dev_res.error());
    }
    const auto& dev = dev_res.value();

    // 1. Attempt dumpsys meminfo first (provides Android-specific RAM breakdown)
    auto dumpsys_res = bridge_->dumpsys_meminfo(dev.serial);
    if (dumpsys_res.is_ok() && dumpsys_res.value().is_success() &&
        dumpsys_res.value().stdout_content.find("Total RAM:") != std::string::npos) {
        auto snap_res = AdbParser::parse_dumpsys_meminfo_summary(dumpsys_res.value().stdout_content);
        if (snap_res.is_ok()) {
            return snap_res;
        }
    }

    // 2. Fallback to /proc/meminfo
    auto proc_res = bridge_->cat_proc_meminfo(dev.serial);
    if (proc_res.is_ok() && proc_res.value().is_success() &&
        proc_res.value().stdout_content.find("MemTotal:") != std::string::npos) {
        return AdbParser::parse_proc_meminfo(proc_res.value().stdout_content);
    }

    return core::Result<AdbMemorySnapshot>::err(
        core::ErrorCode::IoError,
        "Failed reading memory telemetry from device '" + dev.serial + "' via dumpsys or /proc/meminfo."
    );
}

core::Result<std::vector<AdbProcessObservation>> AdbObservationService::get_process_observations(
    std::optional<std::string_view> serial
) {
    auto dev_res = resolve_target_device(serial);
    if (!dev_res.is_ok()) {
        return core::Result<std::vector<AdbProcessObservation>>::err(dev_res.error());
    }
    const auto& dev = dev_res.value();

    // Try dumpsys meminfo process listing first
    auto dumpsys_res = bridge_->dumpsys_meminfo(dev.serial);
    if (dumpsys_res.is_ok() && dumpsys_res.value().is_success()) {
        auto procs_res = AdbParser::parse_dumpsys_meminfo_processes(dumpsys_res.value().stdout_content);
        if (procs_res.is_ok() && !procs_res.value().empty()) {
            return procs_res;
        }
    }

    // Fallback: dumpsys activity processes
    auto act_res = bridge_->dumpsys_activity_processes(dev.serial);
    if (act_res.is_ok() && act_res.value().is_success()) {
        auto act_procs = AdbParser::parse_dumpsys_activity_processes(act_res.value().stdout_content);
        if (act_procs.is_ok() && !act_procs.value().empty()) {
            return act_procs;
        }
    }

    return core::Result<std::vector<AdbProcessObservation>>::err(
        core::ErrorCode::IoError,
        "Failed reading process observations from device '" + dev.serial + "'."
    );
}

core::Result<AdbObservationReport> AdbObservationService::generate_snapshot_report(
    std::optional<std::string_view> serial
) {
    core::Timer timer;

    auto dev_res = resolve_target_device(serial);
    if (!dev_res.is_ok()) {
        return core::Result<AdbObservationReport>::err(dev_res.error());
    }

    AdbObservationReport report{};
    report.device_info = dev_res.value();
    report.collection_timestamp_epoch_ms = current_time_ms();

    // 1. Collect Memory
    auto mem_res = get_memory_snapshot(report.device_info.serial);
    if (mem_res.is_ok()) {
        report.memory_snapshot = mem_res.value();
    } else {
        report.failed_commands.push_back({"dumpsys/proc_meminfo", mem_res.error().message});
    }

    // 2. Collect Processes
    auto procs_res = get_process_observations(report.device_info.serial);
    if (procs_res.is_ok()) {
        report.processes = procs_res.value();
    } else {
        report.failed_commands.push_back({"dumpsys_processes", procs_res.error().message});
    }

    report.collection_duration_us = static_cast<uint64_t>(timer.elapsed_microseconds());
    return core::Result<AdbObservationReport>::ok(std::move(report));
}

} // namespace kernox::adb
