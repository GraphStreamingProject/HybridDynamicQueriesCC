#pragma once

#include "hybrid_conn_shared.h"



template <typename SketchAlgoClass = InputNode, typename RecoverySketchType = NodeRecoveryIBLTCascade> requires(DynamicSketchConcept<SketchAlgoClass>)
class SerialConnectivityManager {
    // TODO 
    public:
        // TODO - make this not public
        SketchAlgoClass sketching_algo;
        SCCWN<> cf_algo;
        
        void set_threshold(size_t threshold) {
            // TODO - do this in an aesthetically better way lol.
            DENSE_THRESHOLD = threshold;
        }
        void set_recovery_size(size_t recovery_size) {
            RECOVERY_SIZE_OVERRIDE = recovery_size;
        }
        void set_move_to_sketch(size_t val) {
            MOVE_TO_SKETCH = val;
        }
        node_id_t sketched_node_count() const {
            return this->recovery_sketches.size();
        }
    private:
        // TODO - this aint a great way
        size_t MOVE_TO_SKETCH = 100;
        size_t DENSE_THRESHOLD = 2000;
        size_t RECOVERY_SIZE_OVERRIDE = 0;
        // size_t MOVE_TO_SKETCH = 1000000;
        
        size_t seed;
        node_id_t num_nodes;
        // GraphTiers<DefaultSketchColumn> sketching_algo;
        absl::flat_hash_map<node_id_t, RecoverySketchType*> recovery_sketches;
        
        
        // tracks which of our CF edges are from the sketching algo
        absl::flat_hash_set<edge_id_t> edges_from_sketch;
        
        // tracks how many dense edges are still in the CF
        // generate plot with varying batch size
        // keeping a global buffer is likely sufficient
        // doing vertex-level might make checkpointing harder - think about this
        std::vector<uint16_t> num_pending_dense_edges;
        
        // tracks total amount of edges incident.
        // we WONT rely on the CF to track edges.
        std::vector<uint32_t> num_edges;
        std::vector<uint32_t> num_cf_edges;

        size_t total_num_edges = 0;
        size_t total_sketch_insertions = 0;
        size_t total_sketch_deletions = 0;
        size_t total_direct_sketch_inserts = 0;
        bool pending_connectivity_work = false;
        
        // buffer for when we need to collect all neighbors
        std::vector<node_id_t> _neighbors_buffer;
        
        // TODO - this might be replaced by something internal to modified-cupcake
        // can also just be a vector probably
        absl::flat_hash_set<node_id_t> _is_vertex_sketched;

        
        size_t count_explicit_neighbors(node_id_t vertex) {
            // std::cout << "count_explicit_neighbors for vertex: " << vertex << std::endl;
            localTree *cf_leaf = cf_algo.leaves[vertex];
            size_t count = 0;
            for (auto &level_edges: cf_leaf->vertex->E) {
                count += level_edges.second->size();
            }
            // if (num_cf_edges[vertex] > 1000) {
            // if (count > 1000) {
            //     std::cout << "THIS IS WAY TOO HIGH: " << num_cf_edges[vertex] << std::endl;
            //     std::cout << "  counted as" << count << std::endl;
            //     std::cout << " total degree: " << num_edges[vertex] << std::endl;
            //     std::cout << " num pending dense edges: " << num_pending_dense_edges[vertex] << std::endl;
            //     // std::cout << "count_explicit_neighbors for vertex: " << vertex << " returning cached value: " << num_cf_edges[vertex] << std::endl;
            //     // return cf_algo.leaves[vertex]->getEdgeLevelCount();
            //     // return cf_algo.leaves[vertex]->getEdgeLevelCount();
            // }
            return num_cf_edges[vertex];
            // return count;
        }

        inline void insert_to_sketch(node_id_t u, node_id_t v) {
            node_id_t src = std::min(u, v);
            node_id_t dst = std::max(u, v);
            sketching_algo.update(GraphUpdate{Edge{src, dst}, INSERT});
            auto it_u = recovery_sketches.find(u);
            auto it_v = recovery_sketches.find(v);
            if (it_u == recovery_sketches.end() || it_u->second == nullptr || it_v == recovery_sketches.end() || it_v->second == nullptr) {
                std::cout
                    << "MISSING RECOVERY SKETCH in insert_to_sketch: u=" << u
                    << " v=" << v
                    << " has_u=" << (it_u != recovery_sketches.end() && it_u->second != nullptr)
                    << " has_v=" << (it_v != recovery_sketches.end() && it_v->second != nullptr)
                    << " sketched_u=" << is_vertex_sketched(u)
                    << " sketched_v=" << is_vertex_sketched(v)
                    << std::endl;
                std::abort();
            }
            it_u->second->update(v);
            it_v->second->update(u);
            total_sketch_insertions++;
        }
        inline void delete_from_sketch(node_id_t u, node_id_t v) {
            node_id_t src = std::min(u, v);
            node_id_t dst = std::max(u, v);
            sketching_algo.update(GraphUpdate{Edge{src, dst}, DELETE});
            auto it_u = recovery_sketches.find(u);
            auto it_v = recovery_sketches.find(v);
            if (it_u == recovery_sketches.end() || it_u->second == nullptr || it_v == recovery_sketches.end() || it_v->second == nullptr) {
                std::cout
                    << "MISSING RECOVERY SKETCH in delete_from_sketch: u=" << u
                    << " v=" << v
                    << " has_u=" << (it_u != recovery_sketches.end() && it_u->second != nullptr)
                    << " has_v=" << (it_v != recovery_sketches.end() && it_v->second != nullptr)
                    << " sketched_u=" << is_vertex_sketched(u)
                    << " sketched_v=" << is_vertex_sketched(v)
                    << std::endl;
                std::abort();
            }
            it_u->second->update(v);
            it_v->second->update(u);
            total_sketch_deletions++;
        }
        
        bool is_forest_edge_from_sketch(Edge edge) {
            // TODO - watch out for performance penalty of this.
            // might be a reason to use an alternate scheme
            return edges_from_sketch.find(concat_pairing_fn(edge.src, edge.dst)) != edges_from_sketch.end();
        }
        
        bool is_edge_in_cf(Edge edge) {
            // TODO - watch out for performance penalty of this.
            // might be a reason to use an alternate scheme
            // return cf_algo.leaves[edge.src]->getEdgeLevel(edge.dst) != MAX_LEVEL + 2; 
            // auto ret = cf_algo.leaves[edge.src]->getEdgeLevel(edge.dst) <= MAX_LEVEL;
            return cf_algo.leaves[edge.src]->getEdgeLevel(edge.dst) <= MAX_LEVEL; 
        }

        bool is_vertex_sketched(node_id_t vertex) {
            return _is_vertex_sketched.find(vertex) != _is_vertex_sketched.end();
        }

        size_t recovery_size() const {
            if (RECOVERY_SIZE_OVERRIDE > 0) {
                return RECOVERY_SIZE_OVERRIDE;
            }
            return std::max<size_t>(1, DENSE_THRESHOLD / 8);
        }
        
        void initialize_vertex_sketch(node_id_t vertex) {
            // std::cout << "Initializing sketch for vertex " << vertex << std::endl << " with neighbors count "
                    //   << count_explicit_neighbors(vertex) << std::endl;
            // TODO - is basically a no-op from the perspective of the sketching algo
            if (is_vertex_sketched(vertex)) {
                return;
            }
            sketching_algo.initialize_node(vertex);
            _is_vertex_sketched.insert(vertex);
            // TODO - realistically, we dont need THAT many recovery sketches
            // i think 5 sample should be enough?
            double cleanup_adjustment_factor = 5.0 / (log2(num_nodes));
            // double cleanup_adjustment_factor = 1.0;
            recovery_sketches[vertex] = new RecoverySketchType((size_t)num_nodes, recovery_size(), cleanup_adjustment_factor, (uint64_t)seed, false);
            
            // update your neighbors' dense edge counts
            for (auto &level_edges: cf_algo.leaves[vertex]->vertex->E) {
                for (node_id_t neighbor: *level_edges.second) {
                    if (is_vertex_sketched(neighbor)) {
                        num_pending_dense_edges[neighbor]++;
                    }
                }
            }
            // for (size_t level=0; level < MAX_LEVEL; level++) {
            //     auto edge_set = localTree::getEdgeSet(cf_algo.leaves[vertex], level);
            //     if (edge_set) {
            //         for (node_id_t neighbor: *edge_set) {
            //             if (is_vertex_sketched(neighbor)) {
            //                 num_pending_dense_edges[neighbor]++;
            //             }
            //         }
            //     }
            // }
        }

        void uninitialize_vertex_sketch(node_id_t vertex) {
            // WEIRD CASE - even though this doesnt put the edges into the CF from the sketch,
            // it takes responsibility of updating dense edge counts.
            // WHICH MEANS - it's gonna remove a pending dense edge that was NEVER counted.
            // unless unintiialize is called before flushing
            // std::cout << "Uninitializing sketch for vertex " << vertex << std::endl;
            unlikely_if (!is_vertex_sketched(vertex)) {
                return;
            }
            // std::cout << "RECOVERY UNINITIALIZE for vertex " << vertex << std::endl;
            _is_vertex_sketched.erase(vertex);
            delete recovery_sketches[vertex];
            recovery_sketches.erase(vertex);
            // std::cout << "Uninitialized sketch for vertex " << vertex << std::endl;
            
            //update your neighbors' dense edge counts
            for (auto &level_edges: cf_algo.leaves[vertex]->vertex->E) {
                for (node_id_t neighbor: *level_edges.second) {
                    // note that we do this for EVERY edge in the CF
                    // EXCEPT for the ones that are because of the sketching algo
                    if (!is_forest_edge_from_sketch(Edge{vertex, neighbor})) {
                        num_pending_dense_edges[neighbor]--;
                    }
                }
            }
            // std::cout << "SKETCH UNINITIALIZE for vertex " << vertex << std::endl;
            sketching_algo.uninitialize_node(vertex);
        }
        
        void flush_transaction_log() {
            // std::cout << "Flushing transaction log of size: " << sketching_algo.get_transaction_log().size() << std::endl;
            // TODO - maybe get rid of this line, but rn we need it for correctness potentially:
            // sketching_algo.process_all_updates();
            for (auto &update: sketching_algo.get_transaction_log()) {
                node_id_t u = std::min(update.edge.src, update.edge.dst);
                node_id_t v = std::max(update.edge.src, update.edge.dst);
                edge_id_t edge_id = concat_pairing_fn(u, v);
                if (update.type == DELETE) {
                    // std::cout << "TL: Deleting edge from CF: " << u << " " << v << std::endl;
                    remove_from_cf(u, v);
                    edges_from_sketch.erase(edge_id);
                }
                else {
                    // std::cout << "TL: Inserting edge into CF: " << u << " " << v << std::endl;
                    insert_to_cf(u, v);
                    edges_from_sketch.insert(edge_id);
                }
            }
            sketching_algo.flush_transaction_log();
        }

        inline void apply_connectivity_sync_barrier() {
            sketching_algo.process_all_updates();
            flush_transaction_log();
            pending_connectivity_work = false;
        }

        inline void sync_connectivity_if_needed() {
            if (!pending_connectivity_work) {
                return;
            }
            apply_connectivity_sync_barrier();
        }

    public:
        SerialConnectivityManager(node_id_t num_nodes, uint32_t num_tiers, int batch_size, size_t seed)
            : sketching_algo(num_nodes, num_tiers, batch_size, seed), cf_algo(num_nodes), seed(seed) {
                this->num_nodes = num_nodes;
                num_pending_dense_edges.resize(num_nodes, 0);
                num_cf_edges.resize(num_nodes, 0);
                num_edges.resize(num_nodes, 0);
            }

        ~SerialConnectivityManager() {}
        void flush_edges_to_sketch(node_id_t vertex_to_flush) {
            // 1) find all edges incident to vertex_to_flush AND to a dense edge
            _neighbors_buffer.clear();
            for (auto &level_edges: cf_algo.leaves[vertex_to_flush]->vertex->E) {
                for (node_id_t neighbor: *level_edges.second) {
                    // TODO - double check if this is the right way to do this
                    if (is_vertex_sketched(neighbor) && !is_forest_edge_from_sketch(Edge{vertex_to_flush, neighbor})) {
                        // if the edge is not from the sketching algo, and it's connected to a dense vertex
                        // add it to the buffer and 
                        // and increment the pending dense edge count
                        _neighbors_buffer.push_back(neighbor);
                    }
                }
            }

            // remove duplicates
            // std::sort(_neighbors_buffer.begin(), _neighbors_buffer.end());
            // auto last = std::unique(_neighbors_buffer.begin(), _neighbors_buffer.end());
            // _neighbors_buffer.resize(std::distance(_neighbors_buffer.begin(), last));
            // reason for separate loops: see if improvements can be had from figuring out
            // a bulk insertion strategy
            
            // 2) increment their pending_dense_edge counts (but don't flush them yourself)
            // (since this vertex is about to densify)
            // for (node_id_t neighbor: _neighbors_buffer) {
            //     num_pending_dense_edges[neighbor]++;
            // }
            // remove edges from the cluster forest
            // NO longer doing step 2 since we initialized elsewhere
            for (node_id_t neighbor: _neighbors_buffer) {
                remove_from_cf(vertex_to_flush, neighbor);
            }
            
            // 3) insert them into the sketching algo
            // AND the recovery sketches
            for (node_id_t neighbor: _neighbors_buffer) {
                if (neighbor != vertex_to_flush) {
                    insert_to_sketch(vertex_to_flush, neighbor);
                }
            }
            if (!_neighbors_buffer.empty()) {
                // CF edges were removed and replacement sketch-forest commits are pending.
                pending_connectivity_work = true;
            }            // // TODO - there is still a bug with updating pending dense edges
            // return false;
            // clear pending_num_dense_edges for this vertex
            num_pending_dense_edges[vertex_to_flush] = 0;
            // transaction log applied on read:
            // pending_connectivity_work = true is set. so that makes sure we do what we need to
        }
        
        bool check_and_perform_recovery(node_id_t vertex) {
            /*
                Assumes the vertex is sketched
                Checks if the recovery sketch is sufficiently sparse
                If so, performs a recovery attempt
            */
            // or use the explicit degree because of well-formed stream assumption 
            if (!is_vertex_sketched(vertex)) {
                return false;
            }
            // std::cout << "Checking recovery for vertex " << vertex << std::endl;
            // std::cout << "num edges for vertex " << vertex << " is " << num_edges[vertex] << std::endl;
            likely_if (num_edges[vertex] > MOVE_TO_SKETCH / 4) {
                return false;
            }
            // }
            auto recovery_attempt = recovery_sketches[vertex]->recover(true);
            if (recovery_attempt.result == FAILURE || recovery_attempt.result == PARTIAL_RECOVERY) {
                if (recovery_attempt.result == PARTIAL_RECOVERY) {
                    // TODO - use sketching_algo (GraphTiers/InputNode) to finish off partial recovery
                }
                return false;
            }
            // std::cout << "RECOVERY SUCCEEDED YA HURD" << std::endl;
            // std::cout << "edge count for vertex " << vertex << " is " << num_edges[vertex] << std::endl;
            // std::cout << "edge count in cf for vertex " << vertex << " is " << num_cf_edges[vertex] << std::endl;
            // std::cout << "recovered: " << recovery_attempt.recovered_indices.size() << std::endl;
            // then remove the vertex from neighbors' recovery structures
            for (vec_t &vec: recovery_attempt.recovered_indices) {
                node_id_t other_vertex = (node_id_t)vec;
                auto other_it = recovery_sketches.find(other_vertex);
                if (other_it == recovery_sketches.end() || other_it->second == nullptr) {
                    std::cout
                        << "MISSING RECOVERY SKETCH in check_and_perform_recovery: vertex=" << vertex
                        << " other_vertex=" << other_vertex
                        << " has_other=" << (other_it != recovery_sketches.end() && other_it->second != nullptr)
                        << " sketched_vertex=" << is_vertex_sketched(vertex)
                        << " sketched_other=" << is_vertex_sketched(other_vertex)
                        << std::endl;
                    std::abort();
                }
                other_it->second->update(vertex);
            }
            // and flush the edges out of the sketching algo
            for (vec_t &vec: recovery_attempt.recovered_indices) {
                node_id_t other_vertex = (node_id_t)vec;
                node_id_t src = std::min(vertex, other_vertex);
                node_id_t dst = std::max(vertex, other_vertex);
                sketching_algo.update(GraphUpdate{Edge{src, dst}, DELETE});
                total_sketch_deletions++;
            }
            // before we flush the transaction log - uninitialize
            // this has to happen here by current designs, since we only want to decrement
            // pending_dense_edges for edges that WERE NOT already part of the recovery process
            // std::cout << "Spooky: Uninitializing sketch for vertex " << vertex << std::endl;
            sketching_algo.process_all_updates();
            uninitialize_vertex_sketch(vertex);

            // and apply the transaction log
            flush_transaction_log();
            // and add the edges back to the cluster forest
            // NOTE - WE KNOW THAT none of the edges are already in the cluster forest
            // this is because we applied the transaction log, so any edges in the forest that
            // came for a sketch forest were removed.
            // TODO - it might be worth thinking about this and optimizing
            //     i.e. if we just apply the transaction log, we might delete an edge from the cf,
            //     and then put it right back here later.
            //     NOTE - THERE MIGHT BE DOUBLE-DIPPED EDGES
            //     
            for (vec_t &vec: recovery_attempt.recovered_indices) {
                node_id_t other_vertex = (node_id_t)vec;
                insert_to_cf(vertex, other_vertex);
            }
            return true;
        }

        inline void insert_to_cf(node_id_t src, node_id_t dst) {
            cf_algo.insert(src, dst);
            num_cf_edges[src]++;
            num_cf_edges[dst]++;
        }
        inline void remove_from_cf(node_id_t src, node_id_t dst) {
            cf_algo.remove(src, dst);
            num_cf_edges[src]--;
            num_cf_edges[dst]--;
        }

        void update(GraphUpdate update) {
            // external garauntee: well-formed stream. a remove is only called if the edge exists
            // would be nice to get rid of assumption
            if (update.edge.src == update.edge.dst) {
                // no self-loops
                std::cout << "WARNING: self-loop detected on vertex " << update.edge.src << std::endl;
                return;
            }
            if (update.edge.src > update.edge.dst) {
                std::swap(update.edge.src, update.edge.dst);
            }
            if (update.type == INSERT) {
                num_edges[update.edge.src]++;
                num_edges[update.edge.dst]++;
                total_num_edges++;
                
                // if both endpoints are sketched AND the endpoints are connected in the cf
                // we can shortcut and just insert into the sketching algo
#if defined(ENABLE_DIRECT_SKETCH) && ENABLE_DIRECT_SKETCH
                if (is_vertex_sketched(update.edge.src) && is_vertex_sketched(update.edge.dst)) {
                    if (cf_algo.is_connected(update.edge.src, update.edge.dst)) {
                        // std::cout << "Inserting edge from sketching algo: " <<  update.edge.src << ", "<< update.edge.dst << std::endl;
                        total_direct_sketch_inserts++;
                        insert_to_sketch(update.edge.src, update.edge.dst);
                        return;
                    }
                }
#endif

                insert_to_cf(update.edge.src, update.edge.dst);
                
                // update num_pending_dense_edges to reflect the edge
                // being inserted
                if (is_vertex_sketched(update.edge.src)) {
                    num_pending_dense_edges[update.edge.dst]++;
                }
                if (is_vertex_sketched(update.edge.dst)) {
                    num_pending_dense_edges[update.edge.src]++;
                }
                // i.e. the state of this should be correct BEFORE we
                // potentially initialize the sketches below

                // check to see if we densified the vertices enough to initialize their sketches
                unlikely_if (!is_vertex_sketched(update.edge.src) && num_edges[update.edge.src] >= DENSE_THRESHOLD) {
                    // these functions should be no-ops on dense edges
                    // std::cout << DENSE_THRESHOLD << "-dense threshold reached!" << std::endl;
                    // std::cout << "neighbor count for " << update.edge.src << " is " << count_explicit_neighbors(update.edge.src) << std::endl;
                    initialize_vertex_sketch(update.edge.src);
                    flush_edges_to_sketch(update.edge.src);

                }
                unlikely_if (!is_vertex_sketched(update.edge.dst) && num_edges[update.edge.dst] >= DENSE_THRESHOLD) {
                    // std::cout << DENSE_THRESHOLD << "-dense threshold reached!" << std::endl;
                    // std::cout << "neighbor count for " << update.edge.dst << " is " << count_explicit_neighbors(update.edge.dst) << std::endl;
                    initialize_vertex_sketch(update.edge.dst);
                    flush_edges_to_sketch(update.edge.dst);
                }
                
                // logic for updating pending dense edge counts + potentially flushing out
                // dense edges to the sketching structure
                if (is_vertex_sketched(update.edge.dst)) {
                    if (num_pending_dense_edges[update.edge.dst] >= MOVE_TO_SKETCH) {
                        flush_edges_to_sketch(update.edge.dst);
                    }
                }
                if (is_vertex_sketched(update.edge.src)) {
                    if (num_pending_dense_edges[update.edge.src] >= MOVE_TO_SKETCH) {
                        flush_edges_to_sketch(update.edge.src);
                    }   
                }
            }
            else if (update.type == DELETE) {
                num_edges[update.edge.src]--;
                num_edges[update.edge.dst]--;
                total_num_edges--;

                // TODO - eventually do more precise casework
                // if edge exists in the CF (1):
                //      * a) edge originally comes from the sketch forest: update the sketch algo; apply transaction log
                //      * b) edge originally comes from the CF: remove it from the CF and you're done.
                
                // if not in cluster forest (2):
                // TODO - this logic should check the cf for which edges exist in it
                // if (cf_edges[update.edge.src].find(update.edge.dst) != cf_edges[update.edge.src].end()) {
                // if (cf_algo.has_edge(update.edge.src, update.edge.dst)) {
                if (this->is_edge_in_cf(update.edge)) {
                    edge_id_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
                    // if edge comes from sketching algo:
                    if (edges_from_sketch.find(edge_id) != edges_from_sketch.end()) {
                        // std::cout << "Connectivity edge from sketching algo: " <<  update.edge.src << ", "<< update.edge.dst << std::endl;
                        // case a)
                        // deleting from sketching algo
                        delete_from_sketch(update.edge.src, update.edge.dst);
                        
                        // Flag for sync on query, avoiding an immediate hard barrier here.
                        pending_connectivity_work = true;
                        
                        check_and_perform_recovery(update.edge.src);
                        check_and_perform_recovery(update.edge.dst);
                        // can we do defered work: yes
                        // do we have to: ??? figure out
                    }
                    else {
                        //case b) edge does not come from sketching algo
                        remove_from_cf(update.edge.src, update.edge.dst);

                        // TODO - same logic is needed as above to DECREMENT pending dense edges
                        // in the sparse part, if this were the case.

                        if (is_vertex_sketched(update.edge.dst))
                        {
                            num_pending_dense_edges[update.edge.src]--;
                        }

                        if (is_vertex_sketched(update.edge.src))
                        {
                            num_pending_dense_edges[update.edge.dst]--;
                        }
                    }
                }
                // 2) edge does not exist in the CF:
                //  * it must be in the sketch algo, so update the sketch algo and apply transaction log.
                else {
                    // Sketch-only deletions can be sent directly; backend batching handles deferral.
                    delete_from_sketch(update.edge.src, update.edge.dst);
                }
                // TODO - eventually implement a check to see if we need to remove
                // one of the vertices from the sketch algo and dump the edges out.
            }
        }

        bool connectivity_query(node_id_t a, node_id_t b) {
            sync_connectivity_if_needed();
            return cf_algo.is_connected(a, b);
        }

        void force_sync() {
            apply_connectivity_sync_barrier();
        }
        
        std::vector<std::set<node_id_t>> cc_query() {
            sync_connectivity_if_needed();
            // TODO - this aint great.
            std::vector<std::set<node_id_t>> ret;
            std::unordered_map<uint64_t, std::set<node_id_t>> component_map;
            for (node_id_t i=0; i < num_nodes; i++) {
                localTree *root = localTree::getRoot(cf_algo.leaves[i]);
                uint64_t root_id = (uint64_t) root;
                // std::cout << "root_id " << root_id << " for node " << i << std::endl;
                auto it = component_map.find(root_id);
                if (it == component_map.end()) {
                    component_map[root_id] = std::set<node_id_t>();
                }
                component_map[root_id].insert(i);
            }
            for (auto &pair: component_map) {
                ret.push_back(pair.second);
            }
            return ret;
        }

        size_t num_sketched_vertices() const {
            return _is_vertex_sketched.size();
        }
        size_t total_edges() const {
            return total_num_edges;
        }
        size_t num_sketch_insertions() const {
            return total_sketch_insertions;
        }
        size_t num_sketch_deletions() const {
            return total_sketch_deletions;
        }
        size_t num_sketched_edges() const {
            return (total_sketch_insertions >= total_sketch_deletions)
                ? (total_sketch_insertions - total_sketch_deletions)
                : 0;
        }
        size_t num_direct_sketch_inserts() const {
            return total_direct_sketch_inserts;
        }
        size_t num_direct_sketch_edges() const {
            // Backward-compatible alias; this is cumulative direct sketch inserts.
            return num_direct_sketch_inserts();
        }

        void end() {
            force_sync();
            sketching_algo.end();
        }
        
        size_t get_space_usage_cf() {
            return cf_algo.getMemUsage();
        }
        
        HybridSpaceReport report_space_usage() {
            force_sync();
            HybridSpaceReport report;
            report.cf_space_bytes = get_space_usage_cf();
            report.driver_space_bytes = get_space_usage_driver();
            report.recovery_sketch_space_bytes = space_usage_recovery_sketch();
            report.sketch_forest_report = sketching_algo.report_space_usage();

            report.total_num_edges = total_edges();
            report.num_sketched_vertices = num_sketched_vertices();
            report.num_sketch_insertions = num_sketch_insertions();
            report.num_sketch_deletions = num_sketch_deletions();
            report.num_sketched_edges = num_sketched_edges();
            report.num_direct_sketch_inserts = num_direct_sketch_inserts();
            return report;
        }
        size_t get_space_usage_driver() {
            // get the space usage of the driver itself
            size_t total = sizeof(*this);
            
            total += num_pending_dense_edges.capacity() * sizeof(uint16_t);
            total += num_edges.capacity() * sizeof(uint32_t);
            total += num_cf_edges.capacity() * sizeof(uint32_t);
            
            total += _neighbors_buffer.capacity() * sizeof(node_id_t);
            
            total += _is_vertex_sketched.bucket_count() * sizeof(node_id_t);
            total += edges_from_sketch.bucket_count() * sizeof(edge_id_t);
            

            return total;
        }
        size_t space_usage_conn_sketch() {
            return sketching_algo.space_usage_bytes();
        }
        size_t space_usage_recovery_sketch() {
            size_t total = 0;
            for (auto &pair: recovery_sketches) {
                total += pair.second->space_usage_bytes();
            }
            total += recovery_sketches.bucket_count() * sizeof(std::pair<node_id_t, RecoverySketchType*>);
            return total;
        }

        long get_num_tree_ops() const { return sketching_algo.get_num_tree_ops(); }
};
