#pragma once
#include "types.h"
#include "util.h"
#include <vector>
#include <atomic>
#include <parlay/sequence.h>
#include <parlay/primitives.h>
// #include <folly/AtomicHashArray.h>
// #include <folly/concurrency/ConcurrentHashMap.h>
#include "parlay_hash/unordered_map.h"

#include "euler_tour_tree.h"
// #include "link_cut_tree.h"
#include "lct_v2.h"
#include "cutset_data_structure.h"
#include "cutsets/ufo_cutset.h"
#include "union_find_local.h"
#include "sketchless_euler_tour_tree.h"
// #include "parlay_hash/unordered_set.h"

// Optional capability: only trees exposing these low-level methods can use the
// CAS sketch-aggregation fast path in BatchTiers.
template <typename TreeStrategy>
concept SupportsCasSketchAggFastPath = requires(TreeStrategy& tree, node_id_t u, const ColumnEntryDelta& delta) {
    { tree.ett_node(u).get_allowed_caller()->update_sketch_atomic_to_level(delta, 1u) };
    { tree.ett_node(u).get_allowed_caller()->find_root_with_cas() };
    { tree.ett_node(u).get_allowed_caller()->clear_cas_flags() };
};

// template <typename SketchClass = DefaultSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
class BatchTiers {
        using SketchClass = typename TreeStrategy::SketchType;
    using ComponentID = typename TreeStrategy::ComponentID;
    using ComponentView = typename TreeStrategy::ComponentView;
    private:
        size_t num_nodes;
        uint64_t seed;
        // size_t maximum_batch_size = 512;
        // size_t maximum_batch_size = 100;
        // size_t maximum_batch_size = 1 << 20;
        size_t maximum_batch_size = 1 << 20;
        // size_t maximum_batch_size = 200;
        // size_t maximum_batch_size = 1 << 14;
        // size_t maximum_batch_size = 1024;
        size_t granularity = 1 << 11;  // suggested number of tier-updates per thread 
        std::vector<TreeStrategy> ett;  // one ETT for each tier
        LinkCutTreeMaxAgg<int8_t> link_cut_tree;
        SketchlessEulerTourTree<> query_ett;
        long tree_ops_count = 0;
        std::mutex lct_and_query_ett_lock;
        parlay::sequence<int32_t> _unique_update_ids;
        
        std::vector<GraphUpdate> transaction_log;

        // TODO - add the sketchless ETT for querying 
        // 

        // "root" nodes for each candidate component at each tier.
        union_find_local<int32_t> _component_reps_dsu;
                
        // static thread_local parlay::sequence<ColumnEntryDelta> _deltas_buffer;
        // static thread_local SketchClass _scratch_sketch;
        // matrix of [num_tiers x ( batch_size * 2 )]
        std::vector<parlay::sequence<ComponentView>> _root_nodes;
        
        // jagged array: track isolated components/probably isolated components. 
        // why are we doing this instead of just using root_nodes?
        
        // a vector mapping each tier to the set of its components that need
        // to be checked for isolation
        // parlay::sequence<parlay::sequence<ComponentView>> _updated_components;
        // TODO - see if we can get rid of redundant checks
        // and only do one PER component. ie if some components share the same
        // root, we need not check them.
        // parlay::sequence<parlay::sequence<ComponentView>> _updated_components;
        parlay::sequence<parlay::sequence<node_id_t>> _updated_components;
        
        // tracks components that were already checked for isolation and had their
        // associated link/cut instructions logged.
        // parlay::sequence<ComponentView> _current_isolated_components;
        
        // key: a root node ptr (to identify same component at current tier)
        // folly::ConcurrentHashMap<size_t, node_id_t> _already_checked_components;
        parlay::parlay_unordered_map_direct<size_t, node_id_t> _already_checked_components;
        
        
        // links to "broadcast" to all higher tiers
        parlay::sequence<Edge> _pending_links;
        // cuts to "broadcast" to all higher tiers, plus the index of the first tier
        // where the cut should be made
        parlay::sequence<std::pair<Edge, uint32_t>> _pending_cuts;
        
        parlay::sequence<GraphUpdate> update_buffer;
        
        
        
    public:
        BatchTiers(node_id_t num_nodes, uint64_t seed);
        BatchTiers(node_id_t num_nodes, uint32_t num_tiers, int batch_size, size_t seed);
        ~BatchTiers();
        
        bool is_initialized(node_id_t u) {
            // no-op with vector implementation
            return ett[0].is_initialized(u);
        };
        
        void initialize_node(node_id_t u) {
            for (auto &tree: ett) {
                tree.initialize_node(u);
            }
            query_ett.initialize_node(u);
            link_cut_tree.initialize_node(u);
        }

        void uninitialize_node(node_id_t u) {
            for (auto &tree: ett) {
                tree.uninitialize_node(u);
            }
            query_ett.uninitialize_node(u);
            link_cut_tree.uninitialize_node(u);
        }
        
        void initialize_all_nodes() {
            // TODO - parallel_for?
            for (auto &tree: ett) {
                tree.initialize_all_nodes(num_nodes);
            }
            query_ett.initialize_all_nodes(num_nodes);
            link_cut_tree.initialize_all_nodes(num_nodes);
        }
        
        void flush_transaction_log() {
            transaction_log.clear();
        }

        void drain_transaction_log(std::vector<GraphUpdate>& out) {
            if (!transaction_log.empty()) {
                out.insert(out.end(), transaction_log.begin(), transaction_log.end());
                transaction_log.clear();
            }
        }

        const std::vector<GraphUpdate>& get_transaction_log() const {
            return transaction_log;
        }
        
        void process_all_updates() {
            if (update_buffer.size() > 0) {
                update_batch(update_buffer);
                update_buffer.clear();
            }
        }

        void update_batch(const parlay::sequence<GraphUpdate> &updates);
        
        bool is_tree_edge(node_id_t a, node_id_t b) {
            return query_ett.has_edge(a, b);
        }
        
        void update(const GraphUpdate &update) {
            // if (!is_initialized(update.edge.src) || !is_initialized(update.edge.dst)) {
            //     std::cout << "ruh oh" << std::endl;
            // }
            assert(this->is_initialized(update.edge.src));
            assert(this->is_initialized(update.edge.dst));
            // add to buffer:
            update_buffer.push_back(update);
            // bool is_tree_edge_deletion = (update.type == DELETE &&
            //                               is_tree_edge(update.edge.src, update.edge.dst));
            // if (update_buffer.size() >= maximum_batch_size || is_tree_edge_deletion) {
            if (update_buffer.size() >= maximum_batch_size) {
                // std::cout << "is_tree_edge_deletion: " << is_tree_edge_deletion << ", buffer size: " << update_buffer.size() << std::endl;
                // process the batch
                update_batch(update_buffer);
                // clear the buffer
                update_buffer.clear();
            }
        }
        
        void flush_buffer() {
            if (update_buffer.size() > 0) {
                update_batch(update_buffer);
                update_buffer.clear();
            }
        }
        size_t space_usage_bytes() {
            size_t total = sizeof(BatchTiers<TreeStrategy>);
            for (auto &tree: ett) {
                total += tree.space_usage_bytes();
            }
            // total += query_ett.space_usage_bytes();
            // total += link_cut_tree.space_usage_bytes();
            // total += _component_reps_dsu.space_usage_bytes();
            // total += _already_checked_components.max_size() * (sizeof(size_t) + sizeof(node_id_t) + sizeof(void*)); // rough estimate
            // total += ((parlay::unordered_map_internal) _already_checked_components).size();
            // TODO - measure the overhead of the actual batch_ttiers class`
            return total;
        }

        bool is_connected(node_id_t a, node_id_t b);

        // query for the connected components of the graph
        std::vector<std::set<node_id_t>> get_cc();
        
        // return the number of tiers
        size_t num_tiers() const {
            return ett.size();
        }

        // Verify the structural integrity of every cutset tier.
        // Returns true if all tiers pass verification.
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

        // find the index of the highest everywhere-maximal tier.
        
        
    private:
        ComponentView& root_node(size_t tier, size_t update_idx, bool src_or_dst) {
            return _root_nodes[tier][update_idx * 2 + (src_or_dst ? 0 : 1)];
        };
        void _process_sketch_aggs_only(const parlay::sequence<GraphUpdate> &updates);
        
        
        // same thing but seperates by tiers. this avoids the needs for atomics.
        void _process_sketch_aggs_tier_sequential(const parlay::sequence<GraphUpdate> &updates);
        
        // same thing but use our CAS tricks
        void _process_sketch_aggs_with_cas(const parlay::sequence<GraphUpdate> &updates);

        uint32_t _search_for_isolated_components(const parlay::sequence<GraphUpdate> &updates);
        
        bool _fix_isolations_at_tier(const parlay::sequence<GraphUpdate> &updates, uint32_t tier_idx);
};

