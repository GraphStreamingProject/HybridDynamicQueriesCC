#!/usr/bin/env python3
"""Plot standalone and hybrid peak space components against Cluster Forest."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter
    import pandas as pd
    import seaborn as sns
except ImportError as exc:  # pragma: no cover
    print(
        "This script requires matplotlib, pandas, and seaborn; install scripts/requirements-plotting.txt.",
        file=sys.stderr,
    )
    raise SystemExit(1) from exc

COMPONENTS = (
    ("peak_cf_bytes", "Cluster Forest"),
    ("peak_sketch_bytes", r"\textsc{BalloonDC}"),
    ("peak_recovery_bytes", "Recovery"),
    ("peak_driver_bytes", "Manager"),
)

CUPCAKE_OUTLIER_RATIO = 2.0
BALLOONDC_OUTLIER_RATIO = 2.0
CLUSTER_FOREST_OUTLIER_RATIO = 2.0
PLOT_HEADROOM = 1.2


def parse_nonnegative(row: dict[str, str], field: str) -> float:
    value = row.get(field, "")
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{field} is not numeric for config {row.get('config', '<unknown>')}: {value!r}") from exc
    if not math.isfinite(parsed) or parsed < 0:
        raise ValueError(f"{field} is unavailable for config {row.get('config', '<unknown>')}: {value!r}")
    return parsed


def read_rows(path: Path) -> list[dict[str, str]]:
    frame = pd.read_csv(path, sep="\t", dtype=str, keep_default_na=False)
    required = {
        "dataset", "algo", "cutset", "sketch", "hybrid",
        "hybrid_threshold_multiplier", "outcome",
    }
    required.update(field for field, _ in COMPONENTS)
    missing = sorted(required - set(frame.columns))
    if missing:
        raise ValueError(f"TSV is missing required columns: {', '.join(missing)}")
    return [
        {str(key): str(value) for key, value in record.items()}
        for record in frame.to_dict(orient="records")
    ]


def select_plot_rows(
    rows: list[dict[str, str]],
) -> tuple[dict[str, str] | None, dict[str, str] | None, list[dict[str, str]], dict[str, str]]:
    normal = [row for row in rows if row.get("outcome", "NORMAL") == "NORMAL"]
    cf_rows = [row for row in normal if row.get("algo", "") == "cf"]
    if len(cf_rows) != 1:
        raise ValueError(f"expected exactly one NORMAL Cluster Forest row, found {len(cf_rows)}")

    cupcake_configs = [
        row for row in rows
        if row.get("algo", "") == "mpi"
        and row.get("cutset", "") == "ett"
        and row.get("sketch", "") == "fixed"
        and row.get("hybrid", "").lower() == "false"
    ]
    if len(cupcake_configs) != 1:
        raise ValueError(f"expected exactly one CUPCaKE row, found {len(cupcake_configs)}")
    cupcake_row = cupcake_configs[0] if cupcake_configs[0] in normal else None

    balloon_only_rows = [
        row for row in normal
        if row.get("algo", "") == "mpi_batch"
        and row.get("cutset", "") == "lct"
        and row.get("hybrid", "").lower() == "false"
    ]
    if len(balloon_only_rows) > 1:
        raise ValueError(
            "expected at most one NORMAL standalone BalloonDC row "
            f"(mpi_batch/lct/non-hybrid), found {len(balloon_only_rows)}"
        )

    hybrid_rows: list[dict[str, str]] = []
    for row in normal:
        if row.get("algo", "") == "cf" or row.get("hybrid", "").lower() != "true":
            continue
        try:
            multiplier = float(row.get("hybrid_threshold_multiplier", "nan"))
        except ValueError:
            continue
        if math.isfinite(multiplier) and multiplier >= 0:
            hybrid_rows.append(row)
    if not hybrid_rows:
        raise ValueError("no NORMAL hybrid threshold rows were found")
    hybrid_rows.sort(key=lambda row: float(row["hybrid_threshold_multiplier"]))
    balloon_only_row = balloon_only_rows[0] if balloon_only_rows else None
    return cupcake_row, balloon_only_row, hybrid_rows, cf_rows[0]


def cupcake_failure_label(rows: list[dict[str, str]]) -> str:
    """Return the display label for an unavailable CUPCaKE configuration."""
    cupcake_rows = [
        row for row in rows
        if row.get("algo", "") == "mpi"
        and row.get("cutset", "") == "ett"
        and row.get("sketch", "") == "fixed"
        and row.get("hybrid", "").lower() == "false"
    ]
    detail = " ".join(
        " ".join(row.get(field, "") for field in ("outcome", "failure_details"))
        for row in cupcake_rows
    ).upper()
    return "OOM" if "OOM" in detail or "OUT_OF_MEMORY" in detail else "DNF"


def dataset_label(rows: list[dict[str, str]], override: str | None) -> str:
    if override:
        return override
    datasets = sorted({row.get("dataset", "").strip() for row in rows if row.get("dataset", "").strip()})
    if len(datasets) == 1:
        return datasets[0]
    if not datasets:
        return "Dataset"
    raise ValueError("summary contains multiple datasets; pass --dataset-name and provide a single-dataset TSV")


def plot_space(summary_path: Path, output_path: Path, name: str | None) -> None:
    rows = read_rows(summary_path)
    cupcake_row, balloon_only_row, hybrid_rows, cf_row = select_plot_rows(rows)
    cupcake_status = cupcake_failure_label(rows) if cupcake_row is None else ""
    standalone_rows = [balloon_only_row] if balloon_only_row is not None else []
    non_cupcake_rows = [*standalone_rows, *hybrid_rows, cf_row]
    plot_rows = ([cupcake_row] if cupcake_row is not None else []) + non_cupcake_rows

    baseline_components = [parse_nonnegative(cf_row, field) for field, _ in COMPONENTS]
    baseline = sum(baseline_components)
    if baseline <= 0:
        raise ValueError("Cluster Forest baseline space must be positive")

    normalized_non_cupcake = [
        [parse_nonnegative(row, field) / baseline for field, _ in COMPONENTS]
        for row in non_cupcake_rows
    ]
    cupcake_total = 0.0
    if cupcake_row is not None:
        cupcake_total = sum(
            parse_nonnegative(cupcake_row, field) / baseline for field, _ in COMPONENTS
        )
    labels = [r"\textsc{CUPCaKE}" if cupcake_row is not None else r"\textsc{CUPCaKE}" + f"\n(\textsc{{{cupcake_status}}})"]
    if balloon_only_row is not None:
        labels.append(r"\textsc{BalloonDC}" + "\nOnly")
    labels += [
        rf"${float(row['hybrid_threshold_multiplier']):g}\times$" for row in hybrid_rows
    ] + ["Cluster Forest\nOnly"]

    sns.set_theme(context="paper", style="whitegrid", font_scale=1.25)
    plt.rcParams.update({"text.usetex": True, "font.family": "serif"})
    colors = sns.color_palette("colorblind", n_colors=len(COMPONENTS) + 1)
    cupcake_color = colors[0]
    component_colors = colors[1:]
    fig_width = max(7.0, 1.05 * (len(non_cupcake_rows) + 1) + 2.5)
    fig, ax = plt.subplots(figsize=(fig_width, 5.0))

    x_positions = list(range(len(non_cupcake_rows) + 1))
    if cupcake_row is not None:
        ax.bar(
            [x_positions[0]],
            [cupcake_total],
            width=0.72,
            label=r"\textsc{CUPCaKE}",
            color=cupcake_color,
        )
    bottoms = [cupcake_total] + [0.0] * len(non_cupcake_rows)
    for component_index, (_, component_label) in enumerate(COMPONENTS):
        values = [0.0] + [row_values[component_index] for row_values in normalized_non_cupcake]
        ax.bar(
            x_positions,
            values,
            bottom=bottoms,
            width=0.72,
            label=component_label,
            color=component_colors[component_index],
        )
        bottoms = [bottom + value for bottom, value in zip(bottoms, values)]

    balloon_only_index = 1 if balloon_only_row is not None else None
    reference_totals = [
        total for index, total in enumerate(bottoms)
        if index != 0 and index != balloon_only_index
    ]
    cf_index = len(bottoms) - 1
    non_cf_totals = [total for index, total in enumerate(bottoms) if index != cf_index]
    cf_total = bottoms[cf_index]
    cf_is_clipped = cf_total > CLUSTER_FOREST_OUTLIER_RATIO * max(non_cf_totals)
    if cf_is_clipped:
        reference_totals = [
            total for index, total in enumerate(bottoms)
            if index not in {0, balloon_only_index, cf_index}
        ]
    reference_max = max(reference_totals)
    cupcake_is_clipped = cupcake_row is not None and cupcake_total > CUPCAKE_OUTLIER_RATIO * reference_max
    balloon_only_total = bottoms[balloon_only_index] if balloon_only_index is not None else 0.0
    balloondc_is_clipped = (
        balloon_only_index is not None
        and balloon_only_total > BALLOONDC_OUTLIER_RATIO * reference_max
    )
    if cupcake_is_clipped or balloondc_is_clipped:
        ceiling_base = reference_max
    elif cf_is_clipped:
        ceiling_base = CLUSTER_FOREST_OUTLIER_RATIO * max(non_cf_totals)
    else:
        ceiling_base = max(bottoms)
    plot_ceiling = PLOT_HEADROOM * ceiling_base
    ax.set_ylim(0, plot_ceiling)

    if cupcake_row is None:
        ax.bar(
            [x_positions[0]], [plot_ceiling], width=0.72,
            color=cupcake_color, label=r"\textsc{CUPCaKE}",
        )

    for index, total in enumerate(bottoms):
        if index == 0 and cupcake_row is None:
            ax.annotate(
                rf"\textsc{{{cupcake_status}}}",
                (index, 0.94 * plot_ceiling),
                ha="center",
                va="bottom",
                color="white",
                fontweight="bold",
            )
            continue
        label_height = min(total, 0.94 * plot_ceiling)
        ax.annotate(
            rf"${total:.2f}\times$",
            (index, label_height),
            xytext=(0, 5),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontweight="bold",
        )

    clipped_bars = (
        (0, cupcake_is_clipped),
        (balloon_only_index, balloondc_is_clipped),
        (cf_index, cf_is_clipped),
    )
    for index, is_clipped in clipped_bars:
        if index is None:
            continue
        if not is_clipped:
            continue
        break_height = 0.87 * plot_ceiling
        break_half_width = 0.18
        break_rise = 0.035 * plot_ceiling
        for offset in (-0.055, 0.055):
            ax.plot(
                [index - break_half_width + offset, index + break_half_width + offset],
                [break_height - break_rise, break_height + break_rise],
                color="white",
                linewidth=3.0,
                solid_capstyle="butt",
                zorder=4,
            )

    ax.set_xticks(x_positions, labels)
    ax.set_xlabel("System / hybrid threshold multiplier")
    ax.set_ylabel("Peak space relative to Cluster Forest")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda value, _position: rf"${value:g}\times$"))
    ax.set_title(f"{dataset_label(plot_rows, name)}: peak system space")
    ax.legend(loc="upper left", bbox_to_anchor=(1.02, 1.0), frameon=False)
    ax.grid(axis="x", visible=False)
    ax.set_axisbelow(True)
    ax.margins(x=0.04)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(output_path, dpi=300, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path, help="Profile *_config_summary.tsv containing hybrid and CF rows")
    parser.add_argument("-o", "--output", type=Path, help="Output image path (default: *_stacked_space.pdf)")
    parser.add_argument(
        "--dataset-name",
        help="Human-readable dataset label for the title (default: dataset column in the TSV)",
    )
    args = parser.parse_args()

    output = args.output or args.summary.with_name(f"{args.summary.stem}_stacked_space.pdf")
    try:
        plot_space(args.summary, output, args.dataset_name)
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
