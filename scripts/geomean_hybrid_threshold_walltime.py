#!/usr/bin/env python3
"""Rank hybrid thresholds by geometric-mean walltime relative to Cluster Forest.

The metric is end-to-end stream walltime when available. For static graph
runs, it is the sum of insert/delete phase walltime and post/interleaved query
time. Each dataset is normalized by the geometric mean of the BalloonSketch
and CameoSketch Cluster Forest walltimes, then geometric means are computed
across datasets for every threshold and sketch type.

Use ``--use-profile-walltime`` only when speed experiments are unavailable.
It uses the walltime emitted by the profiling benchmarks, which includes
profiling/reporting overhead and is therefore not directly comparable to speed
benchmark timings.

Example:
    python3 scripts/geomean_hybrid_threshold_walltime.py \
      ~/research/binary_streams/BALLOON_SWEEP_speed_results \
      ~/research/binary_streams/CAMEO_SWEEP_speed_results \
      --exclude-dataset-regex kron \
      --output results/hybrid_threshold_walltime_geomeans.tsv
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

from sweep_summary_discovery import summary_paths_by_dataset


SKETCH_ROOTS = ("balloon", "cameo")


class ThresholdSummary(TypedDict):
    sketch: str
    threshold_multiplier: float
    num_datasets: int
    geometric_mean_relative_walltime: float
    minimum_relative_walltime: float
    maximum_relative_walltime: float


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as summary_file:
        return list(csv.DictReader(summary_file, delimiter="\t"))


def number(row: dict[str, str], field: str) -> float:
    value = row.get(field, "")
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{field} is unavailable for config {row.get('config', '<unknown>')}") from exc
    if not math.isfinite(parsed) or parsed < 0:
        raise ValueError(f"{field} is unavailable for config {row.get('config', '<unknown>')}")
    return parsed


def walltime_ms(row: dict[str, str]) -> float:
    """Return the full benchmark walltime represented by a speed summary row."""
    profile_walltime = row.get("profile_wall_time_ms", "")
    if profile_walltime not in {"", "nan"}:
        return number(row, "profile_wall_time_ms")
    stream_walltime = row.get("end_to_end_time_ms", "")
    if stream_walltime not in {"", "nan"}:
        return number(row, "end_to_end_time_ms")
    fields = (
        "insert_phase_wall_ms", "delete_phase_wall_ms",
        "post_queries_ms",
    )
    return sum(number(row, field) for field in fields)


def select_rows(rows: list[dict[str, str]]) -> tuple[list[dict[str, str]], dict[str, str]]:
    normal = [row for row in rows if row.get("outcome", "NORMAL") == "NORMAL"]
    cf_rows = [row for row in normal if row.get("algo", "") == "cf"]
    if len(cf_rows) != 1:
        raise ValueError(f"expected one NORMAL Cluster Forest row, found {len(cf_rows)}")
    hybrids = [row for row in normal if row.get("hybrid", "").lower() == "true"]
    if not hybrids:
        raise ValueError("no NORMAL hybrid threshold rows were found")
    for row in hybrids:
        try:
            multiplier = float(row.get("hybrid_threshold_multiplier", "nan"))
        except ValueError as exc:
            raise ValueError(f"invalid hybrid threshold multiplier for {row.get('config', '<unknown>')}") from exc
        if not math.isfinite(multiplier) or multiplier < 0:
            raise ValueError(f"invalid hybrid threshold multiplier for {row.get('config', '<unknown>')}")
    hybrids.sort(key=lambda row: float(row["hybrid_threshold_multiplier"]))
    return hybrids, cf_rows[0]


def baselines(balloon_summaries: dict[str, Path], cameo_summaries: dict[str, Path]) -> dict[str, float]:
    result: dict[str, float] = {}
    for dataset, balloon_path in balloon_summaries.items():
        _, balloon_cf = select_rows(read_rows(balloon_path))
        _, cameo_cf = select_rows(read_rows(cameo_summaries[dataset]))
        balloon_time = walltime_ms(balloon_cf)
        cameo_time = walltime_ms(cameo_cf)
        if balloon_time <= 0 or cameo_time <= 0:
            raise ValueError(f"Cluster Forest walltime must be positive for {dataset}")
        result[dataset] = math.sqrt(balloon_time * cameo_time)
    return result


def summarize(
    sketch: str, summaries: dict[str, Path], cf_baselines: dict[str, float]
) -> list[ThresholdSummary]:
    by_threshold: dict[float, list[tuple[str, float]]] = defaultdict(list)
    for dataset, path in summaries.items():
        hybrids, _ = select_rows(read_rows(path))
        for row in hybrids:
            multiplier = float(row["hybrid_threshold_multiplier"])
            ratio = walltime_ms(row) / cf_baselines[dataset]
            if ratio <= 0 or not math.isfinite(ratio):
                raise ValueError(f"non-positive relative walltime for {dataset} at {multiplier:g}x")
            by_threshold[multiplier].append((dataset, ratio))

    expected_datasets = set(summaries)
    result: list[ThresholdSummary] = []
    for multiplier, entries in sorted(by_threshold.items()):
        observed_datasets = {dataset for dataset, _ in entries}
        if observed_datasets != expected_datasets:
            missing = sorted(expected_datasets - observed_datasets)
            raise ValueError(f"{sketch} threshold {multiplier:g}x is missing datasets: {', '.join(missing)}")
        values = [ratio for _, ratio in entries]
        result.append({
            "sketch": sketch,
            "threshold_multiplier": multiplier,
            "num_datasets": len(values),
            "geometric_mean_relative_walltime": math.exp(sum(math.log(value) for value in values) / len(values)),
            "minimum_relative_walltime": min(values),
            "maximum_relative_walltime": max(values),
        })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("balloon_results", type=Path, help="BalloonSketch speed-sweep result directory")
    parser.add_argument("cameo_results", type=Path, help="CameoSketch speed-sweep result directory")
    parser.add_argument(
        "--use-profile-walltime", action="store_true",
        help="Use profile experiment walltimes instead of speed experiments (includes profiling overhead)",
    )
    parser.add_argument(
        "--exclude-dataset-regex", action="append", default=[], metavar="REGEX",
        help="Exclude dataset names matching REGEX; repeat for multiple patterns",
    )
    parser.add_argument(
        "-o", "--output", type=Path, default=Path("results/hybrid_threshold_walltime_geomeans.tsv"),
        help="TSV output path (default: results/hybrid_threshold_walltime_geomeans.tsv)",
    )
    args = parser.parse_args()

    try:
        bench_type = "profile" if args.use_profile_walltime else "speed"
        if args.use_profile_walltime:
            print("Warning: using profile walltimes, which include profiling/reporting overhead.", file=sys.stderr)
        summaries_by_sketch = {
            "balloon": summary_paths_by_dataset(
                args.balloon_results, bench_type=bench_type,
                refresh_summaries=args.use_profile_walltime,
            ),
            "cameo": summary_paths_by_dataset(
                args.cameo_results, bench_type=bench_type,
                refresh_summaries=args.use_profile_walltime,
            ),
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
        cf_baselines = baselines(summaries_by_sketch["balloon"], summaries_by_sketch["cameo"])
        rows = [
            row
            for sketch in SKETCH_ROOTS
            for row in summarize(sketch, summaries_by_sketch[sketch], cf_baselines)
        ]
    except (OSError, ValueError, re.error) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "sketch", "threshold_multiplier", "num_datasets",
        "geometric_mean_relative_walltime", "minimum_relative_walltime", "maximum_relative_walltime",
    ]
    with args.output.open("w", encoding="utf-8", newline="") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)

    for sketch in SKETCH_ROOTS:
        best = min(
            (row for row in rows if row["sketch"] == sketch),
            key=lambda row: row["geometric_mean_relative_walltime"],
        )
        print(
            f"{sketch}: best threshold {best['threshold_multiplier']:g}x "
            f"(geometric mean walltime {best['geometric_mean_relative_walltime']:.4f}x Cluster Forest)"
        )
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
