#include "../include/mpi_batch_nodes.h"
#include "../include/cutsets/lct_cutset.h"

#include <cassert>

namespace {
  template<typename T> struct is_cutset_lct : std::false_type {};
  template<typename S, typename C>
  struct is_cutset_lct<cutset_lct::CutsetLCT<S, C>> : std::true_type {};
}

// ============================================================================
//  BatchTierNode implementation
// ============================================================================

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
BatchTierNode<TreeStrategy>::BatchTierNode(
    node_id_t num_nodes, uint32_t tier_num, uint32_t num_tiers,
    int batch_size, int seed)
    : ett(num_nodes, tier_num, seed),
      tier_num(tier_num), num_tiers(num_tiers), batch_size(batch_size) {
  update_buffer.resize(batch_size);
  tree_cut_buffer.resize(batch_size);
  dst_order_buffer.resize(batch_size);
  active_vertices.reserve(batch_size * 2);
}

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
BatchTierNode<TreeStrategy>::~BatchTierNode() {
}

// ---------------------------------------------------------------------------
//  Phase 1: Apply all K sketch updates. Keep everything — no rollback.
// ---------------------------------------------------------------------------
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTierNode<TreeStrategy>::apply_sketch_updates(uint32_t num_updates) {
  for (uint32_t i = 0; i < num_updates; i++) {
    const auto& e = update_buffer[i];
    if (!is_initialized(e.src)) initialize_node(e.src);
    if (!is_initialized(e.dst)) initialize_node(e.dst);
  }

  // src pass
  for (uint32_t i = 0; i < num_updates; i++) {
    const auto& e = update_buffer[i];
    edge_id_t edge = VERTICES_TO_EDGE(e.src, e.dst);
    const ColumnEntryDelta delta = ett.generate_entry_delta(e.src, (vec_t)edge);
    ett.update_sketch(e.src, delta);
  }

  // dst pass
  for (uint32_t i = 0; i < num_updates; i++) {
    const uint32_t update_idx = dst_order_buffer[i];
    assert(update_idx < num_updates);
    const auto& e = update_buffer[update_idx];
    edge_id_t edge = VERTICES_TO_EDGE(e.src, e.dst);
    const ColumnEntryDelta delta = ett.generate_entry_delta(e.dst, (vec_t)edge);
    ett.update_sketch(e.dst, delta);
  }
}

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTierNode<TreeStrategy>::apply_tree_cuts(
    const BatchTreeCutMessage* cuts, uint32_t num_tree_cuts) {
  for (uint32_t i = 0; i < num_tree_cuts; i++) {
    const auto& cut_msg = cuts[i];
    if (tier_num >= cut_msg.cut_start_tier) {
      ett.cut(cut_msg.edge.src, cut_msg.edge.dst);
    }
  }
}

// ---------------------------------------------------------------------------
//  Phase 2a: Deduplicate endpoints into component_map.
// ---------------------------------------------------------------------------
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTierNode<TreeStrategy>::deduplicate_components() {
  component_map.clear();
  component_keys.clear();
  component_map.reserve(active_vertices.size());
  component_keys.reserve(active_vertices.size()); // Assuming full un-uniqueness for reserve speed

  // Memoize node→representative for LCT in non-LAZY mode.
  // Multiple vertices sharing the same physical root can skip repeated representative walks.
  constexpr bool use_rep_memo =
      is_cutset_lct<TreeStrategy>::value && (LCT_QUERY_MODE != cutset_lct::LCT_QUERY_MODE_LAZY);
  // constexpr bool use_rep_memo = false;
  if constexpr (use_rep_memo) {
      rep_memo_.clear();
  }

  for (node_id_t v : active_vertices) {
      ComponentView cv = ett.component_view(v);
      size_t cid;
      if constexpr (use_rep_memo) {
          void* memo_key = static_cast<void*>(cv.node);
          auto [memo_it, memo_inserted] = rep_memo_.try_emplace(memo_key, size_t{0});
          if (!memo_inserted) {
              cid = memo_it->second;
          } else {
              cid = static_cast<size_t>(cv.key());
              memo_it->second = cid;
          }
      } else {
          cid = static_cast<size_t>(cv.key());
      }

      auto [it, inserted] = component_map.try_emplace(cid);
      if (!inserted) continue;

      component_keys.push_back(cid);

      ComponentInfo& info = it->second;
      info.rep_vertex = v;  // Save the actual node_id_t
      info.my_size = cv.size();
      info.next_size = 0;

      SketchSample<vec_t> sample = cv.sketch().sample();
      info.sample_result = sample.result;
      info.sampled_edge = sample.idx;
  }
}

// ---------------------------------------------------------------------------
//  Phase 2b: Size exchange with the tier above.
// ---------------------------------------------------------------------------
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTierNode<TreeStrategy>::exchange_sizes() {
  // Build query buffer: one rep_vertex per unique component.
  size_query_buffer.resize(component_keys.size());
  for (size_t i = 0; i < component_keys.size(); i++) {
    size_query_buffer[i] = component_map[component_keys[i]].rep_vertex;
  }
  uint32_t U = size_query_buffer.size();

  // Answer size queries from the tier below us.
  auto answer_below = [&]() {
    int below_rank = tier_num;  // rank of tier_num - 1
    uint32_t below_U = 0;
    MPI_Recv(&below_U, sizeof(uint32_t), MPI_BYTE,
             below_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (below_U > 0) {
      below_query_buffer.resize(below_U);
      below_response_buffer.resize(below_U);
      MPI_Recv(below_query_buffer.data(), below_U * sizeof(node_id_t), MPI_BYTE,
               below_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      for (uint32_t i = 0; i < below_U; i++) {
        below_response_buffer[i] = ett.get_size(below_query_buffer[i]);
      }
      MPI_Send(below_response_buffer.data(), below_U * sizeof(uint32_t), MPI_BYTE,
               below_rank, 0, MPI_COMM_WORLD);
    }
  };

  // Send our rep_vertices up and receive sizes back.
  auto send_up_and_receive = [&]() {
    int above_rank = tier_num + 2;
    MPI_Send(&U, sizeof(uint32_t), MPI_BYTE, above_rank, 0, MPI_COMM_WORLD);
    if (U > 0) {
      MPI_Send(size_query_buffer.data(), U * sizeof(node_id_t), MPI_BYTE,
               above_rank, 0, MPI_COMM_WORLD);
      size_response_buffer.resize(U);
      MPI_Recv(size_response_buffer.data(), U * sizeof(uint32_t), MPI_BYTE,
               above_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      // Write responses back into component_map (key order is explicit).
      for (size_t i = 0; i < component_keys.size(); i++) {
        component_map[component_keys[i]].next_size = size_response_buffer[i];
      }
    }
  };

  if (tier_num == num_tiers - 1) {
    // Last tier: only answer queries from below; no tier above for us.
    answer_below();
    return;
  }

  if (tier_num == 0) {
    // Tier 0: no tier below, just send up.
    send_up_and_receive();
  } else if (tier_num % 2 == 0) {
    // Even: answer below first, then send up (avoids deadlock).
    answer_below();
    send_up_and_receive();
  } else {
    // Odd: send up first, then answer below.
    send_up_and_receive();
    answer_below();
  }
}

// ---------------------------------------------------------------------------
//  Phase 2c-d: Check for isolations, gather tier_num to InputNode.
//  If this tier has any isolation, also send the candidates (Phase 3).
// ---------------------------------------------------------------------------
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
bool BatchTierNode<TreeStrategy>::check_and_report_isolations() {
  std::vector<IsolationCandidate> candidates;
  bool found_isolation = false;
  
  // Wipe active_vertices safely after deduplication completes,
  // we will exclusively populate it with unresolved nodes for the next round.
  active_vertices.clear();

  // Last tier can never be isolated.
  if (tier_num == num_tiers - 1) {
    uint32_t my_isolation = UINT32_MAX;
    gather(&my_isolation, sizeof(uint32_t), nullptr, sizeof(uint32_t), 0);
    return false;
  }

  // Scan unique components for isolation.
  for (auto& [cid, comp] : component_map) {
    if (comp.sample_result == ZERO) continue;

    // Check isolation: same size as next tier AND sample is GOOD.
    if (comp.sample_result == GOOD && comp.my_size == comp.next_size) {
      found_isolation = true;
      IsolationCandidate cand;
      cand.a = (node_id_t)(comp.sampled_edge >> 32);
      cand.b = (node_id_t)(comp.sampled_edge);
      candidates.push_back(cand);

      // ONLY retain it if it was isolated (Its candidate might fail LCT, 
      // requiring it to sample again next round).
      active_vertices.push_back(comp.rep_vertex);
    }
    // If it wasn't isolated, we drop it. It will only wake up 
    // if an instruction explicitly modifies an edge connected to it.
  }

  // Phase 2d (optim 4): gather to InputNode.
  uint32_t my_isolation = found_isolation ? tier_num : UINT32_MAX;
  gather(&my_isolation, sizeof(uint32_t), nullptr, sizeof(uint32_t), 0);

  // Store for main() to use:
  if (found_isolation) {
    _cached_candidates = std::move(candidates);
  }
  _found_isolation = found_isolation;

  return found_isolation;
}

// ---------------------------------------------------------------------------
//  Phase 5: Apply structural instructions from InputNode.
// ---------------------------------------------------------------------------
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTierNode<TreeStrategy>::apply_instructions(
    const BatchEttInstruction* instructions, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    const auto& instr = instructions[i];
    if (tier_num >= instr.start_tier) {
      if (instr.type == CUT) {
        ett.cut(instr.endpoint1, instr.endpoint2);
      } else if (instr.type == LINK) {
        ett.link(instr.endpoint1, instr.endpoint2);
      }
    }
    active_vertices.push_back(instr.endpoint1);
    active_vertices.push_back(instr.endpoint2);
  }
}

// ---------------------------------------------------------------------------
//  Main event loop
// ---------------------------------------------------------------------------
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTierNode<TreeStrategy>::main() {
  while (true) {
    // Receive batch control first.
    bcast(&control_message, sizeof(BatchControlMessage), INPUT_NODE_RANK);

    if (control_message.status == BATCH_UPDATE_END) {
      std::cout << "============= BATCH TIER " << tier_num
                << " NODE =============" << std::endl
                << "Number of components: " << ett.num_components() << std::endl
                << "\tSpace used (MB): "
                << ett.space_usage_bytes() / (1024.0 * 1024.0) << std::endl;
      return;
    }

    if (control_message.status == BATCH_UPDATE_SPACE_REPORT) {
      // TODO - this needs to report
      // the overhead of the tier itself.
      // TODO - will do at later time. that overhead should only scale
      // with max num_updates
      SpaceReportMessage report;
      report.tier_num = tier_num;
      report.space_bytes = ett.space_usage_bytes();
      report.num_components = ett.num_components();
      MPI_Send(&report, sizeof(SpaceReportMessage), MPI_BYTE,
               INPUT_NODE_RANK, 0, MPI_COMM_WORLD);
      continue;
    }

    const uint32_t num_updates = control_message.num_updates;
    const uint32_t num_tree_cuts = control_message.num_tree_cuts;
    if (num_tree_cuts > 0) {
      // recieve tree cuts from input node and apply them
      bcast(tree_cut_buffer.data(), sizeof(BatchTreeCutMessage) * num_tree_cuts, INPUT_NODE_RANK);
      apply_tree_cuts(tree_cut_buffer.data(), num_tree_cuts);
    }
    
    // receive sketch updates
    bcast(update_buffer.data(), sizeof(Edge) * num_updates, INPUT_NODE_RANK);
    bcast(dst_order_buffer.data(), sizeof(uint32_t) * num_updates, INPUT_NODE_RANK);

    // ---- Phase 1: Apply all sketch updates ----
    apply_sketch_updates(num_updates);

    // Initialize the working set with all endpoints of the sketch updates
    active_vertices.clear();
    active_vertices.reserve(num_updates * 2);
    for (uint32_t i = 0; i < num_updates; i++) {
      active_vertices.push_back(update_buffer[i].src);
      active_vertices.push_back(update_buffer[i].dst);
    }

    // ---- Phases 2-6: Fixing loop ----
    while (true) {
      // Phase 2a: Deduplicate endpoints into component_map.
      // (If active_vertices is empty, this does essentially nothing)
      deduplicate_components();

      // Phase 2b: Exchange sizes with tier above/below.
      exchange_sizes();

      // Phase 2c: Check for isolations.
      // Modifies active_vertices by keeping unresolved ones, and clearing the rest.
      _found_isolation = false;
      _cached_candidates.clear();
      check_and_report_isolations();

      // Phase 3: If we're the winner, InputNode will recv from us.
      uint32_t winning_tier = UINT32_MAX;
      bcast(&winning_tier, sizeof(uint32_t), INPUT_NODE_RANK);

      if (winning_tier == UINT32_MAX) {
        // No isolations: done.
        // Still need to receive the FixingRoundControl.
        FixingRoundControl ctrl;
        bcast(&ctrl, sizeof(FixingRoundControl), INPUT_NODE_RANK);
        break;
      }

      // Phase 3 (only winner sends):
      if (tier_num == winning_tier && _found_isolation) {
        uint32_t num_candidates = _cached_candidates.size();
        MPI_Send(&num_candidates, sizeof(uint32_t), MPI_BYTE, INPUT_NODE_RANK, 0, MPI_COMM_WORLD);
        if (num_candidates > 0) {
          MPI_Send(_cached_candidates.data(),
                   num_candidates * sizeof(IsolationCandidate),
                   MPI_BYTE, INPUT_NODE_RANK, 0, MPI_COMM_WORLD);
        }
      }

      // Phase 5: Receive instruction broadcast from InputNode.
      FixingRoundControl ctrl;
      bcast(&ctrl, sizeof(FixingRoundControl), INPUT_NODE_RANK);

      if (ctrl.num_instructions > 0) {
        std::vector<BatchEttInstruction> instructions(ctrl.num_instructions);
        bcast(instructions.data(),
              ctrl.num_instructions * sizeof(BatchEttInstruction), INPUT_NODE_RANK);
        // apply_instructions also adds affected edges to active_vertices
        apply_instructions(instructions.data(), ctrl.num_instructions);
      }

      // Phase 7: Transition the working set
      if (tier_num < winning_tier) {
         // I am a low tier. My structure is fully stable. I can go to sleep.
         active_vertices.clear();
      }
      
      // If we are >= winning_tier, active_vertices currently holds:
      // (unresolved isolations from Phase 2c) + (new link/cut endpoints from Phase 6)
      // So it is already perfectly primed for the next round!
    }
  }
}

// Explicit template instantiations
template class BatchTierNode<EulerTourTree<DefaultSketchColumn>>;
template class BatchTierNode<ufo::CutsetUFOTree<DefaultSketchColumn>>;
template class BatchTierNode<cutset_lct::CutsetLCT<DefaultSketchColumn>>;
