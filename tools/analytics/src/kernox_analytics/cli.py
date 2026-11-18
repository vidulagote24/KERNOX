"""Command-Line Interface (CLI) for KERNOX 2.0 Analytics & Visualization.

Provides command entrypoints:
- validate : Validate schemas and data provenance
- analyze  : Generate comprehensive policy analysis and charts for an experiment run
- compare  : Multi-scenario or multi-policy comparative evaluation
- android  : Real-world Android observation snapshot visualization
- report   : Full report generation workflow
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List, Optional

from .android_analysis import AndroidAnalyzer
from .experiment_analysis import ExperimentAnalyzer
from .loader import (
    DataDomain,
    Dataset,
    load_android_snapshot,
    load_experiment_directory,
    load_experiment_file,
)
from .report_generator import ReportGenerator
from .validators import ProvenanceError, ValidationError
from .visualizations import ChartGenerator


def create_parser() -> argparse.ArgumentParser:
    """Builds and configures the top-level argument parser."""
    parser = argparse.ArgumentParser(
        prog="python -m kernox_analytics.cli",
        description="KERNOX 2.0: Resource Intelligence Analytics & Visualization CLI",
    )
    subparsers = parser.add_subparsers(dest="subcommand", help="Available subcommands")

    # 1. validate
    val_p = subparsers.add_parser("validate", help="Validate schema and provenance of an experiment or snapshot file")
    val_p.add_argument("--input", "-i", required=True, type=str, help="Path to input file or directory")

    # 2. analyze
    ana_p = subparsers.add_parser("analyze", help="Analyze an experiment comparison dataset and generate charts")
    ana_p.add_argument("--input", "-i", required=True, type=str, help="Path to experiment JSON/CSV comparison file")
    ana_p.add_argument("--output", "-o", default="tools/analytics/output", type=str, help="Output directory")

    # 3. compare
    cmp_p = subparsers.add_parser("compare", help="Compare policies across multiple scenarios or directories")
    cmp_p.add_argument("--input", "-i", required=True, type=str, help="Path to directory containing multiple experiment files")
    cmp_p.add_argument("--output", "-o", default="tools/analytics/output", type=str, help="Output directory")

    # 4. android
    and_p = subparsers.add_parser("android", help="Analyze real-world Android ADB observation snapshot")
    and_p.add_argument("--input", "-i", required=True, type=str, help="Path to Android snapshot JSON file")
    and_p.add_argument("--output", "-o", default="tools/analytics/output", type=str, help="Output directory")

    # 5. report
    rep_p = subparsers.add_parser("report", help="Generate full analytical report (Markdown + PNG visualizations)")
    rep_p.add_argument("--input", "-i", required=True, type=str, help="Path to input file or directory")
    rep_p.add_argument("--output", "-o", default="tools/analytics/output", type=str, help="Output directory")

    return parser


def handle_validate(args: argparse.Namespace) -> int:
    """Validates schema and provenance for single file or directory."""
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Path does not exist: {input_path}", file=sys.stderr)
        return 1

    targets: List[Path] = []
    if input_path.is_dir():
        targets = sorted(list(input_path.glob("*.json")) + list(input_path.glob("*.csv")))
        if not targets:
            print(f"Error: No JSON or CSV files discovered in directory '{input_path}'", file=sys.stderr)
            return 1
    else:
        targets = [input_path]

    success_count = 0
    fail_count = 0

    print(f"Validating {len(targets)} file(s)...")
    for t in targets:
        try:
            # Check for Android snapshot
            is_android = False
            with open(t, "r", encoding="utf-8") as f:
                header = f.read(256)
                if '"source": "android_adb_observation"' in header or '"android_adb_observation"' in header:
                    is_android = True

            if is_android:
                ds = load_android_snapshot(t)
            else:
                ds = load_experiment_file(t)

            print(f"  [OK] {t.name} (Domain: {ds.domain.value}, Type: {ds.data_type})")
            success_count += 1
        except Exception as err:
            print(f"  [FAIL] {t.name}: {err}", file=sys.stderr)
            fail_count += 1

    print(f"\nValidation Summary: {success_count} passed, {fail_count} failed.")
    return 0 if fail_count == 0 else 1


def handle_analyze(args: argparse.Namespace) -> int:
    """Runs single experiment comparison analysis and generates core charts."""
    input_path = Path(args.input)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        ds = load_experiment_file(input_path)
    except Exception as err:
        print(f"Error loading experiment file: {err}", file=sys.stderr)
        return 1

    try:
        summary = ExperimentAnalyzer.analyze_comparison(ds)
    except Exception as err:
        print(f"Error analyzing comparison: {err}", file=sys.stderr)
        return 1

    print(f"Analyzing scenario '{summary.scenario_name}' (Policies: {', '.join(summary.policies)})...")
    charts = ChartGenerator(output_dir=output_dir)

    c1 = charts.plot_policy_comparison(summary)
    c2 = charts.plot_process_survival(summary)
    c3 = charts.plot_memory_pressure(summary)
    c4 = charts.plot_execution_performance(summary)

    chart_map = {
        "Policy Comparison": c1,
        "Process Survival Breakdown": c2,
        "Memory Pressure Analysis": c3,
        "Execution Performance": c4,
    }

    rep_gen = ReportGenerator(output_dir=output_dir)
    rep_path = rep_gen.generate_experiment_report(summary, chart_map)

    print(f"Analysis complete.")
    print(f"  Report: {rep_path}")
    print(f"  Charts generated in: {output_dir}")
    return 0


def handle_compare(args: argparse.Namespace) -> int:
    """Runs cross-scenario comparison over a directory of experiment outputs."""
    input_path = Path(args.input)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        if input_path.is_dir():
            datasets = load_experiment_directory(input_path)
        else:
            datasets = [load_experiment_file(input_path)]
    except Exception as err:
        print(f"Error loading datasets for comparison: {err}", file=sys.stderr)
        return 1

    try:
        scenario_summary = ExperimentAnalyzer.analyze_multi_scenario(datasets)
    except Exception as err:
        print(f"Error performing scenario comparison: {err}", file=sys.stderr)
        return 1

    print(f"Comparing {len(scenario_summary.scenarios)} scenarios: {', '.join(scenario_summary.scenarios)}...")
    charts = ChartGenerator(output_dir=output_dir)
    c_scen = charts.plot_scenario_comparison(scenario_summary)

    print(f"Scenario comparison complete.")
    print(f"  Chart generated: {c_scen}")
    return 0


def handle_android(args: argparse.Namespace) -> int:
    """Runs Android snapshot telemetry analysis and generates memory charts."""
    input_path = Path(args.input)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        ds = load_android_snapshot(input_path)
    except Exception as err:
        print(f"Error loading Android snapshot: {err}", file=sys.stderr)
        return 1

    try:
        summary = AndroidAnalyzer.analyze_snapshot(ds)
    except Exception as err:
        print(f"Error analyzing Android snapshot: {err}", file=sys.stderr)
        return 1

    print(f"Analyzing Android device '{summary.model}' ({summary.serial})...")
    charts = ChartGenerator(output_dir=output_dir)
    c1 = charts.plot_android_device_memory(summary)
    c2 = charts.plot_android_process_memory(summary)

    chart_map = {
        "Android Device Memory": c1,
        "Android Process Memory (PSS)": c2,
    }

    rep_gen = ReportGenerator(output_dir=output_dir)
    rep_path = rep_gen.generate_android_report(summary, chart_map)

    print(f"Android observation analysis complete.")
    print(f"  Report: {rep_path}")
    print(f"  Charts generated in: {output_dir / 'android'}")
    return 0


def handle_report(args: argparse.Namespace) -> int:
    """Automated pipeline dispatching either experiment analysis or android observation."""
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input path does not exist: {input_path}", file=sys.stderr)
        return 1

    if input_path.is_file():
        is_android = False
        with open(input_path, "r", encoding="utf-8") as f:
            header = f.read(256)
            if '"source": "android_adb_observation"' in header or '"android_adb_observation"' in header:
                is_android = True

        if is_android:
            return handle_android(args)
        else:
            return handle_analyze(args)

    elif input_path.is_dir():
        # Directory: perform comparison and analysis
        ret1 = handle_compare(args)
        if ret1 != 0:
            return ret1
        # Also analyze the first comparison file found
        for p in input_path.glob("*.json"):
            with open(p, "r", encoding="utf-8") as f:
                header = f.read(256)
                if '"policy_results"' in header:
                    args.input = str(p)
                    return handle_analyze(args)
        return 0

    return 1


def main(argv: Optional[List[str]] = None) -> int:
    """CLI entrypoint."""
    parser = create_parser()
    args = parser.parse_args(argv)

    if not args.subcommand:
        parser.print_help()
        return 0

    handlers = {
        "validate": handle_validate,
        "analyze": handle_analyze,
        "compare": handle_compare,
        "android": handle_android,
        "report": handle_report,
    }

    handler = handlers.get(args.subcommand)
    if not handler:
        parser.print_help()
        return 1

    return handler(args)


if __name__ == "__main__":
    sys.exit(main())
