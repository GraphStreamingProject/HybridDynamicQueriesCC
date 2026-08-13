#!/usr/bin/env python3
"""Create one-row-per-configuration summaries from an exact batch run manifest."""

from __future__ import annotations

import argparse
import csv
import math
import os
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

NAN = "nan"

PROFILE_METRIC_FIELDS = [
    "peak_update_idx", "peak_total_bytes",
    "run_peak_bytes_mean", "run_peak_bytes_stddev", "peak_cf_bytes",
    "peak_driver_bytes", "peak_recovery_bytes", "peak_sketch_bytes",
    "peak_query_tree_bytes", "peak_top_level_lct_bytes", "maximal_tier_at_peak",
    "peak_total_edges", "peak_num_sketched_vertices", "peak_num_sketch_insertions",
    "peak_num_sketch_deletions", "peak_num_sketched_edges",
    "peak_num_direct_sketch_inserts", "max_link_tier",
]

PROFILE_TEXT_FIELDS = ["source_run", "peak_phase"]

SPEED_METRIC_FIELDS = [
    "num_nodes", "max_link_tier", "updates_per_sec", "insert_updates_per_sec",
    "insert_update_calls_per_sec", "delete_updates_per_sec",
    "delete_update_calls_per_sec", "insert_update_calls_ms", "insert_phase_wall_ms",
    "delete_update_calls_ms", "delete_phase_wall_ms", "num_post_queries",
    "post_queries_ms", "num_interleaved_queries", "interleaved_queries_ms",
    "num_queries", "query_time_ms", "update_time_ms", "queries_per_sec",
    "average_query_latency_us", "run_updates_per_sec_mean",
    "run_updates_per_sec_stddev", "sketched_edges", "direct_sketch_inserts",
    "sketched_vertices",
]

CONFIG_FIELDS = [
    "dataset", "dataset_num_nodes", "dataset_num_edges", "config", "algo", "cutset", "sketch", "hybrid",
    "hybrid_threshold", "hybrid_threshold_multiplier", "batch_size", "height_factor",
    "num_tiers", "np", "recovery_size", "move_to_sketch", "stream_seed",
    "post_queries_per_update", "interleaved_queries_per_update",
    "speed_interval", "profile_interval", "static_graph", "do_deletions",
]

CF_SENTINEL_CONFIG_FIELDS = [
    "hybrid_threshold", "hybrid_threshold_multiplier", "batch_size", "num_tiers", "np",
    "recovery_size", "move_to_sketch",
]

MANIFEST_PATH_FIELDS = [
    "stream_path", "result_path", "intervals_path", "static_snapshot_path",
    "space_path", "space_summary_path", "hybrid_summary_path",
    "benchmark_summary_path", "status_path", "stdout_path", "stderr_path",
]


def read_tsv(path: str) -> list[dict[str, str]]:
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def resolve_manifest_paths(rows: list[dict[str, str]], manifest_path: str) -> None:
    """Make manifest artifact paths absolute relative to their manifest file."""
    manifest_dir = Path(manifest_path).resolve().parent
    for row in rows:
        for field in MANIFEST_PATH_FIELDS:
            path = row.get(field, "")
            if path and not os.path.isabs(path):
                row[field] = str(manifest_dir / path)


def number(row: dict[str, str], key: str, default: float = 0.0) -> float:
    value = row.get(key, "")
    try:
        return float(value) if value not in (None, "") else default
    except ValueError:
        return default


def integer_text(value: float) -> str:
    return str(int(value)) if math.isfinite(value) else ""


def ratio_per_second(count: float, milliseconds: float) -> float:
    return count * 1000.0 / milliseconds if milliseconds > 0 else 0.0


def weighted_mean(values: Iterable[tuple[float, float]]) -> float:
    pairs = [(value, weight) for value, weight in values if weight > 0]
    total_weight = sum(weight for _, weight in pairs)
    return sum(value * weight for value, weight in pairs) / total_weight if total_weight else 0.0


def config_key(row: dict[str, str]) -> tuple[str, ...]:
    return tuple(row.get(field, "") for field in CONFIG_FIELDS)


def config_columns(row: dict[str, str]) -> dict[str, Any]:
    output: dict[str, Any] = {field: row.get(field, "") for field in CONFIG_FIELDS}
    if row.get("algo", "") == "cf":
        for field in CF_SENTINEL_CONFIG_FIELDS:
            output[field] = "-1"
    nodes = number(row, "dataset_num_nodes")
    edges = number(row, "dataset_num_edges")
    output["average_degree"] = 2.0 * edges / nodes if nodes > 0 else NAN
    return output


def infer_multiplier(row: dict[str, str]) -> str:
    if row.get("algo", "") == "cf":
        return "-1"
    explicit = row.get("hybrid_threshold_multiplier", "")
    if explicit:
        return explicit
    threshold = number(row, "hybrid_threshold")
    tiers = number(row, "num_tiers")
    if threshold > 0 and tiers > 0 and threshold % tiers == 0:
        return integer_text(threshold / tiers)
    return ""


def run_outcome(manifest: dict[str, str], has_results: bool) -> tuple[str, str, str]:
    """Return normalized outcome, exit code, and detail for one manifested run."""
    status_path = manifest.get("status_path", "")
    state = ""
    exit_code = ""
    detail = ""
    if status_path and os.path.isfile(status_path):
        status_rows = read_tsv(status_path)
        if status_rows:
            state = status_rows[-1].get("state", "").lower()
            exit_code = status_rows[-1].get("exit_code", "")
            detail = status_rows[-1].get("detail", "")

    stderr_text = ""
    stderr_path = manifest.get("stderr_path", "")
    if stderr_path and os.path.isfile(stderr_path):
        try:
            stderr_text = Path(stderr_path).read_text(encoding="utf-8", errors="replace").lower()
        except OSError:
            stderr_text = ""
    evidence = f"{detail} {stderr_text}".lower()

    if has_results and state in ("", "complete"):
        return "NORMAL", exit_code or "0", detail
    if any(token in evidence for token in (
        "out of memory", "out-of-memory", "cannot allocate memory",
        "killed_sigkill_possible_oom",
    )) or "oom" in evidence.split() or exit_code == "137":
        return "OOM", exit_code, detail or "exit_137_or_oom_message"
    if any(token in evidence for token in (
        "time limit", "timelimit", "timeout", "terminated_possible_timeout",
    )) or exit_code == "143":
        return "TIMEOUT", exit_code, detail or "exit_143_or_timeout_message"
    if "cancel" in evidence:
        return "CANCELLED", exit_code, detail
    if state == "running":
        return "RUNNING_OR_INTERRUPTED", exit_code, detail
    if state == "failed" or exit_code not in ("", "0"):
        return "FAILED", exit_code, detail or (f"exit_{exit_code}" if exit_code else "")
    if has_results:
        return "NORMAL", exit_code or "0", detail
    return "MISSING", exit_code, detail


def add_outcome_columns(
    output: dict[str, Any], group: list[dict[str, str]], completed_manifests: set[int]
) -> None:
    outcomes = [run_outcome(manifest, index in completed_manifests)
                for index, manifest in enumerate(group)]
    failed = [outcome for outcome, _, _ in outcomes if outcome != "NORMAL"]
    completed = sum(outcome == "NORMAL" for outcome, _, _ in outcomes)
    if completed == len(group):
        aggregate = "NORMAL"
    elif completed > 0:
        aggregate = "PARTIAL"
    elif len(set(failed)) == 1:
        aggregate = failed[0]
    else:
        aggregate = "MIXED_FAILURE"
    output["expected_runs"] = len(group)
    output["completed_runs"] = completed
    output["outcome"] = aggregate
    output["failed_run_outcomes"] = ",".join(sorted(set(failed)))
    output["exit_codes"] = ",".join(code for _, code, _ in outcomes if code)
    output["failure_details"] = " | ".join(detail for outcome, _, detail in outcomes
                                                  if outcome != "NORMAL" and detail)
    output["status"] = "complete" if completed == len(group) else ("partial" if completed else "failed")


def add_nan_defaults(output: dict[str, Any], fields: list[str]) -> None:
    for field in fields:
        if output.get(field, "") in (None, ""):
            output[field] = NAN


def add_profile_defaults(output: dict[str, Any]) -> None:
    add_nan_defaults(output, PROFILE_METRIC_FIELDS)
    for field in PROFILE_TEXT_FIELDS:
        output.setdefault(field, "")


def summarize_profile(group: list[dict[str, str]]) -> dict[str, Any]:
    output: dict[str, Any] = config_columns(group[0])
    output["hybrid_threshold_multiplier"] = infer_multiplier(group[0])
    completed_manifests: set[int] = set()
    candidates: list[tuple[float, dict[str, str], dict[str, str]]] = []
    run_peaks: list[float] = []
    max_link_tiers: list[float] = []

    for manifest_index, manifest in enumerate(group):
        hybrid_path = manifest.get("hybrid_summary_path", "")
        standard_path = manifest.get("space_summary_path", "")
        source = hybrid_path if hybrid_path and os.path.isfile(hybrid_path) else standard_path
        if not source or not os.path.isfile(source):
            continue
        rows = read_tsv(source)
        if not rows:
            continue
        if run_outcome(manifest, True)[0] != "NORMAL":
            continue
        current_run_totals: list[float] = []
        for raw_snapshot in rows:
            snapshot = dict(raw_snapshot)
            if "total_sketching_system_space_bytes" not in snapshot:
                continue
            sketch_system_bytes = number(snapshot, "total_sketching_system_space_bytes")
            if "total_cf_bytes" in snapshot:
                total = sketch_system_bytes + sum(number(snapshot, key) for key in (
                    "total_cf_bytes", "total_driver_bytes", "total_recovery_bytes",
                ))
            else:
                total = sketch_system_bytes
            candidates.append((total, snapshot, manifest))
            current_run_totals.append(total)
        if not current_run_totals:
            continue
        completed_manifests.add(manifest_index)
        run_peaks.append(max(current_run_totals))

        benchmark_summary = manifest.get("benchmark_summary_path", "")
        if benchmark_summary and os.path.isfile(benchmark_summary):
            for summary_row in read_tsv(benchmark_summary):
                max_link_tiers.append(number(summary_row, "max_link_tier", -1))

    add_outcome_columns(output, group, completed_manifests)
    if not candidates:
        add_profile_defaults(output)
        return output

    peak_total, peak, source_manifest = max(candidates, key=lambda item: item[0])
    peak_update = number(peak, "update_idx", -1)
    insertion_updates = number(source_manifest, "dataset_num_edges", -1)
    if source_manifest.get("static_graph", "false") != "true":
        peak_phase = "stream"
    elif insertion_updates >= 0 and peak_update == insertion_updates:
        peak_phase = "post_insert"
    elif insertion_updates >= 0 and peak_update > insertion_updates:
        peak_phase = "delete"
    elif peak_update >= 0:
        peak_phase = "insert"
    else:
        peak_phase = "unknown"
    is_cf = source_manifest.get("algo", "") == "cf"
    if is_cf:
        peak_cf_bytes = peak.get("total_space_bytes", "")
        peak_driver_bytes = "0"
        peak_recovery_bytes = "0"
        peak_sketch_bytes = "0"
        peak_total_edges = "-1"
        peak_num_sketched_vertices = "-1"
        peak_num_sketch_insertions = "-1"
        peak_num_sketch_deletions = "-1"
        peak_num_sketched_edges = "-1"
        peak_num_direct_sketch_inserts = "-1"
    elif "total_cf_bytes" not in peak:
        peak_cf_bytes = "0"
        peak_driver_bytes = "0"
        peak_recovery_bytes = "0"
        peak_sketch_bytes = integer_text(peak_total)
        peak_total_edges = "-1"
        peak_num_sketched_vertices = "-1"
        peak_num_sketch_insertions = "-1"
        peak_num_sketch_deletions = "-1"
        peak_num_sketched_edges = "-1"
        peak_num_direct_sketch_inserts = "-1"
    else:
        peak_cf_bytes = peak.get("total_cf_bytes", "")
        peak_driver_bytes = peak.get("total_driver_bytes", "")
        peak_recovery_bytes = peak.get("total_recovery_bytes", "")
        peak_sketch_bytes = peak.get(
            "total_sketching_system_space_bytes",
            "",
        )
        peak_total_edges = peak.get("total_edges", "")
        peak_num_sketched_vertices = peak.get("num_sketched_vertices", "")
        peak_num_sketch_insertions = peak.get("num_sketch_insertions", "")
        peak_num_sketch_deletions = peak.get("num_sketch_deletions", "")
        peak_num_sketched_edges = peak.get("num_sketched_edges", "")
        peak_num_direct_sketch_inserts = peak.get("num_direct_sketch_inserts", "")
    output.update({
        "source_run": source_manifest.get("run_suffix", ""),
        "peak_update_idx": peak.get("update_idx", ""),
        "peak_phase": peak_phase,
        "peak_total_bytes": integer_text(peak_total),
        "run_peak_bytes_mean": statistics.fmean(run_peaks),
        "run_peak_bytes_stddev": statistics.stdev(run_peaks) if len(run_peaks) > 1 else 0.0,
        "peak_cf_bytes": peak_cf_bytes,
        "peak_driver_bytes": peak_driver_bytes,
        "peak_recovery_bytes": peak_recovery_bytes,
        "peak_sketch_bytes": peak_sketch_bytes,
        "peak_query_tree_bytes": peak.get("query_tree_bytes", ""),
        "peak_top_level_lct_bytes": peak.get("top_level_lct_bytes", ""),
        "maximal_tier_at_peak": peak.get("maximal_tier", ""),
        "peak_total_edges": peak_total_edges,
        "peak_num_sketched_vertices": peak_num_sketched_vertices,
        "peak_num_sketch_insertions": peak_num_sketch_insertions,
        "peak_num_sketch_deletions": peak_num_sketch_deletions,
        "peak_num_sketched_edges": peak_num_sketched_edges,
        "peak_num_direct_sketch_inserts": peak_num_direct_sketch_inserts,
        "max_link_tier": integer_text(max(max_link_tiers)) if max_link_tiers else "",
    })
    add_profile_defaults(output)
    return output


def summarize_speed(group: list[dict[str, str]]) -> dict[str, Any]:
    output: dict[str, Any] = config_columns(group[0])
    output["hybrid_threshold_multiplier"] = infer_multiplier(group[0])
    completed: list[tuple[dict[str, str], dict[str, str]]] = []
    completed_manifests: set[int] = set()
    for manifest_index, manifest in enumerate(group):
        result_path = manifest.get("result_path", "")
        if result_path and os.path.isfile(result_path):
            result_rows = read_tsv(result_path)
            if result_rows and run_outcome(manifest, True)[0] == "NORMAL":
                completed.append((result_rows[0], manifest))
                completed_manifests.add(manifest_index)

    add_outcome_columns(output, group, completed_manifests)
    if not completed:
        add_nan_defaults(output, SPEED_METRIC_FIELDS)
        return output
    rows = [row for row, _ in completed]

    is_static = "edges" in rows[0]
    output["num_nodes"] = rows[0].get("num_nodes", "")
    output["height_factor"] = rows[0].get("height_factor", "")
    output["max_link_tier"] = integer_text(max(number(row, "max_link_tier", -1) for row in rows))

    if is_static:
        insert_updates = sum(number(row, "edges") for row in rows)
        insert_call_ms = sum(number(row, "insert_update_calls_ms") for row in rows)
        insert_wall_ms = sum(number(row, "insert_phase_wall_ms") for row in rows)
        deletion_updates = sum(number(row, "edges") for row, manifest in completed
                               if manifest.get("do_deletions", "false") == "true")
        delete_call_ms = sum(number(row, "delete_update_calls_ms") for row in rows)
        delete_wall_ms = sum(number(row, "delete_phase_wall_ms") for row in rows)
        post_queries = sum(number(row, "num_post_queries") for row in rows)
        post_query_ms = sum(number(row, "post_queries_ms") for row in rows)
        interleaved_queries = sum(number(row, "num_interleaved_queries") for row in rows)
        interleaved_query_ms = sum(number(row, "interleaved_queries_ms") for row in rows)
        all_queries = post_queries + interleaved_queries
        all_query_ms = post_query_ms + interleaved_query_ms
        output.update({
            "updates_per_sec": ratio_per_second(insert_updates + deletion_updates, insert_wall_ms + delete_wall_ms),
            "insert_updates_per_sec": ratio_per_second(insert_updates, insert_wall_ms),
            "insert_update_calls_per_sec": ratio_per_second(insert_updates, insert_call_ms),
            "delete_updates_per_sec": ratio_per_second(deletion_updates, delete_wall_ms),
            "delete_update_calls_per_sec": ratio_per_second(deletion_updates, delete_call_ms),
            "insert_update_calls_ms": insert_call_ms,
            "insert_phase_wall_ms": insert_wall_ms,
            "delete_update_calls_ms": delete_call_ms,
            "delete_phase_wall_ms": delete_wall_ms,
            "num_post_queries": integer_text(post_queries),
            "post_queries_ms": post_query_ms,
            "num_interleaved_queries": integer_text(interleaved_queries),
            "interleaved_queries_ms": interleaved_query_ms,
            "queries_per_sec": ratio_per_second(all_queries, all_query_ms),
            "average_query_latency_us": all_query_ms * 1000.0 / all_queries if all_queries else 0.0,
        })
        run_rates = [ratio_per_second(number(row, "edges"), number(row, "insert_phase_wall_ms")) for row in rows]
    else:
        updates = sum(number(row, "num_updates") for row in rows)
        update_ms = sum(number(row, "update_time_ms") for row in rows)
        queries = sum(number(row, "num_queries") for row in rows)
        query_ms = sum(number(row, "query_time_ms") for row in rows)
        output.update({
            "updates_per_sec": ratio_per_second(updates, update_ms),
            "insert_updates_per_sec": NAN,
            "delete_updates_per_sec": NAN,
            "update_time_ms": update_ms,
            "num_queries": integer_text(queries),
            "query_time_ms": query_ms,
            "queries_per_sec": ratio_per_second(queries, query_ms),
            "average_query_latency_us": query_ms * 1000.0 / queries if queries else 0.0,
        })
        run_rates = [number(row, "updates_per_sec") for row in rows]

    output["run_updates_per_sec_mean"] = statistics.fmean(run_rates)
    output["run_updates_per_sec_stddev"] = statistics.stdev(run_rates) if len(run_rates) > 1 else 0.0
    for key in ("sketched_edges", "direct_sketch_inserts", "sketched_vertices"):
        if key in rows[0]:
            output[key] = statistics.fmean(number(row, key) for row in rows)
    add_nan_defaults(output, SPEED_METRIC_FIELDS)
    return output


def write_summary(rows: list[dict[str, Any]], path: str) -> None:
    preferred = CONFIG_FIELDS + [
        "expected_runs", "completed_runs", "status", "outcome", "failed_run_outcomes",
        "exit_codes", "failure_details",
    ]
    fields = preferred + sorted({key for row in rows for key in row} - set(preferred))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t", extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, help="Exact manifest emitted by a batch runner")
    parser.add_argument("--output", help="Output TSV (default: manifest name with _config_summary)")
    args = parser.parse_args()

    manifests = read_tsv(args.manifest)
    resolve_manifest_paths(manifests, args.manifest)
    if not manifests:
        print(f"Error: manifest is empty: {args.manifest}", file=sys.stderr)
        return 2
    bench_types = {row.get("bench_type", "") for row in manifests}
    if len(bench_types) != 1 or next(iter(bench_types)) not in {"speed", "profile"}:
        print("Error: manifest must contain exactly one benchmark type: speed or profile", file=sys.stderr)
        return 2
    bench_type = next(iter(bench_types))
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in manifests:
        groups[config_key(row)].append(row)
    summarize = summarize_profile if bench_type == "profile" else summarize_speed
    summaries = [summarize(group) for group in groups.values()]
    output = args.output or str(Path(args.manifest).with_suffix("")) + "_config_summary.tsv"
    write_summary(summaries, output)
    print(f"Wrote {output} ({len(summaries)} configurations)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
