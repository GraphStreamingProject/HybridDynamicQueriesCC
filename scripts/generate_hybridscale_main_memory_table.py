#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path

from dataset_metadata import full_dataset_name, order_datasets

BYTES_PER_GB = 1_000_000_000
EXCLUDED_DATASETS = {"kron_17"}


def number(row: dict[str, str], field: str) -> float:
    try:
        value = float(row[field])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"missing numeric {field!r} for {row.get('dataset', '<unknown>')}") from exc
    if not math.isfinite(value) or value < 0:
        raise ValueError(f"invalid {field!r} for {row.get('dataset', '<unknown>')}: {value!r}")
    return value


def is_hybridscale(row: dict[str, str]) -> bool:
    return (
        row.get("algo") == "mpi_batch"
        and row.get("cutset") == "lct"
        and row.get("sketch") == "resizeable"
        and row.get("hybrid", "").lower() == "true"
        and row.get("outcome", "NORMAL") == "NORMAL"
    )


def read_rows(root: Path) -> dict[str, dict[str, str]]:
    selected: dict[str, dict[str, str]] = {}
    summaries = sorted(root.glob("*_sym/speed_manifest_*_config_summary.tsv"))
    if not summaries:
        raise ValueError(f"no speed config summaries found under {root}")

    for summary_path in summaries:
        with summary_path.open(encoding="utf-8", newline="") as summary_file:
            for row in csv.DictReader(summary_file, delimiter="\t"):
                if not is_hybridscale(row):
                    continue
                dataset = row.get("dataset", "")
                if not dataset:
                    raise ValueError(f"missing dataset in {summary_path}")
                if dataset in selected:
                    raise ValueError(f"multiple HybridScale summaries found for {dataset}")
                selected[dataset] = row
    if not selected:
        raise ValueError(f"no normal HybridScale summaries found under {root}")
    return selected


def latex_escape(text: str) -> str:
    return text.replace("_", r"\_").replace("&", r"\&")


def gb(row: dict[str, str], field: str) -> float:
    return number(row, field) / BYTES_PER_GB


def percent(numerator: float, denominator: float, field: str, dataset: str) -> float:
    if denominator <= 0:
        raise ValueError(f"non-positive denominator for {field} in {dataset}")
    return 100.0 * numerator / denominator


def format_percent(value: float) -> str:
    if value < 100.0 and round(value, 1) >= 100.0:
        return "99.9\\%"
    return f"{value:.1f}\\%"


def write_table(rows: dict[str, dict[str, str]], output: Path) -> None:
    excluded = sorted(dataset for dataset in rows if dataset.removesuffix("_sym") in EXCLUDED_DATASETS)
    if excluded:
        print("Excluded datasets: " + ", ".join(excluded), file=sys.stderr)
        for dataset in excluded:
            rows.pop(dataset)

    datasets, _ = order_datasets(list(rows))
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        r"\begin{tabular}{lcccccc}",
        r"\toprule",
        r"& \multicolumn{4}{c}{Peak memory usage (GB)} & \multicolumn{2}{c}{\% Sketched} \\",
        r"Dataset & Total & CF & Sketching & Recovery & Vertices & Edges \\",
        r"\midrule",
    ]
    for dataset in datasets:
        row = rows[dataset]
        vertex_coverage = percent(
            number(row, "peak_num_sketched_vertices"), number(row, "num_nodes"), "vertices", dataset
        )
        edge_coverage = percent(
            number(row, "peak_num_sketched_edges"), number(row, "peak_total_edges"), "edges", dataset
        )
        values = [
            f"{gb(row, 'peak_total_bytes'):.2f}",
            f"{gb(row, 'peak_cf_bytes'):.2f}",
            f"{gb(row, 'peak_sketch_bytes'):.2f}",
            f"{gb(row, 'peak_recovery_bytes'):.2f}",
            format_percent(vertex_coverage),
            format_percent(edge_coverage),
        ]
        lines.append(latex_escape(full_dataset_name(dataset)) + " & " + " & ".join(values) + r" \\")
    lines.extend([r"\bottomrule", r"\end{tabular}", ""])
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results", type=Path,
        default=Path.home() / "research/last_minute_clone/speed_results_REVISION_fixed",
        help="corrected HybridScale main-experiment result directory",
    )
    parser.add_argument(
        "-o", "--output", type=Path,
        default=Path("results/hybridscale_main_memory_table.tex"),
        help="LaTeX table output path",
    )
    args = parser.parse_args()

    try:
        write_table(read_rows(args.results), args.output)
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
