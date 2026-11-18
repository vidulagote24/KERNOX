"""KERNOX 2.0 — Resource Intelligence Platform Analytics & Visualization Layer.

This package provides deterministic ingestion, schema validation, metrics calculation,
statistical aggregation, publication-quality visualizations, and Markdown reporting
for KERNOX simulation experiments and real-world Android ADB observations.
"""

__version__ = "0.1.0"
__author__ = "KERNOX Engineering Team"

from .loader import (
    load_experiment_file,
    load_experiment_directory,
    load_android_snapshot,
    DataDomain,
    Dataset,
)
from .validators import (
    ValidationError,
    ProvenanceError,
    validate_experiment_result,
    validate_experiment_comparison,
    validate_android_snapshot,
    validate_comparison_dataframe,
)
from .metrics import (
    calculate_memory_metrics,
    calculate_process_metrics,
    calculate_reclamation_metrics,
    calculate_android_memory_metrics,
)
from .statistics import (
    MetricSummary,
    summarize_series,
    aggregate_policy_metrics,
)
from .experiment_analysis import (
    ExperimentAnalyzer,
    PolicyComparisonSummary,
    ScenarioComparisonSummary,
    RankingTier,
    rank_series_with_ties,
    format_ranking_string,
)
from .android_analysis import (
    AndroidAnalyzer,
    AndroidSnapshotSummary,
)
from .visualizations import ChartGenerator
from .report_generator import ReportGenerator

__all__ = [
    "__version__",
    "load_experiment_file",
    "load_experiment_directory",
    "load_android_snapshot",
    "DataDomain",
    "Dataset",
    "ValidationError",
    "ProvenanceError",
    "validate_experiment_result",
    "validate_experiment_comparison",
    "validate_android_snapshot",
    "validate_comparison_dataframe",
    "calculate_memory_metrics",
    "calculate_process_metrics",
    "calculate_reclamation_metrics",
    "calculate_android_memory_metrics",
    "MetricSummary",
    "summarize_series",
    "aggregate_policy_metrics",
    "ExperimentAnalyzer",
    "PolicyComparisonSummary",
    "ScenarioComparisonSummary",
    "RankingTier",
    "rank_series_with_ties",
    "format_ranking_string",
    "AndroidAnalyzer",
    "AndroidSnapshotSummary",
    "ChartGenerator",
    "ReportGenerator",
]
