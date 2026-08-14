#!/usr/bin/env python3
"""Rank hybrid threshold multipliers by geometric-mean peak space.

For each dataset and NORMAL hybrid run, the peak space is divided by the
geometric mean of the BalloonSketch and CameoSketch Cluster Forest peaks. The
script then computes the geometric mean of those ratios across datasets for
each threshold multiplier. Balloon and Cameo sweeps are reported independently
because their sketch types differ.
Per-dataset SLURM profile manifests are supported and summarized automatically
when their sidecar summaries are not already present.

Example:
    python3 scripts/geomean_hybrid_threshold_space.py \
      ~/research/binary_streams/BALLOON_SWEEP_space_results \
      ~/research/binary_streams/CAMEO_SWEEP_space_results \
      --output results/hybrid_threshold_geomeans.tsv
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import TypedDict

from plot_hybrid_threshold_space import (
    COMPONENTS,
    parse_nonnegative,
    read_rows,
    select_plot_rows,
)
from sweep_summary_discovery import summary_paths_by_dataset


SKETCH_ROOTS = ("balloon", "cameo")


class ThresholdSummary(TypedDict):
    sketch: str
    threshold_multiplier: float
    num_datasets: int
    geometric_mean_relative_space: float
    minimum_relative_space: float
    maximum_relative_space: float


def peak_space_bytes(row: dict[str, str]) -> float:
    """Return the full system peak used by the stacked-space plots."""
    return sum(parse_nonnegative(row, field) for field, _ in COMPONENTS)


def cluster_forest_baselines(
    balloon_summaries: dict[str, Path], cameo_summaries: dict[str, Path]
) -> dict[str, float]:
    baselines: dict[str, float] = {}
    for dataset, balloon_path in balloon_summaries.items():
        cameo_path = cameo_summaries[dataset]
        _, _, _, balloon_cf = select_plot_rows(read_rows(balloon_path))
        _, _, _, cameo_cf = select_plot_rows(read_rows(cameo_path))
        balloon_space = peak_space_bytes(balloon_cf)
        cameo_space = peak_space_bytes(cameo_cf)
        if balloon_space <= 0 or cameo_space <= 0:
            raise ValueError(f"Cluster Forest baseline space must be positive for {dataset}")
        baselines[dataset] = math.sqrt(balloon_space * cameo_space)
    return baselines


def ratios_by_threshold(
    summaries: dict[str, Path], baselines: dict[str, float]
) -> dict[float, list[tuple[str, float]]]:
    ratios: dict[float, list[tuple[str, float]]] = defaultdict(list)
    for dataset, path in summaries.items():
        _, _, hybrid_rows, _ = select_plot_rows(read_rows(path))
        baseline = baselines[dataset]
        for row in hybrid_rows:
            multiplier = float(row["hybrid_threshold_multiplier"])
            ratio = peak_space_bytes(row) / baseline
            if ratio <= 0 or not math.isfinite(ratio):
                raise ValueError(
                    f"non-positive or non-finite relative space for {dataset} at {multiplier:g}x"
                )
            ratios[multiplier].append((dataset, ratio))
    return ratios


def summarize(
    sketch: str, summaries: dict[str, Path], baselines: dict[str, float]
) -> list[ThresholdSummary]:
    by_threshold = ratios_by_threshold(summaries, baselines)
    expected_datasets = set(summaries)
    result: list[ThresholdSummary] = []
    for multiplier, entries in sorted(by_threshold.items()):
        observed_datasets = {dataset for dataset, _ in entries}
        if observed_datasets != expected_datasets:
            missing = sorted(expected_datasets - observed_datasets)
            raise ValueError(
                f"{sketch} threshold {multiplier:g}x is missing datasets: {', '.join(missing)}"
            )
        values = [ratio for _, ratio in entries]
        result.append(
            {
                "sketch": sketch,
                "threshold_multiplier": multiplier,
                "num_datasets": len(values),
                "geometric_mean_relative_space": math.exp(sum(math.log(value) for value in values) / len(values)),
                "minimum_relative_space": min(values),
                "maximum_relative_space": max(values),
            }
        )
    if not result:
        raise ValueError(f"no NORMAL hybrid threshold rows found for {sketch}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("balloon_results", type=Path, help="BalloonSketch sweep result directory")
    parser.add_argument("cameo_results", type=Path, help="CameoSketch sweep result directory")
    parser.add_argument(
        "-o", "--output", type=Path, default=Path("results/hybrid_threshold_geomeans.tsv"),
        help="TSV output path (default: results/hybrid_threshold_geomeans.tsv)",
    )
    parser.add_argument(
        "--exclude-dataset-regex", action="append", default=[], metavar="REGEX",
        help="Exclude dataset names matching REGEX when choosing thresholds; repeat for multiple patterns",
    )
    args = parser.parse_args()

    try:
        summaries_by_sketch = {
            "balloon": summary_paths_by_dataset(args.balloon_results),
            "cameo": summary_paths_by_dataset(args.cameo_results),
        }
        if summaries_by_sketch["balloon"].keys() != summaries_by_sketch["cameo"].keys():
            raise ValueError("BalloonSketch and CameoSketch result directories contain different datasets")
        patterns = [re.compile(pattern) for pattern in args.exclude_dataset_regex]
        excluded = sorted(
            dataset for dataset in summaries_by_sketch["balloon"]
            if any(pattern.search(dataset) for pattern in patterns)
        )
        if excluded:
            print(f"Excluded datasets: {', '.join(excluded)}")
        for sketch in SKETCH_ROOTS:
            summaries_by_sketch[sketch] = {
                dataset: path for dataset, path in summaries_by_sketch[sketch].items()
                if dataset not in excluded
            }
        if not summaries_by_sketch["balloon"]:
            raise ValueError("all datasets were excluded")
        baselines = cluster_forest_baselines(
            summaries_by_sketch["balloon"], summaries_by_sketch["cameo"]
        )
        rows = [
            row
            for sketch in SKETCH_ROOTS
            for row in summarize(sketch, summaries_by_sketch[sketch], baselines)
        ]
    except (OSError, ValueError, re.error) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "sketch", "threshold_multiplier", "num_datasets",
        "geometric_mean_relative_space", "minimum_relative_space", "maximum_relative_space",
    ]
    with args.output.open("w", newline="") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)

    for sketch in SKETCH_ROOTS:
        sketch_rows = [row for row in rows if row["sketch"] == sketch]
        best = min(sketch_rows, key=lambda row: float(row["geometric_mean_relative_space"]))
        print(
            f"{sketch}: best threshold {best['threshold_multiplier']:g}x "
            f"(geometric mean {best['geometric_mean_relative_space']:.4f}x Cluster Forest)"
        )
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
