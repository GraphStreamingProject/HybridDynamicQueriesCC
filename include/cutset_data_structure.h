#pragma once

#include <concepts>
#include <cstdint>
#include "types.h"
#include "sketch_interfacing.h"

// Concept for the handle returned by the tree (representing a node or component root)
template <typename Handle, typename Sketch>
concept TreeHandleConcept = requires(Handle h, const Sketch& s) {
    // Basic properties access
    { h->size } -> std::convertible_to<uint32_t>;
    // { h->sketch_agg } -> std::convertible_to<Sketch>; // Direct access if possible, or use method
    
    // Check if the handle (node) has a non-empty sketch
    // { h->sketch_agg.sample() } -> std::same_as<SketchSample>; 

    // Parallel update support (needed for BatchTiers)
    { h->process_updates() } -> std::same_as<void>;
    
    // Potentially needed for specific optimizations in BatchTiers
    // { h->recompute_aggs_topdown(2) } -> std::same_as<void>; 
    // { h->find_root_with_cas() } -> std::convertible_to<Handle>;
    // { h->clear_cas_flags() } -> std::same_as<void>; 
};

// Concept for the Connectivity Tree Strategy
template <typename T, typename Sketch>
concept CutsetDataStructure = requires(T t, node_id_t u, node_id_t v, Sketch& s, const Sketch& cs, vec_t update_idx, ColumnEntryDelta delta) {
    typename T::SketchType; // Must expose SketchType
    typename T::Handle;     // Must expose the Handle type (e.g. SkipListNode*)

    // Basic Connectivity
    { t.link(u, v) } -> std::same_as<void>;
    { t.cut(u, v) } -> std::same_as<void>;
    { t.is_connected(u, v) } -> std::same_as<bool>;
    { t.has_edge(u, v) } -> std::same_as<bool>;

    // Sketch Updates
    // Standardized update interface
    // expectation: you call this, and the ROOT aggregate for this level of the cutset data structure
    // (and everything along the path to the root)
    // will also be updated
    { t.update_sketch(u, update_idx) } -> std::convertible_to<typename T::Handle>;
    { t.generate_entry_delta(u, update_idx) } -> std::same_as<ColumnEntryDelta>;
    { t.update_sketch_atomic(u, delta) } -> std::convertible_to<typename T::Handle>;
    { t.update_sketch(u, delta) } -> std::convertible_to<typename T::Handle>;
    
    // Querying
    { t.get_size(u) } -> std::same_as<uint32_t>;
    { t.get_max_nodes() } -> std::same_as<node_id_t>;
    { t.get_root(u) } -> std::convertible_to<typename T::Handle>;
    { t.get_component_vertices(u) } -> std::convertible_to<std::vector<node_id_t>>;
    
    // Maintenance
    { t.is_initialized(u) } -> std::same_as<bool>;
    { t.initialize_node(u) } -> std::same_as<void>;
    { t.uninitialize_node(u) } -> std::same_as<void>;
    { t.initialize_all_nodes() } -> std::same_as<void>;
    { t.space_usage_bytes() } -> std::convertible_to<size_t>;

    // Handle operations (or the handle itself satisfies requirements)
    requires TreeHandleConcept<typename T::Handle, Sketch>;
};
