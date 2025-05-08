#include "../include/mpi_nodes.h"


long sketch_update_time = 0;
long sketch_query_time = 0;
long greedy_batch_time = 0;
long normal_refresh_time = 0;

long greedy_batch_gather_time = 0;
long size_message_passing_time = 0;

TierNode::TierNode(node_id_t num_nodes, uint32_t tier_num, uint32_t num_tiers, int batch_size, int seed) :
    tier_num(tier_num), num_tiers(num_tiers), batch_size(batch_size), ett(num_nodes, tier_num, seed) {
    update_buffer = (UpdateMessage*) malloc(sizeof(UpdateMessage)*(batch_size+1));
    this_sizes_buffer = (GreedyRefreshMessage*) malloc(sizeof(GreedyRefreshMessage)*batch_size);
    next_sizes_buffer = (GreedyRefreshMessage*) malloc(sizeof(GreedyRefreshMessage)*batch_size);
    query_result_buffer = (SampleResult*) malloc(sizeof(SampleResult)*batch_size*2);
    split_revert_buffer = (bool*) malloc(sizeof(bool)*batch_size);
}

TierNode::~TierNode() {
    free(update_buffer);
    free(this_sizes_buffer);
    free(next_sizes_buffer);
    free(query_result_buffer);
    free(split_revert_buffer);
}

void TierNode::main() {
    while (true) {
        // Receive a batch of updates and check if it is the end of stream
        bcast(update_buffer, sizeof(UpdateMessage)*(batch_size+1), 0);
        if (update_buffer[0].end) {
            // std::cout << "============= TIER " << tier_num << " NODE =============" << std::endl;
            // std::cout << "Greedy batch time (ms): " << greedy_batch_time/1000 << std::endl;
            // std::cout << "\tSketch update time (ms): " << sketch_update_time/1000 << std::endl;
            // std::cout << "\tSketch query time (ms): " << sketch_query_time/1000 << std::endl;
            // std::cout << "\tSize message passing time (ms): " << size_message_passing_time/1000 << std::endl;
            // std::cout << "\tGreedy gather time (ms): " << greedy_batch_gather_time/1000 << std::endl;
            // std::cout << "Normal refresh time (ms): " << normal_refresh_time/1000 << std::endl;
            return;
        }
        uint32_t num_updates = update_buffer[0].update.edge.src;
        using_sliding_window = (bool)update_buffer[0].update.edge.dst;
        // Do the greedy refresh check for all updates in the batch
        START(greedy_batch_timer);
        START(sketch_update_timer);
        for (uint32_t i = 0; i < num_updates; i++) {
            // Perform the sketch updating or root finding
            GraphUpdate update = update_buffer[i+1].update;
            edge_id_t edge = VERTICES_TO_EDGE(update.edge.src, update.edge.dst);
            split_revert_buffer[i] = false;
            unlikely_if (update.type == DELETE && ett.has_edge(update.edge.src, update.edge.dst)) {
                ett.cut(update.edge.src, update.edge.dst);
                ENDPOINT_CANARY("Cutting ETT With", update.edge.src, update.edge.dst);
                split_revert_buffer[i] = true;
            }
            auto roots = ett.update_sketches(update.edge.src, update.edge.dst, (vec_t)edge);
            ENDPOINT_CANARY("Updating Sketch With", update.edge.src, update.edge.dst);
            roots.first->process_updates();
            roots.first->sketch_agg->reset_sample_state();
            query_result_buffer[2*i] = roots.first->sketch_agg->sample().result;
            roots.second->process_updates();
            roots.second->sketch_agg->reset_sample_state();
            query_result_buffer[2*i+1] = roots.second->sketch_agg->sample().result;
    
            // Prepare greedy batch size messages
            GreedyRefreshMessage this_sizes;
            this_sizes.size1 = roots.first->size;
            this_sizes.size2 = roots.second->size;
            this_sizes_buffer[i] = this_sizes;
        }
        STOP(sketch_update_time, sketch_update_timer);
        START(size_message_passing_timer);
        if (tier_num == 0) {
            MPI_Recv(next_sizes_buffer, batch_size*sizeof(GreedyRefreshMessage), MPI_BYTE, tier_num+2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        } else if (tier_num == num_tiers-1) {
            MPI_Send(this_sizes_buffer, batch_size*sizeof(GreedyRefreshMessage), MPI_BYTE, tier_num, 0, MPI_COMM_WORLD);
        } else if (tier_num%2 == 0) {
            MPI_Send(this_sizes_buffer, batch_size*sizeof(GreedyRefreshMessage), MPI_BYTE, tier_num, 0, MPI_COMM_WORLD);
            MPI_Recv(next_sizes_buffer, batch_size*sizeof(GreedyRefreshMessage), MPI_BYTE, tier_num+2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        } else if (tier_num%2 == 1) {
            MPI_Recv(next_sizes_buffer, batch_size*sizeof(GreedyRefreshMessage), MPI_BYTE, tier_num+2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Send(this_sizes_buffer, batch_size*sizeof(GreedyRefreshMessage), MPI_BYTE, tier_num, 0, MPI_COMM_WORLD);
        }
        STOP(size_message_passing_time, size_message_passing_timer);
        START(sketch_query_timer);
        int isolated_update;
        // Check if this tier is isolated for each update
        isolated_update = MAX_INT;
        for (uint32_t i = 0; i < num_updates; i++) {
            // Check if this tier is isolated for this update
            if (tier_num != num_tiers-1) {
                if (this_sizes_buffer[i].size1 == next_sizes_buffer[i].size1)
                    if (query_result_buffer[2*i] == GOOD) {
                        isolated_update = i+1;
                        break;
                    }
                if (this_sizes_buffer[i].size2 == next_sizes_buffer[i].size2)
                    if (query_result_buffer[2*i+1] == GOOD) {
                        isolated_update = i+1;
                        break;
                    }
            }
        }
        STOP(sketch_query_time, sketch_query_timer);
        START(greedy_batch_gather_timer);
        int minimum_isolated_update;
        allreduce(&isolated_update, &minimum_isolated_update);
        // Check for any isolation on any update on any tier
        STOP(greedy_batch_gather_time, greedy_batch_gather_timer);
        STOP(greedy_batch_time, greedy_batch_timer);
        if (minimum_isolated_update == MAX_INT)
            continue;
        // First undo all the sketch updates we did after isolated update
        for (uint32_t update_idx = minimum_isolated_update; update_idx < num_updates+1; update_idx++) {
            GraphUpdate update = update_buffer[update_idx].update;
            edge_id_t edge = VERTICES_TO_EDGE(update.edge.src, update.edge.dst);
            // There could be a cut on a later update that needs to be rolled back
            unlikely_if (split_revert_buffer[update_idx-1]) {
                ett.link(update.edge.src, update.edge.dst);
            }
            ett.update_sketches(update.edge.src, update.edge.dst, (vec_t)edge);
        }
        // ======================================================================================
        // =========================== PROCESS THE ISOLATED UPDATES ===============+=============
        // ======================================================================================
        int end_update_idx = using_sliding_window ? minimum_isolated_update+1 : num_updates+1;
        for (int update_idx = minimum_isolated_update; update_idx < end_update_idx; update_idx++) {
            GraphUpdate update = update_buffer[update_idx].update;
            edge_id_t edge = VERTICES_TO_EDGE(update.edge.src, update.edge.dst);
            unlikely_if (update.type == DELETE && ett.has_edge(update.edge.src, update.edge.dst)) {
                ett.cut(update.edge.src, update.edge.dst);
            }
            ett.update_sketches(update.edge.src, update.edge.dst, (vec_t)edge);
            uint32_t start_tier = 0;
            // Start the refreshing sequence
            START(normal_refresh_timer);
            for (uint32_t tier = start_tier; tier < num_tiers; tier++) {
                int rank = tier + 1;
                // If this node's tier is the current tier process the refresh message from previous tier or input node
                if (tier == tier_num) {
                    RefreshMessage refresh_message;
                    int source = (tier == start_tier) ? 0 : tier_num;
                    MPI_Recv(&refresh_message, sizeof(RefreshMessage), MPI_BYTE, source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    if (tier != 0)
                        refresh_tier(refresh_message);
                    // Send a refresh message to the next tier
                    if (tier < num_tiers-1) {
                        RefreshEndpoint e1, e2;
                        e1.v = refresh_message.endpoints.first.v;
                        e2.v = refresh_message.endpoints.second.v;
                        for (RefreshEndpoint* e : {&e1, &e2}) {
                            e->prev_tier_size = ett.get_size(e->v);
                            SkipListNode<DefaultSketchColumn>* root = ett.get_root(e->v);
                            root->process_updates();
                            DefaultSketchColumn* ett_agg = root->sketch_agg;
                            ett_agg->reset_sample_state();
                            e->sketch_query_result = ett_agg->sample();
                        }
                        RefreshMessage next_refresh_message;
                        next_refresh_message.endpoints = {e1, e2};
                        MPI_Send(&next_refresh_message, sizeof(RefreshMessage), MPI_BYTE, rank+1, 0, MPI_COMM_WORLD);
                    }
                    continue;
                }
                // For every other tier just receive and perform update messages
                if (tier != 0)
                for (int endpoint : {0,1}) {
                    std::ignore = endpoint;
                    // Receive a broadcast to see if the endpoint at the current tier is isolated or not
                    EttUpdateMessage update_message;
                    bcast(&update_message, sizeof(EttUpdateMessage), rank);
                    if (update_message.type == NOT_ISOLATED) continue;
                    // Get the two broadcasts and perform ett updates
                    for (int broadcast : {0,1}) {
                        std::ignore = broadcast;
                        EttUpdateMessage update_message;
                        bcast(&update_message, sizeof(EttUpdateMessage), rank);
                        ett_update_tier(update_message);
                        if (update_message.type == LINK) break;
                    }
                }
            }
            STOP(normal_refresh_time, normal_refresh_timer);
        }
    }
}

void TierNode::ett_update_tier(EttUpdateMessage message) {
    if (message.type == LINK && tier_num >= message.start_tier) {
        ett.link(message.endpoint1, message.endpoint2);
        ENDPOINT_CANARY("Linking ETT With", message.endpoint1, message.endpoint2);
    } else if (message.type == CUT && tier_num >= message.start_tier) {
        ett.cut(message.endpoint1, message.endpoint2);
        ENDPOINT_CANARY("Cutting ETT With", message.endpoint1, message.endpoint2);
    }
}

void TierNode::refresh_tier(RefreshMessage message) {
    for (RefreshEndpoint endpoint: {message.endpoints.first, message.endpoints.second}) {
        // Check if the tree containing this endpoint is isolated
        uint32_t prev_tier_size = endpoint.prev_tier_size;
        uint32_t this_tier_size = ett.get_size(endpoint.v);
        
        node_id_t a = (node_id_t)endpoint.sketch_query_result.idx;
        node_id_t b = (node_id_t)(endpoint.sketch_query_result.idx>>32);
        
        // Tell all other nodes an isolation was found
        EttUpdateMessage update_message;
        update_message.type = (TreeOperationType)(!(prev_tier_size != this_tier_size || endpoint.sketch_query_result.result != GOOD));
        update_message.endpoint1 = a;
        update_message.endpoint2 = b;
        bcast(&update_message, sizeof(EttUpdateMessage), tier_num+1);
				
        if (update_message.type == NOT_ISOLATED)
            continue;
				
        // Query LCT node to check if this new edge forms a cycle
        LctResponseMessage lct_response;
        MPI_Recv(&lct_response, sizeof(LctResponseMessage), MPI_BYTE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // If there is a cycle formed, tell all necessary nodes to delete that edge
        if (lct_response.connected) {
            node_id_t c = (node_id_t)lct_response.cycle_edge;
            node_id_t d = (node_id_t)(lct_response.cycle_edge>>32);

            EttUpdateMessage cut_message;
            cut_message.type = CUT;
            cut_message.endpoint1 = c;
            cut_message.endpoint2 = d;
            cut_message.start_tier = lct_response.weight;
            bcast(&cut_message, sizeof(EttUpdateMessage), tier_num+1);

            if (tier_num >= lct_response.weight) {
                ett.cut(c,d);
                ENDPOINT_CANARY("Cutting ETT With", c, d);
            }
        }

        // Tell all nodes above and including the current tier to add the new edge
        EttUpdateMessage link_message;
        link_message.type = LINK;
        link_message.endpoint1 = a;
        link_message.endpoint2 = b;
        link_message.start_tier = tier_num;
        bcast(&link_message, sizeof(EttUpdateMessage), tier_num+1);
        ett.link(a,b);
        ENDPOINT_CANARY("Linking ETT With", a, b);
    }
}
