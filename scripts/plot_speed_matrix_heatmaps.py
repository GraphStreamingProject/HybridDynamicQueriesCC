#!/usr/bin/env python3
"""Generate matrix heatmaps for space, update speed, and query speed from experiments.
Rows: Cluster Forest, HybridScale, CUPCaKE
Columns: Datasets
"""

import argparse
import sys
from pathlib import Path

try:
    import numpy as np
    import pandas as pd
    import matplotlib.pyplot as plt
    import seaborn as sns
except ImportError as exc:
    print("Requires numpy, pandas, matplotlib, seaborn.", file=sys.stderr)
    raise SystemExit(1) from exc

from sweep_summary_discovery import summarize_manifest

SYSTEM_NAMES = ["Cluster Forest", r"\textsc{HybridSCALE}", r"\textsc{CUPCaKE}"]

def process_dataset(
    dataset_name: str,
    base_revision: Path,
    base_cupcake: Path
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
        
    dataset_dir = base_revision / f"{dataset_name}_sym"
    dataset_cupcake_dir = base_cupcake / f"{dataset_name}_sym"
    
    manifest_paths = []
    if dataset_dir.is_dir():
        manifest_paths.extend([m for m in dataset_dir.glob("speed_manifest_*.tsv") if not m.name.endswith("_config_summary.tsv")])
    if dataset_cupcake_dir.is_dir():
        manifest_paths.extend([m for m in dataset_cupcake_dir.glob("speed_manifest_*.tsv") if not m.name.endswith("_config_summary.tsv")])
        
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
        
        sys_name = None
        if algo == "cf":
            sys_name = "Cluster Forest"
        elif "hybrid" in config:
            sys_name = r"\textsc{HybridSCALE}"
        elif "fixed" in config and algo == "mpi":
            sys_name = r"\textsc{CUPCaKE}"
            
        if not sys_name:
            continue
            
        outcome = str(row.get("outcome", ""))
        is_oom = (outcome == "OOM")
            
        # 1. Space
        if is_oom:
            data["Space (GB)"][sys_name] = np.nan
            annot["Space (GB)"][sys_name] = r"$\ge 128$"
        else:
            try:
                peak_bytes = float(row.get("peak_total_bytes", np.nan))
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
    parser.add_argument("--revision-dir", type=Path, default=Path.home() / "research/speed_results_REVISION")
    parser.add_argument("--cupcake-dir", type=Path, default=Path.home() / "research/speed_results_REVISION_cupcake")
    parser.add_argument("--output-dir", type=Path, default=Path("results/speed_heatmaps"))
    args = parser.parse_args()
    
    if not args.revision_dir.is_dir():
        print(f"Directory not found: {args.revision_dir}")
        return 1
        
    datasets = []
    for path in args.revision_dir.iterdir():
        if path.is_dir() and path.name.endswith("_sym"):
            datasets.append(path.name[:-4])
            
    datasets.sort()
    
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
        ds_data, ds_annot = process_dataset(ds, args.revision_dir, args.cupcake_dir)
        for metric in all_data:
            for sys_name in SYSTEM_NAMES:
                all_data[metric].loc[sys_name, ds] = ds_data[metric][sys_name]
                all_annot[metric].loc[sys_name, ds] = ds_annot[metric][sys_name]
                
    for metric in all_data:
        all_data[metric] = all_data[metric].apply(pd.to_numeric)
        
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
            cbar=False  
        )
        
        # Manually draw text for NaN cells (e.g. OOMs) which seaborn skips
        for y in range(color_df.shape[0]):
            for x in range(color_df.shape[1]):
                if np.isnan(color_df.iloc[y, x]):
                    text_val = annot_df.iloc[y, x]
                    if pd.notna(text_val) and text_val != "NaN":
                        ax.text(x + 0.5, y + 0.5, text_val,
                                ha='center', va='center', color='black')
        
        ax.set_title(f"{metric_name}")
        ax.set_xlabel("")
        ax.set_ylabel("")
        
        plt.xticks(rotation=45, ha='right')
        plt.yticks(rotation=0, va='center')
        
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
