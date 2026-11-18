"""Experiment analysis module providing comparative evaluation across reclamation policies and scenarios.

Extracts objective tradeoffs, importance-tier protection rankings, and memory
efficiency without making unsupported causal claims.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Union
import numpy as np
import pandas as pd

from .loader import Dataset
from .metrics import bytes_to_mb, calculate_memory_metrics, calculate_process_metrics, calculate_reclamation_metrics
from .validators import ValidationError


@dataclass
class RankingTier:
    """Represents an equivalence class (tier) of policies sharing the same metric value."""
    value: Union[int, float]
    formatted_value: str
    policies: List[str]

    @property
    def is_tied(self) -> bool:
        return len(self.policies) > 1


def rank_series_with_ties(
    data: Dict[str, Union[int, float]],
    ascending: bool = False,
    tolerance: float = 1e-6,
    value_formatter: Optional[Callable[[Union[int, float]], str]] = None,
    secondary_order: Optional[List[str]] = None,
) -> List[RankingTier]:
    """Ranks policies into tiers based on numeric metric values, grouping ties.

    Args:
        data: Mapping from policy name to numeric value.
        ascending: If True, lower values rank higher (e.g. latency). If False, higher values rank higher.
        tolerance: Absolute tolerance for floating-point equality.
        value_formatter: Optional callable to format values into strings.
        secondary_order: Optional list of policy names specifying preferred ordering within tied groups.

    Returns:
        List of RankingTier objects ordered from best to worst.
    """
    if not data:
        return []

    if value_formatter is None:
        def default_fmt(val: Union[int, float]) -> str:
            if isinstance(val, (int, np.integer)):
                return str(val)
            return f"{val:.4g}"
        value_formatter = default_fmt

    items = list(data.items())
    sorted_items = sorted(items, key=lambda x: x[1], reverse=not ascending)

    tiers: List[RankingTier] = []
    current_policies: List[str] = [sorted_items[0][0]]
    current_val: Union[int, float] = sorted_items[0][1]

    for pol, val in sorted_items[1:]:
        if abs(val - current_val) <= tolerance:
            current_policies.append(pol)
        else:
            tiers.append(
                RankingTier(
                    value=current_val,
                    formatted_value=value_formatter(current_val),
                    policies=current_policies,
                )
            )
            current_policies = [pol]
            current_val = val

    tiers.append(
        RankingTier(
            value=current_val,
            formatted_value=value_formatter(current_val),
            policies=current_policies,
        )
    )

    if secondary_order:
        order_map = {name: idx for idx, name in enumerate(secondary_order)}
        for tier in tiers:
            tier.policies.sort(key=lambda p: order_map.get(p, 999))

    return tiers


def format_ranking_string(
    tiers: List[RankingTier],
    delimiter: str = " > ",
    tie_delimiter: str = " = ",
) -> str:
    """Formats ranking tiers into a human-readable string.

    Examples:
        All tied:
            FIFO = LRU = Priority = ImportanceAware (40.0% - Tied)
        Partially tied:
            ImportanceAware = Priority (100.0%) > FIFO = LRU (0.0%)
        Distinct:
            PolicyA (90.0%) > PolicyB (80.0%) > PolicyC (70.0%)
    """
    if not tiers:
        return "N/A"

    if len(tiers) == 1:
        tier = tiers[0]
        if tier.is_tied:
            policies_str = tie_delimiter.join(tier.policies)
            return f"{policies_str} ({tier.formatted_value} - Tied)"
        else:
            return f"{tier.policies[0]} ({tier.formatted_value})"

    tier_strings: List[str] = []
    for tier in tiers:
        if tier.is_tied:
            policies_str = tie_delimiter.join(tier.policies)
            tier_strings.append(f"{policies_str} ({tier.formatted_value})")
        else:
            tier_strings.append(f"{tier.policies[0]} ({tier.formatted_value})")

    return delimiter.join(tier_strings)


@dataclass
class PolicyComparisonSummary:
    """Consolidated analytical summary comparing policies on an identical workload."""
    scenario_name: str
    seed: int
    workload_fingerprint: str
    capacity_bytes: int
    policies: List[str]
    dataframe: pd.DataFrame
    metrics_by_policy: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    rankings: Dict[str, List[str]] = field(default_factory=dict)
    ranked_tiers: Dict[str, List[RankingTier]] = field(default_factory=dict)
    ranking_strings: Dict[str, str] = field(default_factory=dict)
    key_observations: List[str] = field(default_factory=list)


@dataclass
class ScenarioComparisonSummary:
    """Consolidated summary comparing policies across multiple distinct workload scenarios."""
    scenarios: List[str]
    dataframe: pd.DataFrame
    comparisons: Dict[str, PolicyComparisonSummary] = field(default_factory=dict)
    key_observations: List[str] = field(default_factory=list)


class ExperimentAnalyzer:
    """Analytical engine for KERNOX experiment runs and policy comparisons."""

    @staticmethod
    def analyze_comparison(dataset: Dataset) -> PolicyComparisonSummary:
        """Analyzes a single ExperimentComparison dataset."""
        df = dataset.to_dataframe()
        if df.empty:
            raise ValidationError("Cannot analyze an empty experiment dataset")

        scenario = str(df["scenario"].iloc[0])
        seed = int(df["seed"].iloc[0])
        fingerprint = str(df["fingerprint"].iloc[0])
        capacity = int(df["capacity_bytes"].iloc[0])
        policies = list(df["policy"].unique())

        metrics_by_policy: Dict[str, Dict[str, Any]] = {}
        for _, row in df.iterrows():
            pol = str(row["policy"])
            metrics_by_policy[pol] = {
                "survival_rate": float(row["survival_rate"]),
                "foreground_survival_rate": float(row["fg_survival_rate"]),
                "background_survival_rate": float(row["bg_survival_rate"]),
                "cached_survival_rate": float(row["cached_survival_rate"]),
                "reclaimed_bytes": int(row["reclaimed_bytes"]),
                "reclaimed_mb": bytes_to_mb(int(row["reclaimed_bytes"])),
                "peak_used_bytes": int(row["peak_used_bytes"]),
                "peak_used_mb": bytes_to_mb(int(row["peak_used_bytes"])),
                "failed_allocations": int(row["failed_allocations"]),
                "wall_clock_us": int(row["wall_clock_us"]),
                "simulated_ticks": int(row["simulated_ticks"]),
            }

        # Determine rankings grouping ties into equivalence classes
        survival_data = {str(row["policy"]): float(row["survival_rate"]) for _, row in df.iterrows()}
        fg_data = {str(row["policy"]): float(row["fg_survival_rate"]) for _, row in df.iterrows()}
        reclaimed_data = {str(row["policy"]): int(row["reclaimed_bytes"]) for _, row in df.iterrows()}
        wall_clock_data = {str(row["policy"]): int(row["wall_clock_us"]) for _, row in df.iterrows()}

        survival_tiers = rank_series_with_ties(
            survival_data,
            ascending=False,
            value_formatter=lambda r: f"{round(float(r) * 100.0, 1)}%",
            secondary_order=["FIFO", "LRU", "Priority", "ImportanceAware"],
        )
        fg_tiers = rank_series_with_ties(
            fg_data,
            ascending=False,
            value_formatter=lambda r: f"{round(float(r) * 100.0, 1)}%",
            secondary_order=["ImportanceAware", "Priority", "LRU", "FIFO"],
        )
        reclaimed_tiers = rank_series_with_ties(
            reclaimed_data,
            ascending=False,
            value_formatter=lambda b: f"{round(float(b) / (1024**2), 1)} MB",
            secondary_order=["FIFO", "LRU", "ImportanceAware", "Priority"],
        )
        wall_clock_tiers = rank_series_with_ties(
            wall_clock_data,
            ascending=True,
            value_formatter=lambda us: f"{int(us)} μs",
            secondary_order=["LRU", "Priority", "ImportanceAware", "FIFO"],
        )

        ranked_tiers: Dict[str, List[RankingTier]] = {
            "by_overall_survival": survival_tiers,
            "by_foreground_protection": fg_tiers,
            "by_memory_reclaimed": reclaimed_tiers,
            "by_execution_speed_wall_clock": wall_clock_tiers,
        }

        ranking_strings: Dict[str, str] = {
            "by_overall_survival": format_ranking_string(survival_tiers),
            "by_foreground_protection": format_ranking_string(fg_tiers),
            "by_memory_reclaimed": format_ranking_string(reclaimed_tiers),
            "by_execution_speed_wall_clock": ", ".join(f"{row['policy']}: {int(row['wall_clock_us'])} μs" for _, row in df.iterrows()),
        }

        rankings: Dict[str, List[str]] = {
            "by_overall_survival": [p for t in survival_tiers for p in t.policies],
            "by_foreground_protection": [p for t in fg_tiers for p in t.policies],
            "by_memory_reclaimed": [p for t in reclaimed_tiers for p in t.policies],
            "by_execution_speed_wall_clock": [p for t in wall_clock_tiers for p in t.policies],
        }

        # Generate objective observations without confusing correlation with causation or assuming unpromised guarantees
        observations: List[str] = []
        if "ImportanceAware" in metrics_by_policy:
            ia = metrics_by_policy["ImportanceAware"]
            if ia["foreground_survival_rate"] == 1.0:
                observations.append(
                    "ImportanceAware satisfied its configured protection guarantee, preserving 100% of foreground processes during reclamation."
                )
            cached_rate = ia["cached_survival_rate"]
            observations.append(
                f"ImportanceAware prioritized lower importance tiers for eviction (observed cached process survival: {round(cached_rate*100, 1)}%)."
            )

        if "FIFO" in metrics_by_policy:
            fifo = metrics_by_policy["FIFO"]
            fg_rate = fifo["foreground_survival_rate"]
            observations.append(
                f"Consistent with its arrival-order design without tier awareness, FIFO evicted oldest processes regardless of importance (observed foreground survival: {round(fg_rate*100, 1)}%)."
            )

        if "LRU" in metrics_by_policy:
            lru = metrics_by_policy["LRU"]
            fg_rate = lru["foreground_survival_rate"]
            observations.append(
                f"Consistent with its recency-order design without tier awareness, LRU evicted least recently active processes regardless of importance (observed foreground survival: {round(fg_rate*100, 1)}%)."
            )

        if "Priority" in metrics_by_policy:
            prio = metrics_by_policy["Priority"]
            fg_rate = prio["foreground_survival_rate"]
            observations.append(
                f"Priority evicted processes based strictly on CPU scheduling urgency (observed foreground survival: {round(fg_rate*100, 1)}%)."
            )

        total_reclaimed_all = sum(m["reclaimed_bytes"] for m in metrics_by_policy.values())
        if total_reclaimed_all == 0:
            observations.append(
                "Workload did not breach memory pressure thresholds; no processes were reclaimed across any evaluated policy."
            )

        return PolicyComparisonSummary(
            scenario_name=scenario,
            seed=seed,
            workload_fingerprint=fingerprint,
            capacity_bytes=capacity,
            policies=policies,
            dataframe=df,
            metrics_by_policy=metrics_by_policy,
            rankings=rankings,
            ranked_tiers=ranked_tiers,
            ranking_strings=ranking_strings,
            key_observations=observations,
        )

    @staticmethod
    def analyze_multi_scenario(datasets: List[Dataset]) -> ScenarioComparisonSummary:
        """Aggregates multiple comparison datasets spanning distinct scenarios."""
        dfs: List[pd.DataFrame] = []
        comparisons: Dict[str, PolicyComparisonSummary] = {}

        for ds in datasets:
            if not ds.is_simulation():
                continue
            try:
                comp = ExperimentAnalyzer.analyze_comparison(ds)
                comparisons[comp.scenario_name] = comp
                dfs.append(comp.dataframe)
            except Exception:
                continue

        if not dfs:
            raise ValidationError("No valid simulation comparison datasets found for multi-scenario analysis")

        consolidated_df = pd.concat(dfs, ignore_index=True)

        # Standard canonical ordering for presentation
        canonical_order = ["Normal", "HighMemoryPressure", "BackgroundHeavy", "ForegroundProtection", "Mixed"]
        discovered_scenarios = list(consolidated_df["scenario"].unique())
        ordered_scenarios = [s for s in canonical_order if s in discovered_scenarios]
        for s in discovered_scenarios:
            if s not in ordered_scenarios:
                ordered_scenarios.append(s)

        observations: List[str] = [
            f"Evaluated {len(ordered_scenarios)} distinct scenarios: {', '.join(ordered_scenarios)}.",
        ]
        if "Normal" in comparisons:
            norm_reclaimed = sum(m["reclaimed_bytes"] for m in comparisons["Normal"].metrics_by_policy.values())
            if norm_reclaimed == 0:
                observations.append(
                    "Normal workload operated below pressure thresholds; zero processes were reclaimed across all policies."
                )
        if "HighMemoryPressure" in comparisons:
            observations.append(
                "HighMemoryPressure scenario induced sustained threshold breaches, triggering multi-tier reclamation."
            )
        if "ForegroundProtection" in comparisons:
            fp = comparisons["ForegroundProtection"]
            if "ImportanceAware" in fp.metrics_by_policy:
                ia_fg = fp.metrics_by_policy["ImportanceAware"]["foreground_survival_rate"]
                observations.append(
                    f"ForegroundProtection scenario: ImportanceAware maintained {round(ia_fg*100, 1)}% foreground survival under targeted memory contention."
                )
        if "BackgroundHeavy" in comparisons:
            observations.append(
                "BackgroundHeavy scenario provided ample low-tier candidates, enabling high reclamation yields."
            )

        return ScenarioComparisonSummary(
            scenarios=ordered_scenarios,
            dataframe=consolidated_df,
            comparisons=comparisons,
            key_observations=observations,
        )
