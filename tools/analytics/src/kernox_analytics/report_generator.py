"""Report generator producing publication-ready Markdown summaries with embedded chart references.

Assembles comprehensive reports detailing:
- Dataset provenance and ingestion metadata
- Workload parameters and hardware capacities
- Tabular policy comparisons and tier survival breakdowns
- Key analytical insights and policy rankings
- Critical scientific boundaries and limitations
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional, Union

from .android_analysis import AndroidSnapshotSummary
from .experiment_analysis import PolicyComparisonSummary, ScenarioComparisonSummary


class ReportGenerator:
    """Assembles and writes standardized Markdown reports."""

    def __init__(self, output_dir: Union[str, Path] = "tools/analytics/output"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def generate_experiment_report(
        self,
        summary: PolicyComparisonSummary,
        chart_paths: Dict[str, Path],
        output_filename: str = "experiment_summary.md",
    ) -> Path:
        """Generates a complete Markdown analytical report for a policy comparison."""
        out_path = self.output_dir / output_filename
        df = summary.dataframe

        lines: List[str] = [
            f"# KERNOX 2.0 — Experiment Analytical Report",
            f"## Scenario: {summary.scenario_name} (Seed: {summary.seed})",
            "",
            "### 1. Data Provenance & Experiment Metadata",
            "",
            "| Parameter | Value |",
            "|---|---|",
            "| **Data Domain** | `kernox_simulation` |",
            f"| **Scenario Name** | `{summary.scenario_name}` |",
            f"| **Random Seed** | `{summary.seed}` |",
            f"| **Workload Fingerprint** | `{summary.workload_fingerprint}` |",
            f"| **Total Memory Capacity** | {round(summary.capacity_bytes / (1024**2))} MB ({summary.capacity_bytes} bytes) |",
            f"| **Active Policies Evaluated** | {', '.join(summary.policies)} |",
            "",
            "### 2. Reclamation Policy Comparison Summary",
            "",
            "| Policy | Reclaimed (MB) | Survival Rate (%) | FG Survival (%) | Peak Used (MB) | Final Used (MB) | Wall-Clock Time (μs) |",
            "|---|:---:|:---:|:---:|:---:|:---:|:---:|",
        ]

        for _, row in df.iterrows():
            pol = str(row["policy"])
            rec_mb = round(float(row["reclaimed_bytes"]) / (1024**2), 1)
            surv_pct = round(float(row["survival_rate"]) * 100.0, 1)
            fg_pct = round(float(row["fg_survival_rate"]) * 100.0, 1)
            peak_mb = round(float(row["peak_used_bytes"]) / (1024**2), 1)
            final_mb = round(float(row["final_used_bytes"]) / (1024**2), 1)
            time_us = int(row["wall_clock_us"])
            lines.append(
                f"| **{pol}** | {rec_mb} MB | {surv_pct}% | {fg_pct}% | {peak_mb} MB | {final_mb} MB | {time_us} μs |"
            )

        lines.extend([
            "",
            "### 3. Process Survival by Importance Tier",
            "",
            "| Policy | Foreground | Visible | Service | Background | Cached |",
            "|---|:---:|:---:|:---:|:---:|:---:|",
        ])

        for _, row in df.iterrows():
            lines.append(
                f"| **{row['policy']}** | "
                f"{round(float(row['fg_survival_rate'])*100, 1)}% | "
                f"{round(float(row['vis_survival_rate'])*100, 1)}% | "
                f"{round(float(row['svc_survival_rate'])*100, 1)}% | "
                f"{round(float(row['bg_survival_rate'])*100, 1)}% | "
                f"{round(float(row['cached_survival_rate'])*100, 1)}% |"
            )

        lines.extend([
            "",
            "### 4. Key Analytical Insights",
            "",
        ])
        for obs in summary.key_observations:
            lines.append(f"- {obs}")

        # Resolve rankings supporting tied metric values
        ranking_survival = summary.ranking_strings.get(
            "by_overall_survival",
            " > ".join(summary.rankings.get("by_overall_survival", [])),
        )
        ranking_fg = summary.ranking_strings.get(
            "by_foreground_protection",
            " > ".join(summary.rankings.get("by_foreground_protection", [])),
        )
        ranking_reclaimed = summary.ranking_strings.get(
            "by_memory_reclaimed",
            " > ".join(summary.rankings.get("by_memory_reclaimed", [])),
        )
        latency_bullets: List[str] = []
        for _, row in df.iterrows():
            pol = str(row["policy"])
            time_us = int(row["wall_clock_us"])
            latency_bullets.append(f"  - {pol}: {time_us} μs")

        lines.extend([
            "",
            "### 5. Policy Rankings",
            "",
            f"- **Overall Process Survival:** {ranking_survival}",
            f"- **Foreground Protection:** {ranking_fg}",
            f"- **Memory Reclaimed:** {ranking_reclaimed}",
            "- **Execution Speed (Wall-Clock):** Single-run host latency observations, not an algorithmic performance ranking:",
        ])
        lines.extend(latency_bullets)
        lines.append(
            "  *The first policy execution recorded substantially higher host latency. Cold-start or initialization overhead may have contributed, but this single measurement cannot establish the cause.*"
        )
        lines.extend([
            "",
            "### 6. Visualizations",
            "",
        ])

        # Link embedded images
        for chart_name, path in chart_paths.items():
            rel_path = path.name if path.parent == self.output_dir else f"{path.parent.name}/{path.name}"
            lines.append(f"#### {chart_name.replace('_', ' ').title()}")
            lines.append(f"![{chart_name}]({rel_path})")
            lines.append("")

        lines.extend([
            "### 7. Scientific Limitations & Boundaries",
            "",
            "1. **Synthetic Deterministic Workload**: Results reflect the synthetic workload generator for this specific scenario and seed. They do not constitute a universal benchmark across arbitrary application behaviors.",
            "2. **Simulation vs Production Kernel**: Memory pressure detection and process reclamation run in a deterministic C++ simulation model. Real Linux/Android kernels include kernel-level slab reclamation, swap compaction, and zRAM dynamics that are abstracted here.",
            "3. **Non-Causal Observations**: Rankings indicate relative performance within this evaluation harness and do not imply universal algorithm superiority.",
            "4. **Observed Latency vs Asymptotic Complexity**: Measured wall-clock execution time reflects a single-run host observation under this specific workload and environment. The first policy execution recorded substantially higher host latency. Cold-start or initialization overhead may have contributed, but this single measurement cannot establish the cause. The benchmark harness does not perform multi-iteration statistical sampling, and single-run host measurements must not be interpreted as algorithmic performance rankings or asymptotic complexity proofs.",
            "5. **Configured Protection Guarantees**: Only ImportanceAware is architecturally configured with a foreground protection guarantee. Arrival-order (FIFO) and recency-order (LRU) policies do not promise tier immunity, and their eviction of foreground processes reflects their algorithmic design rather than an invariant violation.",
            "",
            "---",
            "*Report generated automatically by KERNOX 2.0 Analytics Layer.*",
        ])

        with open(out_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

        return out_path

    def generate_android_report(
        self,
        summary: AndroidSnapshotSummary,
        chart_paths: Dict[str, Path],
        output_filename: str = "android_summary.md",
    ) -> Path:
        """Generates an analytical Markdown report for a real Android ADB observation snapshot."""
        out_path = self.output_dir / output_filename
        mem = summary.memory_metrics

        lines: List[str] = [
            f"# KERNOX 2.0 — Android Observation Report",
            f"## Device: {summary.model} ({summary.serial})",
            "",
            "### 1. Data Provenance & Device Telemetry",
            "",
            "| Parameter | Value |",
            "|---|---|",
            "| **Data Domain** | `android_adb_observation` |",
            f"| **Target Environment** | `{summary.target_type}` |",
            f"| **Device Model** | `{summary.manufacturer} {summary.model}` |",
            f"| **Device Serial** | `{summary.serial}` |",
            f"| **Android OS Version** | Android {summary.android_release} (API {summary.sdk_version or 'N/A'}) |",
            f"| **Timestamp (Epoch ms)** | `{summary.timestamp_epoch_ms}` |",
            f"| **Monitored Processes** | {len(summary.process_df)} |",
            "",
            "### 2. Device Memory Composition",
            "",
            "| Metric | Bytes | Megabytes (MB) | Share of Total RAM (%) |",
            "|---|:---:|:---:|:---:|",
            f"| **Total RAM** | {mem.get('total_ram_bytes', 'N/A')} | {mem.get('total_ram_mb', 'N/A')} MB | 100.0% |",
            f"| **Used RAM** | {mem.get('used_ram_bytes', 'N/A')} | {mem.get('used_ram_mb', 'N/A')} MB | {mem.get('used_ram_pct', 'N/A')}% |",
            f"| **Available RAM** | {mem.get('available_ram_bytes', 'N/A')} | {mem.get('available_ram_mb', 'N/A')} MB | {mem.get('available_ram_pct', 'N/A')}% |",
            f"| **Free RAM** | {mem.get('free_ram_bytes', 'N/A')} | {mem.get('free_ram_mb', 'N/A')} MB | {mem.get('free_ram_pct', 'N/A')}% |",
            f"| **Cached Memory** | {mem.get('cached_bytes', 'N/A')} | {mem.get('cached_mb', 'N/A')} MB | {mem.get('cached_pct', 'N/A')}% |",
            f"| **Buffers** | {mem.get('buffers_bytes', 'N/A')} | {mem.get('buffers_mb', 'N/A')} MB | {mem.get('buffers_pct', 'N/A')}% |",
            f"| **Swap Used** | {mem.get('swap_used_bytes', 'N/A')} | {mem.get('swap_used_mb', 'N/A')} MB | {mem.get('swap_used_pct', 'N/A')}% |",
            "",
            "> **Accounting Semantics Note:** Available RAM is not an independent mutually exclusive partition. It includes Free RAM plus reclaimable Cached RAM (page cache) and reclaimable kernel slab. Free RAM and Available RAM must not be added together.",
            "",
            "### 3. Top Process Memory Consumers (PSS)",
            "",
            "| PID | Process Name | PSS (MB) | Importance Tier | OOM Adj Score |",
            "|---|---|:---:|:---:|:---:|",
        ]

        for p in summary.top_consumers_by_pss:
            lines.append(
                f"| `{p['pid']}` | `{p['name']}` | {p['pss_mb']} MB | {p['importance']} | {p['oom_score_adj'] if p['oom_score_adj'] is not None else 'N/A'} |"
            )

        lines.extend([
            "",
            "### 4. Observations & Device State",
            "",
        ])
        for obs in summary.key_observations:
            lines.append(f"- {obs}")

        lines.extend([
            "",
            "### 5. Visualizations",
            "",
        ])
        for chart_name, path in chart_paths.items():
            rel_path = path.name if path.parent == self.output_dir else f"{path.parent.name}/{path.name}"
            lines.append(f"#### {chart_name.replace('_', ' ').title()}")
            lines.append(f"![{chart_name}]({rel_path})")
            lines.append("")

        lines.extend([
            "### 6. Safety & Provenance Statement",
            "",
            "- **Target Environment:** " + (
                "Android Virtual Device (AVD) / Emulator. Telemetry represents an emulated virtual machine instance, not physical hardware."
                if summary.is_emulator else
                "Physical Android Device. Telemetry reflects actual hardware sensors and physical DRAM."
            ),
            "- **Strictly Passive Observation:** Telemetry was captured non-invasively via read-only ADB shell commands. No device properties or processes were altered.",
            "- **Data Domain Isolation:** This observation report is strictly partitioned under `android_adb_observation` and is completely separate from KERNOX simulation benchmark data.",
            "",
            "---",
            "*Report generated automatically by KERNOX 2.0 Analytics Layer.*",
        ])

        with open(out_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

        return out_path
