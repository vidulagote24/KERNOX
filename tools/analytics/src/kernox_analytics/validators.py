"""Validation module for KERNOX 2.0 schemas, units, and data provenance.

Ensures that experiment results and Android observations satisfy strict type,
boundary, and domain isolation requirements prior to analytics processing.
"""

from __future__ import annotations

from typing import Any, Dict, List
import pandas as pd


class ValidationError(ValueError):
    """Raised when an ingested dataset fails structural, type, or boundary constraints."""
    pass


class ProvenanceError(ValidationError):
    """Raised when an unrecognized provenance domain is detected or unlike domains are mixed."""
    pass


VALID_PROVENANCE_DOMAINS = {
    "kernox_simulation",
    "android_adb_observation",
    "linux_proc_observation",
}

REQUIRED_EXPERIMENT_RESULT_KEYS = {
    "policy_name",
    "scenario_name",
    "seed",
    "workload_fingerprint",
    "metrics",
}

REQUIRED_METRICS_SECTIONS = {
    "memory",
    "processes",
    "reclamation",
    "timing",
}

REQUIRED_MEMORY_METRIC_KEYS = {
    "total_capacity_bytes",
    "initial_used_bytes",
    "peak_used_bytes",
    "final_used_bytes",
    "average_utilization_ratio",
    "total_reclaimed_bytes",
    "pressure_events_moderate",
    "pressure_events_critical",
}

REQUIRED_PROCESS_METRIC_KEYS = {
    "total_processes",
    "processes_completed",
    "processes_terminated_pressure",
    "processes_survived",
    "survival_rate",
    "foreground_survival_rate",
    "visible_survival_rate",
    "service_survival_rate",
    "background_survival_rate",
    "cached_survival_rate",
}

REQUIRED_RECLAMATION_METRIC_KEYS = {
    "reclamation_invocations",
    "reclamation_successes",
    "processes_reclaimed",
    "average_reclaimed_per_victim_bytes",
    "failed_allocations",
    "no_eligible_victim_events",
    "target_achievement_rate",
}

REQUIRED_TIMING_METRIC_KEYS = {
    "simulated_duration_ticks",
    "wall_clock_elapsed_microseconds",
}

REQUIRED_COMPARISON_KEYS = {
    "scenario_name",
    "seed",
    "workload_fingerprint",
    "total_capacity_bytes",
    "policy_count",
    "policy_results",
}

REQUIRED_COMPARISON_CSV_COLUMNS = [
    "scenario",
    "seed",
    "fingerprint",
    "capacity_bytes",
    "policy",
    "reclaimed_bytes",
    "processes_reclaimed",
    "processes_survived",
    "total_processes",
    "survival_rate",
    "fg_survival_rate",
    "vis_survival_rate",
    "svc_survival_rate",
    "bg_survival_rate",
    "cached_survival_rate",
    "peak_used_bytes",
    "final_used_bytes",
    "failed_allocations",
    "simulated_ticks",
    "wall_clock_us",
]

REQUIRED_ANDROID_SNAPSHOT_KEYS = {
    "source",
    "collection_timestamp_epoch_ms",
    "device_info",
}


def validate_provenance(source: str, expected_domain: str) -> None:
    """Validates that a dataset matches the expected provenance domain and is known."""
    if source not in VALID_PROVENANCE_DOMAINS:
        raise ProvenanceError(
            f"Unrecognized data provenance domain '{source}'. "
            f"Expected one of: {sorted(list(VALID_PROVENANCE_DOMAINS))}"
        )
    if source != expected_domain:
        raise ProvenanceError(
            f"Provenance mismatch: dataset domain is '{source}', but expected '{expected_domain}'. "
            "Never combine simulation and physical observation datasets."
        )


def validate_experiment_result(data: Dict[str, Any]) -> None:
    """Validates a single policy ExperimentResult JSON object."""
    if not isinstance(data, dict):
        raise ValidationError(f"ExperimentResult must be a JSON object, got {type(data).__name__}")

    missing_keys = REQUIRED_EXPERIMENT_RESULT_KEYS - data.keys()
    if missing_keys:
        raise ValidationError(f"ExperimentResult missing required fields: {sorted(list(missing_keys))}")

    if not isinstance(data["policy_name"], str) or not data["policy_name"].strip():
        raise ValidationError("ExperimentResult 'policy_name' must be a non-empty string")
    if not isinstance(data["scenario_name"], str) or not data["scenario_name"].strip():
        raise ValidationError("ExperimentResult 'scenario_name' must be a non-empty string")
    if not isinstance(data["seed"], int):
        raise ValidationError(f"ExperimentResult 'seed' must be an integer, got {type(data['seed']).__name__}")
    if not isinstance(data["workload_fingerprint"], str):
        raise ValidationError("ExperimentResult 'workload_fingerprint' must be a string")

    # Metrics validation
    metrics = data["metrics"]
    if not isinstance(metrics, dict):
        raise ValidationError(f"'metrics' must be an object, got {type(metrics).__name__}")

    missing_sections = REQUIRED_METRICS_SECTIONS - metrics.keys()
    if missing_sections:
        raise ValidationError(f"'metrics' missing required sections: {sorted(list(missing_sections))}")

    # Memory
    mem = metrics["memory"]
    missing_mem = REQUIRED_MEMORY_METRIC_KEYS - mem.keys()
    if missing_mem:
        raise ValidationError(f"'metrics.memory' missing fields: {sorted(list(missing_mem))}")
    if mem["total_capacity_bytes"] <= 0:
        raise ValidationError("metrics.memory.total_capacity_bytes must be strictly positive")
    if mem["peak_used_bytes"] > mem["total_capacity_bytes"] * 1.5:  # allowance for soft burst before OOM
        raise ValidationError("metrics.memory.peak_used_bytes unreasonably exceeds total capacity")

    # Processes
    proc = metrics["processes"]
    missing_proc = REQUIRED_PROCESS_METRIC_KEYS - proc.keys()
    if missing_proc:
        raise ValidationError(f"'metrics.processes' missing fields: {sorted(list(missing_proc))}")
    if proc["total_processes"] < 0:
        raise ValidationError("metrics.processes.total_processes must be non-negative")
    if not (0.0 <= float(proc["survival_rate"]) <= 1.0):
        raise ValidationError(f"metrics.processes.survival_rate must be in [0.0, 1.0], got {proc['survival_rate']}")
    for tier_rate in ["foreground_survival_rate", "visible_survival_rate", "service_survival_rate",
                      "background_survival_rate", "cached_survival_rate"]:
        val = float(proc[tier_rate])
        if not (0.0 <= val <= 1.0):
            raise ValidationError(f"metrics.processes.{tier_rate} must be in [0.0, 1.0], got {val}")

    # Reclamation
    rec = metrics["reclamation"]
    missing_rec = REQUIRED_RECLAMATION_METRIC_KEYS - rec.keys()
    if missing_rec:
        raise ValidationError(f"'metrics.reclamation' missing fields: {sorted(list(missing_rec))}")
    if rec["reclamation_invocations"] < 0:
        raise ValidationError("metrics.reclamation.reclamation_invocations must be non-negative")
    if rec["reclamation_successes"] > rec["reclamation_invocations"]:
        raise ValidationError("reclamation_successes cannot exceed reclamation_invocations")

    # Timing
    timing = metrics["timing"]
    missing_timing = REQUIRED_TIMING_METRIC_KEYS - timing.keys()
    if missing_timing:
        raise ValidationError(f"'metrics.timing' missing fields: {sorted(list(missing_timing))}")
    if timing["simulated_duration_ticks"] < 0:
        raise ValidationError("metrics.timing.simulated_duration_ticks must be non-negative")
    if timing["wall_clock_elapsed_microseconds"] < 0:
        raise ValidationError("metrics.timing.wall_clock_elapsed_microseconds must be non-negative")


def validate_experiment_comparison(data: Dict[str, Any]) -> None:
    """Validates an ExperimentComparison JSON object (containing multiple policies)."""
    if not isinstance(data, dict):
        raise ValidationError(f"ExperimentComparison must be an object, got {type(data).__name__}")

    missing_keys = REQUIRED_COMPARISON_KEYS - data.keys()
    if missing_keys:
        raise ValidationError(f"ExperimentComparison missing required fields: {sorted(list(missing_keys))}")

    if not isinstance(data["policy_results"], list):
        raise ValidationError(f"'policy_results' must be a list, got {type(data['policy_results']).__name__}")
    if len(data["policy_results"]) != data["policy_count"]:
        raise ValidationError(
            f"Policy count mismatch: 'policy_count' is {data['policy_count']}, "
            f"but 'policy_results' contains {len(data['policy_results'])} items"
        )

    for i, res in enumerate(data["policy_results"]):
        try:
            validate_experiment_result(res)
        except ValidationError as err:
            raise ValidationError(f"Error validating policy_results[{i}] ({res.get('policy_name', 'unknown')}): {err}") from err


def validate_comparison_dataframe(df: pd.DataFrame) -> None:
    """Validates that a pandas DataFrame parsed from an experiment comparison CSV matches the schema."""
    if not isinstance(df, pd.DataFrame):
        raise ValidationError(f"Expected pandas DataFrame, got {type(df).__name__}")

    if df.empty:
        raise ValidationError("Comparison DataFrame cannot be empty")

    missing_cols = set(REQUIRED_COMPARISON_CSV_COLUMNS) - set(df.columns)
    if missing_cols:
        raise ValidationError(f"Comparison CSV missing required columns: {sorted(list(missing_cols))}")

    # Validate numeric columns
    numeric_cols = [
        "seed", "capacity_bytes", "reclaimed_bytes", "processes_reclaimed",
        "processes_survived", "total_processes", "survival_rate",
        "fg_survival_rate", "vis_survival_rate", "svc_survival_rate",
        "bg_survival_rate", "cached_survival_rate", "peak_used_bytes",
        "final_used_bytes", "failed_allocations", "simulated_ticks",
        "wall_clock_us"
    ]
    for col in numeric_cols:
        if not pd.api.types.is_numeric_dtype(df[col]):
            raise ValidationError(f"Column '{col}' must be numeric, got {df[col].dtype}")


def validate_android_snapshot(data: Dict[str, Any]) -> None:
    """Validates an Android ADB observation snapshot JSON object."""
    if not isinstance(data, dict):
        raise ValidationError(f"Android snapshot must be an object, got {type(data).__name__}")

    missing_keys = REQUIRED_ANDROID_SNAPSHOT_KEYS - data.keys()
    if missing_keys:
        raise ValidationError(f"Android snapshot missing required fields: {sorted(list(missing_keys))}")

    source = data.get("source", "")
    validate_provenance(source, "android_adb_observation")

    if not isinstance(data["collection_timestamp_epoch_ms"], int) or data["collection_timestamp_epoch_ms"] <= 0:
        raise ValidationError("collection_timestamp_epoch_ms must be a positive integer timestamp")

    device_info = data["device_info"]
    if not isinstance(device_info, dict):
        raise ValidationError(f"'device_info' must be an object, got {type(device_info).__name__}")
    if "serial" not in device_info or not isinstance(device_info["serial"], str) or not device_info["serial"].strip():
        raise ValidationError("device_info must contain a valid non-empty 'serial'")

    # Memory snapshot (optional if observation failed)
    mem_snap = data.get("memory_snapshot")
    if mem_snap is not None:
        if not isinstance(mem_snap, dict):
            raise ValidationError(f"'memory_snapshot' must be an object or null, got {type(mem_snap).__name__}")
        metrics = mem_snap.get("memory_metrics", {})
        if not isinstance(metrics, dict):
            raise ValidationError("'memory_snapshot.memory_metrics' must be an object")

    # Processes
    procs = data.get("processes", [])
    if not isinstance(procs, list):
        raise ValidationError(f"'processes' must be a list, got {type(procs).__name__}")

    for i, p in enumerate(procs):
        if not isinstance(p, dict):
            raise ValidationError(f"processes[{i}] must be an object")
        if "pid" not in p or not isinstance(p["pid"], int):
            raise ValidationError(f"processes[{i}] missing integer 'pid'")
        if "process_name" not in p or not isinstance(p["process_name"], str):
            raise ValidationError(f"processes[{i}] missing string 'process_name'")
        p_src = p.get("source", "android_adb_observation")
        validate_provenance(p_src, "android_adb_observation")
