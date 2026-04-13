#include "../include/mpi_batch_nodes.h"

#include <algorithm>
#include <numeric>

// ============================================================================
//  BatchInputNode implementation
// ============================================================================

BatchInputNode::BatchInputNode(node_id_t num_nodes, uint32_t num_tiers, int batch_size, int seed)
    : num_nodes(num_nodes), num_tiers(num_tiers),
  query_forest(num_nodes, seed) {
  buffer_capacity = batch_size;
  update_buffer.resize(buffer_capacity);
  tree_cut_buffer.reserve(batch_size);
  edge_broadcast_buffer.resize(batch_size);
  dst_order_buffer.resize(batch_size);
  buffer_size = 0;
}

BatchInputNode::~BatchInputNode() {
}

void BatchInputNode::update(GraphUpdate update) {
  if (update.edge.src > update.edge.dst) {
    std::swap(update.edge.src, update.edge.dst);
  }
  if (!query_forest.is_initialized(update.edge.src))
    query_forest.initialize_node(update.edge.src);
  if (!query_forest.is_initialized(update.edge.dst))
    query_forest.initialize_node(update.edge.dst);
  BatchUpdateMessage msg;
  msg.edge = update.edge;
  msg.op = (update.type == INSERT) ? BATCH_OP_INSERT : BATCH_OP_DELETE;
  update_buffer[buffer_size++] = msg;
  if (buffer_size == buffer_capacity)
    process_updates();
}

// ---------------------------------------------------------------------------
//  Core batch processing
// ---------------------------------------------------------------------------
void BatchInputNode::process_updates() {
  if (buffer_size == 0) return;

  // Canonicalize batch by edge endpoints: cancel even-parity runs and keep one
  // odd-parity net op per undirected edge.
  const uint32_t raw_num_updates = buffer_size;
  std::vector<GraphUpdate> raw_updates;
  raw_updates.reserve(raw_num_updates);
  for (uint32_t i = 0; i < raw_num_updates; i++) {
    const auto& msg = get_update_buffer(i);
    GraphUpdate upd;
    upd.edge = msg.edge;
    upd.type = (msg.op == BATCH_OP_INSERT) ? INSERT : DELETE;
    if (upd.edge.src > upd.edge.dst) {
      std::swap(upd.edge.src, upd.edge.dst);
    }
    raw_updates.push_back(upd);
  }

  std::sort(raw_updates.begin(), raw_updates.end(),
            [](const GraphUpdate& lhs, const GraphUpdate& rhs) {
              if (lhs.edge.src != rhs.edge.src) return lhs.edge.src < rhs.edge.src;
              return lhs.edge.dst < rhs.edge.dst;
            });

  // compactify updates 
  uint32_t num_updates = 0;
  size_t i = 0;
  while (i < raw_updates.size()) {
    size_t j = i;
    int insert_count = 0;
    int delete_count = 0;
    while (j < raw_updates.size() &&
           raw_updates[j].edge.src == raw_updates[i].edge.src &&
           raw_updates[j].edge.dst == raw_updates[i].edge.dst) {
      if (raw_updates[j].type == INSERT) {
        insert_count++;
      } else {
        delete_count++;
      }
      j++;
    }

    const size_t run_len = j - i;
    if ((run_len & 1) == 1) {
      BatchUpdateMessage& out = get_update_buffer(num_updates);
      out.edge.src = raw_updates[i].edge.src;
      out.edge.dst = raw_updates[i].edge.dst;
      out.op = (insert_count > delete_count) ? BATCH_OP_INSERT : BATCH_OP_DELETE;
      num_updates++;
    }
    i = j;
  }

  if (num_updates == 0) {
    buffer_size = 0;
    return;
  }

  buffer_size = static_cast<int>(num_updates);

  // ---- Phase 0: Pre-compute tree-edge deletion tiers ----
  tree_cut_buffer.clear();
  for (uint32_t i = 0; i < num_updates; i++) {
      BatchUpdateMessage& msg = get_update_buffer(i);
      if (msg.op == BATCH_OP_DELETE && query_forest.has_edge(msg.edge.src, msg.edge.dst)) {
          std::pair<Edge, int8_t> info = query_forest.path_query(msg.edge.src, msg.edge.dst);
          BatchTreeCutMessage cut_msg;
          cut_msg.edge = msg.edge;
          cut_msg.cut_start_tier = static_cast<uint8_t>(info.second);
          tree_cut_buffer.push_back(cut_msg);
      }
  }

  // Broadcast the normal-batch control, then structural cuts, then sketch payload.
  control_message.status = BATCH_UPDATE_NORMAL;
  control_message.num_tree_cuts = static_cast<uint32_t>(tree_cut_buffer.size());
  control_message.num_updates = num_updates;
  bcast(&control_message, sizeof(BatchControlMessage), INPUT_NODE_RANK);
  if (control_message.num_tree_cuts > 0) {
    bcast(tree_cut_buffer.data(),
          sizeof(BatchTreeCutMessage) * control_message.num_tree_cuts,
          INPUT_NODE_RANK);
  }

  // Apply tree-edge cuts locally on LCT + query ETT while tier nodes are
  // processing their received tree cuts.
  for (const auto& cut_msg : tree_cut_buffer) {
    query_forest.cut(cut_msg.edge.src, cut_msg.edge.dst);
    tree_ops_count++;
    transaction_log.push_back({{cut_msg.edge.src, cut_msg.edge.dst}, DELETE});
  }

  // Build edge-only broadcast buffer and dst-order permutation while tier
  // nodes are still processing tree cuts.
  for (uint32_t i = 0; i < num_updates; i++) {
    edge_broadcast_buffer[i] = update_buffer[i].edge;
  }
  std::iota(dst_order_buffer.begin(), dst_order_buffer.begin() + num_updates, 0U);
  std::sort(dst_order_buffer.begin(), dst_order_buffer.begin() + num_updates,
            [&](uint32_t lhs, uint32_t rhs) {
              const auto& l = edge_broadcast_buffer[lhs];
              const auto& r = edge_broadcast_buffer[rhs];
              if (l.dst != r.dst) return l.dst < r.dst;
              return l.src < r.src;
            });
  
  //broadcast information for sketch updates to tier nodes
  bcast(edge_broadcast_buffer.data(), sizeof(Edge) * num_updates, INPUT_NODE_RANK);
  bcast(dst_order_buffer.data(), sizeof(uint32_t) * num_updates, INPUT_NODE_RANK);

  // ---- Phase 1: Tiers apply sketch updates (no InputNode work here). ----

  // ---- Phases 2-6: Fixing loop ----
  // InputNode orchestrates by gathering isolation reports and broadcasting
  // structural instructions.

  // The first round uses the full batch_size endpoints; subsequent rounds narrow to
  // only vertices affected by the previous round's link/cut instructions.

  bool first_round = true;
  uint32_t current_check_tier = 0; // will be overwritten by gather

  while (true) {
    // Phase 2d (optim 4): InputNode gathers isolation info from all tiers.
    // Each tier sends: (tier_num if isolated, UINT32_MAX otherwise).
    // InputNode receives via MPI_Gather and computes the minimum.
    // Rank 0 sends UINT32_MAX as its own contribution.
    uint32_t my_isolation = UINT32_MAX;
    std::vector<uint32_t> all_isolations(num_tiers + 1);
    gather(&my_isolation, sizeof(uint32_t),
          all_isolations.data(), sizeof(uint32_t), INPUT_NODE_RANK);

    uint32_t first_isolated_tier = UINT32_MAX;
    for (uint32_t i = 1; i <= num_tiers; i++) {
      if (all_isolations[i] < first_isolated_tier)
        first_isolated_tier = all_isolations[i];
    }

    if (first_isolated_tier == UINT32_MAX) {
      // No isolations anywhere — broadcast winning_tier=UINT32_MAX, then
      // the "done" control message, and exit.
      uint32_t winning_tier = UINT32_MAX;
      bcast(&winning_tier, sizeof(uint32_t), INPUT_NODE_RANK);
      FixingRoundControl ctrl;
      ctrl.num_instructions = 0;
      ctrl.winning_tier = UINT32_MAX;
      bcast(&ctrl, sizeof(FixingRoundControl), INPUT_NODE_RANK);
      break;
    }

    // Broadcast winning tier so all tiers know who sends candidates.
    bcast(&first_isolated_tier, sizeof(uint32_t), INPUT_NODE_RANK);

    // ---- Phase 3: Receive candidates from winning tier ----
    int winning_rank = first_isolated_tier + 1;
    uint32_t num_candidates = 0;
    MPI_Recv(&num_candidates, sizeof(uint32_t), MPI_BYTE, winning_rank,
             0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    candidate_buffer.resize(num_candidates);
    if (num_candidates > 0) {
      MPI_Recv(candidate_buffer.data(),
               num_candidates * sizeof(IsolationCandidate),
               MPI_BYTE, winning_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // ---- Phase 4: Resolve candidates against LCT ----
    instruction_buffer.clear();

    for (uint32_t c = 0; c < num_candidates; c++) {
      node_id_t a = candidate_buffer[c].a;
      node_id_t b = candidate_buffer[c].b;
      if (a > b) {
        std::swap(a, b);
      }

      if (query_forest.connected(a, b)) {
        std::pair<Edge, int8_t> max_edge = query_forest.path_query(a, b);
        node_id_t c_node = max_edge.first.src;
        node_id_t d_node = max_edge.first.dst;
        uint32_t appeared_tier = static_cast<uint32_t>(max_edge.second);

        if (appeared_tier == first_isolated_tier + 1) {
          // Same optimization as batch_tiers: skip this candidate.
          continue;
        }

        // Cut the old edge.
        BatchEttInstruction cut_instr;
        cut_instr.type = CUT;
        cut_instr.endpoint1 = c_node;
        cut_instr.endpoint2 = d_node;
        cut_instr.start_tier = appeared_tier;
        instruction_buffer.push_back(cut_instr);

        query_forest.cut(c_node, d_node);
        tree_ops_count++;
        transaction_log.push_back({{c_node, d_node}, DELETE});

        // Link the new edge.
        BatchEttInstruction link_instr;
        link_instr.type = LINK;
        link_instr.endpoint1 = a;
        link_instr.endpoint2 = b;
        link_instr.start_tier = first_isolated_tier + 1;
        instruction_buffer.push_back(link_instr);

        query_forest.link(a, b, static_cast<int8_t>(first_isolated_tier + 1));
        tree_ops_count++;
        if (static_cast<int>(first_isolated_tier) > _max_link_tier) _max_link_tier = static_cast<int>(first_isolated_tier);
        transaction_log.push_back({{a, b}, INSERT});
      } else {
        // No cycle: just link.
        BatchEttInstruction link_instr;
        link_instr.type = LINK;
        link_instr.endpoint1 = a;
        link_instr.endpoint2 = b;
        link_instr.start_tier = first_isolated_tier + 1;
        instruction_buffer.push_back(link_instr);

        query_forest.link(a, b, static_cast<int8_t>(first_isolated_tier + 1));
        tree_ops_count++;
        if (static_cast<int>(first_isolated_tier) > _max_link_tier) _max_link_tier = static_cast<int>(first_isolated_tier);
        transaction_log.push_back({{a, b}, INSERT});
      }
    }

    // ---- Phase 5: Broadcast instructions to all tiers ----
    FixingRoundControl ctrl;
    
    if (first_isolated_tier == UINT32_MAX) {
      // Done! No isolations found anywhere.
      ctrl.num_instructions = UINT32_MAX;
      ctrl.winning_tier = UINT32_MAX;
      bcast(&ctrl, sizeof(FixingRoundControl), INPUT_NODE_RANK);
      break;
    } else {
      ctrl.num_instructions = instruction_buffer.size();
      ctrl.winning_tier = first_isolated_tier;
      bcast(&ctrl, sizeof(FixingRoundControl), INPUT_NODE_RANK);

      if (ctrl.num_instructions > 0) {
        bcast(instruction_buffer.data(),
            ctrl.num_instructions * sizeof(BatchEttInstruction), INPUT_NODE_RANK);
      }
    }
    
    first_round = false;
  }

  buffer_size = 0;
}

void BatchInputNode::process_all_updates() {
  while (buffer_size > 0)
    process_updates();
}

bool BatchInputNode::connectivity_query(node_id_t a, node_id_t b) {
  process_all_updates();
  return query_forest.is_connected(a, b);
}

std::vector<std::set<node_id_t>> BatchInputNode::cc_query() {
  process_all_updates();
  return query_forest.cc_query();
}

void BatchInputNode::end() {
  process_all_updates();
  control_message.status = BATCH_UPDATE_END;
  control_message.num_tree_cuts = 0;
  control_message.num_updates = 0;
  bcast(&control_message, sizeof(BatchControlMessage), INPUT_NODE_RANK);
  std::cout << "======================= BATCH INPUT NODE ======================" << std::endl;
}

SpaceReport BatchInputNode::report_space_usage() {
  process_all_updates();
  control_message.status = BATCH_UPDATE_SPACE_REPORT;
  control_message.num_tree_cuts = 0;
  control_message.num_updates = 0;
  bcast(&control_message, sizeof(BatchControlMessage), INPUT_NODE_RANK);
  control_message.status = BATCH_UPDATE_NORMAL;

  SpaceReport report;
  report.tier_reports.resize(num_tiers);
  for (uint32_t i = 0; i < num_tiers; i++) {
    MPI_Recv(&report.tier_reports[i], sizeof(SpaceReportMessage), MPI_BYTE,
             i + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  }
  report.query_tree_bytes = query_forest.space_usage_bytes();
  report.top_level_lct_bytes = query_forest.lct_space_usage_bytes();
  return report;
}
