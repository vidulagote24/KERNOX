"""Data ingestion module for KERNOX 2.0 experiment and telemetry artifacts.

Provides unified file loading, automatic format detection, schema validation,
and provenance tagging for JSON and CSV exports.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass, field
from enum import Enum
import json
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

import pandas as pd

from .validators import (
    ProvenanceError,
    ValidationError,
    validate_android_snapshot,
    validate_comparison_dataframe,
    validate_experiment_comparison,
    validate_experiment_result,
)


class DataDomain(str, Enum):
    """Categorical data provenance domains ensuring strict separation of unlike sources."""
    SIMULATION = "kernox_simulation"
    ANDROID = "android_adb_observation"
    LINUX = "linux_proc_observation"


@dataclass
class Dataset:
    """Encapsulates a loaded KERNOX dataset with validated provenance and schema metadata."""
    domain: DataDomain
    source_path: Path
    data_type: str  # "single_result", "comparison", "comparison_table", "decisions_log", "android_snapshot"
    raw_data: Union[Dict[str, Any], pd.DataFrame]
    metadata: Dict[str, Any] = field(default_factory=dict)

    def is_simulation(self) -> bool:
        return self.domain == DataDomain.SIMULATION

    def is_android(self) -> bool:
        return self.domain == DataDomain.ANDROID

    def to_dataframe(self) -> pd.DataFrame:
        """Converts the dataset to a flattened pandas DataFrame if applicable."""
        if isinstance(self.raw_data, pd.DataFrame):
            return self.raw_data.copy()

        if self.data_type == "comparison":
            rows = []
            comp = self.raw_data
            for r in comp.get("policy_results", []):
                m = r.get("metrics", {})
                mem = m.get("memory", {})
                proc = m.get("processes", {})
                rec = m.get("reclamation", {})
                tim = m.get("timing", {})
                rows.append({
                    "scenario": comp.get("scenario_name", r.get("scenario_name", "")),
                    "seed": comp.get("seed", r.get("seed", 0)),
                    "fingerprint": comp.get("workload_fingerprint", r.get("workload_fingerprint", "")),
                    "capacity_bytes": comp.get("total_capacity_bytes", mem.get("total_capacity_bytes", 0)),
                    "policy": r.get("policy_name", ""),
                    "reclaimed_bytes": mem.get("total_reclaimed_bytes", 0),
                    "processes_reclaimed": rec.get("processes_reclaimed", 0),
                    "processes_survived": proc.get("processes_survived", 0),
                    "total_processes": proc.get("total_processes", 0),
                    "survival_rate": proc.get("survival_rate", 0.0),
                    "fg_survival_rate": proc.get("foreground_survival_rate", 0.0),
                    "vis_survival_rate": proc.get("visible_survival_rate", 0.0),
                    "svc_survival_rate": proc.get("service_survival_rate", 0.0),
                    "bg_survival_rate": proc.get("background_survival_rate", 0.0),
                    "cached_survival_rate": proc.get("cached_survival_rate", 0.0),
                    "peak_used_bytes": mem.get("peak_used_bytes", 0),
                    "final_used_bytes": mem.get("final_used_bytes", 0),
                    "failed_allocations": rec.get("failed_allocations", 0),
                    "simulated_ticks": tim.get("simulated_duration_ticks", 0),
                    "wall_clock_us": tim.get("wall_clock_elapsed_microseconds", 0),
                })
            df = pd.DataFrame(rows)
            validate_comparison_dataframe(df)
            return df

        if self.data_type == "single_result":
            r = self.raw_data
            m = r.get("metrics", {})
            mem = m.get("memory", {})
            proc = m.get("processes", {})
            rec = m.get("reclamation", {})
            tim = m.get("timing", {})
            row = {
                "scenario": r.get("scenario_name", ""),
                "seed": r.get("seed", 0),
                "fingerprint": r.get("workload_fingerprint", ""),
                "capacity_bytes": mem.get("total_capacity_bytes", 0),
                "policy": r.get("policy_name", ""),
                "reclaimed_bytes": mem.get("total_reclaimed_bytes", 0),
                "processes_reclaimed": rec.get("processes_reclaimed", 0),
                "processes_survived": proc.get("processes_survived", 0),
                "total_processes": proc.get("total_processes", 0),
                "survival_rate": proc.get("survival_rate", 0.0),
                "fg_survival_rate": proc.get("foreground_survival_rate", 0.0),
                "vis_survival_rate": proc.get("visible_survival_rate", 0.0),
                "svc_survival_rate": proc.get("service_survival_rate", 0.0),
                "bg_survival_rate": proc.get("background_survival_rate", 0.0),
                "cached_survival_rate": proc.get("cached_survival_rate", 0.0),
                "peak_used_bytes": mem.get("peak_used_bytes", 0),
                "final_used_bytes": mem.get("final_used_bytes", 0),
                "failed_allocations": rec.get("failed_allocations", 0),
                "simulated_ticks": tim.get("simulated_duration_ticks", 0),
                "wall_clock_us": tim.get("wall_clock_elapsed_microseconds", 0),
            }
            df = pd.DataFrame([row])
            validate_comparison_dataframe(df)
            return df

        raise ValidationError(f"Cannot convert dataset of type '{self.data_type}' to comparison DataFrame")


def load_experiment_file(file_path: Union[str, Path]) -> Dataset:
    """Loads a single KERNOX experiment result or comparison file (JSON or CSV)."""
    p = Path(file_path)
    if not p.is_file():
        raise FileNotFoundError(f"Experiment file not found: {p}")

    suffix = p.suffix.lower()

    if suffix == ".json":
        try:
            with open(p, "r", encoding="utf-8") as f:
                data = json.load(f)
        except json.JSONDecodeError as err:
            raise ValidationError(f"Invalid JSON in '{p}': {err}") from err

        if "policy_results" in data:
            validate_experiment_comparison(data)
            return Dataset(
                domain=DataDomain.SIMULATION,
                source_path=p,
                data_type="comparison",
                raw_data=data,
                metadata={
                    "scenario": data.get("scenario_name"),
                    "seed": data.get("seed"),
                    "fingerprint": data.get("workload_fingerprint"),
                    "capacity_bytes": data.get("total_capacity_bytes"),
                    "policy_count": data.get("policy_count"),
                },
            )
        elif "policy_name" in data:
            validate_experiment_result(data)
            return Dataset(
                domain=DataDomain.SIMULATION,
                source_path=p,
                data_type="single_result",
                raw_data=data,
                metadata={
                    "policy": data.get("policy_name"),
                    "scenario": data.get("scenario_name"),
                    "seed": data.get("seed"),
                    "fingerprint": data.get("workload_fingerprint"),
                },
            )
        elif data.get("source") == "android_adb_observation":
            raise ProvenanceError(
                f"File '{p}' is an Android ADB observation snapshot, not an experiment result. "
                "Use load_android_snapshot() instead."
            )
        else:
            raise ValidationError(f"Unrecognized experiment JSON structure in '{p}'")

    elif suffix == ".csv":
        try:
            df = pd.read_csv(p)
        except Exception as err:
            raise ValidationError(f"Failed to parse CSV file '{p}': {err}") from err

        if "victim_pid" in df.columns and "memory_reclaimed_bytes" in df.columns:
            return Dataset(
                domain=DataDomain.SIMULATION,
                source_path=p,
                data_type="decisions_log",
                raw_data=df,
                metadata={"record_count": len(df)},
            )
        else:
            validate_comparison_dataframe(df)
            return Dataset(
                domain=DataDomain.SIMULATION,
                source_path=p,
                data_type="comparison_table",
                raw_data=df,
                metadata={
                    "scenarios": list(df["scenario"].unique()),
                    "policies": list(df["policy"].unique()),
                    "record_count": len(df),
                },
            )

    else:
        raise ValidationError(f"Unsupported file format '{suffix}'. Expected .json or .csv")


def load_android_snapshot(file_path: Union[str, Path]) -> Dataset:
    """Loads a single real Android ADB observation snapshot JSON file."""
    p = Path(file_path)
    if not p.is_file():
        raise FileNotFoundError(f"Android snapshot file not found: {p}")

    try:
        with open(p, "r", encoding="utf-8") as f:
            data = json.load(f)
    except json.JSONDecodeError as err:
        raise ValidationError(f"Invalid JSON in '{p}': {err}") from err

    validate_android_snapshot(data)

    dev = data.get("device_info", {})
    return Dataset(
        domain=DataDomain.ANDROID,
        source_path=p,
        data_type="android_snapshot",
        raw_data=data,
        metadata={
            "serial": dev.get("serial"),
            "model": dev.get("model"),
            "target_type": dev.get("target_type", "Android Virtual Device (AVD)" if dev.get("is_emulator") or "emulator" in str(dev.get("serial", "")) else "Physical Device"),
            "is_emulator": bool(dev.get("is_emulator") or "emulator" in str(dev.get("serial", ""))),
            "android_release": dev.get("android_release"),
            "sdk_version": dev.get("sdk_version"),
            "timestamp_ms": data.get("collection_timestamp_epoch_ms"),
            "process_count": len(data.get("processes", [])),
        },
    )


def load_experiment_directory(directory_path: Union[str, Path]) -> List[Dataset]:
    """Scans and loads all valid experiment files (JSON and CSV) in a directory."""
    d = Path(directory_path)
    if not d.is_dir():
        raise NotADirectoryError(f"Directory not found: {d}")

    datasets: List[Dataset] = []
    candidates = sorted(list(d.glob("*.json")) + list(d.glob("*.csv")))

    for path in candidates:
        try:
            # Skip android snapshots if mixed in folder
            with open(path, "r", encoding="utf-8") as f:
                header = f.read(256)
                if '"source": "android_adb_observation"' in header or '"android_adb_observation"' in header:
                    continue
            ds = load_experiment_file(path)
            datasets.append(ds)
        except Exception:
            # Ignore unrelated files or malformed non-experiment files in multi-file scan
            continue

    if not datasets:
        raise ValidationError(f"No valid KERNOX experiment datasets discovered in directory '{d}'")

    return datasets
