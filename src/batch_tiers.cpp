#include "../include/batch_tiers.h"
#include "util.h"
#include <random>
#include <atomic>
#include <tbb/tbb.h>

// // #define CANARY(X) do {if (update.edge.src == 1784 && update.edge.dst == 4420) { std::cout << __FILE__ << ":" << __LINE__ << " says " << X << std::endl;}} while (false)
// #define CANARY(X) ;
// // #define ENDPOINT_CANARY(X, src, dst) do {if ((src == 7781 || dst == 7781)) {std::cout << __FILE__ << ":" << __LINE__ << " says " << X << " " << src << " " << dst << std::endl;}} while (false)
// #define ENDPOINT_CANARY(X, src, dst) ;

// long lct_time = 0;
// long ett_time = 0;
// long ett_find_root = 0;
// long ett_get_agg = 0;
// long sketch_query = 0;
// long sketch_time = 0;
// long refresh_time = 0;
// long parallel_isolated_check = 0;
// long tiers_grown = 0;
// long normal_refreshes = 0;


// template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
// bool Batch<SketchClass>::is_connected(node_id_t a, node_id_t b) {
// 	return this->link_cut_tree.find_root(a) == this->link_cut_tree.find_root(b);
// }

// template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
// thread_local parlay::sequence<ColumnEntryDelta> BatchTiers<SketchClass>::_deltas_buffer = parlay::sequence<ColumnEntryDelta>();

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
BatchTiers<TreeStrategy>::BatchTiers(node_id_t num_nodes, uint64_t seed) : num_nodes(num_nodes), seed(seed), link_cut_tree(num_nodes), query_ett(num_nodes, 0, seed) , _already_checked_components(2048, true), _unique_update_ids(2048), _component_reps_dsu(0) {
    // TODO - use the batch_size parameter?
    _component_reps_dsu = union_find_local<int32_t>(maximum_batch_size * 2);
	// Algorithm parameters
	uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
	// Initialize all the ETTs
	std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    // int seed = dist(rng);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
	dist(rng); // To give 1:1 correspondence with MPI seeds
	// Reserve capacity to prevent reallocation (which would invalidate internal pointers)
	ett.reserve(num_tiers);
	for (uint32_t i = 0; i < num_tiers; i++) {
		int tier_seed = dist(rng);
		ett.emplace_back(num_nodes, i, tier_seed);
	}

	// Initialize the root nodes matrix
	_root_nodes.resize(num_tiers);
	for (auto& tier_roots : _root_nodes) {
		tier_roots.resize(maximum_batch_size * 2);
	}
    // and _updated_components
    _updated_components.resize(num_tiers);
    // {
    //     auto tmp = parlay::parlay_unordered_map_direct<size_t, node_id_t>(2 * maximum_batch_size, true);
    //     std::swap(this->_already_checked_components, tmp);
    // }
    // {
    //     auto tmp2 = parlay::parlay_unordered_map_direct<int32_t, std::monostate>(2 * maximum_batch_size, true);
    //     std::swap(this->_unique_update_ids, tmp2);
    // }
}

template <typename TreeStrategy>
    requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
BatchTiers<TreeStrategy>::BatchTiers(
    node_id_t num_nodes, uint32_t num_tiers, int batch_size, size_t seed) : num_nodes(num_nodes), seed(seed), link_cut_tree(num_nodes), maximum_batch_size(batch_size), query_ett(num_nodes, 0, seed), _already_checked_components(num_nodes, true), _unique_update_ids(2048), _component_reps_dsu(0) {
    // TODO - use the batch_size parameter?
    _component_reps_dsu = union_find_local<int32_t>(maximum_batch_size * 2);

    // Initialize all the ETTs
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    // int seed = dist(rng);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
    dist(rng); // To give 1:1 correspondence with MPI seeds
    // Reserve capacity to prevent reallocation (which would invalidate internal pointers)
    ett.reserve(num_tiers);
    for (uint32_t i = 0; i < num_tiers; i++) {
        int tier_seed = dist(rng);
        ett.emplace_back(num_nodes, i, tier_seed);
    }

    // Initialize the root nodes matrix
    _root_nodes.resize(num_tiers);
    for (auto& tier_roots : _root_nodes) {
        tier_roots.resize(maximum_batch_size * 2);
    }
    // and _updated_components
    _updated_components.resize(num_tiers);
}

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
BatchTiers<TreeStrategy>::~BatchTiers() {};


// TODO - check correctness on doing links/cuts out of order. lowkey it should be fine
// from a correctness pov
template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTiers<TreeStrategy>::update_batch(const parlay::sequence<GraphUpdate> &updates) {

    size_t num_updates = updates.size();
    size_t num_tiers = ett.size();
    assert(num_updates <= maximum_batch_size);
    _already_checked_components.clear();
    // std::cout << "Processing batch of size " << num_updates << " on " << num_tiers << " tiers." << std::endl;
    
    // treat all update endpoints as coming from independent components
    _component_reps_dsu.reset();

    std::vector<int32_t> cut_start_tier(num_updates, -1);
    for (size_t update_idx = 0; update_idx < num_updates; ++update_idx) {
        const auto& update = updates[update_idx];
        if (update.type == DELETE && is_tree_edge(update.edge.src, update.edge.dst)) {
            std::pair<Edge, int8_t> cut_edge_info = link_cut_tree.path_query(update.edge.src, update.edge.dst);
            cut_start_tier[update_idx] = static_cast<int32_t>(cut_edge_info.second);
        }
    }

    // 0) Step 0: Process any necessary tree cut operations on every tier. 
    // we WONT immediately do the sketch updates in this case, and will rely on the next parallel branch for that
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, ett.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                for (size_t update_idx = 0; update_idx < updates.size(); ++update_idx) {
                    const auto& update = updates[update_idx];
                    if (cut_start_tier[update_idx] >= 0 && i >= static_cast<size_t>(cut_start_tier[update_idx])) {
                        ett[i].cut(update.edge.src, update.edge.dst);
                    }
                }
            }
        },
        tbb::static_partitioner{}
    );
    // note: can just put this in the above region or use pardo
    // and process on the LCT:
    for (size_t update_idx = 0; update_idx < updates.size(); ++update_idx) {
        const auto& update = updates[update_idx];
        if (cut_start_tier[update_idx] >= 0) {
            link_cut_tree.cut(update.edge.src, update.edge.dst);
            query_ett.cut(update.edge.src, update.edge.dst);
            transaction_log.push_back(update);
        }
    }
    // 1) Step 1: Process all sketch aggs in true batch parallel.
    // _process_sketch_aggs_only(updates);
    _process_sketch_aggs_tier_sequential(updates);
    // _process_sketch_aggs_with_cas(updates);
    
    // 2) Step 2: Check for isolated components.
    uint32_t first_isolated_tier = _search_for_isolated_components(updates);
    // std::cout << "First isolated tier: " << first_isolated_tier << std::endl;
    if (first_isolated_tier == UINT32_MAX) {
        // no isolated components found, so we can return early
        return;
    }
    // the first isolated tier has had no link/cut modifications to it. so its roots array is a valid
    // check 

    _unique_update_ids.clear();
    _unique_update_ids.resize(num_updates * 2);
    std::atomic<size_t> num_unique_components = 0;
    // construct _unique_update_ids such that it contains just ONE idx for every unique
    // component at the first isolated tier
    parlay::parlay_unordered_map_direct<typename BatchTiers<TreeStrategy>::ComponentID, int32_t> component_to_unique_id(2048, true);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_updates),
        [&](const tbb::blocked_range<size_t>& r) {
            for (size_t update_idx = r.begin(); update_idx != r.end(); ++update_idx) {
                for (bool src_or_dst : {true, false}) {
                    node_id_t vertex = src_or_dst ? updates[update_idx].edge.src : updates[update_idx].edge.dst;
                    auto root = root_node(first_isolated_tier, update_idx, src_or_dst);
                    // assign a unique id to this component if it doesnt have one already
                    //
                    std::optional<int32_t> existing_id = component_to_unique_id.Insert(root.key(), vertex);
                    if (!existing_id.has_value()) {
                        size_t idx = num_unique_components.fetch_add(1);
                        _unique_update_ids[idx] = vertex;
                    }
                }
            }
        });
    _unique_update_ids.resize(num_unique_components.load());

    // 3) proceed tier-serially: 
    // * at the first isolated tier, collect all components that are isolated.
    //  * each isolated component will give a new edge (a,b)
    //  * if a path exists already between a and b in the final tier/LCT, then cut the maximum weight
    //    edge on the path, starting from the tier where it first appears (call it tier M) and going until the final one.
    //    
    //    (NOTE THAT tier M has to have a higher index than the first isolated tier. Because we know that
    //    the first isolated tier has a forest such that the endpoints of (a) and (b) were not connected.
    //    if there were a lower index tier, that wouldve violated the subset invariant.)
    //
    //  * if apath does not exist, then we link the two endpoints in all tiers ABOVE the first isolated tier.
    //    Note that this can cause NEW isolated components to appear in tiers above. 
    //
    //
    //  * once we do this for every isolated component at the first isolated tier, check the next tier
    //    to see if it has any isolated components. If it does, repeat (3) at the next tier.   
    //
    // 
    // SHORT CUTS: we can also tell if a component is maximized by checking for an empty sketch. This
    // means we can avoid doing further isolation checks. 
    // for (uint32_t)
    // TODO - is_empty check optimization
    // return;
    for (uint32_t tier = first_isolated_tier; tier < ett.size()-1; tier++) {
        _updated_components[tier].clear();
    }
    for (uint32_t tier = first_isolated_tier; tier < ett.size()-1; tier++) {
        bool components_maximized = _fix_isolations_at_tier(updates, tier);
        if (components_maximized) {
            // if all components were maximized, we can skip the next tier
            // we know that at this point, there are no isolations at higher tiers.
            // because all potential isolated components must be a union of the modified components
            // found at this tier. so we can just return
            // std::cout << "All components maximized at tier " << tier << ", skipping further checks" << std::endl;
            return;
        }
    }
};

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
std::vector<std::set<node_id_t>> BatchTiers<TreeStrategy>::get_cc() {
    this->flush_buffer();
    return query_ett.cc_query();
}

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
bool BatchTiers<TreeStrategy>::is_connected(node_id_t a, node_id_t b) {
    this->flush_buffer();
    // TODO - use a sketchless ETT
	// return this->link_cut_tree.find_root(a) == this->link_cut_tree.find_root(b);
    return query_ett.is_connected(a, b); 
}

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTiers<TreeStrategy>::_process_sketch_aggs_only(const parlay::sequence<GraphUpdate> &updates) {
    size_t num_updates = updates.size();
    size_t num_tiers = ett.size();
    assert(num_updates <= maximum_batch_size);
    // 1) STEP 1: Speculative non-tree edge update processing
    // (plus cleaning up and doing the updates for the tree edge deletions)
    // in parallel, accross every tier and update,
    // update the ETT aggregates
    // then, reduce to find the maximum 
    // TODO - make sure tree edge deletions arent being processed twice.
    // parlay::parallel_for(0, num_tiers*num_updates, [&](size_t i) {
    //     size_t tier = i / num_updates;
    //     size_t update_idx = i % num_updates;
    //     GraphUpdate update = updates[update_idx];
    //     vec_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
    //     SkipListNode<> *src_parent = ett[tier].update_sketch_atomic(update.edge.src, edge_id);
    //     SkipListNode<> *dst_parent = ett[tier].update_sketch_atomic(update.edge.dst, edge_id);

    //     root_node(tier, update_idx, true) = src_parent;
    //     root_node(tier, update_idx, false) = dst_parent;
    // }, granularity);

    // step 1 memory optimization:
    // enforce greater locality by first doing edges in
    // lower, higher sorted order (only do the srcs)
    // then in higher, lower (invert, then do dsts)
    auto src_sorted_update_idxs = parlay::tabulate(num_updates, [&](size_t i) {
        return i;
    });
    parlay::sort_inplace(src_sorted_update_idxs, [&](size_t i, size_t j) {
        return updates[i].edge.src < updates[j].edge.src;
    });
    auto dst_sorted_update_idxs = parlay::tabulate(num_updates, [&](size_t i) {
        return i;
    });
    parlay::sort_inplace(dst_sorted_update_idxs, [&](size_t i, size_t j) {
        return updates[i].edge.dst < updates[j].edge.dst;
    });

    // bool conservative=true;
    // do src updates:
    // parlay::blocked_for(0, num_updates * num_tiers, granularity, [&](size_t block_idx, size_t start, size_t end) {
        // for (size_t i = start; i < end; i++) {
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_updates * num_tiers, granularity),
        [&](const tbb::blocked_range<size_t> &r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
            size_t tier = i / num_updates;
            size_t update_idx = src_sorted_update_idxs[i % num_updates];
            GraphUpdate update = updates[update_idx];
            vec_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
            ColumnEntryDelta delta = ett[tier].generate_entry_delta(update.edge.src, edge_id);
            root_node(tier, update_idx, true) = ett[tier].update_sketch_atomic(update.edge.src, delta);
        }
    });
    // }, tbb::static_partitioner{});
    // }, conservative);
    // now dst updates:
    // parlay::blocked_for(0, num_updates * num_tiers, granularity, [&](size_t block_idx, size_t start, size_t end) {
        // for (size_t i = start; i < end; i++) {
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_updates * num_tiers, granularity),
        [&](const tbb::blocked_range<size_t> &r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                size_t tier = i / num_updates;
                size_t update_idx = dst_sorted_update_idxs[i % num_updates];
                GraphUpdate update = updates[update_idx];
                vec_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
                ColumnEntryDelta delta = ett[tier].generate_entry_delta(update.edge.dst, edge_id);
                root_node(tier, update_idx, false) = ett[tier].update_sketch_atomic(update.edge.dst, delta);
                // }, conservative);}
            }
        });
    // tbb::static_partitioner{});
    // }, conservative);
}

template <typename TreeStrategy>
    requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTiers<TreeStrategy>::_process_sketch_aggs_with_cas(const parlay::sequence<GraphUpdate>& updates) {
    if constexpr (!SupportsCasSketchAggFastPath<TreeStrategy>) {
        // This path is intentionally strategy-specific (ETT/UFO-like trees).
        // For other trees (e.g. LCT variants), use the generic tier-sequential
        // updater to avoid requiring CAS/root-capture internals.
        _process_sketch_aggs_tier_sequential(updates);
        return;
    } else {
        size_t num_updates = updates.size();
        size_t num_tiers = ett.size();
        assert(num_updates <= maximum_batch_size);
        auto src_sorted_update_idxs = parlay::tabulate(num_updates, [&](size_t i) { return i; });
        parlay::sort_inplace(src_sorted_update_idxs,
                             [&](size_t i, size_t j) { return updates[i].edge.src < updates[j].edge.src; });
        auto dst_sorted_update_idxs = parlay::tabulate(num_updates, [&](size_t i) { return i; });
        parlay::sort_inplace(dst_sorted_update_idxs,
                             [&](size_t i, size_t j) { return updates[i].edge.dst < updates[j].edge.dst; });
        // in src order:
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_updates * num_tiers, granularity),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    size_t tier = i / num_updates;
                    size_t update_idx = src_sorted_update_idxs[i % num_updates];
                    GraphUpdate update = updates[update_idx];
                    const ColumnEntryDelta delta = ett[tier].generate_entry_delta(
                        update.edge.src, concat_pairing_fn(update.edge.src, update.edge.dst));
                    auto src_parent = ett[tier]
                                            .ett_node(update.edge.src)
                                            .update_sketch_atomic_to_level(delta, 1);  // 3 levels up
                    auto root = src_parent->find_root_with_cas();
                    root_node(tier, update_idx, true) = typename BatchTiers<TreeStrategy>::ComponentView{root};
                }
            });
        // in dst order:
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_updates * num_tiers, granularity),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    size_t tier = i / num_updates;
                    size_t update_idx = dst_sorted_update_idxs[i % num_updates];
                    GraphUpdate update = updates[update_idx];

                    const ColumnEntryDelta delta = ett[tier].generate_entry_delta(
                        update.edge.dst, concat_pairing_fn(update.edge.src, update.edge.dst));
                    auto dst_parent = ett[tier]
                                            .ett_node(update.edge.dst)
                                            .update_sketch_atomic_to_level(delta, 1);  // 3 levels up
                    auto root = dst_parent->find_root_with_cas();
                    root_node(tier, update_idx, false) = typename BatchTiers<TreeStrategy>::ComponentView{root};
                }
            });
        // TODO - this is gonna be unperformant, but I'd say worth it for simplicity in testing
        // update root_node matrix
        // in src order:
        tbb::parallel_for(tbb::blocked_range<size_t>(0, num_updates * num_tiers, granularity),
                          [&](const tbb::blocked_range<size_t>& r) {
                              for (size_t i = r.begin(); i != r.end(); ++i) {
                                  size_t tier = i / num_updates;
                                  size_t update_idx = src_sorted_update_idxs[i % num_updates];
                                  GraphUpdate update = updates[update_idx];
                                  if (root_node(tier, update_idx, true).root != nullptr) {
                                      root_node(tier, update_idx, true).root->recompute_aggs_topdown(2);
                                  } else {
                                      root_node(tier, update_idx, true) = ett[tier].component_view(update.edge.src);
                                  }
                              }
                          });
        // in dst order:
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_updates * num_tiers, granularity),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    size_t tier = i / num_updates;
                    size_t update_idx = dst_sorted_update_idxs[i % num_updates];
                    GraphUpdate update = updates[update_idx];
                    if (root_node(tier, update_idx, false).root != nullptr) {
                        root_node(tier, update_idx, false).root->recompute_aggs_topdown(2);
                    } else {
                        root_node(tier, update_idx, false) = ett[tier].component_view(update.edge.dst);
                    }
                }
            });
    }
}

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
void BatchTiers<TreeStrategy>::_process_sketch_aggs_tier_sequential(const parlay::sequence<GraphUpdate> &updates) {
    size_t num_updates = updates.size();
    size_t num_tiers = ett.size();
    assert(num_updates <= maximum_batch_size);
    auto src_sorted_update_idxs = parlay::tabulate(num_updates, [&](size_t i) {
        return i;
    });
    // parlay::sort_inplace(src_sorted_update_idxs, [&](size_t i, size_t j) {
    //     return updates[i].edge.src < updates[j].edge.src;
    // });
    auto dst_sorted_update_idxs = parlay::tabulate(num_updates, [&](size_t i) {
        return i;
    });
    // parlay::sort_inplace(dst_sorted_update_idxs, [&](size_t i, size_t j) {
    //     return updates[i].edge.dst < updates[j].edge.dst;
    // });

    // bool conservative=false;
    // bool conservative=true;
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_tiers, 1),
        [&](const tbb::blocked_range<size_t> &r) {
            for (size_t tier = r.begin(); tier != r.end(); ++tier) {
                for (size_t i = 0; i < num_updates; i++) {
                    size_t update_idx = src_sorted_update_idxs[i];
                    // size_t update_idx = i;
                    GraphUpdate update = updates[update_idx];
                    vec_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
                    // SkipListNode<SketchClass> *src_parent = ett[tier].update_sketch(update.edge.src, edge_id);
                    const ColumnEntryDelta delta = ett[tier].generate_entry_delta(update.edge.src, edge_id);
                    // SkipListNode<SketchClass> *src_parent = ett[tier].update_sketch_atomic(update.edge.src, delta);
                    root_node(tier, update_idx, true) = ett[tier].update_sketch(update.edge.src, delta);
                }
                for (size_t i = 0; i < num_updates; i++) {
                    // root_node(tier, i, true).process_updates();
                }
                for (size_t i = 0; i < num_updates; i++) {
                    size_t update_idx = dst_sorted_update_idxs[i];
                    // size_t update_idx = i;
                    GraphUpdate update = updates[update_idx];
                    vec_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
                    // SkipListNode<SketchClass> *dst_parent = ett[tier].update_sketch(update.edge.dst, edge_id);
                    const ColumnEntryDelta delta = ett[tier].generate_entry_delta(update.edge.dst, edge_id);
                    root_node(tier, update_idx, false) = ett[tier].update_sketch(update.edge.dst, delta);
                }
                for (size_t i = 0; i < num_updates; i++) {
                    // root_node(tier, i, false).process_updates();
                }
            }
        },
        tbb::static_partitioner{}
    );
    // 0, conservative);
    // tbb::parallel_for(
    //     tbb::blocked_range<size_t>(0, num_tiers, 1),
    //     [&](const tbb::blocked_range<size_t> &r) {
    //         for (size_t tier = r.begin(); tier != r.end(); ++tier) {
    //             // for (size_t tier = 0; tier < num_tiers; tier++) {
    //             // source loop:
    //             parlay::sequence<ColumnEntryDelta> _deltas_buffer;
    //             size_t i = 0;
    //             while (i < num_updates) {
    //                 _deltas_buffer.clear();
    //                 size_t j = i;
    //                 while (j < num_updates && updates[src_sorted_update_idxs[j]].edge.src == updates[src_sorted_update_idxs[i]].edge.src) {
    //                     GraphUpdate update = updates[src_sorted_update_idxs[j]];
    //                     vec_t edge_id = concat_pairing_fn(
    //                         update.edge.src,
    //                         update.edge.dst);
    //                     auto delta = ett[tier].generate_entry_delta(
    //                         update.edge.src,
    //                         edge_id);
    //                     _deltas_buffer.push_back(delta);

    //                     j++;
    //                 }
    //                 SkipListNode<SketchClass> *src_parent = this->ett[tier].update_sketch(
    //                     updates[src_sorted_update_idxs[i]].edge.src,
    //                     _deltas_buffer.head(_deltas_buffer.size()));
    //                 for (size_t k = i; k < j; k++) {
    //                     size_t update_idx = src_sorted_update_idxs[k];
    //                     root_node(tier, update_idx, true) = src_parent;
    //                 }
    //                 i = j;
    //             }
    //             // dest loop:
    //             i = 0;
    //             while (i < num_updates) {
    //                 _deltas_buffer.clear();
    //                 size_t j = i;
    //                 while (j < num_updates && updates[dst_sorted_update_idxs[j]].edge.dst == updates[dst_sorted_update_idxs[i]].edge.dst) {
    //                     GraphUpdate update = updates[dst_sorted_update_idxs[j]];
    //                     vec_t edge_id = concat_pairing_fn(
    //                         update.edge.src,
    //                         update.edge.dst);
    //                     auto delta = ett[tier].generate_entry_delta(
    //                         update.edge.dst,
    //                         edge_id);
    //                     _deltas_buffer.push_back(delta);
    //                     j++;
    //                 }
    //                 SkipListNode<SketchClass> *dst_parent = this->ett[tier].update_sketch(
    //                     updates[dst_sorted_update_idxs[i]].edge.dst,
    //                     _deltas_buffer.head(_deltas_buffer.size()));
    //                 for (size_t k = i; k < j; k++) {
    //                     size_t update_idx = dst_sorted_update_idxs[k];
    //                     root_node(tier, update_idx, false) = dst_parent;
    //                 }
    //                 i = j;
    //             }
    //             parlay::parallel_for(0, num_updates, [&](size_t k) {
    //                 root_node(tier, k, true)->process_updates();
    //                 root_node(tier, k, false)->process_updates();
    //             });
    //             // for (size_t k = 0; k < num_updates; k++) {
    //             //     root_node(tier, k, true)->process_updates();
    //             //     root_node(tier, k, false)->process_updates();
    //             // }
    //         }
    //     },
    //     tbb::static_partitioner{}
    // ); 
}

template <typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
uint32_t BatchTiers<TreeStrategy>::_search_for_isolated_components(const parlay::sequence<GraphUpdate> &updates) {
    size_t num_updates = updates.size();
    size_t num_tiers = ett.size();
    assert(num_updates <= maximum_batch_size);
    // we can use parlay::find, as long as we are using "tier-major" order
    auto isolation_tabulate = parlay::delayed_tabulate(
        (num_tiers - 1) * num_updates,
        [&](size_t i) {
            size_t tier = i / num_updates;
            size_t update_idx = i % num_updates;
            for (bool src_or_dst : {true, false}) {
                auto root = root_node(tier, update_idx, src_or_dst);
                auto next_root = root_node(tier + 1, update_idx, src_or_dst);
                uint32_t tier_size = root.size();
                uint32_t next_size = next_root.size();
                if (tier_size == next_size) {
                    // This means that the component is isolated
                    if (root.sketch().sample().result == GOOD) {
                        // this means that the component is isolated
                        // std::cout << "isolation found at tier " << tier << " for update idx " << update_idx << std::endl;
                        return true;
                    }
                }
            }
            return false;
        });
    auto first_isolated_iter = parlay::find(isolation_tabulate, true);
    if (first_isolated_iter == isolation_tabulate.end()) {
        // no isolated components!
        return UINT32_MAX;
    }
    uint32_t first_isolated_idx = first_isolated_iter - isolation_tabulate.begin();
    // note - i dont think we care about the isolation idx
    uint32_t first_isolated_tier = first_isolated_idx / num_updates;
    return first_isolated_tier;
}

template<typename TreeStrategy>
requires(CutsetDataStructure<TreeStrategy, typename TreeStrategy::SketchType>)
bool BatchTiers<TreeStrategy>::_fix_isolations_at_tier(const parlay::sequence<GraphUpdate> &updates, uint32_t tier_idx) {
    (void)updates;
    // size_t num_tiers = ett.size();

    // needs to be atomically updated.
    bool components_maximized = true;
    for (size_t i= 0 ; i < _unique_update_ids.size(); i++) {
        node_id_t vertex = _unique_update_ids[i];
        _updated_components[tier_idx].push_back(vertex);
    }
    // for each update, we only need to grab ROOTS
    // for (size_t i=0; i < num_updates * 2; i++) {
    // for (size_t i = 0; i < num_updates * 2; i++) {
        // // only if you are STILL a root.
        // // AND your sketch is non-empty
        // likely_if (!_component_reps_dsu.is_root(i)) {
        //     // return;
        //     continue;
        // }
        // bool src_or_dst = static_cast<bool>(i % 2);
        // size_t update_idx = i / 2;
        // _updated_components[tier].push_back(
        //     src_or_dst ? updates[update_idx].edge.src : updates[update_idx].edge.dst);
    // };
    // now, _updated_components contains all components that need to be
    // including ones that may have been inherited from doing links/cuts below.
    // for (size_t i = 0; i < _updated_components[tier].size(); i++) {
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, _updated_components[tier_idx].size()),
        [&](const tbb::blocked_range<size_t>& r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                node_id_t vertex_in_component = _updated_components[tier_idx][i];
                // TODO - we can do some work to avoid checking the same component (maybe?)
                // in case a component was previously merged already
                // SkipListNode<SketchClass>* component_root = ett[tier].get_root(vertex_in_component);
                auto component_view = ett[tier_idx].component_view(vertex_in_component);
                auto next_tier_view = ett[tier_idx + 1].component_view(vertex_in_component);

                // TODO - this is no longer necessary. because we are using the DSU to keep the smallest
                // possible set of _updated_components settings
                // actually, we'll keep it for now anyway.
                // this is because the current DSU filter is just being used as a simple filter.
                // since we arent doing any changes to it past the first isolated tier.
                // Atomically claim this component root so only one thread processes it.


                // note behavior of parlay-hash insert is it only inserts 
                // if the key doesn't exist. (we would have used Upsert otherwise).

                // so this should stay correct?
                std::optional<node_id_t> previous_tier =
                    _already_checked_components.Insert(static_cast<size_t>(component_view.key()), tier_idx);
                if (previous_tier.has_value()) {
                    continue;
                }
                // component_view.process_updates();
                SketchClass& ett_agg = component_view.sketch();
                // TODO - do we want to sample before? idts. but we can at least
                // do the empty check with a special new primitive
                SketchSample query_result = ett_agg.sample();
                if (query_result.result != ZERO) {
                    if (components_maximized) {
                        // bool f = false;
                        // bool t = true;
                        __sync_bool_compare_and_swap((bool*)&components_maximized, true, false);
                    }
                }
                {
                    if (component_view.size() == next_tier_view.size()) {
                        if (query_result.result == GOOD) {
                            std::lock_guard<std::mutex> guard(this->lct_and_query_ett_lock);
                            // .. and see if a path exists between the endpoints in the LCT
                            edge_id_t edge = query_result.idx;
                            node_id_t a = (node_id_t)edge;
                            node_id_t b = (node_id_t)(edge >> 32);

                            // check if a path exists between the endpoints
                            // auto a_root = link_cut_tree.find_root(a);
                            // auto b_root = link_cut_tree.find_root(b);
                            // TODO - ETT

                            // if it does, then we either need to cut it, or ignore this update

                            // if (a_root == b_root) {
                            if (link_cut_tree.connected(a, b)) {
                                // a path exists, so we need to cut the maximum weight edge
                                // on the path
                                // THIS REALLY CANT BE PARALLELIZED atm
                                std::pair<Edge, int8_t> max_edge = link_cut_tree.path_query(a, b);
                                node_id_t c = max_edge.first.src;
                                node_id_t d = max_edge.first.dst;
                                // node_id_t c = (node_id_t)max_edge.first;
                                // node_id_t d = (node_id_t)(max_edge.first >> 32);
                                uint32_t first_appeared_tier = max_edge.second;
                                // if the first appeared tier is equal to tier+1, then we should check if this
                                // was a link we had just discovered. If so, we neither cut it, not include this link.
                                if (first_appeared_tier == tier_idx + 1) {
                                    // YOU KNOW that these couldnt have been connected in the tier above
                                    // because otherwise the components coulld not have been the same size
                                    // (which is necessary for isolation condition)
                                    //
                                    // so: DO NOTHING
                                } else {
                                    // likewise, if it's a higher tier, definitely perform the cut
                                    _pending_cuts.push_back({{c, d}, first_appeared_tier});
                                    link_cut_tree.cut(c, d);
                                    query_ett.cut(c, d);
                                    transaction_log.push_back({{c, d}, DELETE});

                                    // and push the link we just found
                                    _pending_links.push_back({a, b});
                                    link_cut_tree.link(a, b, tier_idx + 1);
                                    query_ett.link(a, b);
                                    transaction_log.push_back({{a, b}, INSERT});
                                    // and update the dsu
                                }
                            } else {
                                // if there was no competing link between the endpoints in the LCT,
                                // then we just link them.
                                _pending_links.push_back({a, b});
                                link_cut_tree.link(a, b, tier_idx + 1);
                                query_ett.link(a, b);
                                transaction_log.push_back({{a, b}, INSERT});
                            }
                        }
                    }
                }
            }
        });

    // at this point, we know exactly what cuts and links we need to do at higher tiers.
    // for each tier, we'll perform the cuts and links, and then add any entries to _updated_components[tier] that
    // we need to.
    // parlay::parallel_for(tier + 1, ett.size(), [&](size_t t) {
    tbb::parallel_for(
        tbb::blocked_range<size_t>(tier_idx + 1, ett.size(), 1),
        [&](const tbb::blocked_range<size_t> &r) {
            for (size_t t = r.begin(); t != r.end(); ++t) {
                // for (size_t t = tier + 1; t < ett.size(); t
                for (auto &cut : _pending_cuts) {
                    uint32_t edge_appears_at_tier = cut.second;
                    Edge to_cut = cut.first;
                    // do not perform cut if the edge has not yet appeared (duh?)
                    // if (cut.second < t)
                    // OOPS - this was the incorrected order
                    if (t < edge_appears_at_tier)
                        continue;
                    // cut the edge in the current tier
                    ett[t].cut(to_cut.src, to_cut.dst);
                }
                for (const Edge &link : _pending_links) {
                    ett[t].link(link.src, link.dst);
                }
            }
        },
        tbb::static_partitioner{});
    // });

    // at this point, all links and cuts induced have been performed, and we have a log
    // of components that need to be checked for isolation in the next tier.
    _pending_links.clear();
    _pending_cuts.clear();
    _already_checked_components.clear();

    return components_maximized;

}

template class BatchTiers<EulerTourTree<DefaultSketchColumn>>; 
template class BatchTiers<ufo::CutsetUFOTree<>>;