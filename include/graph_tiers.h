#pragma once

#include "types.h"
#include <vector>
#include <atomic>

#include "euler_tour_tree.h"
// #include "link_cut_tree.h"
#include "cutsets/ufo_cutset.h"
#include "lct_v2.h"
#include "sketchless_euler_tour_tree.h"


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
#include "cutset_data_structure.h"

// maintains the tiers of the algorithm
// and the spanning forest of the entire graph
template <typename TreeStrategy>
// template <typename SketchClass = DefaultSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
class GraphTiers {
  // FRIEND_TEST(GraphTiersSuite, mini_correctness_test);
  using SketchClass = typename TreeStrategy::SketchType;
  using ComponentView = typename TreeStrategy::ComponentView;
private:
  std::vector<TreeStrategy> ett;  // one ETT for each tier
  std::vector<ComponentView> root_nodes;
  LinkCutTreeMaxAgg<int8_t> link_cut_tree;
  SketchlessEulerTourTree<> query_ett;
  void refresh(GraphUpdate update, bool did_cut);

public:
  GraphTiers(node_id_t num_nodes, uint64_t seed);
  ~GraphTiers();

  bool is_initialized(node_id_t u) {
    return ett[0].is_initialized(u);
  }

  void initialize_node(node_id_t u) {
    for (auto &tree : ett) {
      tree.initialize_node(u);
    }
    link_cut_tree.initialize_node(u);
    query_ett.initialize_node(u);
  }

  void uninitialize_node(node_id_t u) {
    for (auto &tree : ett) {
      tree.uninitialize_node(u);
    }
    link_cut_tree.uninitialize_node(u);
    query_ett.uninitialize_node(u);
  }

  void initialize_all_nodes() {
    for (auto &tree : ett) {
      tree.initialize_all_nodes();
    }
    link_cut_tree.initialize_all_nodes();
    query_ett.initialize_all_nodes();
  }

  // apply an edge update
  void update(GraphUpdate update);

  // query for the connected components of the graph
  std::vector<std::set<node_id_t>> get_cc();

  // query for if a is connected to b
  bool is_connected(node_id_t a, node_id_t b);

  // Verify the structural integrity of every cutset tier.
  bool verify_all_structures() {
      bool valid = true;
      for (size_t i = 0; i < ett.size(); ++i) {
          bool tier_ok = ett[i].verify_structure();
          if (!tier_ok) {
              std::cout << "verify_structure FAILED at tier " << i << std::endl;
              valid = false;
          }
      }
      return valid;
  }

  std::vector<SpaceReportMessage> report_space_usage();
};
