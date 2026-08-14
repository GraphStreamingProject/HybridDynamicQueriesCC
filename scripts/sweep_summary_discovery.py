"""Discover per-dataset profile or speed summaries, including SLURM manifests."""

from __future__ import annotations

import subprocess
import sys
import csv
from pathlib import Path


def summary_path_for_manifest(manifest: Path) -> Path:
    return manifest.with_suffix("").with_name(f"{manifest.stem}_config_summary.tsv")


def summarize_manifest(manifest: Path, refresh: bool = False) -> Path:
    """Create the normal sidecar summary for one completed profile manifest."""
    summary = summary_path_for_manifest(manifest)
    if summary.is_file() and not refresh:
        return summary
    summarizer = Path(__file__).with_name("summarize_batch_results.py")
    result = subprocess.run(
        [sys.executable, str(summarizer), "--manifest", str(manifest)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0 or not summary.is_file():
        detail = result.stderr.strip() or result.stdout.strip() or "no summary was produced"
        raise ValueError(f"failed to summarize SLURM manifest {manifest}: {detail}")
    return summary


def dataset_from_summary(path: Path) -> str:
    with path.open(encoding="utf-8", newline="") as summary_file:
        rows = list(csv.DictReader(summary_file, delimiter="\t"))
    datasets = {row.get("dataset", "").strip() for row in rows if row.get("dataset", "").strip()}
    if len(datasets) != 1:
        raise ValueError(f"expected one dataset in summary {path}, found {len(datasets)}")
    return datasets.pop()


def newest_path_by_dataset(paths: list[Path], dataset_reader) -> dict[str, Path]:
    """Select the newest artifact for each dataset, preferring usable summaries."""
    selected: dict[str, Path] = {}
    for path in paths:
        dataset = dataset_reader(path)
        current = selected.get(dataset)
        if current is None or artifact_sort_key(path) > artifact_sort_key(current):
            selected[dataset] = path
    return selected


def summary_is_usable(path: Path) -> bool:
    """A completed sweep needs a normal CF reference and at least one hybrid."""
    if not path.name.endswith("_config_summary.tsv"):
        return False
    with path.open(encoding="utf-8", newline="") as summary_file:
        rows = list(csv.DictReader(summary_file, delimiter="\t"))
    normal = [row for row in rows if row.get("outcome", "NORMAL") == "NORMAL"]
    return any(row.get("algo", "") == "cf" for row in normal) and any(
        row.get("hybrid", "").lower() == "true" for row in normal
    )


def artifact_sort_key(path: Path) -> tuple[bool, float, str]:
    return (summary_is_usable(path), path.stat().st_mtime, path.name)


def dataset_from_manifest(path: Path) -> str:
    with path.open(encoding="utf-8", newline="") as manifest_file:
        first_row = next(csv.DictReader(manifest_file, delimiter="\t"), None)
    if first_row is None or not first_row.get("dataset", "").strip():
        raise ValueError(f"manifest has no dataset row: {path}")
    return first_row["dataset"].strip()


def summary_paths_by_dataset(
    root: Path, bench_type: str = "profile", refresh_summaries: bool = False
) -> dict[str, Path]:
    """Find one summary per dataset, generating sidecars for SLURM manifests."""
    if bench_type not in {"profile", "speed"}:
        raise ValueError(f"unsupported benchmark type: {bench_type}")
    if not root.is_dir():
        raise ValueError(f"result directory does not exist: {root}")

    manifests = sorted(
        path for path in root.rglob(f"{bench_type}_manifest_*.tsv")
        if not path.name.endswith("_config_summary.tsv")
    )
    summaries = [] if refresh_summaries else sorted(root.rglob(f"{bench_type}_manifest_*_config_summary.tsv"))
    if not summaries:
        if not manifests:
            raise ValueError(
                f"no {bench_type} summaries or {bench_type} manifests found recursively under {root}"
            )
        summaries = [summarize_manifest(manifest, refresh=refresh_summaries) for manifest in manifests]

    return newest_path_by_dataset(summaries, dataset_from_summary)
