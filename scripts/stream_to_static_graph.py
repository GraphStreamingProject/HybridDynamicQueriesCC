#!/usr/bin/env python3
"""Convert a binary edge stream to a static graph file (binary symmetric CSR).

Reads a binary stream (.bin), collects every INSERT edge, removes edges that
are later DELETEd, canonicalises to (u < v), deduplicates, and writes a
binary symmetric CSR file compatible with graph_utils::break_sym_graph_from_bin.

Usage:
    python3 scripts/stream_to_static_graph.py input_stream.bin output_graph.bin
"""
import argparse
import os
import struct
import sys

# Stream update types (matches types.h: INSERT=0, DELETE=1, BREAKPOINT=2)
INSERT = 0
DELETE = 1
BREAKPOINT = 2


def read_stream(path: str):
    """Read binary stream and return (num_nodes, set of live edges).

    Each edge is stored as (min(a,b), max(a,b)).
    """
    with open(path, "rb") as f:
        header = f.read(12)
        if len(header) < 12:
            raise ValueError("Stream file too small for header")
        num_nodes = struct.unpack("<I", header[:4])[0]
        num_edges = struct.unpack("<Q", header[4:12])[0]

        edge_size = 1 + 4 + 4  # uint8 + uint32 + uint32
        edges = set()
        buf = f.read()

    if len(buf) < num_edges * edge_size:
        print(
            f"Warning: stream claims {num_edges} edges but file has data for "
            f"{len(buf) // edge_size}",
            file=sys.stderr,
        )

    offset = 0
    count = 0
    while offset + edge_size <= len(buf):
        update_type = buf[offset]
        a = struct.unpack_from("<I", buf, offset + 1)[0]
        b = struct.unpack_from("<I", buf, offset + 5)[0]
        offset += edge_size
        count += 1

        if update_type == INSERT:
            edge = (min(a, b), max(a, b))
            edges.add(edge)
        elif update_type == DELETE:
            edge = (min(a, b), max(a, b))
            edges.discard(edge)
        # BREAKPOINT edges are queries — skip them

    print(f"Read {count} stream operations, {len(edges)} unique live edges", file=sys.stderr)
    return num_nodes, edges


def write_binary_csr(path: str, num_vertices: int, edges: set):
    """Write binary symmetric CSR (break_sym_graph_from_bin format).

    Edges are stored as directed half-edges where src < dst.
    Layout:
        size_t num_vertices
        size_t num_edges (number of half-edges stored)
        size_t sizes     (total file size in bytes)
        uint64_t[num_vertices+1] offsets
        uint32_t[num_edges] edge targets
    """
    # Build adjacency: for each edge (u,v) with u<v, store v under u.
    adj = [[] for _ in range(num_vertices)]
    for u, v in edges:
        if u == v:
            continue
        # Edges are already canonical (u < v)
        adj[u].append(v)

    # Sort each adjacency list
    for i in range(num_vertices):
        adj[i].sort()

    # Build CSR offsets
    num_edges = sum(len(a) for a in adj)
    offsets = [0] * (num_vertices + 1)
    for i in range(num_vertices):
        offsets[i + 1] = offsets[i] + len(adj[i])
    assert offsets[num_vertices] == num_edges

    sizes = (num_vertices + 1) * 8 + num_edges * 4 + 3 * 8

    with open(path, "wb") as f:
        f.write(struct.pack("<Q", num_vertices))
        f.write(struct.pack("<Q", num_edges))
        f.write(struct.pack("<Q", sizes))
        for o in offsets:
            f.write(struct.pack("<Q", o))
        for i in range(num_vertices):
            for v in adj[i]:
                f.write(struct.pack("<I", v))

    print(
        f"Wrote {path}: {num_vertices} vertices, {num_edges} edges, "
        f"{sizes} bytes",
        file=sys.stderr,
    )


def main():
    parser = argparse.ArgumentParser(
        description="Convert a binary edge stream to a static graph file (binary CSR)."
    )
    parser.add_argument("input", help="Input binary stream file (.bin)")
    parser.add_argument("output", help="Output binary CSR graph file")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: {args.input} not found", file=sys.stderr)
        sys.exit(1)

    num_nodes, edges = read_stream(args.input)
    write_binary_csr(args.output, num_nodes, edges)


if __name__ == "__main__":
    main()
