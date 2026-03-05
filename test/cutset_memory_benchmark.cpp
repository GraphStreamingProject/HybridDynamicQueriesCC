#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <gtest/gtest.h>
#include "cutsets/lct_cutset.h"
#include "cutsets/ufo_cutset.h"
#include "euler_tour_tree.h"


extern vec_t sketch_len; // Defined in skiplist.cpp

extern int command_line_n;
extern int command_line_k;
extern int command_line_num_trials;
extern long command_line_seed;


TEST(CutsetSuite, cutset_memory_test) {
  srand(time(NULL));
  int num_trials = command_line_num_trials == 0 ? 1 : command_line_num_trials;
  int nodecount = command_line_n == 0 ? 1000 : command_line_n;
  int n_ops = command_line_k == 0 ? 2000 : command_line_k;
  sketch_len = nodecount;
  std::cout << "Running " << num_trials << " trials." << std::endl;
  std::cout << "n: " << nodecount << std::endl;
  std::cout << "n_ops: " << n_ops << std::endl;

  for (int trial = 0; trial < num_trials; ++trial) {
    int current_seed = command_line_seed == -1 ? rand() : command_line_seed;
    srand(current_seed);
    std::cout << "Trial " << trial + 1 << " seed: " << current_seed << std::endl;
    std::unordered_set<uint64_t> edges;

    cutset_lct::CutsetLCT<DefaultSketchColumn> lct(nodecount, current_seed);
    ufo::CutsetUFOTree<DefaultSketchColumn> ufo(nodecount, 1, current_seed);
    EulerTourTree<DefaultSketchColumn> ett(nodecount, 1, current_seed);
    ett.initialize_all_nodes();

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
    std::cout << "MEMORY USAGE: " << std::endl;
    std::cout << "Link-Cut Tree:   " << lct.space() << std::endl;
    std::cout << "UFO Tree:        " << ufo.space() << std::endl;
    std::cout << "Euler Tour Tree: " << ett.space_usage_bytes() << std::endl;
  }
}
