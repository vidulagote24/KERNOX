"""Publication-quality visualization generator using Matplotlib.

Produces figures for:
- Policy comparisons (reclaimed memory, peak/final utilization, survival rates)
- Process survival breakdowns by importance tier
- Memory pressure analysis and threshold interactions
- Cross-scenario evaluations
- Execution performance (logical simulation ticks vs host wall-clock time)
- Real Android device memory composition and process PSS distributions
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union
import matplotlib
matplotlib.use("Agg")  # Non-interactive headless backend
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from .android_analysis import AndroidSnapshotSummary
from .experiment_analysis import PolicyComparisonSummary, ScenarioComparisonSummary

# Authoritative theme colors adhering to project config
POLICY_COLORS = {
    "FIFO": "#4C72B0",
    "LRU": "#55A868",
    "Priority": "#C44E52",
    "ImportanceAware": "#8172B3",
    "Importance-Aware": "#8172B3",
}

TIER_COLORS = {
    "Foreground": "#2CA02C",
    "Visible": "#1F77B4",
    "Service": "#FF7F0E",
    "Background": "#D62728",
    "Cached": "#9467BD",
}


class ChartGenerator:
    """Generates and exports standardized KERNOX analytical charts."""

    def __init__(self, output_dir: Union[str, Path] = "tools/analytics/output", dpi: int = 300):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.dpi = dpi

    def _get_policy_color(self, policy: str) -> str:
        return POLICY_COLORS.get(policy, "#333333")

    # =========================================================================
    # VISUALIZATION A: Policy Comparison (2x2 Panel)
    # =========================================================================
    def plot_policy_comparison(
        self, summary: PolicyComparisonSummary, filename: str = "policy_comparison.png"
    ) -> Path:
        """Generates a 4-panel comparison of memory reclaimed, utilization, and survival rates."""
        df = summary.dataframe
        policies = list(df["policy"])
        colors = [self._get_policy_color(p) for p in policies]

        fig, axs = plt.subplots(2, 2, figsize=(11, 8.5))
        fig.suptitle(
            f"KERNOX 2.0: Reclamation Policy Comparison\n"
            f"Scenario: {summary.scenario_name} | Seed: {summary.seed} | Capacity: {round(summary.capacity_bytes / (1024**2))} MB",
            fontsize=13, fontweight="bold", y=0.98
        )

        # Panel 1: Memory Reclaimed
        reclaimed_mb = [r / (1024**2) for r in df["reclaimed_bytes"]]
        bars1 = axs[0, 0].bar(policies, reclaimed_mb, color=colors, width=0.55)
        axs[0, 0].set_title("Total Memory Reclaimed", fontsize=11, fontweight="semibold")
        axs[0, 0].set_ylabel("Reclaimed Memory (MB)")
        axs[0, 0].grid(axis="y", linestyle="--", alpha=0.5)
        for bar in bars1:
            h = bar.get_height()
            axs[0, 0].annotate(f"{h:.1f} MB", xy=(bar.get_x() + bar.get_width() / 2, h),
                               xytext=(0, 3), textcoords="offset points", ha="center", va="bottom", fontsize=9)

        # Panel 2: Peak vs Final Memory Used
        x = np.arange(len(policies))
        width = 0.35
        peak_mb = [p / (1024**2) for p in df["peak_used_bytes"]]
        final_mb = [f / (1024**2) for f in df["final_used_bytes"]]
        cap_mb = summary.capacity_bytes / (1024**2)

        axs[0, 1].bar(x - width/2, peak_mb, width, label="Peak Used", color="#C44E52", alpha=0.85)
        axs[0, 1].bar(x + width/2, final_mb, width, label="Final Used", color="#4C72B0", alpha=0.85)
        axs[0, 1].axhline(y=cap_mb, color="red", linestyle="--", label=f"Capacity ({int(cap_mb)} MB)")
        axs[0, 1].set_title("Peak vs Final Memory Footprint", fontsize=11, fontweight="semibold")
        axs[0, 1].set_ylabel("Memory (MB)")
        axs[0, 1].set_xticks(x)
        axs[0, 1].set_xticklabels(policies)
        axs[0, 1].legend(loc="lower right", fontsize=8)
        axs[0, 1].grid(axis="y", linestyle="--", alpha=0.5)

        # Panel 3: Overall Process Survival Rate
        survival_pct = [s * 100.0 for s in df["survival_rate"]]
        bars3 = axs[1, 0].bar(policies, survival_pct, color=colors, width=0.55)
        axs[1, 0].set_title("Overall Process Survival Rate", fontsize=11, fontweight="semibold")
        axs[1, 0].set_ylabel("Survival Rate (%)")
        axs[1, 0].set_ylim(0, 115)
        axs[1, 0].grid(axis="y", linestyle="--", alpha=0.5)
        for bar in bars3:
            h = bar.get_height()
            axs[1, 0].annotate(f"{h:.1f}%", xy=(bar.get_x() + bar.get_width() / 2, h),
                               xytext=(0, 3), textcoords="offset points", ha="center", va="bottom", fontsize=9)

        # Panel 4: Foreground Process Protection
        fg_pct = [f * 100.0 for f in df["fg_survival_rate"]]
        bars4 = axs[1, 1].bar(policies, fg_pct, color=colors, width=0.55)
        axs[1, 1].set_title("Foreground Process Protection", fontsize=11, fontweight="semibold")
        axs[1, 1].set_ylabel("Foreground Survival (%)")
        axs[1, 1].set_ylim(0, 115)
        axs[1, 1].axhline(y=100.0, color="green", linestyle=":", alpha=0.7, label="100% Target")
        axs[1, 1].grid(axis="y", linestyle="--", alpha=0.5)
        axs[1, 1].legend(loc="lower left", fontsize=8)
        for bar in bars4:
            h = bar.get_height()
            axs[1, 1].annotate(f"{h:.1f}%", xy=(bar.get_x() + bar.get_width() / 2, h),
                               xytext=(0, 3), textcoords="offset points", ha="center", va="bottom", fontsize=9)

        plt.tight_layout(rect=[0, 0.03, 1, 0.95])
        out_path = self.output_dir / filename
        plt.savefig(out_path, dpi=self.dpi)
        plt.close(fig)
        return out_path

    # =========================================================================
    # VISUALIZATION B: Process Survival Analysis by Tier
    # =========================================================================
    def plot_process_survival(
        self, summary: PolicyComparisonSummary, filename: str = "process_survival.png"
    ) -> Path:
        """Generates grouped bar chart showing survival rate by importance tier across policies."""
        df = summary.dataframe
        tiers = ["Foreground", "Visible", "Service", "Background", "Cached"]
        tier_cols = ["fg_survival_rate", "vis_survival_rate", "svc_survival_rate", "bg_survival_rate", "cached_survival_rate"]

        policies = list(df["policy"])
        n_policies = len(policies)
        n_tiers = len(tiers)

        fig, ax = plt.subplots(figsize=(10, 5.5))
        x = np.arange(n_tiers)
        bar_width = 0.8 / n_policies

        for i, policy in enumerate(policies):
            pol_row = df[df["policy"] == policy].iloc[0]
            rates = [float(pol_row[col]) * 100.0 for col in tier_cols]
            offset = (i - (n_policies - 1) / 2) * bar_width
            color = self._get_policy_color(policy)
            ax.bar(x + offset, rates, bar_width, label=policy, color=color, alpha=0.9)

        ax.set_title(
            f"Process Survival Rate by Importance Tier\n"
            f"Scenario: {summary.scenario_name} (Evaluation of Priority Inversion & Selective Protection)",
            fontsize=12, fontweight="bold"
        )
        ax.set_ylabel("Survival Rate (%)", fontsize=10)
        ax.set_xlabel("Process Importance Tier", fontsize=10)
        ax.set_xticks(x)
        ax.set_xticklabels(tiers, fontsize=10)
        ax.set_ylim(0, 115)
        ax.grid(axis="y", linestyle="--", alpha=0.5)
        ax.legend(title="Reclamation Policy", loc="upper right", framealpha=0.9)

        plt.tight_layout()
        out_path = self.output_dir / filename
        plt.savefig(out_path, dpi=self.dpi)
        plt.close(fig)
        return out_path

    # =========================================================================
    # VISUALIZATION C: Memory Pressure Analysis
    # =========================================================================
    def plot_memory_pressure(
        self,
        summary: PolicyComparisonSummary,
        filename: str = "memory_pressure.png",
        moderate_ratio: float = 0.75,
        critical_ratio: float = 0.90,
    ) -> Path:
        """Visualizes initial, peak, and final memory usage relative to capacity and pressure thresholds.

        Thresholds match Phase C engine definitions (default: Moderate=75%, Critical=90%).
        """
        df = summary.dataframe
        policies = list(df["policy"])
        cap_mb = summary.capacity_bytes / (1024**2)

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 5.2), gridspec_kw={"width_ratios": [2.5, 1]})
        fig.suptitle(f"Memory Pressure & Utilization Analysis ({summary.scenario_name})", fontsize=12, fontweight="bold")

        x = np.arange(len(policies))
        w = 0.25

        peak_mb = [p / (1024**2) for p in df["peak_used_bytes"]]
        final_mb = [f / (1024**2) for f in df["final_used_bytes"]]

        ax1.bar(x - w, [p - f for p, f in zip(peak_mb, final_mb)], w, bottom=final_mb,
                label="Reclaimed Memory Delta", color="#2CA02C", alpha=0.7)
        ax1.bar(x - w, final_mb, w, label="Final Used Memory", color="#4C72B0", alpha=0.9)
        ax1.bar(x, peak_mb, w, label="Peak Used Memory", color="#C44E52", alpha=0.85)
        ax1.axhline(y=cap_mb, color="red", linestyle="--", label=f"Total Capacity ({int(cap_mb)} MB)")
        ax1.axhline(y=cap_mb * critical_ratio, color="crimson", linestyle=":", label=f"Critical Threshold ({int(critical_ratio*100)}%)")
        ax1.axhline(y=cap_mb * moderate_ratio, color="darkorange", linestyle=":", label=f"Moderate Threshold ({int(moderate_ratio*100)}%)")

        ax1.set_ylabel("Memory (MB)")
        ax1.set_xticks(x)
        ax1.set_xticklabels(policies)
        ax1.legend(loc="lower left", fontsize=8)
        ax1.grid(axis="y", linestyle="--", alpha=0.5)

        # Ax2: Total Reclaimed by Policy
        rec_mb = [r / (1024**2) for r in df["reclaimed_bytes"]]
        colors = [self._get_policy_color(p) for p in policies]
        ax2.bar(policies, rec_mb, color=colors, width=0.5)
        ax2.set_title("Total Reclaimed (MB)", fontsize=10, fontweight="semibold")
        ax2.set_ylabel("MB")
        ax2.tick_params(axis="x", rotation=30)
        ax2.grid(axis="y", linestyle="--", alpha=0.5)

        plt.tight_layout()
        out_path = self.output_dir / filename
        plt.savefig(out_path, dpi=self.dpi)
        plt.close(fig)
        return out_path

    # =========================================================================
    # VISUALIZATION D: Scenario Comparison
    # =========================================================================
    def plot_scenario_comparison(
        self, summary: ScenarioComparisonSummary, filename: str = "scenario_comparison.png"
    ) -> Path:
        """Compares reclamation policy survival and memory metrics across multiple scenarios."""
        df = summary.dataframe
        scenarios = summary.scenarios
        policies = list(df["policy"].unique())

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 6))
        fig.suptitle("Cross-Scenario Policy Performance Comparison", fontsize=13, fontweight="bold")

        x = np.arange(len(scenarios))
        w = 0.8 / len(policies)

        # Subplot 1: Survival Rate by Scenario
        for i, pol in enumerate(policies):
            pol_df = df[df["policy"] == pol]
            rates = []
            for sc in scenarios:
                sub = pol_df[pol_df["scenario"] == sc]
                rates.append(float(sub["survival_rate"].iloc[0]) * 100.0 if not sub.empty else 0.0)
            offset = (i - (len(policies) - 1) / 2) * w
            color = self._get_policy_color(pol)
            ax1.bar(x + offset, rates, w, label=pol, color=color, alpha=0.9)

        ax1.set_title("Overall Survival Rate by Scenario", fontsize=11, fontweight="semibold")
        ax1.set_ylabel("Survival Rate (%)")
        ax1.set_xticks(x)
        ax1.set_xticklabels(scenarios, rotation=25, ha="right", fontsize=9)
        ax1.set_ylim(0, 115)
        ax1.legend(loc="lower left", fontsize=8)
        ax1.grid(axis="y", linestyle="--", alpha=0.5)

        # Subplot 2: Memory Reclaimed by Scenario
        for i, pol in enumerate(policies):
            pol_df = df[df["policy"] == pol]
            reclaimed = []
            for sc in scenarios:
                sub = pol_df[pol_df["scenario"] == sc]
                reclaimed.append(float(sub["reclaimed_bytes"].iloc[0]) / (1024**2) if not sub.empty else 0.0)
            offset = (i - (len(policies) - 1) / 2) * w
            color = self._get_policy_color(pol)
            ax2.bar(x + offset, reclaimed, w, label=pol, color=color, alpha=0.9)

        ax2.set_title("Total Memory Reclaimed by Scenario", fontsize=11, fontweight="semibold")
        ax2.set_ylabel("Reclaimed Memory (MB)")
        ax2.set_xticks(x)
        ax2.set_xticklabels(scenarios, rotation=25, ha="right", fontsize=9)
        ax2.grid(axis="y", linestyle="--", alpha=0.5)

        plt.tight_layout()
        out_path = self.output_dir / filename
        plt.savefig(out_path, dpi=self.dpi)
        plt.close(fig)
        return out_path

    # =========================================================================
    # VISUALIZATION E: Execution Performance
    # =========================================================================
    def plot_execution_performance(
        self, summary: PolicyComparisonSummary, filename: str = "execution_performance.png"
    ) -> Path:
        """Visualizes observed host wall-clock elapsed time (microseconds) across policies.

        Note: Reflects empirical benchmark host runtime under this specific workload configuration.
        It does not establish or prove theoretical asymptotic algorithmic complexity.
        """
        df = summary.dataframe
        policies = list(df["policy"])
        colors = [self._get_policy_color(p) for p in policies]

        fig, ax1 = plt.subplots(figsize=(8.5, 5.0))
        fig.suptitle("Observed Benchmark Wall-Clock Elapsed Time", fontsize=12, fontweight="bold")

        x = np.arange(len(policies))
        wall_clock = df["wall_clock_us"].tolist()
        ticks = df["simulated_ticks"].tolist()

        w = 0.45
        bars = ax1.bar(x, wall_clock, w, color=colors, alpha=0.85)
        ax1.set_ylabel("Wall-Clock Elapsed Time (microseconds, $\\mu s$)", color="black", fontsize=10)
        ax1.set_xticks(x)
        ax1.set_xticklabels(policies, fontsize=10)
        ax1.grid(axis="y", linestyle="--", alpha=0.5)

        for bar in bars:
            h = bar.get_height()
            ax1.annotate(f"{int(h)} $\\mu s$", xy=(bar.get_x() + bar.get_width() / 2, h),
                         xytext=(0, 3), textcoords="offset points", ha="center", va="bottom", fontsize=9)

        # Explicit caption distinguishing observed benchmark time from theoretical asymptotic complexity
        ax1.set_xlabel(
            f"All policies evaluated for exact identical logical duration: {ticks[0]} simulated ticks.\n"
            f"Measured wall-clock latency reflects host implementation overhead during this benchmark run.\n"
            f"(Does not establish asymptotic algorithmic complexity)",
            fontsize=8.5, fontstyle="italic"
        )

        plt.tight_layout()
        out_path = self.output_dir / filename
        plt.savefig(out_path, dpi=self.dpi)
        plt.close(fig)
        return out_path

    # =========================================================================
    # ANDROID VISUALIZATIONS
    # =========================================================================
    def plot_android_device_memory(
        self, summary: AndroidSnapshotSummary, filename: str = "android/device_memory.png"
    ) -> Path:
        """Renders real Android memory telemetry as separate non-additive measurements against Total RAM."""
        out_dir = self.output_dir / "android"
        out_dir.mkdir(parents=True, exist_ok=True)
        out_path = out_dir / Path(filename).name

        mem = summary.memory_metrics
        total_mb = mem.get("total_ram_mb", 0)
        used_mb = mem.get("used_ram_mb", 0)
        avail_mb = mem.get("available_ram_mb", 0)
        free_mb = mem.get("free_ram_mb", 0)
        cached_mb = mem.get("cached_mb", 0)
        swap_used_mb = mem.get("swap_used_mb", 0)

        fig, ax = plt.subplots(figsize=(9.5, 5.5))
        fig.suptitle(
            f"Android Device Memory Telemetry Measurements\n"
            f"Target: {summary.target_type} ({summary.model}, {summary.serial}) | OS: Android {summary.android_release}\n"
            f"[Source: {summary.provenance}]",
            fontsize=11, fontweight="bold"
        )

        categories = ["Used RAM", "Available RAM", "Free RAM", "Cached Memory", "Swap Used"]
        values = [used_mb, avail_mb, free_mb, cached_mb, swap_used_mb]
        colors = ["#C44E52", "#4C72B0", "#55A868", "#9467BD", "#8C564B"]

        bars = ax.bar(categories, values, color=colors, width=0.52)
        ax.set_ylabel("Memory (MB)", fontsize=10)
        ax.grid(axis="y", linestyle="--", alpha=0.5)
        if total_mb:
            ax.axhline(y=total_mb, color="red", linestyle="--", label=f"Total Physical RAM ({total_mb} MB)")
            ax.legend(loc="upper right", fontsize=8.5)

        for bar in bars:
            h = bar.get_height()
            pct = (h / total_mb * 100.0) if total_mb and total_mb > 0 else 0.0
            ax.annotate(f"{h:.1f} MB\n({pct:.1f}%)", xy=(bar.get_x() + bar.get_width() / 2, h),
                        xytext=(0, 3), textcoords="offset points", ha="center", va="bottom", fontsize=8.5)

        ax.set_xlabel(
            "Note: Measurements are non-additive. Available RAM includes Free RAM plus reclaimable Cached RAM.\n"
            "Categories overlap and must not be summed together into a single total.",
            fontsize=8.5, fontstyle="italic"
        )

        plt.tight_layout()
        plt.savefig(out_path, dpi=self.dpi)
        plt.close(fig)
        return out_path

    def plot_android_process_memory(
        self, summary: AndroidSnapshotSummary, filename: str = "android/process_memory.png"
    ) -> Path:
        """Renders top Android processes ranked by Proportional Set Size (PSS)."""
        out_dir = self.output_dir / "android"
        out_dir.mkdir(parents=True, exist_ok=True)
        out_path = out_dir / Path(filename).name

        top_procs = summary.top_consumers_by_pss
        if not top_procs:
            # Fallback placeholder if no processes observed
            fig, ax = plt.subplots(figsize=(8, 4))
            ax.text(0.5, 0.5, "No observed process memory records available", ha="center", va="center")
            plt.savefig(out_path, dpi=self.dpi)
            plt.close(fig)
            return out_path

        names = [p["name"] for p in reversed(top_procs)]
        pss_vals = [p["pss_mb"] for p in reversed(top_procs)]
        tiers = [p["importance"] for p in reversed(top_procs)]
        colors = [TIER_COLORS.get(t, "#4C72B0") for t in tiers]

        fig, ax = plt.subplots(figsize=(10, 5.5))
        fig.suptitle(
            f"Android Process Memory Footprint (Top Monitored Processes)\n"
            f"Device: {summary.model} | [Source: Real Android ADB Observation]",
            fontsize=11, fontweight="bold"
        )

        bars = ax.barh(names, pss_vals, color=colors, height=0.6)
        ax.set_xlabel("Proportional Set Size (PSS, MB)", fontsize=10)
        ax.grid(axis="x", linestyle="--", alpha=0.5)

        for bar in bars:
            w = bar.get_width()
            ax.annotate(f"{w:.1f} MB", xy=(w, bar.get_y() + bar.get_height() / 2),
                        xytext=(5, 0), textcoords="offset points", ha="left", va="center", fontsize=8)

        # Legend for importance tiers
        handles = [plt.Rectangle((0, 0), 1, 1, color=c) for c in TIER_COLORS.values()]
        ax.legend(handles, TIER_COLORS.keys(), title="Importance Tier", loc="lower right", fontsize=8)

        plt.tight_layout()
        plt.savefig(out_path, dpi=self.dpi)
        plt.close(fig)
        return out_path
