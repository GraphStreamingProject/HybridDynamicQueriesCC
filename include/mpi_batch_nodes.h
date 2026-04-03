#pragma once

#include <mpi.h>
#include <cstdint>
#include <vector>
#include <absl/container/flat_hash_map.h>

#include "types.h"
#include "util.h"
#include "euler_tour_tree.h"
#include "sketchless_euler_tour_tree.h"
#include "lct_v2.h"
#include "mpi_nodes.h"
#include "mpi_functions.h"
#include "sketch/sketch_concept.h"
#include "sketch/sketch_columns.h"
#include "sketch_interfacing.h"
#include "cutset_data_structure.h"
#include "cutsets/ufo_cutset.h"

// ============================================================================
//  Message types for the batch MPI protocol
// ============================================================================

enum BatchUpdateStatus : uint8_t {
  BATCH_UPDATE_NORMAL = 0,
  BATCH_UPDATE_END    = 1,
  BATCH_UPDATE_SPACE_REPORT = 2,
};

enum BatchSketchOp : uint8_t {
  BATCH_OP_INSERT = 0,
  BATCH_OP_DELETE = 1,
};

inline constexpr uint8_t BATCH_NO_CUT_TIER = UINT8_MAX;

inline constexpr int INPUT_NODE_RANK = 0;

struct BatchUpdateMessage {
  Edge edge;
  uint8_t op = BATCH_OP_INSERT;
};

struct BatchTreeCutMessage {
  Edge edge;
  uint8_t cut_start_tier = BATCH_NO_CUT_TIER;
};

struct BatchControlMessage {
  BatchUpdateStatus status = BATCH_UPDATE_NORMAL;
  uint32_t num_tree_cuts = 0;
  uint32_t num_updates = 0;
};

// Per-component info collected during Phase 2a and enriched in Phase 2b.
struct ComponentInfo {
  node_id_t    rep_vertex;
  uint32_t     my_size;        // this tier's component size
  uint32_t     next_size;      // tier above's component size (filled by exchange_sizes)
  SampleResult sample_result;  // this tier's sketch sample result
  edge_id_t    sampled_edge;   // the sampled edge (if GOOD)
};

// Sent from winning tier to InputNode (Phase 3): one per isolated component.
struct IsolationCandidate {
  node_id_t a;     // candidate replacement edge endpoint
  node_id_t b;     // candidate replacement edge endpoint
};

// Sent from InputNode to ALL tiers (Phase 5): structural operations to apply.
struct BatchEttInstruction {
  TreeOperationType type;   // LINK or CUT  (reuse enum from mpi_nodes.h-style)
  node_id_t endpoint1;
  node_id_t endpoint2;
  uint32_t  start_tier;     // apply at tiers >= start_tier
};

// Broadcast from InputNode to coordinate the fixing loop.
struct FixingRoundControl {
  uint32_t num_instructions;       // how many BatchEttInstructions follow. UINT32_MAX = done
  uint32_t winning_tier;           // the tier that won this round. UINT32_MAX if none.
};

// ============================================================================
//  BatchInputNode  (rank 0)
// ============================================================================

class BatchInputNode {
  node_id_t num_nodes;
  uint32_t  num_tiers;
  LinkCutTreeMaxAgg<int8_t> link_cut_tree;
  SketchlessEulerTourTree<> query_ett;
  long tree_ops_count = 0;

  std::vector<BatchUpdateMessage> update_buffer;
  std::vector<BatchTreeCutMessage> tree_cut_buffer;
  // Edge-only array broadcast to tier nodes (no op field needed for sketches).
  std::vector<Edge> edge_broadcast_buffer;
  // Permutation of canonical updates sorted by (dst, src) for Phase 1 locality.
  std::vector<uint32_t> dst_order_buffer;
  int buffer_size;
  int buffer_capacity;

  std::vector<GraphUpdate> transaction_log;

  BatchControlMessage control_message;

  // Scratch buffers used during process_updates()
  std::vector<IsolationCandidate> candidate_buffer;
  std::vector<BatchEttInstruction> instruction_buffer;

  inline BatchUpdateMessage& get_update_buffer(uint32_t idx) {
    return update_buffer[idx];
  }
  inline const BatchUpdateMessage& get_update_buffer(uint32_t idx) const {
    return update_buffer[idx];
  }

  void process_updates();

public:
  BatchInputNode(node_id_t num_nodes, uint32_t num_tiers, int batch_size, int seed);
  ~BatchInputNode();

  void initialize_node(node_id_t u) {
    query_ett.initialize_node(u);
    link_cut_tree.initialize_node(u);
  }
  void uninitialize_node(node_id_t u) {
    query_ett.uninitialize_node(u);
    link_cut_tree.uninitialize_node(u);
  }
  void initialize_all_nodes() {
    query_ett.initialize_all_nodes(num_nodes);
    link_cut_tree.initialize_all_nodes(num_nodes);
  }

  void update(GraphUpdate update);
  void process_all_updates();
  bool connectivity_query(node_id_t a, node_id_t b);
  std::vector<std::set<node_id_t>> cc_query();
  void end();

  SpaceReport report_space_usage();

  void flush_transaction_log() { transaction_log.clear(); }
  void drain_transaction_log(std::vector<GraphUpdate>& out) {
    if (!transaction_log.empty()) {
      out.insert(out.end(), transaction_log.begin(), transaction_log.end());
      transaction_log.clear();
    }
  }
  const std::vector<GraphUpdate>& get_transaction_log() const { return transaction_log; }
  size_t space_usage_bytes() const { return 0; }
  long get_num_tree_ops() const { return tree_ops_count; }
};

// ============================================================================
//  BatchTierNode  (ranks 1 .. num_tiers)
// ============================================================================

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
class BatchTierNode {
  using SketchClass  = typename TreeStrategy::SketchType;
  using ComponentView = typename TreeStrategy::ComponentView;
  using ComponentID   = typename TreeStrategy::ComponentID;

  TreeStrategy ett;
  uint32_t tier_num;
  uint32_t num_tiers;
  int      batch_size;

  // Receive buffer for the update batch (edge-only; op not needed for sketches).
  std::vector<Edge> update_buffer;
  std::vector<BatchTreeCutMessage> tree_cut_buffer;
  // Permutation of update indices [0, num_updates) sorted by (dst, src).
  // (i.e. for doing sketch updates in dst order)
  std::vector<uint32_t> dst_order_buffer;

  BatchControlMessage control_message;

  // Phase 2a-b: unique component info keyed by ComponentID.
  absl::flat_hash_map<size_t, ComponentInfo> component_map;
  // Stable per-round key order used to align query and response buffers.
  std::vector<size_t> component_keys;

  // Scratch buffers for MPI size exchange (Phase 2b).
  std::vector<node_id_t> size_query_buffer;   // rep vertices sent to tier above
  std::vector<uint32_t>  size_response_buffer; // sizes received from tier above
  std::vector<node_id_t> below_query_buffer;   // rep vertices received from tier below
  std::vector<uint32_t>  below_response_buffer; // sizes sent to tier below

  // Working sets for the fixing loop
  std::vector<node_id_t> active_vertices;      // The shrinking working set for the current round

  // Scratch state carried between check_and_report_isolations() and main().
  bool _found_isolation = false;
  std::vector<IsolationCandidate> _cached_candidates;

  void initialize_node(node_id_t u)  { ett.initialize_node(u); }
  void uninitialize_node(node_id_t u){ ett.uninitialize_node(u); }
  void initialize_all_nodes(node_id_t n) { ett.initialize_all_nodes(n); }
  bool is_initialized(node_id_t u)   { return ett.is_initialized(u); }

  // Phase 1: apply all sketch updates.
  void apply_sketch_updates(uint32_t num_updates);

  // Structural pre-pass: apply tree-edge cuts for this batch.
  void apply_tree_cuts(const BatchTreeCutMessage* cuts, uint32_t num_tree_cuts);

  // Phase 2a: deduplicate endpoints into component_map.
  // Acts exclusively on active_vertices.
  void deduplicate_components();

  // Phase 2b: exchange sizes with tier above/below.
  void exchange_sizes();

  // Phase 2c-d: check for isolations, gather to InputNode.
  // Returns true if this tier found at least one isolation.
  bool check_and_report_isolations();

  // Phase 5: receive and apply BatchEttInstructions.
  // Populates modified_vertices for the next tier's narrow re-scan.
  void apply_instructions(const BatchEttInstruction* instructions, uint32_t count);

public:
  BatchTierNode(node_id_t num_nodes, uint32_t tier_num, uint32_t num_tiers,
                int batch_size, int seed);
  ~BatchTierNode();
  void main();
};
