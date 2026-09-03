#!/usr/bin/env python3
"""Write a LaTeX table of system update-phase times by query workload.

The zero-interleaved workload is read from a main-results directory. The 0.1
and 1.0 interleaved-query workloads are read from an interleaved-results
directory. Static insert/delete phase timers include interleaved query work,
so each table entry is insert time + delete time, excluding post-stream
queries from the zero-interleaved workload.
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path
from typing import TypedDict

from dataset_metadata import canonical_dataset_name, full_dataset_name, order_datasets

QUERY_RATES = (0.0, 0.1, 1.0)
QUERY_RATE_LABELS = {
    0.0: "0 interleaved queries",
    0.1: "1 query per 10 updates",
    1.0: "1 query per update",
}
EXCLUDED_DATASETS = {"kron_17"}
SYSTEM_DEFAULTS = {
    "hybridscale": {
        "zero_results": Path.home() / "research/speed_results_REVISION",
        "interleaved_results": Path.home() / "research/speed_results_REVISION_interleaved",
        "output": Path("results/hybridscale_interleaved_total_time_table.tex"),
    },
    "cupcake": {
        "zero_results": Path.home() / "research/speed_results_REVISION_cupcake",
        "interleaved_results": Path.home() / "research/last_minute_clone/speed_results_REVISION_cupcake_interleave",
        "output": Path("results/cupcake_interleaved_total_time_table.tex"),
    },
}


class Run(TypedDict):
    dataset: str
    query_rate: float
    total_time_ms: float
    config: str


def number(row: dict[str, str], field: str) -> float:
    try:
        value = float(row[field])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"missing numeric {field!r}") from exc
    if not math.isfinite(value) or value < 0:
        raise ValueError(f"invalid {field!r}: {value!r}")
    return value


def rate_key(value: float) -> float:
    for expected in QUERY_RATES:
        if math.isclose(value, expected, rel_tol=0.0, abs_tol=1e-9):
            return expected
    raise ValueError(f"unsupported interleaved query rate {value:g}")


def static_update_time_ms(result: dict[str, str]) -> float:
    return sum(number(result, field) for field in (
        "insert_phase_wall_ms",
        "delete_phase_wall_ms",
    ))


def is_selected_system(manifest: dict[str, str], system: str) -> bool:
    if system == "hybridscale":
        return manifest.get("algo") == "mpi_batch" and manifest.get("hybrid", "").lower() == "true"
    return (
        manifest.get("algo") == "mpi"
        and manifest.get("cutset") == "ett"
        and manifest.get("sketch") == "fixed"
        and manifest.get("hybrid", "").lower() == "false"
    )


def read_runs(root: Path, expected_rates: set[float], system: str) -> list[Run]:
    runs: list[Run] = []
    manifests = sorted(
        path for path in root.glob("*_sym/speed_manifest_*.tsv")
        if not path.name.endswith("_config_summary.tsv")
    )
    for manifest_path in manifests:
        with manifest_path.open(encoding="utf-8", newline="") as manifest_file:
            for manifest in csv.DictReader(manifest_file, delimiter="\t"):
                if not is_selected_system(manifest, system):
                    continue
                rate = rate_key(number(manifest, "interleaved_queries_per_update"))
                if rate not in expected_rates:
                    continue
                result_path = manifest.get("result_path", "")
                if not result_path:
                    raise ValueError(f"missing result path in {manifest_path}")
                path = manifest_path.parent / result_path
                if not path.is_file():
                    continue
                with path.open(encoding="utf-8", newline="") as result_file:
                    result = next(csv.DictReader(result_file, delimiter="\t"), None)
                if result is None:
                    raise ValueError(f"empty benchmark result {path}")
                runs.append({
                    "dataset": manifest["dataset"],
                    "query_rate": rate,
                    "total_time_ms": static_update_time_ms(result),
                    "config": manifest["config"],
                })
    return runs


def select_runs(runs: list[Run]) -> dict[str, Run]:
    selected: dict[str, Run] = {}
    for run in runs:
        current = selected.get(run["dataset"])
        if current is not None:
            raise ValueError(
                f"multiple matching runs for {run['dataset']} at "
                f"{run['query_rate']:g} interleaved queries/update"
            )
        selected[run["dataset"]] = run
    return selected


def format_duration(milliseconds: float) -> str:
    total_seconds = int(round(milliseconds / 1000.0))
    hours, remainder = divmod(total_seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{seconds:02d}"
    return f"{minutes}:{seconds:02d}"


def latex_escape(text: str) -> str:
    return text.replace("_", r"\_").replace("&", r"\&")


def write_table(
    runs_by_rate: dict[float, dict[str, Run]], output: Path, complete_only: bool
) -> None:
    all_datasets = set.union(*(set(runs_by_rate[rate]) for rate in QUERY_RATES))
    datasets = (
        set.intersection(*(set(runs_by_rate[rate]) for rate in QUERY_RATES))
        if complete_only else all_datasets
    )
    incomplete = sorted(all_datasets - datasets)
    if incomplete:
        print("Excluded incomplete datasets: " + ", ".join(incomplete), file=sys.stderr)
    excluded = sorted(
        dataset for dataset in datasets
        if canonical_dataset_name(dataset) in EXCLUDED_DATASETS
    )
    if excluded:
        print("Excluded datasets: " + ", ".join(excluded), file=sys.stderr)
        datasets.difference_update(excluded)
        for rate in QUERY_RATES:
            for dataset in excluded:
                runs_by_rate[rate].pop(dataset, None)
    missing = {
        rate: sorted(datasets - set(runs_by_rate[rate]))
        for rate in QUERY_RATES
    }
    if any(missing.values()):
        for rate, names in missing.items():
            if names:
                print(
                    f"Warning: missing {QUERY_RATE_LABELS[rate]} for "
                    + ", ".join(names),
                    file=sys.stderr,
                )

    ordered_datasets, _ = order_datasets(list(datasets))
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        r"\begin{tabular}{lrrr}",
        r"\toprule",
        r"\multicolumn{4}{c}{Update-phase time (insert + delete)} \\",
        "Dataset & " + " & ".join(QUERY_RATE_LABELS[rate] for rate in QUERY_RATES) + r" \\",
        r"\midrule",
    ]
    for dataset in ordered_datasets:
        values = [
            format_duration(runs_by_rate[rate][dataset]["total_time_ms"])
            if dataset in runs_by_rate[rate] else "--"
            for rate in QUERY_RATES
        ]
        lines.append(latex_escape(full_dataset_name(dataset)) + " & " + " & ".join(values) + r" \\")
    lines.extend([r"\bottomrule", r"\end{tabular}", ""])
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--system", choices=sorted(SYSTEM_DEFAULTS), default="hybridscale")
    parser.add_argument(
        "--zero-results", type=Path,
        help="main results containing the zero-interleaved workload",
    )
    parser.add_argument(
        "--interleaved-results", type=Path,
        help="results containing 0.1 and 1.0 interleaved workloads",
    )
    parser.add_argument(
        "-o", "--output", type=Path,
        help="LaTeX table output path",
    )
    parser.add_argument(
        "--complete-only", action="store_true",
        help="omit datasets without completed runs at all three query rates",
    )
    args = parser.parse_args()
    defaults = SYSTEM_DEFAULTS[args.system]
    zero_results = args.zero_results or defaults["zero_results"]
    interleaved_results = args.interleaved_results or defaults["interleaved_results"]
    output = args.output or defaults["output"]

    try:
        zero_runs = select_runs(read_runs(zero_results, {0.0}, args.system))
        interleaved_runs = read_runs(interleaved_results, {0.1, 1.0}, args.system)
        runs_by_rate = {
            0.0: zero_runs,
            0.1: select_runs([run for run in interleaved_runs if run["query_rate"] == 0.1]),
            1.0: select_runs([run for run in interleaved_runs if run["query_rate"] == 1.0]),
        }
        write_table(runs_by_rate, output, args.complete_only)
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
