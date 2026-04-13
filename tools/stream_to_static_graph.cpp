/**
 * stream_to_static_graph.cpp
 *
 * Reads a binary edge stream, collects the set of live edges
 * (INSERTs minus DELETEs), canonicalises to (u < v), deduplicates,
 * and writes a binary symmetric CSR file compatible with
 * graph_utils::break_sym_graph_from_bin / read_static_graph_auto.
 *
 * Usage:
 *   ./stream_to_static_graph input_stream.bin output_graph.bin
 */
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include "binary_graph_stream.h"
#include "types.h"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input_stream.bin> <output_graph.bin>\n";
        return 1;
    }
    const std::string input_path = argv[1];
    const std::string output_path = argv[2];

    // Read the stream
    BinaryGraphStream stream(input_path, 1 << 20);  // 1 MB buffer
    uint32_t num_nodes = stream.nodes();
    uint64_t num_ops = stream.edges();

    std::cout << "Stream: " << num_nodes << " nodes, " << num_ops << " operations\n";

    // Collect live edges as a set of canonical (u < v) pairs
    using Edge = std::pair<uint32_t, uint32_t>;
    std::set<Edge> live;

    for (uint64_t i = 0; i < num_ops; ++i) {
        GraphUpdate op = stream.get_edge();
        if (op.type == INSERT) {
            uint32_t u = std::min(op.edge.src, op.edge.dst);
            uint32_t v = std::max(op.edge.src, op.edge.dst);
            if (u != v) live.insert({u, v});
        } else if (op.type == DELETE) {
            uint32_t u = std::min(op.edge.src, op.edge.dst);
            uint32_t v = std::max(op.edge.src, op.edge.dst);
            live.erase({u, v});
        }
        // BREAKPOINT = query, skip
    }

    std::cout << "Live edges after stream: " << live.size() << "\n";

    // Build CSR: adj[u] contains sorted v values where u < v
    std::vector<std::vector<uint32_t>> adj(num_nodes);
    for (const auto& [u, v] : live) {
        adj[u].push_back(v);
    }
    // adj lists are already sorted because std::set iterates in order

    uint64_t num_edges = live.size();

    // Build offsets
    std::vector<uint64_t> offsets(num_nodes + 1);
    offsets[0] = 0;
    for (uint32_t i = 0; i < num_nodes; ++i) {
        offsets[i + 1] = offsets[i] + adj[i].size();
    }

    // Write binary CSR
    uint64_t sizes = (num_nodes + 1) * 8 + num_edges * 4 + 3 * 8;

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: cannot open output file " << output_path << "\n";
        return 1;
    }

    auto write_u64 = [&](uint64_t val) { out.write(reinterpret_cast<const char*>(&val), 8); };
    auto write_u32 = [&](uint32_t val) { out.write(reinterpret_cast<const char*>(&val), 4); };

    // Header
    write_u64(static_cast<uint64_t>(num_nodes));
    write_u64(num_edges);
    write_u64(sizes);

    // Offsets
    for (uint32_t i = 0; i <= num_nodes; ++i) {
        write_u64(offsets[i]);
    }

    // Edge targets
    for (uint32_t i = 0; i < num_nodes; ++i) {
        for (uint32_t v : adj[i]) {
            write_u32(v);
        }
    }

    out.close();
    std::cout << "Wrote " << output_path << ": " << num_nodes << " vertices, "
              << num_edges << " edges, " << sizes << " bytes\n";
    return 0;
}
