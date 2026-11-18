#include "kernox/modules/reclaim/pressure_detector.hpp"

#include <iomanip>
#include <sstream>

namespace kernox::reclaim {

std::string_view to_string(MemoryPressureLevel level) noexcept {
    switch (level) {
        case MemoryPressureLevel::Normal:
            return "Normal";
        case MemoryPressureLevel::Moderate:
            return "Moderate";
        case MemoryPressureLevel::Critical:
            return "Critical";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& os, MemoryPressureLevel level) {
    return os << to_string(level);
}

core::Result<void> PressureThresholds::validate() const {
    if (total_memory_bytes == 0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Total memory capacity must be greater than zero bytes"
        );
    }

    if (moderate_threshold_ratio <= 0.0 || moderate_threshold_ratio >= 1.0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Moderate threshold ratio must be strictly between 0.0 and 1.0"
        );
    }

    if (critical_threshold_ratio <= 0.0 || critical_threshold_ratio > 1.0) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Critical threshold ratio must be in range (0.0, 1.0]"
        );
    }

    if (moderate_threshold_ratio >= critical_threshold_ratio) {
        return core::Result<void>::err(
            core::ErrorCode::InvalidArgument,
            "Moderate threshold ratio (" + std::to_string(moderate_threshold_ratio) +
            ") must be strictly less than critical threshold ratio (" +
            std::to_string(critical_threshold_ratio) + ")"
        );
    }

    return core::Result<void>::ok();
}

core::Result<MemoryPressureDetector> MemoryPressureDetector::create(PressureThresholds thresholds) {
    auto val_res = thresholds.validate();
    if (val_res.is_err()) {
        return core::Result<MemoryPressureDetector>::err(val_res.error());
    }
    return core::Result<MemoryPressureDetector>::ok(MemoryPressureDetector(thresholds));
}

core::Result<MemoryPressureDetector> MemoryPressureDetector::create(
    uint64_t total_memory_bytes,
    double moderate_ratio,
    double critical_ratio
) {
    PressureThresholds t{total_memory_bytes, moderate_ratio, critical_ratio};
    return create(t);
}

core::Result<void> MemoryPressureDetector::update_thresholds(PressureThresholds new_thresholds) {
    auto val_res = new_thresholds.validate();
    if (val_res.is_err()) {
        return val_res;
    }
    thresholds_ = new_thresholds;
    return core::Result<void>::ok();
}

MemoryPressureLevel MemoryPressureDetector::detect(uint64_t used_bytes) const noexcept {
    if (used_bytes >= thresholds_.critical_limit_bytes()) {
        return MemoryPressureLevel::Critical;
    }
    if (used_bytes >= thresholds_.moderate_limit_bytes()) {
        return MemoryPressureLevel::Moderate;
    }
    return MemoryPressureLevel::Normal;
}

double MemoryPressureDetector::utilization_ratio(uint64_t used_bytes) const noexcept {
    if (thresholds_.total_memory_bytes == 0) {
        return 0.0;
    }
    return static_cast<double>(used_bytes) / static_cast<double>(thresholds_.total_memory_bytes);
}

uint64_t MemoryPressureDetector::bytes_to_reach_normal(uint64_t used_bytes) const noexcept {
    uint64_t mod_limit = thresholds_.moderate_limit_bytes();
    if (used_bytes < mod_limit) {
        return 0;
    }
    return used_bytes - mod_limit + 1;
}

uint64_t MemoryPressureDetector::bytes_to_reach_moderate(uint64_t used_bytes) const noexcept {
    uint64_t crit_limit = thresholds_.critical_limit_bytes();
    if (used_bytes < crit_limit) {
        return 0;
    }
    return used_bytes - crit_limit + 1;
}

std::string MemoryPressureDetector::describe_pressure(uint64_t used_bytes) const {
    std::ostringstream oss;
    double util = utilization_ratio(used_bytes) * 100.0;
    MemoryPressureLevel level = detect(used_bytes);

    oss << "MemoryPressure[" << to_string(level) << "]: "
        << used_bytes << " / " << thresholds_.total_memory_bytes << " bytes ("
        << std::fixed << std::setprecision(1) << util << "%) "
        << "[Moderate Limit: " << thresholds_.moderate_limit_bytes() << " B, "
        << "Critical Limit: " << thresholds_.critical_limit_bytes() << " B]";
    return oss.str();
}

} // namespace kernox::reclaim
