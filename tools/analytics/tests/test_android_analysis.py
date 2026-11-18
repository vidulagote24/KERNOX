"""Unit tests for kernox_analytics.android_analysis module."""

from pathlib import Path
import pytest

from kernox_analytics.android_analysis import AndroidAnalyzer
from kernox_analytics.loader import load_android_snapshot, load_experiment_file
from kernox_analytics.validators import ProvenanceError

EXAMPLES_DIR = Path(__file__).resolve().parent.parent / "examples"


def test_analyze_android_snapshot():
    path = EXAMPLES_DIR / "android" / "pixel7_snapshot.json"
    ds = load_android_snapshot(path)
    summary = AndroidAnalyzer.analyze_snapshot(ds)

    assert summary.provenance == "android_adb_observation"
    assert summary.serial == "emulator-5554"
    assert summary.model == "Pixel 7 (Emulator)"
    assert summary.is_emulator is True
    assert summary.target_type == "Android Virtual Device (AVD) / Emulator"
    assert summary.manufacturer == "Google"
    assert summary.android_release == "14"
    assert summary.sdk_version == 34

    # Memory
    mem = summary.memory_metrics
    assert mem["total_ram_mb"] is not None
    assert mem["total_ram_mb"] > 7000.0
    assert mem["used_ram_pct"] is not None

    # Processes
    assert len(summary.process_df) == 8
    assert len(summary.top_consumers_by_pss) > 0
    # Highest consumer should be system_server (482 MB PSS)
    assert summary.top_consumers_by_pss[0]["name"] == "system_server"

    # Aggregated PSS by importance
    assert "Foreground" in summary.pss_by_importance
    assert summary.pss_by_importance["Foreground"] > 0


def test_android_analyzer_rejects_simulation_dataset():
    sim_path = EXAMPLES_DIR / "experiments" / "normal_comparison.json"
    sim_ds = load_experiment_file(sim_path)

    with pytest.raises(ProvenanceError):
        AndroidAnalyzer.analyze_snapshot(sim_ds)
