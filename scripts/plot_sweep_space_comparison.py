#!/usr/bin/env python3
"""Create combined BalloonSketch/CameoSketch peak-space sweep PDFs.

Each input directory is searched recursively for ``*_config_summary.tsv`` files.
When only SLURM's per-dataset ``profile_manifest_*.tsv`` files are present,
their normal sidecar summaries are generated automatically.
The first PDF has one page per dataset with the existing BalloonSketch and
CameoSketch charts side by side. The second PDF has one page per dataset with
paired stacked bars for each shared hybrid-threshold multiplier.

Example:
    python3 scripts/plot_sweep_space_comparison.py \
      ~/research/binary_streams/BALLOON_SWEEP_space_results \
      ~/research/binary_streams/CAMEO_SWEEP_space_results
"""

from __future__ import annotations

import argparse
import math
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_pdf import PdfPages
    from matplotlib.patches import Patch
    from matplotlib.ticker import FuncFormatter
    import pandas as pd
    import seaborn as sns
except ImportError as exc:  # pragma: no cover
    print(
        "This script requires matplotlib, pandas, and seaborn; install scripts/requirements-plotting.txt.",
        file=sys.stderr,
    )
    raise SystemExit(1) from exc

from plot_hybrid_threshold_space import (
    COMPONENTS,
    cupcake_failure_label,
    dataset_label,
    hybrid_threshold_label,
    parse_nonnegative,
    plot_space,
    read_rows,
    select_plot_rows,
)
from sweep_summary_discovery import summary_paths_by_dataset
from dataset_metadata import canonical_dataset_name, full_dataset_name, order_datasets


SKETCH_LABELS = {
    "balloon": r"\textsc{BalloonSketch}",
    "cameo": r"\textsc{CameoSketch}",
}
SKETCH_HATCHES = {"balloon": "..", "cameo": "//"}
CLUSTER_FOREST_OUTLIER_RATIO = 4.0
CUPCAKE_AND_BALLOONDC_OUTLIER_RATIO = 2.0


def total_space(row: dict[str, str]) -> float:
    return sum(parse_nonnegative(row, field) for field, _ in COMPONENTS)


def geometric_components(
    balloon_row: dict[str, str], cameo_row: dict[str, str], baseline: float
) -> list[float]:
    return [
        math.sqrt(parse_nonnegative(balloon_row, field) * parse_nonnegative(cameo_row, field)) / baseline
        for field, _ in COMPONENTS
    ]


@dataclass(frozen=True)
class PairedDatasetRows:
    cupcake: tuple[dict[str, str], dict[str, str]] | None
    balloon_only: tuple[dict[str, str], dict[str, str]]
    balloon_hybrids: list[dict[str, str]]
    cameo_hybrids: list[dict[str, str]]
    cluster_forest: tuple[dict[str, str], dict[str, str]]
    baseline: float


def paired_hybrid_rows(
    balloon_rows: list[dict[str, str]], cameo_rows: list[dict[str, str]]
) -> PairedDatasetRows:
    """Select rows and use the two sweep baselines' geometric mean."""
    balloon_cupcake, balloon_only, balloon_hybrids, balloon_cf = select_plot_rows(balloon_rows)
    cameo_cupcake, cameo_only, cameo_hybrids, cameo_cf = select_plot_rows(cameo_rows)

    balloon_baseline = total_space(balloon_cf)
    cameo_baseline = total_space(cameo_cf)
    if balloon_baseline <= 0 or cameo_baseline <= 0:
        raise ValueError("Cluster Forest baseline space must be positive")
    if balloon_only is None or cameo_only is None:
        raise ValueError("paired plot requires a NORMAL standalone BalloonDC row in each sweep")

    def by_multiplier(rows: list[dict[str, str]]) -> dict[float, dict[str, str]]:
        return {float(row["hybrid_threshold_multiplier"]): row for row in rows}

    balloon_by_multiplier = by_multiplier(balloon_hybrids)
    cameo_by_multiplier = by_multiplier(cameo_hybrids)
    if balloon_by_multiplier.keys() != cameo_by_multiplier.keys():
        missing_balloon = sorted(cameo_by_multiplier.keys() - balloon_by_multiplier.keys())
        missing_cameo = sorted(balloon_by_multiplier.keys() - cameo_by_multiplier.keys())
        details = []
        if missing_balloon:
            details.append(f"missing BalloonSketch multipliers {missing_balloon}")
        if missing_cameo:
            details.append(f"missing CameoSketch multipliers {missing_cameo}")
        raise ValueError("hybrid threshold multiplier sets differ: " + "; ".join(details))

    multipliers = sorted(balloon_by_multiplier)
    return PairedDatasetRows(
        cupcake=(balloon_cupcake, cameo_cupcake)
        if balloon_cupcake is not None and cameo_cupcake is not None else None,
        balloon_only=(balloon_only, cameo_only),
        balloon_hybrids=[balloon_by_multiplier[value] for value in multipliers],
        cameo_hybrids=[cameo_by_multiplier[value] for value in multipliers],
        cluster_forest=(balloon_cf, cameo_cf),
        baseline=math.sqrt(balloon_baseline * cameo_baseline),
    )


def plot_paired_dataset(
    dataset: str,
    balloon_summary: Path,
    cameo_summary: Path,
    pdf: PdfPages,
    individual_output: Path,
) -> None:
    balloon_rows = read_rows(balloon_summary)
    cameo_rows = read_rows(cameo_summary)
    paired_rows = paired_hybrid_rows(balloon_rows, cameo_rows)
    baseline = paired_rows.baseline
    cupcake_status = (
        "OOM"
        if "OOM" in {cupcake_failure_label(balloon_rows), cupcake_failure_label(cameo_rows)}
        else "DNF"
    )

    sns.set_theme(context="paper", style="whitegrid", font_scale=1.1)
    plt.rcParams.update({"text.usetex": True, "font.family": "serif"})
    colors = sns.color_palette("colorblind", n_colors=len(COMPONENTS) + 1)
    cupcake_color = colors[0]
    component_colors = colors[1:]
    multipliers = [float(row["hybrid_threshold_multiplier"]) for row in paired_rows.balloon_hybrids]
    hybrid_positions = list(range(2, len(multipliers) + 2))
    cf_position = len(multipliers) + 2
    width = 0.34
    fig_width = max(10.0, 1.05 * (len(multipliers) + 3) + 3.0)
    fig, ax = plt.subplots(figsize=(fig_width, 5.2))

    totals: list[tuple[float, str, float]] = []

    def draw_paired_stack(
        rows: list[dict[str, str]], positions: list[float], sketch: str
    ) -> list[float]:
        bottoms = [0.0] * len(rows)
        hatch = SKETCH_HATCHES[sketch]
        for index, (field, component_label) in enumerate(COMPONENTS):
            values = [parse_nonnegative(row, field) / baseline for row in rows]
            ax.bar(
                positions,
                values,
                width=width,
                bottom=bottoms,
                color=component_colors[index],
                edgecolor="black" if hatch else "none",
                linewidth=0.35 if hatch else 0.0,
                hatch=hatch,
                label=component_label if sketch == "balloon" else None,
            )
            bottoms = [bottom + value for bottom, value in zip(bottoms, values)]
        return bottoms

    if paired_rows.cupcake is not None:
        cupcake_components = geometric_components(*paired_rows.cupcake, baseline)
        cupcake_total = sum(cupcake_components)
        ax.bar(0, cupcake_total, width=0.7, color=cupcake_color, label=r"\textsc{CUPCaKE}")
        totals.append((cupcake_total, "cupcake", 0.0))

    standalone_positions = [1 - width / 2, 1 + width / 2]
    for sketch, row, position in (
        ("balloon", paired_rows.balloon_only[0], standalone_positions[0]),
        ("cameo", paired_rows.balloon_only[1], standalone_positions[1]),
    ):
        totals.extend(
            (total, "balloondc", position)
            for total in draw_paired_stack([row], [position], sketch)
        )

    for sketch, rows, shift in (
        ("balloon", paired_rows.balloon_hybrids, -width / 2),
        ("cameo", paired_rows.cameo_hybrids, width / 2),
    ):
        positions = [position + shift for position in hybrid_positions]
        totals.extend(
            (total, "hybrid", position)
            for position, total in zip(positions, draw_paired_stack(rows, positions, sketch))
        )

    cf_components = geometric_components(*paired_rows.cluster_forest, baseline)
    cf_total = sum(cf_components)
    bottoms = 0.0
    for index, (_, component_label) in enumerate(COMPONENTS):
        value = cf_components[index]
        ax.bar(cf_position, value, width=0.7, bottom=bottoms, color=component_colors[index])
        bottoms += value
    totals.append((cf_total, "cf", cf_position))

    reference_max = max(total for total, kind, _ in totals if kind in {"hybrid", "cf"})
    non_cf_max = max(total for total, kind, _ in totals if kind != "cf")
    cupcakes_and_balloondc_clipped = any(
        kind in {"cupcake", "balloondc"}
        and total > CUPCAKE_AND_BALLOONDC_OUTLIER_RATIO * reference_max
        for total, kind, _ in totals
    )
    cf_is_clipped = cf_total > CLUSTER_FOREST_OUTLIER_RATIO * non_cf_max
    ceiling_base = max(total for total, _, _ in totals)
    if cupcakes_and_balloondc_clipped:
        ceiling_base = min(ceiling_base, reference_max)
    if cf_is_clipped:
        ceiling_base = min(ceiling_base, CLUSTER_FOREST_OUTLIER_RATIO * non_cf_max)
    plot_ceiling = 1.16 * ceiling_base

    if paired_rows.cupcake is None:
        ax.bar(0, plot_ceiling, width=0.7, color=cupcake_color, label=r"\textsc{CUPCaKE}")
        ax.annotate(
            rf"\textsc{{{cupcake_status}}}", (0, 0.94 * plot_ceiling),
            ha="center", va="bottom", color="white", fontweight="bold",
        )

    for total, _, position in totals:
        ax.annotate(
            rf"${total:.2f}\times$",
            (position, min(total, 0.94 * plot_ceiling)),
            xytext=(0, 4), textcoords="offset points", ha="center", va="bottom", fontsize=9,
        )

    for total, kind, position in totals:
        limit = (
            CLUSTER_FOREST_OUTLIER_RATIO * non_cf_max
            if kind == "cf"
            else CUPCAKE_AND_BALLOONDC_OUTLIER_RATIO * reference_max
        )
        if kind not in {"cupcake", "balloondc", "cf"} or total <= limit:
            continue
        break_height = 0.87 * plot_ceiling
        break_half_width = 0.18
        break_rise = 0.035 * plot_ceiling
        for offset in (-0.055, 0.055):
            ax.plot(
                [position - break_half_width + offset, position + break_half_width + offset],
                [break_height - break_rise, break_height + break_rise],
                color="white", linewidth=3.0, solid_capstyle="butt", zorder=4,
            )

    labels = [r"\textsc{CUPCaKE}", r"\textsc{BalloonDC}" + "\nOnly"]
    labels += [hybrid_threshold_label(value) for value in multipliers]
    labels += ["Cluster Forest\nOnly"]
    ax.set_xticks([0, 1, *hybrid_positions, cf_position], labels)
    ax.set_xlabel("Hybrid Threshold")
    ax.set_ylabel("Peak Space Usage (relative to CF)")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda value, _position: rf"${value:g}\times$"))
    ax.set_title(full_dataset_name(dataset), fontsize=18)
    component_handles = [Patch(facecolor=component_colors[i], label=label) for i, (_, label) in enumerate(COMPONENTS)]
    sketch_handles = [
        Patch(facecolor="white", edgecolor="black", hatch=SKETCH_HATCHES[sketch], label=label)
        for sketch, label in SKETCH_LABELS.items()
    ]
    ax.legend(
        handles=[*component_handles, *sketch_handles],
        loc="upper left",
        bbox_to_anchor=(1.02, 1.0),
        frameon=False,
    )
    ax.grid(axis="x", visible=False)
    ax.set_axisbelow(True)
    ax.margins(x=0.04)
    ax.set_ylim(0, plot_ceiling)
    fig.tight_layout()
    pdf.savefig(fig, bbox_inches="tight")
    individual_output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(individual_output, bbox_inches="tight")
    plt.close(fig)


def write_existing_chart_pdf(
    datasets: list[str],
    balloon_summaries: dict[str, Path],
    cameo_summaries: dict[str, Path],
    output: Path,
) -> None:
    """Place the existing one-summary chart design side by side per dataset."""
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sweep_space_") as temp_dir, PdfPages(output) as pdf:
        temp_path = Path(temp_dir)
        for dataset in datasets:
            display_name = full_dataset_name(dataset)
            balloon_image = temp_path / f"{dataset}_balloon.png"
            cameo_image = temp_path / f"{dataset}_cameo.png"
            plot_space(balloon_summaries[dataset], balloon_image, display_name)
            plot_space(cameo_summaries[dataset], cameo_image, display_name)

            fig, axes = plt.subplots(1, 2, figsize=(15.0, 5.8))
            for ax, image, label in zip(
                axes,
                (balloon_image, cameo_image),
                (SKETCH_LABELS["balloon"], SKETCH_LABELS["cameo"]),
            ):
                ax.imshow(plt.imread(image))
                ax.set_title(label)
                ax.axis("off")
            fig.suptitle(display_name, fontsize=16)
            fig.tight_layout()
            pdf.savefig(fig, bbox_inches="tight")
            plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("balloon_results", type=Path, help="BalloonSketch sweep result directory")
    parser.add_argument("cameo_results", type=Path, help="CameoSketch sweep result directory")
    parser.add_argument(
        "-o", "--output-dir", type=Path, default=Path("results/sweep_space_plots"),
        help="Directory for the two generated PDFs (default: results/sweep_space_plots)",
    )
    parser.add_argument(
        "--individual-output", type=Path,
        help="Combined PDF for the existing per-sketch chart design",
    )
    parser.add_argument(
        "--paired-output", type=Path,
        help="Combined PDF for the paired BalloonSketch/CameoSketch threshold chart",
    )
    parser.add_argument(
        "--per-dataset-output-dir", type=Path,
        help="Directory for one paired-threshold PDF per dataset",
    )
    args = parser.parse_args()

    individual_output = args.individual_output or args.output_dir / "sweep_space_by_sketch.pdf"
    paired_output = args.paired_output or args.output_dir / "sweep_space_paired_thresholds.pdf"
    per_dataset_output_dir = (
        args.per_dataset_output_dir
        or args.output_dir / "paired_thresholds_by_dataset"
    )
    try:
        balloon_summaries = summary_paths_by_dataset(args.balloon_results)
        cameo_summaries = summary_paths_by_dataset(args.cameo_results)
        if balloon_summaries.keys() != cameo_summaries.keys():
            missing_balloon = sorted(cameo_summaries.keys() - balloon_summaries.keys())
            missing_cameo = sorted(balloon_summaries.keys() - cameo_summaries.keys())
            details = []
            if missing_balloon:
                details.append(f"only in CameoSketch: {', '.join(missing_balloon)}")
            if missing_cameo:
                details.append(f"only in BalloonSketch: {', '.join(missing_cameo)}")
            raise ValueError("dataset sets differ: " + "; ".join(details))
        datasets, _ = order_datasets(list(balloon_summaries))

        write_existing_chart_pdf(datasets, balloon_summaries, cameo_summaries, individual_output)
        paired_output.parent.mkdir(parents=True, exist_ok=True)
        with PdfPages(paired_output) as pdf:
            for dataset in datasets:
                dataset_output = per_dataset_output_dir / (
                    f"{canonical_dataset_name(dataset)}_paired_thresholds.pdf"
                )
                plot_paired_dataset(
                    dataset,
                    balloon_summaries[dataset],
                    cameo_summaries[dataset],
                    pdf,
                    dataset_output,
                )
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    print(f"Wrote {individual_output}")
    print(f"Wrote {paired_output}")
    print(f"Wrote per-dataset PDFs to {per_dataset_output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
