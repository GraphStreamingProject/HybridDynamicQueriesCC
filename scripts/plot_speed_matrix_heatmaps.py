#!/usr/bin/env python3
"""Generate matrix heatmaps for space, update speed, and query speed from experiments.
Rows: Cluster Forest, HybridScale, CUPCaKE
Columns: Datasets
"""

import argparse
import math
import sys
from pathlib import Path
from typing import cast

try:
    import numpy as np
    import pandas as pd
    import matplotlib.pyplot as plt
    import seaborn as sns
except ImportError as exc:
    print("Requires numpy, pandas, matplotlib, seaborn.", file=sys.stderr)
    raise SystemExit(1) from exc

from sweep_summary_discovery import summarize_manifest
from dataset_metadata import order_datasets

SYSTEM_NAMES = ["Cluster Forest", r"\textsc{HybridSCALE}", r"\textsc{CUPCaKE}"]

LCT_OBJECT_BYTES = 24
LCT_NODE_BYTES = 40
LCT_MAP_SLOT_BYTES = 16
ABSL_GROUP_WIDTH = 16


def absl_capacity_to_growth(capacity: int) -> int:
    """Mirror Abseil Swiss-table growth limits for the current 16-slot groups."""
    max_full_capacity = ABSL_GROUP_WIDTH * 4 - 1
    if capacity <= max_full_capacity:
        return capacity - int(capacity >= ABSL_GROUP_WIDTH - 1)
    return capacity - capacity // 8


def absl_capacity_for_size(size: int) -> int:
    """Return the smallest 2^k-1 Swiss-table capacity that can hold size entries."""
    if size <= 0:
        return 0
    capacity = 1
    while absl_capacity_to_growth(capacity) < size:
        capacity = capacity * 2 + 1
    return capacity


def latex_bold_label(label: str) -> str:
    return "\n".join(rf"\textbf{{{line}}}" for line in label.split("\n"))


def approximate_lct_bytes(row: pd.Series) -> float:
    """Estimate the TopLevelForest LCT allocation for the row's build mode."""
    if str(row.get("algo", "")) == "cf":
        return 0.0

    if str(row.get("hybrid", "")).lower() == "true":
        active_vertices = int(float(row.get("peak_num_sketched_vertices", 0)))
        capacity = absl_capacity_for_size(active_vertices)
        return float(
            LCT_OBJECT_BYTES
            + active_vertices * LCT_NODE_BYTES
            + capacity * LCT_MAP_SLOT_BYTES
        )

    num_nodes = int(float(row.get("num_nodes", 0)))
    return float(LCT_OBJECT_BYTES + num_nodes * LCT_NODE_BYTES)


def total_space_bytes(row: pd.Series, approximate_lct: bool) -> float:
    peak_total = float(row.get("peak_total_bytes", np.nan))
    if not approximate_lct or str(row.get("algo", "")) == "cf":
        return peak_total

    recorded_lct = float(row.get("peak_top_level_lct_bytes", 0) or 0)
    if np.isnan(recorded_lct):
        recorded_lct = 0.0
    return peak_total - recorded_lct + approximate_lct_bytes(row)


def print_hybridscale_space_stats(space: pd.DataFrame, approximate_lct: bool) -> None:
    hybrid_name = r"\textsc{HybridSCALE}"
    alternative_names = ["Cluster Forest", r"\textsc{CUPCaKE}"]
    ratios: list[float] = []

    for dataset in space.columns:
        hybrid_space = cast(float, space.at[hybrid_name, dataset])
        alternatives: list[float] = []
        for name in alternative_names:
            alternative_space = cast(float, space.at[name, dataset])
            if np.isfinite(alternative_space) and alternative_space > 0:
                alternatives.append(alternative_space)
        if not np.isfinite(hybrid_space) or hybrid_space <= 0 or not alternatives:
            continue
        ratios.append(hybrid_space / min(alternatives))

    mode = "approximated LCT" if approximate_lct else "recorded LCT"
    if not ratios:
        print(f"HybridSCALE space statistics ({mode}): no comparable graphs")
        return

    geometric_mean = math.exp(sum(math.log(ratio) for ratio in ratios) / len(ratios))
    best_count = sum(ratio <= 1.0 for ratio in ratios)
    graph_count = len(ratios)

    print(f"HybridSCALE space statistics ({mode}, {graph_count} comparable graphs):")
    print(f"  Geometric mean vs best alternative: {geometric_mean:.4f}x")
    print(f"  Best system: {best_count}/{graph_count} ({100.0 * best_count / graph_count:.1f}%)")
    for percent in (10, 15, 20):
        within_count = sum(ratio <= 1.0 + percent / 100.0 for ratio in ratios)
        print(
            f"  Within {percent}% of best alternative: {within_count}/{graph_count} "
            f"({100.0 * within_count / graph_count:.1f}%)"
        )

def process_dataset(
    dataset_name: str,
    base_cluster_forest: Path,
    base_hybridscale: Path,
    base_cupcake: Path,
    approximate_lct: bool,
) -> tuple[dict[str, dict[str, float]], dict[str, dict[str, str]]]:
    
    data = {
        "Space (GB)": {},
        "Update Speed (100k/s)": {},
        "Query Speed (100k/s)": {}
    }
    annot = {
        "Space (GB)": {},
        "Update Speed (100k/s)": {},
        "Query Speed (100k/s)": {}
    }
    
    for sys_name in SYSTEM_NAMES:
        for metric in data:
            data[metric][sys_name] = np.nan
            annot[metric][sys_name] = "NaN"
        
    result_sources = [
        ("Cluster Forest", base_cluster_forest),
        (r"\textsc{HybridSCALE}", base_hybridscale),
        (r"\textsc{CUPCaKE}", base_cupcake),
    ]

    for expected_system, base_dir in result_sources:
        dataset_dir = base_dir / f"{dataset_name}_sym"
        if not dataset_dir.is_dir():
            continue
        manifest_paths = [
            manifest for manifest in dataset_dir.glob("speed_manifest_*.tsv")
            if not manifest.name.endswith("_config_summary.tsv")
        ]
        for manifest in manifest_paths:
            try:
                summary_path = summarize_manifest(manifest, refresh=False)
            except Exception as e:
                print(f"Warning: Failed to summarize {manifest}: {e}", file=sys.stderr)
                continue

            summary_df = pd.read_csv(summary_path, sep="\t")
            if summary_df.empty:
                continue

            row = summary_df.iloc[-1]

            algo = str(row.get("algo", ""))
            config = str(row.get("config", ""))

            is_expected_system = (
                (expected_system == "Cluster Forest" and algo == "cf")
                or (expected_system == r"\textsc{HybridSCALE}" and "hybrid" in config)
                or (expected_system == r"\textsc{CUPCaKE}" and "fixed" in config and algo == "mpi")
            )
            if not is_expected_system:
                continue
            sys_name = expected_system

            outcome = str(row.get("outcome", ""))
            is_oom = (outcome == "OOM")

            # 1. Space
            if is_oom:
                data["Space (GB)"][sys_name] = np.nan
                annot["Space (GB)"][sys_name] = r"$\ge 128$"
            else:
                try:
                    peak_bytes = total_space_bytes(row, approximate_lct)
                    val = peak_bytes / 1e9
                    data["Space (GB)"][sys_name] = val
                    if not np.isnan(val):
                        annot["Space (GB)"][sys_name] = f"{val:.2f}"
                except ValueError:
                    pass

            # 2. Update Speed
            if is_oom:
                annot["Update Speed (100k/s)"][sys_name] = "OOM"
            else:
                try:
                    insert_ups = str(row.get("insert_updates_per_sec", "nan"))
                    if insert_ups.lower() != "nan" and float(insert_ups) > 0:
                        ups = float(insert_ups)
                    else:
                        ups = float(row.get("updates_per_sec", np.nan))
                    val = ups / 100000.0
                    data["Update Speed (100k/s)"][sys_name] = val
                    if not np.isnan(val):
                        annot["Update Speed (100k/s)"][sys_name] = f"{val:.1f}"
                except ValueError:
                    pass

            # 3. Query Speed
            if is_oom:
                annot["Query Speed (100k/s)"][sys_name] = "OOM"
            else:
                try:
                    query_ups = float(row.get("post_queries_per_sec", np.nan))
                    val = query_ups / 100000.0
                    data["Query Speed (100k/s)"][sys_name] = val
                    if not np.isnan(val):
                        annot["Query Speed (100k/s)"][sys_name] = f"{val:.1f}"
                except ValueError:
                    pass

    return data, annot

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--cluster-forest-dir", "--revision-dir", dest="cluster_forest_dir", type=Path,
        default=Path.home() / "research/speed_results_REVISION"
    )
    parser.add_argument("--hybridscale-dir", type=Path, default=Path.home() / "research/speed_results_REVISION_25x")
    parser.add_argument("--cupcake-dir", type=Path, default=Path.home() / "research/speed_results_REVISION_cupcake")
    parser.add_argument("--output-dir", type=Path, default=Path("results/speed_heatmaps"))
    parser.add_argument(
        "--approximate-lct",
        action="store_true",
        help="replace recorded top-level LCT bytes with a container-aware estimate",
    )
    args = parser.parse_args()
    
    if not args.cluster_forest_dir.is_dir():
        print(f"Directory not found: {args.cluster_forest_dir}")
        return 1
    if not args.hybridscale_dir.is_dir():
        print(f"Directory not found: {args.hybridscale_dir}")
        return 1
        
    datasets = []
    for path in args.cluster_forest_dir.iterdir():
        if path.is_dir() and path.name.endswith("_sym"):
            datasets.append(path.name[:-4])
            
    datasets, dataset_labels = order_datasets(datasets)
    
    all_data = {
        "Space (GB)": pd.DataFrame(index=SYSTEM_NAMES, columns=datasets),
        "Update Speed (100k/s)": pd.DataFrame(index=SYSTEM_NAMES, columns=datasets),
        "Query Speed (100k/s)": pd.DataFrame(index=SYSTEM_NAMES, columns=datasets)
    }
    all_annot = {
        "Space (GB)": pd.DataFrame(index=SYSTEM_NAMES, columns=datasets),
        "Update Speed (100k/s)": pd.DataFrame(index=SYSTEM_NAMES, columns=datasets),
        "Query Speed (100k/s)": pd.DataFrame(index=SYSTEM_NAMES, columns=datasets)
    }
    
    for ds in datasets:
        ds_data, ds_annot = process_dataset(
            ds,
            args.cluster_forest_dir,
            args.hybridscale_dir,
            args.cupcake_dir,
            args.approximate_lct,
        )
        for metric in all_data:
            for sys_name in SYSTEM_NAMES:
                all_data[metric].loc[sys_name, ds] = ds_data[metric][sys_name]
                all_annot[metric].loc[sys_name, ds] = ds_annot[metric][sys_name]
                
    for metric in all_data:
        all_data[metric] = all_data[metric].apply(pd.to_numeric)

    print_hybridscale_space_stats(all_data["Space (GB)"], args.approximate_lct)
        
    args.output_dir.mkdir(parents=True, exist_ok=True)
    
    sns.set_theme(context="paper", style="white", font_scale=1.1)
    plt.rcParams.update({"text.usetex": True, "font.family": "serif"})
    
    metrics_config = [
        ("Space (GB)", "RdYlGn_r", "speed_heatmap_space.pdf"),
        ("Update Speed (100k/s)", "RdYlGn", "speed_heatmap_update.pdf"),
        ("Query Speed (100k/s)", "RdYlGn", "speed_heatmap_query.pdf")
    ]
    
    for metric_name, cmap, filename in metrics_config:
        df = all_data[metric_name]
        annot_df = all_annot[metric_name]
        
        # Calculate a unified "Penalty Ratio" (>= 1.0, where 1.0 is the best system)
        if "Space" in metric_name:
            penalty = df / df.min(axis=0)
        else:
            penalty = df.max(axis=0) / df
            
        # Use an S-shaped dropoff (tanh) on the log-penalty. 
        # Dividing by 4.0 weakens the initial steepness, making it more linear for small values.
        color_df = np.tanh(np.log2(penalty) / 2.0)
        
        fig_width = max(10, len(datasets) * 0.8)
        fig_height = 3.5
        fig, ax = plt.subplots(figsize=(fig_width, fig_height))
        
        ax.set_facecolor("lightgrey")
        
        sns.heatmap(
            color_df,
            annot=annot_df,
            fmt="",  
            cmap="RdYlGn_r",  # 0 (best) is Green, high (worse) is Red
            vmin=0.0,
            vmax=1.0,  # tanh naturally bounds between 0 and 1
            linewidths=0.5,
            ax=ax,
            cbar=False,
            annot_kws={"fontsize": 13},
        )
        ax.set_xticklabels([latex_bold_label(label) for label in dataset_labels])
        ax.set_yticklabels([rf"\textbf{{{label}}}" for label in SYSTEM_NAMES])
        
        # Manually draw text for NaN cells (e.g. OOMs) which seaborn skips
        for y in range(color_df.shape[0]):
            for x in range(color_df.shape[1]):
                if np.isnan(color_df.iloc[y, x]):
                    text_val = annot_df.iloc[y, x]
                    if pd.notna(text_val) and text_val != "NaN":
                        ax.text(x + 0.5, y + 0.5, text_val,
                            ha='center', va='center', color='black', fontsize=13)
        
        ax.set_title(rf"\textbf{{{metric_name}}}", fontsize=16, pad=12)
        ax.set_xlabel("")
        ax.set_ylabel("")
        
        plt.xticks(rotation=0, ha="center", fontweight="bold")
        plt.yticks(rotation=0, va="center", fontsize=12, fontweight="bold")
        
        fig.tight_layout()
        out_path = args.output_dir / filename
        fig.savefig(out_path, bbox_inches="tight")
        plt.close(fig)
        print(f"Saved {out_path}")
        
    return 0

if __name__ == "__main__":
    import warnings
    warnings.filterwarnings("ignore", category=RuntimeWarning)
    sys.exit(main())
