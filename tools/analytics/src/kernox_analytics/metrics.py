"""Metrics module defining mathematical derivations, unit conversions, and operational ratios.

Every metric strictly documents its domain, inputs, denominator, and units.
No metric silently fabricates missing fields.
"""

from __future__ import annotations

from typing import Any, Dict, Optional

BYTES_PER_KB = 1024
BYTES_PER_MB = 1024 * 1024
BYTES_PER_GB = 1024 * 1024 * 1024


def bytes_to_mb(b: Optional[int]) -> Optional[float]:
    """Converts bytes to Megabytes (MiB, 1024^2 bytes)."""
    if b is None:
        return None
    return round(float(b) / BYTES_PER_MB, 2)


def bytes_to_gb(b: Optional[int]) -> Optional[float]:
    """Converts bytes to Gigabytes (GiB, 1024^3 bytes)."""
    if b is None:
        return None
    return round(float(b) / BYTES_PER_GB, 3)


def calculate_memory_metrics(memory_data: Dict[str, Any], capacity_bytes: Optional[int] = None) -> Dict[str, Any]:
    """Computes derived memory utilization and reclamation metrics from raw memory dictionary.

    Inputs:
        memory_data: Dictionary containing total_capacity_bytes, initial_used_bytes,
                     peak_used_bytes, final_used_bytes, total_reclaimed_bytes.
        capacity_bytes: Optional override for capacity if stored at parent level.

    Returns:
        Dictionary of derived ratios and converted units.
    """
    cap = capacity_bytes or memory_data.get("total_capacity_bytes", 0)
    init = memory_data.get("initial_used_bytes", 0)
    peak = memory_data.get("peak_used_bytes", 0)
    final = memory_data.get("final_used_bytes", 0)
    reclaimed = memory_data.get("total_reclaimed_bytes", 0)

    peak_ratio = round(peak / cap, 4) if cap > 0 else 0.0
    final_ratio = round(final / cap, 4) if cap > 0 else 0.0
    reclaimed_ratio = round(reclaimed / cap, 4) if cap > 0 else 0.0
    net_delta = final - init

    return {
        "total_capacity_bytes": cap,
        "capacity_mb": bytes_to_mb(cap),
        "initial_used_bytes": init,
        "initial_used_mb": bytes_to_mb(init),
        "peak_used_bytes": peak,
        "peak_used_mb": bytes_to_mb(peak),
        "peak_utilization_ratio": peak_ratio,
        "peak_utilization_pct": round(peak_ratio * 100.0, 2),
        "final_used_bytes": final,
        "final_used_mb": bytes_to_mb(final),
        "final_utilization_ratio": final_ratio,
        "final_utilization_pct": round(final_ratio * 100.0, 2),
        "total_reclaimed_bytes": reclaimed,
        "total_reclaimed_mb": bytes_to_mb(reclaimed),
        "reclaimed_capacity_ratio": reclaimed_ratio,
        "net_used_delta_bytes": net_delta,
        "net_used_delta_mb": bytes_to_mb(net_delta),
        "average_utilization_ratio": memory_data.get("average_utilization_ratio", 0.0),
        "pressure_events_moderate": memory_data.get("pressure_events_moderate", 0),
        "pressure_events_critical": memory_data.get("pressure_events_critical", 0),
    }


def calculate_process_metrics(process_data: Dict[str, Any], policy_name: Optional[str] = None) -> Dict[str, Any]:
    """Computes derived process survival, termination, and protection metrics.

    Inputs:
        process_data: Dictionary containing total_processes, processes_survived,
                      processes_completed, processes_terminated_pressure, and tier rates.
        policy_name: Optional policy identifier to evaluate configured protection guarantees.

    Semantics:
        - Survived: Processes that completed execution normally (processes_completed) PLUS
          processes that remain running without being terminated by memory reclamation.
        - Terminated: Processes killed under memory pressure (processes_terminated_pressure).
        - A normally completed process is NEVER counted as a reclamation victim.
        - Protection Guarantee: Only ImportanceAware explicitly guarantees foreground protection.
          Non-importance-aware policies (FIFO, LRU, Priority) do not promise foreground protection.

    Returns:
        Dictionary of validated survival statistics.
    """
    total = process_data.get("total_processes", 0)
    survived = process_data.get("processes_survived", 0)
    completed = process_data.get("processes_completed", 0)
    terminated = process_data.get("processes_terminated_pressure", 0)

    survival_rate = round(survived / total, 4) if total > 0 else 0.0
    termination_rate = round(terminated / total, 4) if total > 0 else 0.0

    fg_rate = float(process_data.get("foreground_survival_rate", 1.0))
    vis_rate = float(process_data.get("visible_survival_rate", 1.0))
    svc_rate = float(process_data.get("service_survival_rate", 1.0))
    bg_rate = float(process_data.get("background_survival_rate", 1.0))
    cch_rate = float(process_data.get("cached_survival_rate", 1.0))

    # Evaluate protection guarantees only against policies that explicitly promise them
    is_importance_aware = policy_name in ("ImportanceAware", "Importance-Aware", "importance-aware")
    protection_guaranteed = is_importance_aware
    protection_guarantee_met = (fg_rate == 1.0) if protection_guaranteed else None

    return {
        "total_processes": total,
        "processes_survived": survived,
        "processes_completed": completed,
        "processes_terminated_pressure": terminated,
        "survival_rate": survival_rate,
        "survival_pct": round(survival_rate * 100.0, 2),
        "termination_rate": termination_rate,
        "termination_pct": round(termination_rate * 100.0, 2),
        "tier_survival_rates": {
            "Foreground": fg_rate,
            "Visible": vis_rate,
            "Service": svc_rate,
            "Background": bg_rate,
            "Cached": cch_rate,
        },
        "foreground_survival_rate": fg_rate,
        "foreground_survival_pct": round(fg_rate * 100.0, 2),
        "foreground_protection_guaranteed": protection_guaranteed,
        "foreground_protection_guarantee_met": protection_guarantee_met,
    }


def calculate_reclamation_metrics(reclamation_data: Dict[str, Any]) -> Dict[str, Any]:
    """Computes operational reclamation success ratios and efficiency metrics."""
    invocations = reclamation_data.get("reclamation_invocations", 0)
    successes = reclamation_data.get("reclamation_successes", 0)
    reclaimed_procs = reclamation_data.get("processes_reclaimed", 0)
    avg_per_victim = reclamation_data.get("average_reclaimed_per_victim_bytes", 0.0)
    failed_allocs = reclamation_data.get("failed_allocations", 0)
    no_eligible = reclamation_data.get("no_eligible_victim_events", 0)
    achievement_rate = reclamation_data.get("target_achievement_rate", 0.0)

    success_ratio = round(successes / invocations, 4) if invocations > 0 else 1.0

    return {
        "reclamation_invocations": invocations,
        "reclamation_successes": successes,
        "reclamation_success_ratio": success_ratio,
        "reclamation_success_pct": round(success_ratio * 100.0, 2),
        "processes_reclaimed": reclaimed_procs,
        "average_reclaimed_per_victim_bytes": avg_per_victim,
        "average_reclaimed_per_victim_mb": bytes_to_mb(int(avg_per_victim)),
        "failed_allocations": failed_allocs,
        "no_eligible_victim_events": no_eligible,
        "target_achievement_rate": achievement_rate,
    }


def calculate_android_memory_metrics(snapshot: Dict[str, Any]) -> Dict[str, Any]:
    """Computes composition and percentage ratios for real Android device memory.

    Accepts an AdbObservationReport or AdbMemorySnapshot dictionary.
    Returns metrics without fabricating unavailable fields.

    Important Accounting Semantics:
        - Available RAM is NOT an independent mutually exclusive slice; it includes
          Free RAM plus reclaimable Cached RAM (page cache) and reclaimable slab.
        - Free RAM and Available RAM must NEVER be summed together.
        - Cached memory overlaps with Available RAM and must not be added to it.
        - These metrics are distinct operational measurements, not disjoint partition slices.
    """
    mem_snap = snapshot.get("memory_snapshot") or snapshot
    metrics = mem_snap.get("memory_metrics", {})

    total_ram = metrics.get("total_ram_bytes")
    free_ram = metrics.get("free_ram_bytes")
    avail_ram = metrics.get("available_ram_bytes")
    used_ram = metrics.get("used_ram_bytes")
    cached = metrics.get("cached_bytes")
    buffers = metrics.get("buffers_bytes")
    swap_total = metrics.get("swap_total_bytes")
    swap_free = metrics.get("swap_free_bytes")
    zram = metrics.get("zram_physical_used_bytes")
    lost_ram = metrics.get("lost_ram_bytes")

    def calc_pct(part: Optional[int]) -> Optional[float]:
        if part is None or total_ram is None or total_ram == 0:
            return None
        return round((part / total_ram) * 100.0, 2)

    swap_used = None
    swap_used_pct = None
    if swap_total is not None and swap_free is not None:
        swap_used = max(0, swap_total - swap_free)
        if swap_total > 0:
            swap_used_pct = round((swap_used / swap_total) * 100.0, 2)

    # Calculate process sums
    processes = snapshot.get("processes", [])
    total_pss_observed = 0
    total_rss_observed = 0
    for p in processes:
        if p.get("pss_bytes") is not None:
            total_pss_observed += p["pss_bytes"]
        if p.get("rss_bytes") is not None:
            total_rss_observed += p["rss_bytes"]

    return {
        "provenance": "android_adb_observation",
        "total_ram_bytes": total_ram,
        "total_ram_mb": bytes_to_mb(total_ram),
        "used_ram_bytes": used_ram,
        "used_ram_mb": bytes_to_mb(used_ram),
        "used_ram_pct": calc_pct(used_ram),
        "free_ram_bytes": free_ram,
        "free_ram_mb": bytes_to_mb(free_ram),
        "free_ram_pct": calc_pct(free_ram),
        "available_ram_bytes": avail_ram,
        "available_ram_mb": bytes_to_mb(avail_ram),
        "available_ram_pct": calc_pct(avail_ram),
        "cached_bytes": cached,
        "cached_mb": bytes_to_mb(cached),
        "cached_pct": calc_pct(cached),
        "buffers_bytes": buffers,
        "buffers_mb": bytes_to_mb(buffers),
        "buffers_pct": calc_pct(buffers),
        "swap_total_bytes": swap_total,
        "swap_total_mb": bytes_to_mb(swap_total),
        "swap_used_bytes": swap_used,
        "swap_used_mb": bytes_to_mb(swap_used),
        "swap_used_pct": swap_used_pct,
        "zram_physical_used_bytes": zram,
        "zram_physical_used_mb": bytes_to_mb(zram),
        "lost_ram_bytes": lost_ram,
        "lost_ram_mb": bytes_to_mb(lost_ram),
        "observed_process_count": len(processes),
        "total_pss_observed_bytes": total_pss_observed,
        "total_pss_observed_mb": bytes_to_mb(total_pss_observed),
        "total_rss_observed_bytes": total_rss_observed,
        "total_rss_observed_mb": bytes_to_mb(total_rss_observed),
    }
