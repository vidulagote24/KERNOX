"""Unit tests for kernox_analytics.experiment_analysis and statistics modules."""

from pathlib import Path
import pandas as pd
import pytest

from kernox_analytics.experiment_analysis import (
    ExperimentAnalyzer,
    RankingTier,
    format_ranking_string,
    rank_series_with_ties,
)
from kernox_analytics.loader import load_experiment_file
from kernox_analytics.report_generator import ReportGenerator
from kernox_analytics.statistics import aggregate_policy_metrics, summarize_series

EXAMPLES_DIR = Path(__file__).resolve().parent.parent / "examples"


def test_analyze_comparison_high_pressure():
    path = EXAMPLES_DIR / "experiments" / "high_pressure_comparison.json"
    ds = load_experiment_file(path)
    summary = ExperimentAnalyzer.analyze_comparison(ds)

    assert summary.scenario_name == "HighMemoryPressure"
    assert summary.seed == 42
    assert len(summary.policies) == 4
    assert "ImportanceAware" in summary.metrics_by_policy

    ia = summary.metrics_by_policy["ImportanceAware"]
    assert ia["foreground_survival_rate"] == 1.0
    assert ia["cached_survival_rate"] == 0.0

    fifo = summary.metrics_by_policy["FIFO"]
    assert fifo["foreground_survival_rate"] == 0.0

    # Rankings
    assert "by_foreground_protection" in summary.rankings
    assert summary.rankings["by_foreground_protection"][0] in ("ImportanceAware", "Priority")

    # Observations
    assert len(summary.key_observations) >= 2


def test_analyze_multi_scenario_all_five():
    scenario_files = [
        "normal_comparison.json",
        "high_pressure_comparison.json",
        "background_heavy_comparison.json",
        "foreground_protection_comparison.json",
        "mixed_comparison.json",
    ]
    datasets = [load_experiment_file(EXAMPLES_DIR / "experiments" / f) for f in scenario_files]

    scen_summary = ExperimentAnalyzer.analyze_multi_scenario(datasets)
    assert len(scen_summary.scenarios) == 5
    assert set(scen_summary.scenarios) == {
        "Normal", "HighMemoryPressure", "BackgroundHeavy", "ForegroundProtection", "Mixed"
    }
    # 4 policies * 5 scenarios = 20 rows
    assert len(scen_summary.dataframe) == 20
    assert len(scen_summary.key_observations) >= 3


def test_statistics_summarize_series():
    # Empty
    s_empty = summarize_series([])
    assert s_empty.count == 0
    assert s_empty.mean == 0.0

    # Values
    vals = [10.0, 20.0, 30.0, 40.0, 50.0]
    s = summarize_series(vals)
    assert s.count == 5
    assert s.mean == 30.0
    assert s.median == 30.0
    assert s.min == 10.0
    assert s.max == 50.0
    assert round(s.std, 2) == 15.81


def test_aggregate_policy_metrics():
    path = EXAMPLES_DIR / "experiments" / "high_pressure_comparison.json"
    ds = load_experiment_file(path)
    df = ds.to_dataframe()

    agg = aggregate_policy_metrics(df)
    assert "FIFO" in agg
    assert "survival_rate" in agg["FIFO"]
    assert agg["FIFO"]["survival_rate"].count == 1


def test_rank_series_with_ties_all_equal():
    """Validates that equal metric values are grouped into a single tier and marked as tied."""
    data = {"FIFO": 0.4, "LRU": 0.4, "Priority": 0.4, "ImportanceAware": 0.4}
    tiers = rank_series_with_ties(
        data,
        ascending=False,
        value_formatter=lambda v: f"{round(v * 100, 1)}%",
        secondary_order=["FIFO", "LRU", "Priority", "ImportanceAware"],
    )
    assert len(tiers) == 1
    assert tiers[0].is_tied is True
    assert tiers[0].value == 0.4
    assert tiers[0].formatted_value == "40.0%"
    assert tiers[0].policies == ["FIFO", "LRU", "Priority", "ImportanceAware"]

    formatted = format_ranking_string(tiers)
    assert formatted == "FIFO = LRU = Priority = ImportanceAware (40.0% - Tied)"
    assert " > " not in formatted


def test_rank_series_with_ties_partially_tied():
    """Validates that partial ties are grouped correctly with distinct tiers separated by '>'."""
    # 2 tied at top, 2 tied at bottom
    data = {"FIFO": 0.0, "LRU": 0.0, "Priority": 1.0, "ImportanceAware": 1.0}
    tiers = rank_series_with_ties(
        data,
        ascending=False,
        value_formatter=lambda v: f"{round(v * 100, 1)}%",
        secondary_order=["ImportanceAware", "Priority", "LRU", "FIFO"],
    )
    assert len(tiers) == 2
    assert tiers[0].is_tied is True
    assert tiers[0].policies == ["ImportanceAware", "Priority"]
    assert tiers[0].formatted_value == "100.0%"

    assert tiers[1].is_tied is True
    assert tiers[1].policies == ["LRU", "FIFO"]
    assert tiers[1].formatted_value == "0.0%"

    formatted = format_ranking_string(tiers)
    assert formatted == "ImportanceAware = Priority (100.0%) > LRU = FIFO (0.0%)"

    # 1 tied pair, 2 distinct singles
    data2 = {"FIFO": 1158, "LRU": 1158, "ImportanceAware": 1066, "Priority": 985}
    tiers2 = rank_series_with_ties(
        data2,
        ascending=False,
        value_formatter=lambda v: f"{v} MB",
        secondary_order=["FIFO", "LRU", "ImportanceAware", "Priority"],
    )
    assert len(tiers2) == 3
    assert tiers2[0].is_tied is True
    assert tiers2[0].policies == ["FIFO", "LRU"]
    assert tiers2[1].is_tied is False
    assert tiers2[1].policies == ["ImportanceAware"]
    assert tiers2[2].is_tied is False
    assert tiers2[2].policies == ["Priority"]

    formatted2 = format_ranking_string(tiers2)
    assert formatted2 == "FIFO = LRU (1158 MB) > ImportanceAware (1066 MB) > Priority (985 MB)"


def test_rank_series_with_ties_all_distinct():
    """Validates strictly distinct metric values produce individual tiers without '='."""
    data = {"PolicyA": 0.9, "PolicyB": 0.8, "PolicyC": 0.7, "PolicyD": 0.6}
    tiers = rank_series_with_ties(
        data,
        ascending=False,
        value_formatter=lambda v: f"{round(v * 100, 1)}%",
    )
    assert len(tiers) == 4
    for tier in tiers:
        assert tier.is_tied is False
        assert len(tier.policies) == 1

    formatted = format_ranking_string(tiers)
    assert formatted == "PolicyA (90.0%) > PolicyB (80.0%) > PolicyC (70.0%) > PolicyD (60.0%)"
    assert " = " not in formatted


def test_rank_series_with_ties_ascending_latency():
    """Validates ascending ranking for execution time with '<' delimiter."""
    data = {"FIFO": 6635, "LRU": 134, "Priority": 149, "ImportanceAware": 203}
    tiers = rank_series_with_ties(
        data,
        ascending=True,
        value_formatter=lambda us: f"{int(us)} μs",
    )
    assert len(tiers) == 4
    assert tiers[0].policies == ["LRU"]
    assert tiers[1].policies == ["Priority"]
    assert tiers[2].policies == ["ImportanceAware"]
    assert tiers[3].policies == ["FIFO"]

    formatted = format_ranking_string(tiers, delimiter=" < ")
    assert formatted == "LRU (134 μs) < Priority (149 μs) < ImportanceAware (203 μs) < FIFO (6635 μs)"


def test_high_pressure_rankings_and_report_generation(tmp_path):
    """End-to-end verification of tied metric formatting and wall-clock observation labeling."""
    repo_root = Path(__file__).resolve().parent.parent.parent.parent
    artifact_path = repo_root / "artifacts" / "benchmarks" / "high_pressure_comparison.json"
    if artifact_path.exists():
        target_path = artifact_path
    else:
        target_path = EXAMPLES_DIR / "experiments" / "high_pressure_comparison.json"

    ds = load_experiment_file(target_path)
    summary = ExperimentAnalyzer.analyze_comparison(ds)

    # Issue 1 verification: tied rankings
    assert summary.ranking_strings["by_overall_survival"] == "FIFO = LRU = Priority = ImportanceAware (40.0% - Tied)"
    assert summary.ranking_strings["by_foreground_protection"] == "ImportanceAware = Priority (100.0%) > LRU = FIFO (0.0%)"
    assert summary.ranking_strings["by_memory_reclaimed"] == "FIFO = LRU (1158.0 MB) > ImportanceAware (1066.0 MB) > Priority (985.0 MB)"

    # Issue 2 verification: wall-clock time preservation
    if target_path == artifact_path:
        assert "FIFO: 6635 μs" in summary.ranking_strings["by_execution_speed_wall_clock"]
        assert "LRU: 134 μs" in summary.ranking_strings["by_execution_speed_wall_clock"]
        assert "Priority: 149 μs" in summary.ranking_strings["by_execution_speed_wall_clock"]
        assert "ImportanceAware: 203 μs" in summary.ranking_strings["by_execution_speed_wall_clock"]
    else:
        assert "FIFO" in summary.ranking_strings["by_execution_speed_wall_clock"]

    # Backwards compatibility checks
    assert "by_overall_survival" in summary.rankings
    assert len(summary.rankings["by_overall_survival"]) == 4
    assert summary.rankings["by_foreground_protection"][0] in ("ImportanceAware", "Priority")

    # Verify generated report
    rep_gen = ReportGenerator(output_dir=tmp_path)
    out_file = rep_gen.generate_experiment_report(summary, {})
    content = out_file.read_text(encoding="utf-8")

    # Section 5 checks
    assert "- **Overall Process Survival:** FIFO = LRU = Priority = ImportanceAware (40.0% - Tied)" in content
    assert "FIFO > LRU > Priority > ImportanceAware" not in content
    assert "- **Foreground Protection:** ImportanceAware = Priority (100.0%) > LRU = FIFO (0.0%)" in content
    assert "- **Memory Reclaimed:** FIFO = LRU (1158.0 MB) > ImportanceAware (1066.0 MB) > Priority (985.0 MB)" in content
    assert "- **Execution Speed (Wall-Clock):** Single-run host latency observations, not an algorithmic performance ranking:" in content
    assert "  - FIFO: 6635 μs" in content
    assert "  - LRU: 134 μs" in content
    assert "  - Priority: 149 μs" in content
    assert "  - ImportanceAware: 203 μs" in content
    assert "The first policy execution recorded substantially higher host latency. Cold-start or initialization overhead may have contributed, but this single measurement cannot establish the cause." in content

    # Verify no less-than operator in Section 5 execution speed lines
    sec5_text = content[content.find("### 5. Policy Rankings"):content.find("### 6. Visualizations")]
    assert "<" not in sec5_text

    # Section 7 limitation check
    assert "The benchmark harness does not perform multi-iteration statistical sampling" in content
    assert "The first policy execution recorded substantially higher host latency. Cold-start or initialization overhead may have contributed, but this single measurement cannot establish the cause." in content
