#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <gtest/gtest.h>
#include <chrono>
#include "cutsets/lct_cutset.h"
#include "cutsets/ufo_cutset.h"
#include "euler_tour_tree.h"


extern vec_t sketch_len; // Defined in skiplist.cpp

extern int command_line_n;
extern int command_line_k;
extern int command_line_num_trials;
extern long command_line_seed;
extern double height_factor;

namespace {
double benchmark_height_factor(int nodecount) {
  if (nodecount <= 16) {
    return 1.0;
  }
  const double n = static_cast<double>(nodecount);
  const double hf = std::log(std::log(n)) / std::log(n);
  return std::max(hf, 1e-6);
}
}


TEST(CutsetSuite, cutset_memory_benchmark) {
  srand(time(NULL));
  int num_trials = command_line_num_trials == 0 ? 1 : command_line_num_trials;
  int nodecount = command_line_n == 0 ? 1000 : command_line_n;
  int n_ops = command_line_k == 0 ? 2000 : command_line_k;
  sketch_len = nodecount;
  height_factor = benchmark_height_factor(nodecount);
  std::cout << "Running " << num_trials << " trials." << std::endl;
  std::cout << "n: " << nodecount << std::endl;
  std::cout << "n_ops: " << n_ops << std::endl;
  std::cout << "height_factor: " << height_factor << std::endl;

  for (int trial = 0; trial < num_trials; ++trial) {
    int current_seed = command_line_seed == -1 ? rand() : command_line_seed;
    srand(current_seed);
    std::cout << "Trial " << trial + 1 << " seed: " << current_seed << std::endl;
    std::unordered_set<uint64_t> edges;

    cutset_lct::CutsetLCT<DefaultSketchColumn> lct(nodecount, current_seed);
    ufo::CutsetUFOTree<DefaultSketchColumn> ufo(nodecount, 1, current_seed);
    EulerTourTree<DefaultSketchColumn> ett(nodecount, 1, current_seed);
    ett.initialize_all_nodes();
    lct.initialize_all_nodes();

    for (int i = 0; i < n_ops; i++) {
      int u = rand() % nodecount;
      int v = rand() % nodecount;
      if (u == v) continue;
      uint64_t edge = ((uint64_t)(u < v ? u : v) << 32) | (u < v ? v : u);
      if (lct.is_connected(u, v)) {
        lct.update_sketch(u, v);
        lct.update_sketch(v, u);
        ufo.update_sketch(u, v);
        ufo.update_sketch(v, u);
        ett.update_sketch(u, v);
        ett.update_sketch(v, u);
      } else {
        lct.link(u, v);
        ufo.link(u, v);
        ett.link(u, v);
        edges.insert(edge);
      }
    }
    std::cout << "MEMORY USAGE (bytes): " << std::endl;
    std::cout << "Link-Cut Tree:   " << lct.space() << std::endl;
    std::cout << "UFO Tree:        " << ufo.space() << std::endl;
    std::cout << "Euler Tour Tree: " << ett.space_usage_bytes() << std::endl;
  }
}

TEST(CutsetSuite, cutset_link_benchmark) {
  long seed = command_line_seed == -1 ? time(NULL) : command_line_seed;
  srand(seed);
  int nodecount = command_line_n == 0 ? 1000 : command_line_n;
  sketch_len = nodecount;
  height_factor = benchmark_height_factor(nodecount);

  std::cout << "--- BENCHMARK: LINKS ---" << std::endl;
  std::cout << "Nodes: " << nodecount << std::endl;
  std::cout << "height_factor: " << height_factor << std::endl;

  // Pre-generate a random spanning tree to guarantee 100% link operations
  std::vector<std::pair<int, int>> link_edges;
  link_edges.reserve(nodecount - 1);
  for (int i = 1; i < nodecount; ++i) {
    link_edges.push_back({i, rand() % i});
  }

  cutset_lct::CutsetLCT<DefaultSketchColumn> lct(nodecount, seed);
  ufo::CutsetUFOTree<DefaultSketchColumn> ufo(nodecount, 1, seed);
  EulerTourTree<DefaultSketchColumn> ett(nodecount, 1, seed);
  ett.initialize_all_nodes();

  auto start_lct = std::chrono::high_resolution_clock::now();
  for (const auto& edge : link_edges) {
    lct.link(edge.first, edge.second);
  }
  auto end_lct = std::chrono::high_resolution_clock::now();

  auto start_ufo = std::chrono::high_resolution_clock::now();
  for (const auto& edge : link_edges) {
    ufo.link(edge.first, edge.second);
  }
  auto end_ufo = std::chrono::high_resolution_clock::now();

  auto start_ett = std::chrono::high_resolution_clock::now();
  for (const auto& edge : link_edges) {
    ett.link(edge.first, edge.second);
  }
  auto end_ett = std::chrono::high_resolution_clock::now();

  std::cout << "LCT Link Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_lct - start_lct).count() << " ms\n";
  std::cout << "UFO Link Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_ufo - start_ufo).count() << " ms\n";
  std::cout << "ETT Link Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_ett - start_ett).count() << " ms\n\n";
}


TEST(CutsetSuite, cutset_sketch_update_benchmark) {
  long seed = command_line_seed == -1 ? time(NULL) : command_line_seed;
  srand(seed);
  int nodecount = command_line_n == 0 ? 1000 : command_line_n;
  int n_ops = command_line_k == 0 ? 2000 : command_line_k;
  sketch_len = nodecount;
  height_factor = benchmark_height_factor(nodecount);

  std::cout << "--- BENCHMARK: SKETCH UPDATES ---" << std::endl;
  std::cout << "Nodes: " << nodecount << " | Update Ops: " << n_ops << std::endl;
  std::cout << "height_factor: " << height_factor << std::endl;

  cutset_lct::CutsetLCT<DefaultSketchColumn> lct(nodecount, seed);
  ufo::CutsetUFOTree<DefaultSketchColumn> ufo(nodecount, 1, seed);
  EulerTourTree<DefaultSketchColumn> ett(nodecount, 1, seed);
  ett.initialize_all_nodes();

  // 1. Build a spanning tree first so all nodes are connected
  for (int i = 1; i < nodecount; ++i) {
    int u = i;
    int v = rand() % i;
    lct.link(u, v);
    ufo.link(u, v);
    ett.link(u, v);
  }

  // 2. Pre-generate update operations
  std::vector<std::pair<int, int>> update_edges;
  update_edges.reserve(n_ops);
  for (int i = 0; i < n_ops; ++i) {
    int u = rand() % nodecount;
    int v = rand() % nodecount;
    while (u == v) { v = rand() % nodecount; } // avoid self-loops
    update_edges.push_back({u, v});
  }

  // 3. Time the updates
  auto start_lct = std::chrono::high_resolution_clock::now();
  for (const auto& edge : update_edges) {
    lct.update_sketch(edge.first, edge.second);
    lct.update_sketch(edge.second, edge.first);
  }
  auto end_lct = std::chrono::high_resolution_clock::now();

  auto start_ufo = std::chrono::high_resolution_clock::now();
  for (const auto& edge : update_edges) {
    ufo.update_sketch(edge.first, edge.second);
    ufo.update_sketch(edge.second, edge.first);
  }
  auto end_ufo = std::chrono::high_resolution_clock::now();

  auto start_ett = std::chrono::high_resolution_clock::now();
  for (const auto& edge : update_edges) {
    ett.update_sketch(edge.first, edge.second);
    ett.update_sketch(edge.second, edge.first);
  }
  auto end_ett = std::chrono::high_resolution_clock::now();

  std::cout << "LCT Update Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_lct - start_lct).count() << " ms\n";
  std::cout << "UFO Update Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_ufo - start_ufo).count() << " ms\n";
  std::cout << "ETT Update Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_ett - start_ett).count() << " ms\n\n";
}
