#pragma once

#include "types.h"
#include <vector>
#include <atomic>

#include "euler_tour_tree.h"
// #include "link_cut_tree.h"
#include "cutsets/ufo_cutset.h"
#include "lct_v2.h"
#include "top_level_forest.h"


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
  TopLevelForest query_forest;
  long tree_ops_count = 0;
  std::vector<GraphUpdate> transaction_log;
  void refresh(GraphUpdate update, bool did_cut);

public:
  GraphTiers(node_id_t num_nodes, uint64_t seed);
  // Overload accepting (and ignoring) num_tiers/batch_size so GraphTiers
  // satisfies the same constructor shape as BatchTiers / InputNode.
  GraphTiers(node_id_t num_nodes, uint32_t /*num_tiers*/, int /*batch_size*/, uint64_t seed)
      : GraphTiers(num_nodes, seed) {}
  ~GraphTiers();

  bool is_initialized(node_id_t u) {
    return ett[0].is_initialized(u);
  }

  void initialize_node(node_id_t u) {
    for (auto &tree : ett) {
      tree.initialize_node(u);
    }
    query_forest.initialize_node(u);
  }

  void uninitialize_node(node_id_t u) {
    for (auto &tree : ett) {
      tree.uninitialize_node(u);
    }
    query_forest.uninitialize_node(u);
  }

  void initialize_all_nodes() {
    for (auto &tree : ett) {
      tree.initialize_all_nodes();
    }
    query_forest.initialize_all_nodes();
  }

  // apply an edge update
  void update(GraphUpdate update);

  // DynamicSketchConcept satisfaction
  void process_all_updates() {} // no-op: GraphTiers processes immediately

  void drain_transaction_log(std::vector<GraphUpdate>& out) {
    if (!transaction_log.empty()) {
      out.insert(out.end(), transaction_log.begin(), transaction_log.end());
      transaction_log.clear();
    }
  }

  size_t space_usage_bytes() {
    size_t total = sizeof(GraphTiers<TreeStrategy>);
    for (auto& tree : ett) {
      total += tree.space_usage_bytes();
    }
    return total;
  }

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

  SpaceReport report_space_usage();
  long get_num_tree_ops() const { return tree_ops_count; }
};
