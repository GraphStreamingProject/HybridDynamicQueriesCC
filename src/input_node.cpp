#include "../include/mpi_nodes.h"

#include <algorithm>


long normal_refreshes = 0;
long dt_operation_time = 0;
long num_updates = 0;

InputNode::InputNode(node_id_t num_nodes, uint32_t num_tiers, int batch_size, int seed) :
    num_nodes(num_nodes), num_tiers(num_tiers), query_forest(num_nodes, seed) {
    update_buffer = (UpdateMessage*) malloc(sizeof(UpdateMessage)*(batch_size+1));
    buffer_capacity = batch_size+1;
    UpdateMessage msg;
    update_buffer[0] = msg;
    buffer_size = 1;
    split_revert_buffer = (int*) malloc(sizeof(int)*batch_size);
    history_size = 2*batch_size;
    for (int i=0; i<history_size; i++)
        isolation_history_queue.push(true);
    isolation_count = history_size;
};

InputNode::~InputNode() {
    free(update_buffer);
    free(split_revert_buffer);
}

void InputNode::update(GraphUpdate update) {
    num_updates++;
    if (!query_forest.is_initialized(update.edge.src))
        query_forest.initialize_node(update.edge.src);
    if (!query_forest.is_initialized(update.edge.dst))
        query_forest.initialize_node(update.edge.dst);
    UpdateMessage update_message;
    update_message.update = update;
    update_buffer[buffer_size++] = update_message;
    if (buffer_size == buffer_capacity)
        process_updates();
}

void InputNode::process_updates() {
    if (buffer_size == 1)
        return;
    // Canonicalize same-edge updates within the batch.
    // Even multiplicities cancel; odd multiplicities keep the last update
    // type for that undirected edge.
    {
        const uint32_t raw_num_updates = buffer_size - 1;
        std::vector<GraphUpdate> raw_updates;
        raw_updates.reserve(raw_num_updates);
        for (uint32_t i = 0; i < raw_num_updates; i++) {
            GraphUpdate update = update_buffer[i + 1].update;
            if (update.edge.src > update.edge.dst) {
                std::swap(update.edge.src, update.edge.dst);
            }
            raw_updates.push_back(update);
        }

        std::stable_sort(raw_updates.begin(), raw_updates.end(),
                         [](const GraphUpdate& lhs, const GraphUpdate& rhs) {
                             if (lhs.edge.src != rhs.edge.src) {
                                 return lhs.edge.src < rhs.edge.src;
                             }
                             return lhs.edge.dst < rhs.edge.dst;
                         });

        uint32_t canonical_count = 0;
        size_t i = 0;
        while (i < raw_updates.size()) {
            size_t j = i + 1;
            while (j < raw_updates.size() &&
                   raw_updates[j].edge.src == raw_updates[i].edge.src &&
                   raw_updates[j].edge.dst == raw_updates[i].edge.dst) {
                j++;
            }

            const size_t run_len = j - i;
            if ((run_len & 1) == 1) {
                UpdateMessage msg;
                msg.update = raw_updates[j - 1];
                update_buffer[canonical_count + 1] = msg;
                canonical_count++;
            }
            i = j;
        }

        buffer_size = static_cast<int>(canonical_count + 1);
        if (buffer_size == 1) {
            return;
        }
    }
    // BUFFER PRE-PROCESSING !
    // for every update; if we know it's isolated (adds new connectivity) info,
    // swap it to the front of the buffer


    uint32_t num_updates = buffer_size-1;
    const size_t tx_log_start = transaction_log.size();
    // If less than 1/10 of the last updates are isolated use sliding window
    bool prev_strat = using_sliding_window;
    using_sliding_window = false;//(isolation_count<history_size/10) ? true : false;
    // using_sliding_window = true;
    if (using_sliding_window != prev_strat)
        std::cout << "SWITCHED TO " << (using_sliding_window ? "SLIDING WINDOW" : "NORMAL STRAT") << std::endl;
    // Pre-compute which tiers should be cut for each delete.
    for (uint32_t i = 0; i < num_updates; i++) {
        GraphUpdate update = update_buffer[i+1].update;
        update_buffer[i+1].cut_start_tier = UINT32_MAX;
        split_revert_buffer[i] = MAX_INT;
        unlikely_if (update.type == DELETE && query_forest.has_edge(update.edge.src, update.edge.dst)) {
            // NOTE - since query ett and lct are sync'd, we know that the path_query will return 
            // exactly the weight of the edge, which is the tier it belongs to.
            std::pair<Edge, int8_t> max_edge = query_forest.path_query(update.edge.src, update.edge.dst);
            // note that max_edge.second HAS to be between 0 and num_tiers-1, 
            // since we assign edge weights based on tier in the LCT, so no need for extra checks here
            split_revert_buffer[i] = max_edge.second;
            update_buffer[i+1].cut_start_tier = static_cast<uint32_t>(max_edge.second);
        }
    }
    // Broadcast the batch of updates to all nodes
    update_buffer[0].update.edge.src = num_updates;
    update_buffer[0].update.edge.dst = (int)using_sliding_window;
    bcast(&update_buffer[0], sizeof(UpdateMessage)*buffer_capacity, 0);
    // Do all the link cut tree cuts for tree-edge deletes in the batch.
    for (uint32_t i = 0; i < num_updates; i++) {
        GraphUpdate update = update_buffer[i+1].update;
        unlikely_if (update_buffer[i+1].cut_start_tier != UINT32_MAX) {
            // probably where most structural (spanning forest) deletes happen?
            // potentially - revisit
            query_forest.cut(update.edge.src, update.edge.dst);
            tree_ops_count++;
            // transaction_log.add(update.edge, DELETE);
            transaction_log.push_back(update);
        }
    }
    // Attempt to do the entire batch parallel with greedy refresh
    int isolated_update = MAX_INT;
    int minimum_isolated_update;
    allreduce(&isolated_update, &minimum_isolated_update);
    // Check for any isolation on any update on any tier
    if (minimum_isolated_update == MAX_INT) {
        buffer_size = 1;
        return;
    }
    // Pre-cut emits provisional deletes for the full batch. Once isolation is
    // found, only updates strictly before minimum_isolated_update are finalized
    // at this point; suffix updates are replayed below.
    transaction_log.resize(tx_log_start);
    for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(minimum_isolated_update); i++) {
        if (split_revert_buffer[i] != MAX_INT) {
            transaction_log.push_back(update_buffer[i + 1].update);
        }
    }
    // First undo all the link cut tree cuts we did after isolated update
    for (uint32_t update_idx = minimum_isolated_update; update_idx < num_updates+1; update_idx++) {
        GraphUpdate update = update_buffer[update_idx].update;
        // There could be a cut on a later update that needs to be rolled back
        unlikely_if (split_revert_buffer[update_idx-1] != MAX_INT) {
            query_forest.link(update.edge.src, update.edge.dst, static_cast<int8_t>(split_revert_buffer[update_idx-1]));
            tree_ops_count++;
            // do not count rollbacks
            // if (split_revert_buffer[update_idx-1] > _max_link_tier) _max_link_tier = split_revert_buffer[update_idx-1];
            // Rollback links are provisional and must not leak to the external
            // transaction log consumed by the hybrid manager.
        }
    }
    // Update the isolation history
    for (uint32_t i = 1; i < minimum_isolated_update; i++) {
        isolation_count -= (int)isolation_history_queue.front();
        isolation_history_queue.pop();
        isolation_history_queue.push(true);
    }
    // ======================================================================================
    // =========================== PROCESS THE ISOLATED UPDATES =============================
    // ======================================================================================
    int end_update_idx = using_sliding_window ? minimum_isolated_update+1 : num_updates+1;
    for (int update_idx = minimum_isolated_update; update_idx < end_update_idx; update_idx++) {
        GraphUpdate update = update_buffer[update_idx].update;
        UpdateMessage replay_update_message;
        replay_update_message.update = update;
        replay_update_message.cut_start_tier = UINT32_MAX;
        replay_update_message.status = UPDATE_NORMAL;
        START(dt_operation_timer1);
        // Re-evaluate delete cuts against the live forest state during replay.
        // Precomputed cut_start_tier can be stale after rollback/refresh relinks.
        unlikely_if (update.type == DELETE && query_forest.has_edge(update.edge.src, update.edge.dst)) {
            std::pair<Edge, int8_t> max_edge = query_forest.path_query(update.edge.src, update.edge.dst);
            replay_update_message.cut_start_tier = static_cast<uint32_t>(max_edge.second);
            query_forest.cut(update.edge.src, update.edge.dst);
            tree_ops_count++;
            // transaction_log.add(update.edge, DELETE);
            transaction_log.push_back(update);
        }
        // Broadcast the replay-time cut tier so tier nodes apply deletes using
        // fresh state rather than stale batch prepass metadata.
        bcast(&replay_update_message, sizeof(UpdateMessage), 0);
        STOP(dt_operation_time, dt_operation_timer1);
        uint32_t start_tier = 0;
        normal_refreshes++;
        bool this_update_isolated = false;
        // Initiate the refresh sequence and receive all the broadcasts
        RefreshEndpoint e1, e2;
        e1.v = update.edge.src;
        e2.v = update.edge.dst;
        RefreshMessage refresh_message;
        refresh_message.endpoints = {e1, e2};
        MPI_Send(&refresh_message, sizeof(RefreshMessage), MPI_BYTE, start_tier+1, 0, MPI_COMM_WORLD);
        for (uint32_t tier = start_tier; tier < num_tiers; tier++) {
            int rank = tier + 1;
            // bool break_early = true;
            if (tier != 0)
            for (auto endpoint : {0,1}) {
                std::ignore = endpoint;
                // Receive a broadcast to see if the current tier/endpoint is isolated or not
                EttUpdateMessage update_message;
                bcast(&update_message, sizeof(EttUpdateMessage), rank);
                if (update_message.type == NOT_ISOLATED) {
                    continue;
                }
                // else {
                //     break_early = false;
                // }
                this_update_isolated = true;
                // Process a LCT query message first
                LctResponseMessage response_message;
                // response_message.connected = link_cut_tree.find_root(update_message.endpoint1) == link_cut_tree.find_root(update_message.endpoint2);
                response_message.connected = query_forest.is_connected(update_message.endpoint1, update_message.endpoint2);
                if (response_message.connected) {
                    std::pair<Edge, int8_t> max = query_forest.path_query(update_message.endpoint1, update_message.endpoint2);
                    response_message.cycle_edge = VERTICES_TO_EDGE(max.first.src, max.first.dst);
                    response_message.weight = max.second;
                }
                MPI_Send(&response_message, sizeof(LctResponseMessage), MPI_BYTE, rank, 0, MPI_COMM_WORLD);

                // Then process two update broadcasts to potentially cut and link in the LCT
                for (auto broadcast : {0,1}) {
                    std::ignore = broadcast;
                    EttUpdateMessage update_message;
                    bcast(&update_message, sizeof(EttUpdateMessage), rank);
                    START(dt_operation_timer2);
                    if (update_message.type == LINK) {
                        query_forest.link(update_message.endpoint1, update_message.endpoint2,
                                          static_cast<int8_t>(update_message.start_tier));
                        tree_ops_count++;
                        if (static_cast<int>(update_message.start_tier) > _max_link_tier) _max_link_tier = static_cast<int>(update_message.start_tier);
                        // transaction_log.add(update_message, INSERT);
                        transaction_log.push_back(
                            GraphUpdate{Edge{update_message.endpoint1, update_message.endpoint2}, INSERT});
                        break;
                    } else if (update_message.type == CUT) {
                        query_forest.cut(update_message.endpoint1, update_message.endpoint2);
                        tree_ops_count++;
                        // transaction_log.add(update_message, DELETE);
                        transaction_log.push_back(
                            GraphUpdate{Edge{update_message.endpoint1, update_message.endpoint2}, DELETE});
                    }
                    STOP(dt_operation_time, dt_operation_timer2);
                }
            }
            // if (break_early) break;
        }
        isolation_count -= (int)isolation_history_queue.front();
        isolation_history_queue.pop();
        if (this_update_isolated) {
            isolation_history_queue.push(true);
            isolation_count += 1;
        } else
            isolation_history_queue.push(false);
    }
    // Shift the rest of the updates to the beginning of the buffer
    if (using_sliding_window) {
        for (int i = 0; i < buffer_size-minimum_isolated_update-1; i++)
            update_buffer[i+1] = update_buffer[minimum_isolated_update+i+1];
        buffer_size = buffer_size-minimum_isolated_update;
    } else {
        buffer_size = 1;
    }
}

void InputNode::process_all_updates() {
    while (buffer_size > 1)
        process_updates();
}

bool InputNode::connectivity_query(node_id_t a, node_id_t b) {
    process_all_updates();
    return query_forest.is_connected(a, b);
}

std::vector<std::set<node_id_t>> InputNode::cc_query() {
    process_all_updates();
    return query_forest.cc_query();
}

void InputNode::end() {
    process_all_updates();
    // Tell all nodes the stream is over
    update_buffer[0].status = UPDATE_END;
    bcast(update_buffer, sizeof(UpdateMessage)*buffer_capacity, 0);
     std::cout << "======================= INPUT NODE ======================" << std::endl;
     std::cout << "Dynamic tree operations time (ms): " << dt_operation_time/1000 << std::endl;
     std::cout << "Normal refreshes: " << normal_refreshes << std::endl;
     std::cout << "Number of updates: " << num_updates << std::endl;
}

SpaceReport InputNode::report_space_usage() {
    process_all_updates();
    // Tell all tier nodes to report their space usage
    update_buffer[0].status = UPDATE_SPACE_REPORT;
    bcast(update_buffer, sizeof(UpdateMessage)*buffer_capacity, 0);
    // Reset status so normal processing can continue
    update_buffer[0].status = UPDATE_NORMAL;

    SpaceReport report;
    report.tier_reports.resize(num_tiers);
    // Collect space reports from each tier node (ranks 1..num_tiers)
    for (uint32_t i = 0; i < num_tiers; i++) {
        MPI_Recv(&report.tier_reports[i], sizeof(SpaceReportMessage), MPI_BYTE,
                 i + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    report.query_tree_bytes = query_forest.space_usage_bytes();
    report.top_level_lct_bytes = query_forest.lct_space_usage_bytes();
    return report;
}
