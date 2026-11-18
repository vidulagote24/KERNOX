"""Android observation analytics module for Phase E ADB telemetry snapshots.

Provides device memory composition analysis, process PSS/RSS ranking, and
importance tier memory allocation profiling with strict provenance guarantees.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional
import pandas as pd

from .loader import Dataset
from .metrics import bytes_to_mb, calculate_android_memory_metrics
from .validators import ProvenanceError, ValidationError


@dataclass
class AndroidSnapshotSummary:
    """Analytical summary of an observed real Android device state."""
    provenance: str
    serial: str
    model: str
    manufacturer: str
    android_release: str
    sdk_version: Optional[int]
    timestamp_epoch_ms: int
    memory_metrics: Dict[str, Any]
    process_df: pd.DataFrame
    is_emulator: bool = False
    target_type: str = "Physical Device"
    pss_by_importance: Dict[str, float] = field(default_factory=dict)
    top_consumers_by_pss: List[Dict[str, Any]] = field(default_factory=list)
    key_observations: List[str] = field(default_factory=list)


class AndroidAnalyzer:
    """Analyzes real Android device memory and process observations."""

    @staticmethod
    def analyze_snapshot(dataset: Dataset) -> AndroidSnapshotSummary:
        """Analyzes a single Android ADB observation snapshot."""
        if not dataset.is_android():
            raise ProvenanceError(
                f"Cannot perform Android analysis on dataset with domain '{dataset.domain.value}'. "
                "Expected 'android_adb_observation'."
            )

        data = dataset.raw_data
        if not isinstance(data, dict):
            raise ValidationError(f"Invalid raw Android snapshot format: expected dict, got {type(data).__name__}")

        dev = data.get("device_info", {})
        serial = str(dev.get("serial", "unknown"))
        is_emu = serial.startswith("emulator-") or dev.get("is_emulator", False) or "Emulator" in dev.get("model", "")
        target_type = "Android Virtual Device (AVD) / Emulator" if is_emu else "Physical Android Device"

        mem_metrics = calculate_android_memory_metrics(data)

        # Process analysis
        raw_procs = data.get("processes", [])
        proc_rows = []
        for p in raw_procs:
            pss = p.get("pss_bytes")
            rss = p.get("rss_bytes")
            proc_rows.append({
                "pid": p.get("pid", 0),
                "process_name": p.get("process_name", ""),
                "package_name": p.get("package_name", ""),
                "pss_bytes": pss,
                "pss_mb": bytes_to_mb(pss) if pss is not None else None,
                "rss_bytes": rss,
                "rss_mb": bytes_to_mb(rss) if rss is not None else None,
                "importance": p.get("normalized_importance", "Unknown"),
                "raw_importance": p.get("raw_importance", ""),
                "oom_score_adj": p.get("oom_score_adj"),
            })

        proc_df = pd.DataFrame(proc_rows)

        # Aggregate PSS by importance tier
        pss_by_importance: Dict[str, float] = {}
        top_consumers: List[Dict[str, Any]] = []

        if not proc_df.empty and "pss_mb" in proc_df.columns:
            valid_pss_df = proc_df.dropna(subset=["pss_mb"])
            if not valid_pss_df.empty:
                for imp, grp in valid_pss_df.groupby("importance"):
                    pss_by_importance[str(imp)] = round(float(grp["pss_mb"].sum()), 2)

                top_df = valid_pss_df.sort_values(by="pss_mb", ascending=False).head(10)
                for _, r in top_df.iterrows():
                    top_consumers.append({
                        "pid": int(r["pid"]),
                        "name": str(r["process_name"]),
                        "pss_mb": float(r["pss_mb"]),
                        "importance": str(r["importance"]),
                        "oom_score_adj": r["oom_score_adj"],
                    })

        # Observations
        observations: List[str] = [
            f"Observed target: {target_type} ({dev.get('model', 'Unknown')}, serial: {serial}) running Android {dev.get('android_release', 'Unknown')} (API {dev.get('sdk_version', 'N/A')})."
        ]
        if is_emu:
            observations.append(
                "Provenance Note: This observation originates from an Android Virtual Device (AVD) emulator, not physical hardware."
            )

        if mem_metrics.get("total_ram_mb") is not None:
            total_mb = mem_metrics["total_ram_mb"]
            used_mb = mem_metrics.get("used_ram_mb", 0)
            avail_mb = mem_metrics.get("available_ram_mb")
            free_mb = mem_metrics.get("free_ram_mb", 0)
            cached_mb = mem_metrics.get("cached_mb", 0)
            avail_str = f", {avail_mb} MB available" if avail_mb is not None else ""
            observations.append(
                f"Memory Telemetry: Total RAM {total_mb} MB ({used_mb} MB used{avail_str}, {free_mb} MB free, {cached_mb} MB cached)."
            )
            observations.append(
                "Accounting Note: Available RAM includes Free RAM plus reclaimable Cached RAM. These categories overlap and must not be summed together."
            )

        if top_consumers:
            lead = top_consumers[0]
            observations.append(
                f"Highest memory consumer: '{lead['name']}' consuming {lead['pss_mb']} MB PSS ({lead['importance']})."
            )

        return AndroidSnapshotSummary(
            provenance="android_adb_observation",
            serial=serial,
            model=str(dev.get("model", "unknown")),
            manufacturer=str(dev.get("manufacturer", "unknown")),
            android_release=str(dev.get("android_release", "unknown")),
            sdk_version=dev.get("sdk_version"),
            timestamp_epoch_ms=data.get("collection_timestamp_epoch_ms", 0),
            memory_metrics=mem_metrics,
            process_df=proc_df,
            is_emulator=is_emu,
            target_type=target_type,
            pss_by_importance=pss_by_importance,
            top_consumers_by_pss=top_consumers,
            key_observations=observations,
        )
