#!/usr/bin/env python3
"""
Plot peak memory vs. average degree (log-log) for cf/, mpi_hybrid/, and mpi_ett/
under build/results. X is average degree 2|E|/10^6 for |V|=10^6 at each update budget
(10K-60K); |E| is fixed per budget (see _EDGES_BY_DENSITY_K).

- cf/ and mpi_ett/: max over rows in each *.bin_space_summary.tsv of
  (total_space_bytes + query_tree_bytes + top_level_lct_bytes).
- mpi_hybrid/: max over rows in each *.bin_space_hybrid_summary.tsv of
  (total_cf_bytes + total_driver_bytes + total_recovery_bytes + total_sketch_bytes).
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path

try:
    import matplotlib as mpl
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, NullFormatter, NullLocator
except ImportError as e:  # pragma: no cover
    print("This script requires matplotlib: pip install matplotlib", file=sys.stderr)
    raise SystemExit(1) from e

# (results subdirectory name, legend label, plot marker shape, color)
SYSTEM_SPECS: tuple[tuple[str, str, str, str], ...] = (
    ("cf", "Cluster Forest", "o", "red"),
    ("mpi_ett", "CUPCaKE", "^", "blue"),
    ("mpi_hybrid", "HybridSCALE", "s", "green"),
)
DENSITIES_K = (10, 20, 30, 40, 50, 60)

# |E| for each update budget (10K … 60K); average degree on plots is 2|E|/10^6 with |V|=10^6.
_EDGES_BY_DENSITY_K: tuple[tuple[int, int], ...] = (
    (10, 1_339_972),
    (20, 9_416_006),
    (30, 32_876_557),
    (40, 91_171_358),
    (50, 226_803_334),
    (60, 519_995_903),
)
AVG_DEGREE_BY_DENSITY_K: dict[int, float] = {
    k: 2.0 * edges / 1_000_000.0 for k, edges in _EDGES_BY_DENSITY_K
}

# X-axis majors only: 2^1 … 2^10 (log base 2). Axis extends to 2^10.5 with no tick there.
_X_MAJOR_POW2: list[float] = [float(2**i) for i in range(1, 11)]
_X_MAX = float(2**10.5)

_Y_MIN, _Y_MAX = 1e8, 1e11
_Y_MAJOR: list[float] = [10**e for e in (8, 9, 10, 11)]


def _format_x_pow2(x: float, _pos: int) -> str:
    k = int(round(math.log2(x)))
    return rf"$2^{{{k}}}$"


def _format_memory_power10_tick(x: float, _pos: int) -> str:
    """Y tick label for log scale: decade as $10^e$ (value is in bytes)."""
    if x <= 0:
        return ""
    e = int(round(math.log10(x)))
    return rf"$10^{{{e}}}$"


_SUMMARY_COLS = ("total_space_bytes", "query_tree_bytes", "top_level_lct_bytes")

_HYBRID_SUMMARY_COLS = (
    "total_cf_bytes",
    "total_driver_bytes",
    "total_recovery_bytes",
    "total_sketch_bytes",
)


def peak_total_space_from_summary(path: Path) -> int | None:
    """
    Max over data rows of (total_space_bytes + query_tree_bytes + top_level_lct_bytes)
    in a *_bin_space_summary.tsv file.
    """
    peak: int | None = None
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        if reader.fieldnames is None:
            return None
        fieldnames = {h.strip(): h for h in reader.fieldnames}
        try:
            keys = [fieldnames[c] for c in _SUMMARY_COLS]
        except KeyError:
            return None
        for row in reader:
            try:
                total = sum(int(row[k]) for k in keys)
            except (ValueError, KeyError):
                continue
            peak = total if peak is None else max(peak, total)
    return peak


def peak_total_space_from_hybrid_summary(path: Path) -> int | None:
    """
    Max over data rows of (total_cf + total_driver + total_recovery + total_sketch_bytes)
    in a *_bin_space_hybrid_summary.tsv file (hybrid MPI profile output).
    """
    peak: int | None = None
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        if reader.fieldnames is None:
            return None
        fieldnames = {h.strip(): h for h in reader.fieldnames}
        try:
            keys = [fieldnames[c] for c in _HYBRID_SUMMARY_COLS]
        except KeyError:
            return None
        for row in reader:
            try:
                total = sum(int(row[k]) for k in keys)
            except (ValueError, KeyError):
                continue
            peak = total if peak is None else max(peak, total)
    return peak


def bin_space_summary_paths_for_run(results_root: Path, system_dir: str, density_k: int) -> list[Path]:
    """Paths to use for one (system folder, density); prefers the sift_RS naming convention."""
    sub = results_root / system_dir / f"{density_k}K"
    if not sub.is_dir():
        return []
    preferred = sub / f"sift_RS-{density_k}K_sym.bin_space_summary.tsv"
    if preferred.is_file():
        return [preferred]
    return sorted(sub.glob("*.bin_space_summary.tsv"))


def bin_space_hybrid_summary_paths_for_run(results_root: Path, system_dir: str, density_k: int) -> list[Path]:
    """Hybrid MPI: *_bin_space_hybrid_summary.tsv (not the regular *_bin_space_summary.tsv)."""
    sub = results_root / system_dir / f"{density_k}K"
    if not sub.is_dir():
        return []
    preferred = sub / f"sift_RS-{density_k}K_sym.bin_space_hybrid_summary.tsv"
    if preferred.is_file():
        return [preferred]
    return sorted(sub.glob("*.bin_space_hybrid_summary.tsv"))


def collect_series(results_root: Path) -> dict[str, list[tuple[int, int]]]:
    """For each results subdirectory, list of (density_k, peak_bytes) for runs that have data."""
    out: dict[str, list[tuple[int, int]]] = {folder: [] for folder, _, _, _ in SYSTEM_SPECS}
    for folder, _, _, _ in SYSTEM_SPECS:
        for density_k in DENSITIES_K:
            if folder == "mpi_hybrid":
                paths = bin_space_hybrid_summary_paths_for_run(results_root, folder, density_k)
                peak_fn = peak_total_space_from_hybrid_summary
            else:
                paths = bin_space_summary_paths_for_run(results_root, folder, density_k)
                peak_fn = peak_total_space_from_summary
            peaks: list[int] = []
            for p in paths:
                m = peak_fn(p)
                if m is not None:
                    peaks.append(m)
            if peaks:
                out[folder].append((density_k, max(peaks)))
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results",
        type=Path,
        default=Path("build/results"),
        help="Root directory containing cf/, mpi_hybrid/, mpi_ett/ (default: build/results)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output path (default: <results>/peak_space_by_density.pdf)",
    )
    args = parser.parse_args()
    results_root = args.results.resolve()
    out_path = args.output or (results_root / "peak_space_by_density.pdf")

    series = collect_series(results_root)
    for folder, label, _, _ in SYSTEM_SPECS:
        if not series[folder]:
            if folder == "mpi_hybrid":
                hint = (
                    f"sift_RS-<N>K_sym.bin_space_hybrid_summary.tsv "
                    f"(sum of total_cf + driver + recovery + sketch columns)"
                )
                glob_pat = "*.bin_space_hybrid_summary.tsv"
            else:
                hint = f"sift_RS-<N>K_sym.bin_space_summary.tsv"
                glob_pat = "*.bin_space_summary.tsv"
            print(
                f"Skipping {label}: no usable {glob_pat} under "
                f"{results_root / folder}/{{10..60}}K/ (expected e.g. {hint}).",
                file=sys.stderr,
            )

    _fs = float(mpl.rcParamsDefault["font.size"]) * 1.5 * 1.2
    _lw = float(mpl.rcParamsDefault["lines.linewidth"]) * (3.0 / 1.2)
    _ms = float(mpl.rcParamsDefault["lines.markersize"]) * (2.0 / 1.2)
    mpl.rcParams.update(
        {
            "font.family": "serif",
            "mathtext.fontset": "dejavuserif",
            "font.size": _fs,
            "axes.labelsize": _fs,
            "axes.titlesize": _fs,
            "xtick.labelsize": _fs,
            "ytick.labelsize": _fs,
            "legend.fontsize": _fs,
            "figure.titlesize": _fs,
            "lines.linewidth": _lw,
            "lines.markersize": _ms,
            # Embed TrueType as Type 42 (not Type 3) in PDF/PS output.
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )

    fig, ax = plt.subplots(figsize=(8.5, 4.65))
    for folder, label, marker, color in SYSTEM_SPECS:
        pts = series[folder]
        if not pts:
            continue
        xs = [AVG_DEGREE_BY_DENSITY_K[p[0]] for p in pts]
        ys = [p[1] for p in pts]
        ax.plot(
            xs,
            ys,
            marker=marker,
            color=color,
            label=label,
            linewidth=_lw,
            markersize=_ms,
        )

    ax.set_xlabel(r"Average Degree ($2|E|/|V|$)", fontweight="bold")
    ax.set_ylabel("Memory (B)", fontweight="bold")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=10)
    ax.set_xticks(_X_MAJOR_POW2)
    ax.xaxis.set_major_formatter(FuncFormatter(_format_x_pow2))
    ax.xaxis.set_minor_locator(NullLocator())
    ax.set_xlim(_X_MAJOR_POW2[0], _X_MAX)
    ax.set_ylim(_Y_MIN, _Y_MAX)
    ax.set_yticks(_Y_MAJOR)
    ax.yaxis.set_major_formatter(FuncFormatter(_format_memory_power10_tick))
    ax.yaxis.set_minor_locator(NullLocator())
    ax.yaxis.set_minor_formatter(NullFormatter())
    ax.legend(
        bbox_to_anchor=(0.5, 1.0),
        loc="lower center",
        ncol=len(SYSTEM_SPECS),
        frameon=False,
        borderaxespad=0,
        handlelength=1.0,
        handletextpad=0.45,
        columnspacing=0.9,
        labelspacing=0.35,
        prop={"weight": "bold"},
    )
    ax.grid(True, which="major", linestyle=":", alpha=0.6)
    ax.grid(False, which="minor")

    # Leave only enough figure height above the axes for the legend (avoids a tall empty band).
    fig.tight_layout(rect=(0.0, 0.0, 1.0, 0.93))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=150, bbox_inches="tight", pad_inches=0.02)
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
