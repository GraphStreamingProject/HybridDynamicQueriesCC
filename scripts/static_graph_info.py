#!/usr/bin/env python3
"""Report vertex/edge counts for static graph files and emit a dataset CSV.

Supports both binary symmetric CSR and Parlay text graph formats
(auto-detected the same way as graph_utils::read_static_graph_auto).

Usage:
    python3 scripts/static_graph_info.py graph1.bin graph2.adj ...
    python3 scripts/static_graph_info.py --output datasets.csv graph1.bin graph2.adj ...
    python3 scripts/static_graph_info.py --keep-input-paths --output datasets.csv graph1.bin ...

The CSV matches the format expected by --dataset-config in batch_speed.sh:
    dataset_name,filepath,num_vertices,num_edges
"""
import argparse
import csv
import os
import struct
import sys


def is_binary_csr(path: str) -> bool:
    """Mirror graph_utils::is_binary_sym_graph_file."""
    try:
        file_size = os.path.getsize(path)
    except OSError:
        return False
    header_bytes = 3 * 8  # three size_t fields
    if file_size < header_bytes:
        return False
    with open(path, "rb") as f:
        data = f.read(header_bytes)
    if len(data) < header_bytes:
        return False
    num_vertices, num_edges, sizes = struct.unpack("<QQQ", data)
    expected = (num_vertices + 1) * 8 + num_edges * 4 + 3 * 8
    return sizes == expected and file_size == sizes and num_vertices > 0


def read_binary_csr(path: str):
    """Read binary symmetric CSR and return (num_vertices, num_edges).

    num_edges is the number of *stored* directed half-edges (u < v).
    This matches the convention used by graph_utils::break_sym_graph_from_bin.
    """
    with open(path, "rb") as f:
        num_vertices, num_edges, _ = struct.unpack("<QQQ", f.read(24))
    return num_vertices, num_edges


def read_parlay_text(path: str):
    """Read Parlay text graph and return (num_vertices, num_edges).

    The file stores adjacency lists with delta-encoded neighbours.
    num_edges here is the total stored adjacency entries.
    We need to deduplicate to report unique undirected edge count.
    """
    with open(path, "r") as f:
        tokens = f.read().split()
    n = int(tokens[0])
    m = int(tokens[1])
    # Decode degrees and delta-encoded edges to count unique undirected edges.
    degrees = [int(tokens[2 + i]) for i in range(n)]
    edges_flat = [int(tokens[2 + n + i]) for i in range(m)]

    unique_edges = set()
    offset = 0
    for v in range(n):
        deg = degrees[v]
        prefix_sum = 0
        for j in range(deg):
            prefix_sum += edges_flat[offset + j]
            u = prefix_sum
            edge = (min(v, u), max(v, u))
            if edge[0] != edge[1]:
                unique_edges.add(edge)
        offset += deg
    return n, len(unique_edges)


def read_graph(path: str):
    if is_binary_csr(path):
        return read_binary_csr(path)
    return read_parlay_text(path)


def dataset_name_from_path(path: str) -> str:
    return os.path.splitext(os.path.basename(path))[0]


def main():
    parser = argparse.ArgumentParser(
        description="Report vertex/edge counts for static graph files."
    )
    parser.add_argument("graphs", nargs="+", help="Paths to static graph files")
    parser.add_argument(
        "-o", "--output", default=None,
        help="Write dataset CSV to this path (default: stdout only)"
    )
    parser.add_argument(
        "--keep-input-paths", action="store_true",
        help="Preserve input path spelling instead of canonicalizing paths in output",
    )
    args = parser.parse_args()

    rows = []
    for input_path in args.graphs:
        path = input_path if args.keep_input_paths else os.path.realpath(input_path)
        if not os.path.isfile(path):
            print(f"Warning: {path} not found, skipping", file=sys.stderr)
            continue
        try:
            num_vertices, num_edges = read_graph(path)
        except Exception as e:
            print(f"Warning: failed to read {path}: {e}", file=sys.stderr)
            continue
        name = dataset_name_from_path(path)
        rows.append((name, path, num_vertices, num_edges))
        print(f"{name}: {num_vertices} vertices, {num_edges} edges")

    if args.output:
        with open(args.output, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["dataset_name", "filepath", "num_vertices", "num_edges"])
            for name, filepath, nv, ne in rows:
                writer.writerow([name, filepath, nv, ne])
        print(f"\nWrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
