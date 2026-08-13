#!/usr/bin/env python3
"""Append a compatible completed run from an older batch manifest."""

from __future__ import annotations

import argparse
import csv
import os
import sys
from pathlib import Path

MATCH_FIELDS = (
    "dataset",
    "dataset_num_nodes",
    "dataset_num_edges",
    "bench_type",
    "config",
    "algo",
    "cutset",
    "sketch",
    "hybrid",
    "hybrid_threshold",
    "hybrid_threshold_multiplier",
    "batch_size",
    "height_factor",
    "num_tiers",
    "np",
    "recovery_size",
    "move_to_sketch",
    "stream_seed",
    "post_queries_per_update",
    "interleaved_queries_per_update",
    "speed_interval",
    "profile_interval",
    "static_graph",
    "do_deletions",
    "stream_path",
)

PATH_FIELDS = {
    "stream_path", "result_path", "intervals_path", "static_snapshot_path",
    "space_path", "space_summary_path", "hybrid_summary_path",
    "benchmark_summary_path", "status_path", "stdout_path", "stderr_path",
}


def read_rows(path: Path) -> list[dict[str, str]]:
    try:
        with path.open(encoding="utf-8", newline="") as handle:
            return list(csv.DictReader(handle, delimiter="\t"))
    except (OSError, csv.Error):
        return []


def resolve_path(path: str, manifest_dir: Path) -> str:
    if not path or os.path.isabs(path):
        return path
    return str(manifest_dir / path)


def status_is_complete(path: str) -> bool:
    if not path or not os.path.isfile(path):
        return False
    rows = read_rows(Path(path))
    return bool(rows and rows[-1].get("state") == "complete" and rows[-1].get("exit_code") == "0")


def tsv_has_column(path: str, column: str) -> bool:
    if not path or not os.path.isfile(path):
        return False
    try:
        with open(path, encoding="utf-8", newline="") as handle:
            reader = csv.reader(handle, delimiter="\t")
            return column in next(reader, [])
    except (OSError, csv.Error):
        return False


def primary_artifact(row: dict[str, str], manifest_dir: Path) -> str:
    if row.get("bench_type") != "profile":
        return resolve_path(row.get("result_path", ""), manifest_dir)
    if row.get("hybrid", "").lower() == "true":
        return resolve_path(row.get("hybrid_summary_path", ""), manifest_dir)
    return resolve_path(row.get("space_summary_path", ""), manifest_dir)


def artifacts_are_usable(row: dict[str, str], manifest_dir: Path) -> bool:
    if not status_is_complete(resolve_path(row.get("status_path", ""), manifest_dir)):
        return False
    if row.get("bench_type") != "profile":
        artifact = primary_artifact(row, manifest_dir)
        return bool(artifact and os.path.isfile(artifact))

    summary_path = primary_artifact(row, manifest_dir)
    space_path = resolve_path(row.get("space_path", ""), manifest_dir)
    benchmark_summary_path = resolve_path(row.get("benchmark_summary_path", ""), manifest_dir)
    return (
        bool(space_path)
        and os.path.isfile(space_path)
        and bool(benchmark_summary_path)
        and os.path.isfile(benchmark_summary_path)
        and tsv_has_column(summary_path, "total_sketching_system_space_bytes")
    )


def matches(
    candidate: dict[str, str], candidate_dir: Path, prior: dict[str, str], prior_dir: Path
) -> bool:
    for field in MATCH_FIELDS:
        candidate_value = candidate.get(field, "")
        prior_value = prior.get(field, "")
        if field in PATH_FIELDS:
            candidate_value = resolve_path(candidate_value, candidate_dir)
            prior_value = resolve_path(prior_value, prior_dir)
        if candidate_value != prior_value:
            return False
    return True


def append_reused_row(
    destination: Path,
    invocation_id: str,
    candidate_values: list[str],
) -> str | None:
    with destination.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        fieldnames = reader.fieldnames or []
        destination_rows = list(reader)

    expected_values = len(fieldnames) - 1
    if not fieldnames or fieldnames[0] != "invocation_id" or len(candidate_values) != expected_values:
        raise ValueError(
            f"candidate has {len(candidate_values)} values; manifest requires {expected_values}"
        )

    candidate = dict(zip(fieldnames[1:], candidate_values))
    destination_dir = destination.parent
    used_artifacts = {primary_artifact(row, destination_dir) for row in destination_rows}
    manifest_pattern = f"{candidate['bench_type']}_manifest_*.tsv"
    prior_manifests = sorted(
        (path for path in destination.parent.glob(manifest_pattern) if path != destination),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )

    for prior_manifest in prior_manifests:
        prior_dir = prior_manifest.parent
        for prior in reversed(read_rows(prior_manifest)):
            artifact = primary_artifact(prior, prior_dir)
            if not artifact or artifact in used_artifacts:
                continue
            if not matches(candidate, destination_dir, prior, prior_dir) or not artifacts_are_usable(prior, prior_dir):
                continue

            reused = {field: prior.get(field, "") for field in fieldnames}
            reused["invocation_id"] = invocation_id
            reused["run_number"] = candidate.get("run_number", "")
            with destination.open("a", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t", lineterminator="\n")
                writer.writerow(reused)
            return f"{prior_manifest}:{prior.get('run_suffix', '')}"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--invocation-id", required=True)
    parser.add_argument("candidate_values", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    try:
        reused_from = append_reused_row(
            args.manifest.resolve(),
            args.invocation_id,
            args.candidate_values,
        )
    except (OSError, ValueError) as exc:
        print(f"Error checking reusable runs: {exc}", file=sys.stderr)
        return 2
    if reused_from is None:
        return 1
    print(reused_from)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
