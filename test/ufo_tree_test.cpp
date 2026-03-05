#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "cutsets/ufo_cutset.h"


// using namespace ufo;


// Define sketch_len for linking (extern in ufo_cluster.h)
extern vec_t sketch_len; // Defined in skiplist.cpp

extern int command_line_n;
extern int command_line_k;
extern int command_line_num_trials;
extern long command_line_seed;


TEST(UFOTreeSuite, simple_link_cut_connectivity) {
    sketch_len = 10; // Initialize sketch_len
    CutsetUFOTree ufo(10, 1, 0); // Added required arguments
    
    EXPECT_FALSE(ufo.is_connected(0, 1));
    ufo.link(0, 1);
    EXPECT_TRUE(ufo.is_connected(0, 1));
    
    EXPECT_FALSE(ufo.is_connected(2, 3));
    ufo.link(2, 3);
    EXPECT_TRUE(ufo.is_connected(2, 3));
    
    // Connect components {0,1} and {2,3} via (1,2)
    ufo.link(1, 2);
    EXPECT_TRUE(ufo.is_connected(0, 3));
    EXPECT_TRUE(ufo.is_connected(0, 2));
    EXPECT_TRUE(ufo.is_connected(1, 3));

    // verify structure didn't break other nodes
    EXPECT_FALSE(ufo.is_connected(0, 4));

    // Break the middle link
    ufo.cut(1, 2);
    EXPECT_TRUE(ufo.is_connected(0, 1));
    EXPECT_TRUE(ufo.is_connected(2, 3));
    EXPECT_FALSE(ufo.is_connected(0, 3));
}

TEST(UFOTreeSuite, stress_test) {
  srand(time(NULL));
  int num_trials = command_line_num_trials == 0 ? 1 : command_line_num_trials;
  int nodecount = command_line_n == 0 ? 1000 : command_line_n;
  int n_ops = command_line_k == 0 ? 2000 : command_line_k;
  sketch_len = nodecount;

  std::cout << "Running " << num_trials << " trials." << std::endl;

  for (int trial = 0; trial < num_trials; ++trial) {
    int current_seed = command_line_seed == -1 ? rand() : command_line_seed;
    srand(current_seed);
    std::cout << "Trial " << trial + 1 << "/" << num_trials 
              << " - Seeding stress test with " << current_seed << std::endl;
    
    // Initialize a fresh tree for this trial
    CutsetUFOTree ufo(nodecount, 1, current_seed);

    for (int i = 0; i < n_ops; i++) {
      int a = rand() % nodecount, b = rand() % nodecount;
      if (a == b) continue;
      // Simple random link attempts. UFO handles existing connections gracefully? 
      // Assuming yes for now, similar to ETT.
      if (!ufo.is_connected(a, b)) {
          ufo.link(a, b);
      }
    }
  }
}

TEST(UFOTreeSuite, stress_test_with_cuts) {
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
    
    // Initialize a fresh tree for this trial
    CutsetUFOTree ufo(nodecount, 1, current_seed);

    for (int i = 0; i < n_ops; i++) {
      int u = rand() % nodecount;
      int v = rand() % nodecount;
      if (u == v) continue;

      if (ufo.is_connected(u, v)) {
          if (ufo.has_edge(u, v)) {
              ufo.cut(u, v);
          }
      } else {
          ufo.link(u, v);
      }
      
      ASSERT_TRUE(ufo.verify_structure()) << "Trial " << trial + 1 << " structure invalid at step " << i;
    }
    // ufo.print_tree();
  }
}
