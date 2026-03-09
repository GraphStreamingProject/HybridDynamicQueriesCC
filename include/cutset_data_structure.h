#pragma once

#include <concepts>
#include <cstdint>
#include <utility>
#include "types.h"
#include "util.h"
#include "sketch_interfacing.h"

// Views are intentionally lightweight and can be refreshed via component_view(v).
// note that the view's sketch and size CAN and WILL be invalidated by future updates/queries, so users should not
// hold onto them (e.g. after any sort of transformation happens to this component).
// However, tree->component_view(view.key()) should always return a valid view of the same component, 
// even if the original view's sketch/size are stale.
template <typename View, typename Sketch, typename ComponentID>
concept TreeComponentViewConcept = requires(View view) {
    { view.key() } -> std::convertible_to<ComponentID>;
    { view.size() } -> std::convertible_to<uint32_t>;
    { view.sketch() } -> std::same_as<Sketch&>;
};

// Concept for the Connectivity Tree Strategy
template <typename T, typename Sketch>
concept CutsetDataStructure = requires(T t, node_id_t u, node_id_t v, Sketch& s, const Sketch& cs, vec_t update_idx, ColumnEntryDelta delta) {
    typename T::SketchType; // Must expose SketchType
    typename T::ComponentID;
    typename T::ComponentView;

    // Basic Connectivity
    { t.link(u, v) } -> std::same_as<void>;
    { t.cut(u, v) } -> std::same_as<void>;
    { t.is_connected(u, v) } -> std::same_as<bool>;

    // Sketch Updates
    // Standardized update interface
    // expectation: you call this, and the ROOT aggregate for this level of the cutset data structure
    // (and everything along the path to the root)
    // will also be updated
    { t.update_sketch(u, update_idx) } -> std::same_as<typename T::ComponentView>;
    { t.generate_entry_delta(u, update_idx) } -> std::same_as<ColumnEntryDelta>;
    { t.update_sketch_atomic(u, delta) } -> std::same_as<typename T::ComponentView>;
    { t.update_sketch(u, delta) } -> std::same_as<typename T::ComponentView>;
    { t.update_sketches(u, v, update_idx) } -> std::same_as<std::pair<typename T::ComponentView, typename T::ComponentView>>;
    
    // Querying
    { t.get_size(u) } -> std::same_as<uint32_t>;
    { t.get_max_nodes() } -> std::same_as<node_id_t>;
    { t.component_id(u) } -> std::convertible_to<typename T::ComponentID>;
    { t.component_view(u) } -> std::same_as<typename T::ComponentView>;
    
    // Maintenance
    { t.is_initialized(u) } -> std::same_as<bool>;
    { t.initialize_node(u) } -> std::same_as<void>;
    { t.uninitialize_node(u) } -> std::same_as<void>;
    { t.initialize_all_nodes() } -> std::same_as<void>;
    { t.space_usage_bytes() } -> std::convertible_to<size_t>;

    // Component view operations
    requires TreeComponentViewConcept<typename T::ComponentView, Sketch, typename T::ComponentID>;
};
