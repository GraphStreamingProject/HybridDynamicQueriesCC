#pragma once

#include "types.h"
#include <vector>
#include <atomic>

#include "euler_tour_tree.h"
#include "link_cut_tree.h"


// Global variables for performance testing
extern long lct_time;
extern long ett_time;
extern long ett_find_root;
extern long ett_get_agg;
extern long sketch_query;
extern long sketch_time;
extern long refresh_time;
extern long parallel_isolated_check;
extern long tiers_grown;
extern long normal_refreshes;
extern std::atomic<long> num_sketch_updates;
extern std::atomic<long> num_sketch_batches;

// maintains the tiers of the algorithm
// and the spanning forest of the entire graph
template <typename SketchClass = FixedSizeSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
class GraphTiers {
  FRIEND_TEST(GraphTiersSuite, mini_correctness_test);
private:
  std::vector<EulerTourTree<SketchClass>> ett;  // one ETT for each tier
  std::vector<SkipListNode<SketchClass>*> root_nodes;
  LinkCutTree link_cut_tree;
  void refresh(GraphUpdate update);

public:
  GraphTiers(node_id_t num_nodes);
  ~GraphTiers();

  // apply an edge update
  void update(GraphUpdate update);

  // query for the connected components of the graph
  std::vector<std::set<node_id_t>> get_cc();

  // query for if a is connected to b
  bool is_connected(node_id_t a, node_id_t b);
};
