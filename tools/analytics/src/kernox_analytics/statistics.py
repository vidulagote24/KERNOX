"""Statistical aggregation and summary utilities for multi-run experiment comparisons.

Provides deterministic statistical moments (mean, std, median, min, max)
over series of simulation and operational metrics.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Sequence, Union
import numpy as np
import pandas as pd


@dataclass
class MetricSummary:
    """Summary statistics for a continuous or discrete numeric metric series."""
    count: int
    mean: float
    std: float
    median: float
    min: float
    max: float

    def to_dict(self) -> Dict[str, Any]:
        return {
            "count": self.count,
            "mean": round(self.mean, 4),
            "std": round(self.std, 4),
            "median": round(self.median, 4),
            "min": round(self.min, 4),
            "max": round(self.max, 4),
        }


def summarize_series(values: Sequence[Union[int, float]]) -> MetricSummary:
    """Computes sample moments for a sequence of numeric values.

    Handles empty sequences gracefully by returning zeroes.
    """
    cleaned = [float(v) for v in values if v is not None and not np.isnan(v)]
    if not cleaned:
        return MetricSummary(count=0, mean=0.0, std=0.0, median=0.0, min=0.0, max=0.0)

    arr = np.array(cleaned, dtype=np.float64)
    n = len(arr)
    mean_val = float(np.mean(arr))
    std_val = float(np.std(arr, ddof=1)) if n > 1 else 0.0
    median_val = float(np.median(arr))
    min_val = float(np.min(arr))
    max_val = float(np.max(arr))

    return MetricSummary(
        count=n,
        mean=mean_val,
        std=std_val,
        median=median_val,
        min=min_val,
        max=max_val,
    )


def aggregate_policy_metrics(df: pd.DataFrame) -> Dict[str, Dict[str, MetricSummary]]:
    """Groups comparison DataFrame by policy and summarizes key metrics across runs."""
    if df.empty or "policy" not in df.columns:
        return {}

    metrics_to_summarize = [
        "survival_rate",
        "fg_survival_rate",
        "reclaimed_bytes",
        "peak_used_bytes",
        "failed_allocations",
        "wall_clock_us",
    ]

    result: Dict[str, Dict[str, MetricSummary]] = {}

    for policy_name, group in df.groupby("policy"):
        policy_str = str(policy_name)
        result[policy_str] = {}
        for metric in metrics_to_summarize:
            if metric in group.columns:
                result[policy_str][metric] = summarize_series(group[metric].tolist())

    return result
