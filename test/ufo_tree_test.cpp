#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "ufo_tree/ufo_tree.h"


// using namespace ufo;


// Define sketch_len for linking (extern in ufo_cluster.h)
// vec_t sketch_len; // Defined in skiplist.cpp

TEST(UFOTreeSuite, stress_test) {
  int nodecount = 1000;
  int n = 100000;
  sketch_len = nodecount; // Initialize sketch_len
  int seed = time(NULL);
  srand(seed);
  std::cout << "Seeding stress test with " << seed << std::endl;
  CutsetUFOTree ufo(nodecount, 1, seed); // Added required tier_num and seed arguments

  for (int i = 0; i < n; i++) {
    int a = rand() % nodecount, b = rand() % nodecount;
    if (a == b) continue;
    // Simple random link attempts. UFO handles existing connections gracefully? 
    // Assuming yes for now, similar to ETT.
    if (!ufo.is_connected(a, b)) {
        ufo.link(a, b);
    }
  }
}

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
