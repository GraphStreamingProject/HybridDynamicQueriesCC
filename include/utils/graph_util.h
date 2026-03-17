// Copied from parlaylib with heavy modification
#pragma once
#include <iostream>
#include <parlay/io.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>
#include <string>
#include <algorithm>
#include "types.h"


namespace ufo {

using namespace parlay;

struct graph_utils {
  using vertex = uint32_t;
  using edge = std::pair<vertex, vertex>;
  using edges = parlay::sequence<edge>;
  using vertices = parlay::sequence<vertex>;
  using graph = parlay::sequence<vertices>;

  using element = std::pair<int, float>;
  using row = parlay::sequence<element>;
  using sparse_matrix = parlay::sequence<row>;

  static edges to_edges(const graph& G) {
    return flatten(parlay::tabulate(G.size(), [&](vertex u) {
      return parlay::map(G[u], [=](vertex v) { return edge{(vertex)u, (vertex)v}; });
    }));
  }

  static parlay::sequence<std::pair<int, edge>> generate_random_weight_edges(edges& e, long seed = -1){
    srand(seed);
    parlay::sequence<std::pair<int, edge>> v = parlay::map(e, [] (edge e1) {return std::pair(rand(), e1);});
    return v;
  }

  static void print_graph_stats(const graph& G) {
    long num_edges = reduce(parlay::map(G, parlay::size_of()));
    std::cout << "Vertices = " << G.size() << std::endl;
    std::cout << "Edges    = " << num_edges << std::endl;
  }

  static size_t get_component_count(const graph& G) {
    int n = G.size();
    std::vector<bool> visited(n, false);
    size_t component_count = 0;
    for (vertex start = 0; start < n; ++start) {
        if (!visited[start]) {
            component_count += 1;
            std::queue<vertex> q;
            q.push(start);
            visited[start] = true;
            while (!q.empty()) {
                vertex v = q.front();
                q.pop();
                for (vertex neighbor : G[v]) {
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        q.push(neighbor);
                    }
                }
            }
        }
    }
    return component_count;
  }

  static size_t get_vertex_eccentricity(const graph& G, vertex v) {
      int n = G.size();
      std::vector<size_t> dist(n, -1);
      std::queue<vertex> q;
      dist[v] = 0;
      q.push(v);
      size_t maxDist = 0;
      while (!q.empty()) {
          int v = q.front();
          q.pop();
          for (vertex neighbor : G[v]) {
              if (dist[neighbor] == -1) {
                  dist[neighbor] = dist[v] + 1;
                  q.push(neighbor);
                  maxDist = std::max(maxDist, dist[neighbor]);
              }
          }
      }
      return maxDist;
  }

  static size_t get_graph_diameter(const graph& G) {
      int n = G.size();
      parlay::sequence<size_t> eccentricities(n);
      parlay::parallel_for(0, n, [&] (size_t i) {
          int maxDist = get_vertex_eccentricity(G, i);
          eccentricities[i] = maxDist;
      });
      size_t diameter = parlay::reduce(eccentricities, parlay::maximum<size_t>());
      return diameter;
  }

  static size_t get_forest_diameter(const edges& F, vertex n) {
    std::vector<std::vector<vertex>> adj(n);
    for (const auto& edge : F) {
        vertex u = edge.first;
        vertex v = edge.second;
        adj[u].push_back(v);
        adj[v].push_back(u);
    }
    auto bfs = [&](vertex start, std::vector<bool>& visited, bool markVisits) -> std::pair<vertex, size_t> {
        if (visited[start])
          return {-1 ,-1};
        std::vector<vertex> dist(n, -1);
        std::queue<vertex> q;
        q.push(start);
        dist[start] = 0;
        if (markVisits)
          visited[start] = true;
        vertex farthestNode = start;
        size_t maxDist = 0;
        while (!q.empty()) {
            vertex node = q.front();
            q.pop();
            for (vertex neighbor : adj[node]) {
                if (dist[neighbor] == -1) {
                    dist[neighbor] = dist[node] + 1;
                    if (markVisits)
                      visited[neighbor] = true;
                    q.push(neighbor);
                    if (dist[neighbor] > maxDist) {
                        maxDist = dist[neighbor];
                        farthestNode = neighbor;
                    }
                }
            }
        }
        return {farthestNode, maxDist};
    };
    std::vector<bool> visited(n, false);
    size_t maxTreeDiameter = 0;
    for (vertex v = 0; v < n; ++v) {
      std::pair<vertex, size_t> bfsResult = bfs(v, visited, false);
      if(bfsResult.first != -1) {
        bfsResult = bfs(bfsResult.first, visited, true);
        maxTreeDiameter = std::max(maxTreeDiameter, bfsResult.second);
      }
    }
    return maxTreeDiameter;
  }

  static graph read_graph_from_file(const std::string &filename) {
    auto str = parlay::file_map(filename);
    auto tokens =
        parlay::tokens(str, [](char c) { return c == '\n' || c == ' '; });
    long n = parlay::chars_to_long(tokens[0]);
    long m = parlay::chars_to_long(tokens[1]);
    if (tokens.size() != n + m + 2) {
      std::cout << "Bad file format, read_graph_from_file expects:\n"
                << "<n> <m> <degree 0> <degree 1> ... <degree n-1> <edge 0> "
                   "... <edge m-1>\n"
                << "Edges are sorted and each difference encoded with respect "
                   "to the previous one."
                << "First per vertex is encoded directly." << std::endl;
      return graph();
    }
    auto lengths = parlay::delayed::tabulate(
        n, [&](long i) { return parlay::chars_to_double(tokens[i + 2]); });
    auto edges = parlay::tabulate(m, [&](long i) {
      return (vertex)parlay::chars_to_double(tokens[i + n + 2]);
    });
    auto [offsets, total] = scan(lengths);
    return parlay::tabulate(n, [&, o = offsets.begin()](vertex i) {
      return scan_inclusive(edges.cut(o[i], o[i] + lengths[i]));
    });
  }

  static void write_graph_to_file(const graph &G, const std::string &filename) {
    using lseq = parlay::sequence<long>;
    auto lengths = parlay::map(G, [](auto &nghs) { return (long)nghs.size(); });
    auto edges = parlay::flatten(parlay::tabulate(G.size(), [&](long i) {
      auto nghs = sort(G[i]);
      return parlay::tabulate(nghs.size(), [&](long j) -> long {
        return j == 0 ? nghs[0] : nghs[j] - nghs[j - 1];
      });
    }));
    auto all = flatten(
        parlay::sequence<lseq>({lseq(1, (long)G.size()),
                                lseq(1, (long)edges.size()), lengths, edges}));
    auto newline = parlay::chars(1, '\n');
    parlay::chars_to_file(
        parlay::flatten(parlay::map(
            all, [&](long v) { return append(parlay::to_chars(v), newline); })),
        filename);
  }

  static void write_symmetric_graph_to_file(const graph &G,
                                            const std::string &filename) {
    auto GR = parlay::tabulate(G.size(), [&](vertex u) {
      return parlay::filter(G[u], [&](vertex v) { return v > u; });
    });
    write_graph_to_file(GR, filename);
  }

  static graph break_sym_graph_from_bin(const std::string &filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
      std::cerr << "Error: Cannot open file " << filename << '\n';
      abort();
    }
    size_t num_vertices, num_edges, sizes;
    ifs.read(reinterpret_cast<char *>(&num_vertices), sizeof(size_t));
    ifs.read(reinterpret_cast<char *>(&num_edges), sizeof(size_t));
    ifs.read(reinterpret_cast<char *>(&sizes), sizeof(size_t));
    assert(sizes == (num_vertices + 1) * 8 + num_edges * 4 + 3 * 8);
    parlay::sequence<uint64_t> offset(num_vertices + 1);
    parlay::sequence<uint32_t> edge(num_edges);
    ifs.read(reinterpret_cast<char *>(offset.begin()), (num_vertices + 1) * 8);
    ifs.read(reinterpret_cast<char *>(edge.begin()), num_edges * 4);
    if (ifs.peek() != EOF) {
      std::cerr << "Error: Bad data\n";
      abort();
    }
    ifs.close();
    // std::cout << num_vertices << std::endl
    //           << num_edges << std::endl
    //           << sizes << std::endl;
    auto edges =
        parlay::tabulate(num_edges, [&](vertex i) { return (vertex)edge[i]; });
    return parlay::tabulate(num_vertices, [&](vertex i) {
      return parlay::filter(
          parlay::to_sequence(edges.cut(offset[i], offset[i + 1])),
          [=](auto v) { return i < v; });
    });
  }

};

}
