"""Unit tests for kernox_analytics.validators module."""

import pandas as pd
import pytest

from kernox_analytics.validators import (
    ProvenanceError,
    ValidationError,
    validate_android_snapshot,
    validate_comparison_dataframe,
    validate_experiment_comparison,
    validate_experiment_result,
    validate_provenance,
)


@pytest.fixture
def valid_experiment_result():
    return {
        "policy_name": "FIFO",
        "scenario_name": "Normal",
        "seed": 42,
        "workload_fingerprint": "WKL-12345",
        "metrics": {
            "memory": {
                "total_capacity_bytes": 1073741824,
                "initial_used_bytes": 0,
                "peak_used_bytes": 500000000,
                "final_used_bytes": 500000000,
                "average_utilization_ratio": 0.46,
                "total_reclaimed_bytes": 0,
                "pressure_events_moderate": 0,
                "pressure_events_critical": 0,
            },
            "processes": {
                "total_processes": 10,
                "processes_completed": 10,
                "processes_terminated_pressure": 0,
                "processes_survived": 10,
                "survival_rate": 1.0,
                "foreground_survival_rate": 1.0,
                "visible_survival_rate": 1.0,
                "service_survival_rate": 1.0,
                "background_survival_rate": 1.0,
                "cached_survival_rate": 1.0,
            },
            "reclamation": {
                "reclamation_invocations": 0,
                "reclamation_successes": 0,
                "processes_reclaimed": 0,
                "average_reclaimed_per_victim_bytes": 0.0,
                "failed_allocations": 0,
                "no_eligible_victim_events": 0,
                "target_achievement_rate": 1.0,
            },
            "timing": {
                "simulated_duration_ticks": 100,
                "wall_clock_elapsed_microseconds": 1500,
            },
        },
    }


def test_valid_experiment_result_passes(valid_experiment_result):
    validate_experiment_result(valid_experiment_result)


def test_missing_required_key(valid_experiment_result):
    del valid_experiment_result["policy_name"]
    with pytest.raises(ValidationError, match="missing required fields"):
        validate_experiment_result(valid_experiment_result)


def test_invalid_seed_type(valid_experiment_result):
    valid_experiment_result["seed"] = "not-an-integer"
    with pytest.raises(ValidationError, match="must be an integer"):
        validate_experiment_result(valid_experiment_result)


def test_missing_metrics_section(valid_experiment_result):
    del valid_experiment_result["metrics"]["timing"]
    with pytest.raises(ValidationError, match="missing required sections"):
        validate_experiment_result(valid_experiment_result)


def test_survival_rate_out_of_bounds(valid_experiment_result):
    valid_experiment_result["metrics"]["processes"]["survival_rate"] = 1.5
    with pytest.raises(ValidationError, match="survival_rate must be in"):
        validate_experiment_result(valid_experiment_result)


def test_reclamation_successes_exceed_invocations(valid_experiment_result):
    valid_experiment_result["metrics"]["reclamation"]["reclamation_invocations"] = 2
    valid_experiment_result["metrics"]["reclamation"]["reclamation_successes"] = 5
    with pytest.raises(ValidationError, match="cannot exceed reclamation_invocations"):
        validate_experiment_result(valid_experiment_result)


def test_validate_provenance_success_and_failures():
    validate_provenance("kernox_simulation", "kernox_simulation")
    validate_provenance("android_adb_observation", "android_adb_observation")

    with pytest.raises(ProvenanceError, match="Unrecognized data provenance"):
        validate_provenance("unknown_domain", "unknown_domain")

    with pytest.raises(ProvenanceError, match="Provenance mismatch"):
        validate_provenance("android_adb_observation", "kernox_simulation")


def test_validate_experiment_comparison():
    comp = {
        "scenario_name": "Normal",
        "seed": 42,
        "workload_fingerprint": "WKL-1234",
        "total_capacity_bytes": 1073741824,
        "policy_count": 1,
        "policy_results": [
            {
                "policy_name": "FIFO",
                "scenario_name": "Normal",
                "seed": 42,
                "workload_fingerprint": "WKL-1234",
                "metrics": {
                    "memory": {
                        "total_capacity_bytes": 1073741824,
                        "initial_used_bytes": 0,
                        "peak_used_bytes": 100,
                        "final_used_bytes": 100,
                        "average_utilization_ratio": 0.1,
                        "total_reclaimed_bytes": 0,
                        "pressure_events_moderate": 0,
                        "pressure_events_critical": 0,
                    },
                    "processes": {
                        "total_processes": 1,
                        "processes_completed": 1,
                        "processes_terminated_pressure": 0,
                        "processes_survived": 1,
                        "survival_rate": 1.0,
                        "foreground_survival_rate": 1.0,
                        "visible_survival_rate": 1.0,
                        "service_survival_rate": 1.0,
                        "background_survival_rate": 1.0,
                        "cached_survival_rate": 1.0,
                    },
                    "reclamation": {
                        "reclamation_invocations": 0,
                        "reclamation_successes": 0,
                        "processes_reclaimed": 0,
                        "average_reclaimed_per_victim_bytes": 0.0,
                        "failed_allocations": 0,
                        "no_eligible_victim_events": 0,
                        "target_achievement_rate": 1.0,
                    },
                    "timing": {
                        "simulated_duration_ticks": 10,
                        "wall_clock_elapsed_microseconds": 100,
                    },
                },
            }
        ],
    }
    validate_experiment_comparison(comp)

    comp["policy_count"] = 2
    with pytest.raises(ValidationError, match="Policy count mismatch"):
        validate_experiment_comparison(comp)


def test_validate_comparison_dataframe():
    df = pd.DataFrame([{
        "scenario": "Normal",
        "seed": 42,
        "fingerprint": "WKL-1",
        "capacity_bytes": 1073741824,
        "policy": "FIFO",
        "reclaimed_bytes": 0,
        "processes_reclaimed": 0,
        "processes_survived": 10,
        "total_processes": 10,
        "survival_rate": 1.0,
        "fg_survival_rate": 1.0,
        "vis_survival_rate": 1.0,
        "svc_survival_rate": 1.0,
        "bg_survival_rate": 1.0,
        "cached_survival_rate": 1.0,
        "peak_used_bytes": 500,
        "final_used_bytes": 500,
        "failed_allocations": 0,
        "simulated_ticks": 100,
        "wall_clock_us": 1000,
    }])
    validate_comparison_dataframe(df)

    # Empty df
    with pytest.raises(ValidationError, match="cannot be empty"):
        validate_comparison_dataframe(pd.DataFrame())

    # Missing column
    df_missing = df.drop(columns=["policy"])
    with pytest.raises(ValidationError, match="missing required columns"):
        validate_comparison_dataframe(df_missing)


def test_validate_android_snapshot():
    snap = {
        "source": "android_adb_observation",
        "collection_timestamp_epoch_ms": 1731672000000,
        "device_info": {
            "serial": "emulator-5554",
            "state": "device",
        },
        "processes": [
            {"pid": 100, "process_name": "system_server", "source": "android_adb_observation"}
        ],
    }
    validate_android_snapshot(snap)

    snap_wrong_src = dict(snap, source="kernox_simulation")
    with pytest.raises(ProvenanceError):
        validate_android_snapshot(snap_wrong_src)
