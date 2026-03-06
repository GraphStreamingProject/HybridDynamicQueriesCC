#pragma once

#include <mpi.h>
#include <queue>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "types.h"
#include "euler_tour_tree.h"
#include "sketchless_euler_tour_tree.h"
// #include "link_cut_tree.h"
#include "lct_v2.h"
#include "mpi_functions.h"
#include "sketch/sketch_concept.h"
#include "sketch/sketch_columns.h"
#include "sketch_interfacing.h"
#include "cutset_data_structure.h"
#include "cutsets/ufo_cutset.h"


enum TreeOperationType {
  NOT_ISOLATED=0, ISOLATED=1, EMPTY, LINK, CUT, LCT_QUERY, MAXIMIZED
};

enum UpdateStatus : uint8_t { UPDATE_NORMAL = 0, UPDATE_END = 1, UPDATE_SPACE_REPORT = 2 };

typedef struct {
  GraphUpdate update;
  UpdateStatus status = UPDATE_NORMAL;
} UpdateMessage;

typedef struct {
  TreeOperationType type = EMPTY;
  node_id_t endpoint1 = 0;
  node_id_t endpoint2 = 0;
  uint32_t start_tier = 0;
} EttUpdateMessage;

typedef struct {
  TreeOperationType type = EMPTY;
  node_id_t endpoint1 = 0;
  node_id_t endpoint2 = 0;
} LctQueryMessage;

typedef struct {
  bool connected = false;
  edge_id_t cycle_edge = 0;
  uint32_t weight = 0;
} LctResponseMessage;

typedef struct {
  node_id_t v = 0;
  uint32_t prev_tier_size = 0;
  SketchSample<vec_t> sketch_query_result;
} RefreshEndpoint;

typedef struct {
  std::pair<RefreshEndpoint, RefreshEndpoint> endpoints;
} RefreshMessage;

typedef struct {
  uint32_t size1 = 0;
  uint32_t size2 = 0;
} GreedyRefreshMessage;

typedef struct {
  uint32_t tier_num = 0;
  size_t space_bytes = 0;
  size_t num_components = 0;
} SpaceReportMessage;

class InputNode {
  node_id_t num_nodes;
  uint32_t num_tiers;
  // LinkCutTree<> link_cut_tree;
  LinkCutTreeMaxAgg<int8_t> link_cut_tree;
  SketchlessEulerTourTree<> query_ett;
  UpdateMessage* update_buffer;
  
  std::vector<GraphUpdate> transaction_log;


  int buffer_size;
  int buffer_capacity;
  int* split_revert_buffer;
  void process_updates();
  std::queue<bool> isolation_history_queue;
  int history_size;
  int isolation_count;
  bool using_sliding_window = false;
public:
  InputNode(node_id_t num_nodes, uint32_t num_tiers, int batch_size, int seed);
  ~InputNode();
  // TODO - in reality, the input node needs to communicate
  // wihh its tier nodes to initialize data structures.
  // in any hybrid tests, we're just gonna do this ahead of time.
  void initialize_node(node_id_t u) {
    query_ett.initialize_node(u);
    link_cut_tree.initialize_node(u);
  }; // no-op
  void uninitialize_node(node_id_t u) {
    query_ett.uninitialize_node(u);
    link_cut_tree.uninitialize_node(u);
  }; // no-op
  void initialize_all_nodes() {
    query_ett.initialize_all_nodes(num_nodes);
    link_cut_tree.initialize_all_nodes(num_nodes);
  }; // no-op
  void update(GraphUpdate update);
  void process_all_updates();
  bool connectivity_query(node_id_t a, node_id_t b);
  std::vector<std::set<node_id_t>> cc_query();
  void end();

  /**
   * Triggers a space report from all TierNodes.
   * Returns a vector of (tier_num, space_bytes, num_components) per tier.
   */
  std::vector<SpaceReportMessage> report_space_usage();

  /**
   * Triggers a space report and writes it as a TSV to the given output stream.
   * Columns: tier\tspace_bytes\tnum_components
   * The last row is "total\t<sum_bytes>\t-".
   */
  void report_space_usage_tsv(std::ostream& out);

  /**
   * Convenience: triggers a space report and writes TSV to the given file path.
   */
  void report_space_usage_tsv(const std::string& file_path);

  void flush_transaction_log() {
    transaction_log.clear();
  };
  size_t space_usage_bytes() const {
    return 0; // TODO - implement
  }
  
  const std::vector<GraphUpdate>& get_transaction_log() const {
    return transaction_log;
  }
  
};

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
class TierNode {
  using SketchClass = typename TreeStrategy::SketchType;
  using Handle = typename TreeStrategy::Handle;
  TreeStrategy ett;
  uint32_t tier_num;
  uint32_t num_tiers;
  int batch_size;
  UpdateMessage* update_buffer;
  GreedyRefreshMessage* this_sizes_buffer;
  GreedyRefreshMessage* next_sizes_buffer;
  SampleResult* query_result_buffer;
  bool* split_revert_buffer;
  bool using_sliding_window = false;
  void initialize_node(node_id_t u) {
      ett.initialize_node(u);
  };
  void uninitialize_node(node_id_t u) {
      ett.uninitialize_node(u);
  };
  void initialize_all_nodes(node_id_t max_num_nodes) {
      ett.initialize_all_nodes(max_num_nodes);
  };
  bool is_initialized(node_id_t u) {
      return ett.is_initialized(u);
  };
  void update_tier(GraphUpdate update);
  void ett_update_tier(EttUpdateMessage message);
  void refresh_tier(RefreshMessage messsage);
public:
  TierNode(node_id_t num_nodes, uint32_t tier_num, uint32_t num_tiers, int batch_size, int seed);
  ~TierNode();
  void main();
};

// #define CANARY(X) do {if (true) {int canary_h; MPI_Comm_rank(MPI_COMM_WORLD, &canary_h); std::cout << __FILE__ << ":" << __LINE__ << " @ " << canary_h << " says " << X << std::endl;}} while (false)
#define CANARY(X) ;
// #define ENDPOINT_CANARY(X, src, dst) do {if (true) {int canary_h; MPI_Comm_rank(MPI_COMM_WORLD, &canary_h); std::cout << __FILE__ << ":" << __LINE__ << " @ Tier " << canary_h-1 << " says " << X << " " << src << " " << dst << std::endl;}} while (false)
# define ENDPOINT_CANARY(X, src, dst) ;
