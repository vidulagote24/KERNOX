"""Unit tests for kernox_analytics.metrics module."""

from kernox_analytics.metrics import (
    bytes_to_gb,
    bytes_to_mb,
    calculate_android_memory_metrics,
    calculate_memory_metrics,
    calculate_process_metrics,
    calculate_reclamation_metrics,
)


def test_unit_conversions():
    assert bytes_to_mb(1048576) == 1.0
    assert bytes_to_mb(1073741824) == 1024.0
    assert bytes_to_gb(1073741824) == 1.0
    assert bytes_to_mb(None) is None
    assert bytes_to_gb(None) is None


def test_calculate_memory_metrics():
    raw_mem = {
        "total_capacity_bytes": 1000,
        "initial_used_bytes": 100,
        "peak_used_bytes": 800,
        "final_used_bytes": 600,
        "total_reclaimed_bytes": 200,
        "average_utilization_ratio": 0.5,
        "pressure_events_moderate": 2,
        "pressure_events_critical": 1,
    }
    m = calculate_memory_metrics(raw_mem)

    assert m["total_capacity_bytes"] == 1000
    assert m["peak_utilization_ratio"] == 0.8
    assert m["peak_utilization_pct"] == 80.0
    assert m["final_utilization_ratio"] == 0.6
    assert m["final_utilization_pct"] == 60.0
    assert m["reclaimed_capacity_ratio"] == 0.2
    assert m["net_used_delta_bytes"] == 500


def test_calculate_process_metrics():
    raw_proc = {
        "total_processes": 10,
        "processes_survived": 7,
        "processes_completed": 7,
        "processes_terminated_pressure": 3,
        "foreground_survival_rate": 1.0,
        "visible_survival_rate": 1.0,
        "service_survival_rate": 1.0,
        "background_survival_rate": 0.5,
        "cached_survival_rate": 0.0,
    }
    # 1. Unspecified policy
    p = calculate_process_metrics(raw_proc)
    assert p["survival_rate"] == 0.7
    assert p["survival_pct"] == 70.0
    assert p["termination_rate"] == 0.3
    assert p["termination_pct"] == 30.0
    assert p["foreground_protection_guaranteed"] is False
    assert p["foreground_protection_guarantee_met"] is None
    assert p["tier_survival_rates"]["Cached"] == 0.0

    # 2. ImportanceAware policy with guaranteed protection
    p_ia = calculate_process_metrics(raw_proc, policy_name="ImportanceAware")
    assert p_ia["foreground_protection_guaranteed"] is True
    assert p_ia["foreground_protection_guarantee_met"] is True

    # 3. FIFO policy without protection guarantee
    raw_proc_fifo = dict(raw_proc, foreground_survival_rate=0.0)
    p_fifo = calculate_process_metrics(raw_proc_fifo, policy_name="FIFO")
    assert p_fifo["foreground_protection_guaranteed"] is False
    assert p_fifo["foreground_protection_guarantee_met"] is None
    assert p_fifo["foreground_survival_rate"] == 0.0


def test_survivor_semantics_completed_vs_terminated():
    # Demonstrates that completed processes are counted as survivors and NOT reclamation victims
    data = {
        "total_processes": 8,
        "processes_completed": 5,           # Finished execution normally
        "processes_survived": 6,            # 5 completed + 1 still active at end
        "processes_terminated_pressure": 2, # Reclaimed due to memory contention
        "foreground_survival_rate": 1.0,
        "visible_survival_rate": 1.0,
        "service_survival_rate": 1.0,
        "background_survival_rate": 0.5,
        "cached_survival_rate": 0.0,
    }
    m = calculate_process_metrics(data)
    assert m["processes_survived"] == 6
    assert m["processes_completed"] == 5
    assert m["processes_terminated_pressure"] == 2
    # Survival rate = 6 / 8 = 0.75
    assert m["survival_rate"] == 0.75
    # Termination rate = 2 / 8 = 0.25
    assert m["termination_rate"] == 0.25
    assert m["survival_rate"] + m["termination_rate"] == 1.0


def test_calculate_reclamation_metrics():
    # With invocations
    raw_rec = {
        "reclamation_invocations": 5,
        "reclamation_successes": 4,
        "processes_reclaimed": 8,
        "average_reclaimed_per_victim_bytes": 1048576.0,
        "failed_allocations": 1,
        "no_eligible_victim_events": 0,
        "target_achievement_rate": 0.8,
    }
    r = calculate_reclamation_metrics(raw_rec)
    assert r["reclamation_success_ratio"] == 0.8
    assert r["reclamation_success_pct"] == 80.0
    assert r["average_reclaimed_per_victim_mb"] == 1.0

    # Zero invocations edge case
    raw_rec_zero = {
        "reclamation_invocations": 0,
        "reclamation_successes": 0,
        "processes_reclaimed": 0,
    }
    r_zero = calculate_reclamation_metrics(raw_rec_zero)
    assert r_zero["reclamation_success_ratio"] == 1.0


def test_calculate_android_memory_metrics():
    snap = {
        "memory_snapshot": {
            "memory_metrics": {
                "total_ram_bytes": 4000,
                "free_ram_bytes": 1000,
                "available_ram_bytes": 2000,
                "used_ram_bytes": 3000,
                "cached_bytes": 1000,
                "buffers_bytes": 200,
                "swap_total_bytes": 2000,
                "swap_free_bytes": 1500,
            }
        },
        "processes": [
            {"pss_bytes": 500, "rss_bytes": 700},
            {"pss_bytes": 300, "rss_bytes": 400},
        ],
    }
    am = calculate_android_memory_metrics(snap)

    assert am["provenance"] == "android_adb_observation"
    assert am["total_ram_bytes"] == 4000
    assert am["used_ram_pct"] == 75.0
    assert am["free_ram_pct"] == 25.0
    assert am["available_ram_pct"] == 50.0
    assert am["swap_used_bytes"] == 500
    assert am["swap_used_pct"] == 25.0
    # Overlap semantics: Available includes Free plus reclaimable Cached
    assert am["available_ram_bytes"] > am["free_ram_bytes"]
    assert am["available_ram_bytes"] == am["free_ram_bytes"] + am["cached_bytes"]
    # Verify Free and Available must never be summed to equal Total
    assert am["free_ram_bytes"] + am["available_ram_bytes"] != am["total_ram_bytes"]
    assert am["total_pss_observed_bytes"] == 800
    assert am["total_rss_observed_bytes"] == 1100
