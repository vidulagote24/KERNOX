"""Unit tests for kernox_analytics.loader module."""

from pathlib import Path
import json
import pytest

from kernox_analytics.loader import (
    DataDomain,
    Dataset,
    load_android_snapshot,
    load_experiment_directory,
    load_experiment_file,
)
from kernox_analytics.validators import ProvenanceError, ValidationError

EXAMPLES_DIR = Path(__file__).resolve().parent.parent / "examples"
FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"


def test_load_experiment_comparison_json():
    path = EXAMPLES_DIR / "experiments" / "normal_comparison.json"
    ds = load_experiment_file(path)

    assert ds.domain == DataDomain.SIMULATION
    assert ds.data_type == "comparison"
    assert ds.metadata["scenario"] == "Normal"
    assert ds.metadata["policy_count"] == 4
    df = ds.to_dataframe()
    assert len(df) == 4
    assert set(df["policy"]) == {"FIFO", "LRU", "Priority", "ImportanceAware"}


def test_load_experiment_single_result_json():
    path = EXAMPLES_DIR / "experiments" / "single_policy_importance.json"
    ds = load_experiment_file(path)

    assert ds.domain == DataDomain.SIMULATION
    assert ds.data_type == "single_result"
    assert ds.metadata["policy"] == "ImportanceAware"
    df = ds.to_dataframe()
    assert len(df) == 1
    assert df["policy"].iloc[0] == "ImportanceAware"


def test_load_experiment_comparison_csv():
    path = EXAMPLES_DIR / "experiments" / "normal_comparison.csv"
    ds = load_experiment_file(path)

    assert ds.domain == DataDomain.SIMULATION
    assert ds.data_type == "comparison_table"
    assert ds.metadata["record_count"] == 4
    df = ds.to_dataframe()
    assert len(df) == 4


def test_load_android_snapshot():
    path = FIXTURES_DIR / "android_snapshot_fixture.json"
    ds = load_android_snapshot(path)

    assert ds.domain == DataDomain.ANDROID
    assert ds.data_type == "android_snapshot"
    assert ds.metadata["serial"] == "emulator-5554"
    assert ds.metadata["model"] == "Pixel 7 (Emulator)"
    assert ds.metadata.get("is_emulator") is True
    assert ds.metadata["process_count"] == 8


def test_load_experiment_directory():
    exp_dir = EXAMPLES_DIR / "experiments"
    datasets = load_experiment_directory(exp_dir)

    assert len(datasets) >= 3
    for ds in datasets:
        assert ds.domain == DataDomain.SIMULATION


def test_load_nonexistent_file():
    with pytest.raises(FileNotFoundError):
        load_experiment_file("/nonexistent/path/data.json")

    with pytest.raises(FileNotFoundError):
        load_android_snapshot("/nonexistent/path/snap.json")


def test_load_invalid_json(tmp_path):
    bad_file = tmp_path / "bad.json"
    bad_file.write_text("{ unclosed json", encoding="utf-8")

    with pytest.raises(ValidationError):
        load_experiment_file(bad_file)


def test_load_unsupported_extension(tmp_path):
    unsupported = tmp_path / "data.xml"
    unsupported.write_text("<data></data>", encoding="utf-8")

    with pytest.raises(ValidationError):
        load_experiment_file(unsupported)


def test_provenance_separation_experiment_loader_rejects_android(tmp_path):
    android_file = FIXTURES_DIR / "android_snapshot_fixture.json"
    with pytest.raises(ProvenanceError):
        load_experiment_file(android_file)
