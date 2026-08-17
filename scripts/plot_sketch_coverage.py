#!/usr/bin/env python3
"""Create sketch coverage sweep PDFs.

Each input directory is searched recursively for ``*_config_summary.tsv`` files.
The PDF has one page per dataset showing the percentage of edges and vertices
sketched for each hybrid-threshold multiplier.

Example:
    python3 scripts/plot_sketch_coverage.py \
      ~/research/binary_streams/BALLOON_SWEEP_space_results
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_pdf import PdfPages
    import seaborn as sns
except ImportError as exc:  # pragma: no cover
    print(
        "This script requires matplotlib, pandas, and seaborn; install scripts/requirements-plotting.txt.",
        file=sys.stderr,
    )
    raise SystemExit(1) from exc

from plot_hybrid_threshold_space import read_rows, select_plot_rows
from sweep_summary_discovery import summary_paths_by_dataset


def threshold_sketch_percentages(rows: list[dict[str, str]]) -> list[dict[str, float]]:
    """Return the edge- and vertex-sketch coverage for each hybrid threshold value."""
    entries: list[dict[str, float]] = []
    for row in sorted(rows, key=lambda item: float(item.get("hybrid_threshold_multiplier", "nan"))):
        try:
            multiplier = float(row.get("hybrid_threshold_multiplier", "nan"))
        except ValueError:
            continue
        if not math.isfinite(multiplier):
            continue
        total_edges = float(row.get("dataset_num_edges", "0") or "0")
        total_vertices = float(row.get("dataset_num_vertices", row.get("dataset_num_nodes", "0")) or "0")
        sketched_edges = float(row.get("peak_num_sketched_edges", "0") or "0")
        sketched_vertices = float(row.get("peak_num_sketched_vertices", "0") or "0")
        if total_edges <= 0:
            edge_percent = 0.0
        else:
            edge_percent = 100.0 * sketched_edges / total_edges
        if total_vertices <= 0:
            vertex_percent = 0.0
        else:
            vertex_percent = 100.0 * sketched_vertices / total_vertices
        entries.append({
            "multiplier": multiplier,
            "edge_percent": edge_percent,
            "vertex_percent": vertex_percent,
        })
    return entries


def plot_threshold_sketch_coverage(
    dataset: str,
    summary: Path,
    pdf: PdfPages,
) -> None:
    rows = read_rows(summary)
    _, _, hybrid_rows, _ = select_plot_rows(rows)

    coverage = threshold_sketch_percentages(hybrid_rows)
    multipliers = [entry["multiplier"] for entry in coverage]

    x_positions = list(range(len(multipliers)))
    bar_width = 0.38
    fig, axes = plt.subplots(1, 2, figsize=(12.0, 4.4), sharey=True)

    for axis, metric_name, label in (
        (axes[0], "edge_percent", "% of edges sketched"),
        (axes[1], "vertex_percent", "% of vertices sketched"),
    ):
        axis.bar(
            x_positions,
            [entry[metric_name] for entry in coverage],
            width=bar_width,
            color=sns.color_palette("deep", n_colors=1)[0],
        )
        axis.set_xticks(x_positions)
        axis.set_xticklabels([rf"${value:g}\times$" for value in multipliers])
        axis.set_xlabel("Hybrid threshold multiplier")
        axis.set_ylabel(label)
        axis.set_ylim(0, 100)
        axis.grid(axis="y", linestyle="--", alpha=0.3)

    fig.suptitle(f"{dataset}: sketch coverage by threshold")
    fig.tight_layout()
    pdf.savefig(fig, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, help="Sweep result directory")
    parser.add_argument(
        "-o", "--output-dir", type=Path, default=Path("results/sweep_space_plots"),
        help="Directory for the generated PDF (default: results/sweep_space_plots)",
    )
    parser.add_argument(
        "--coverage-output", type=Path,
        help="Combined PDF for the sketch coverage chart",
    )
    args = parser.parse_args()

    coverage_output = args.coverage_output or args.output_dir / "sweep_sketch_coverage.pdf"
    try:
        summaries = summary_paths_by_dataset(args.results)
        datasets = sorted(summaries)

        coverage_output.parent.mkdir(parents=True, exist_ok=True)
        with PdfPages(coverage_output) as pdf:
            for dataset in datasets:
                plot_threshold_sketch_coverage(dataset, summaries[dataset], pdf)
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    print(f"Wrote {coverage_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
