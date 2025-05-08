#pragma once

#include <mpi.h>
#include <queue>

#include "types.h"
#include "euler_tour_tree.h"
#include "sketchless_euler_tour_tree.h"
#include "link_cut_tree.h"
#include "mpi_functions.h"
#include "sketch/sketch_concept.h"
#include "sketch/sketch_columns.h"


enum TreeOperationType {
  NOT_ISOLATED=0, ISOLATED=1, EMPTY, LINK, CUT, LCT_QUERY
};

typedef struct {
  GraphUpdate update;
  bool end = false;
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

class InputNode {
  node_id_t num_nodes;
  uint32_t num_tiers;
  LinkCutTree link_cut_tree;
  SketchlessEulerTourTree query_ett;
  UpdateMessage* update_buffer;
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
  void update(GraphUpdate update);
  void process_all_updates();
  bool connectivity_query(node_id_t a, node_id_t b);
  std::vector<std::set<node_id_t>> cc_query();
  void end();
};

class TierNode {
  EulerTourTree<FixedSizeSketchColumn> ett;
  uint32_t tier_num;
  uint32_t num_tiers;
  int batch_size;
  UpdateMessage* update_buffer;
  GreedyRefreshMessage* this_sizes_buffer;
  GreedyRefreshMessage* next_sizes_buffer;
  SampleResult* query_result_buffer;
  bool* split_revert_buffer;
  bool using_sliding_window = false;
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
