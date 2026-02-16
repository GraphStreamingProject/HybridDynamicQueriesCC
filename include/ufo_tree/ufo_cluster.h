#pragma once
#include "ufo_tree/ufo_types.h"
#include "ufo_tree/util.h"
#include "sketch_interfacing.h"
#include <absl/container/flat_hash_set.h>
#include <atomic>
#include <cassert>

/* These constants determines the maximum size of array of nieghbors and
the vector of neighbors for each UFOCluster. Any additional neighbors will
be stored in the hash set for efficiency. Minimum value is 3 for queries
function correctly. */
#define UFO_ARRAY_MAX 3

// #define COLLECT_ROOT_CLUSTER_STATS
#ifdef COLLECT_ROOT_CLUSTER_STATS
    static std::map<int, int> root_clusters_histogram;
#endif


namespace ufo {

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class UFOCluster {
using Cluster = UFOCluster<SketchClass>;
using NeighborSet = absl::flat_hash_set<Cluster*>;
public:
    // Parent pointer
    Cluster* parent = nullptr;
    // Center pointer: points to the "center" child cluster (hub of star-shaped merge).
    // Enables top-down traversal: from center, all other children are center->neighbors
    // filtered by parent == this. nullptr for leaf clusters.
    // QDM: this is bad because of high fanout clusters
    Cluster* center = nullptr;
    /* We tag the last neighbor pointer in the array with information about the degree of the cluster.
    If it is 1, 2, or 3, that is the degree of the cluster. If it is 4, then the cluster has degree 4
    or higher and the last neighbor pointer is actually a pointer to the NeighborsSet object containing
    the remaining neighbors of the cluster. */
    
    // lowkey can just do this on the up for CAS recomputing
    Cluster* neighbors[UFO_ARRAY_MAX];
    // do we need degree and fanout?
    uint32_t degree = 0;
    uint32_t fanout = 0;

    // --- Sketch aggregation support ---
    SketchClass sketch_agg;             // aggregate of component subtree
    uint32_t size = 0;                  // component size
    int8_t needs_update = 0;            // CAS coordination flag (last for packing)

    // Constructors
    UFOCluster(uint64_t seed) : parent(nullptr), center(nullptr), neighbors(), degree(0), fanout(0), sketch_agg(SketchClass::suggest_capacity(sketch_len), seed) {
        for(int i=0; i<UFO_ARRAY_MAX; ++i) neighbors[i] = nullptr;
    };
    // Helper functions
    Cluster* get_root();
    bool contracts();
    int get_degree();
    bool has_neighbor_set();
    NeighborSet* get_neighbor_set();
    bool parent_high_fanout();
    bool contains_neighbor(Cluster* c);
    void insert_neighbor(Cluster* c);
    void remove_neighbor(Cluster* c);
    size_t calculate_size();

    // --- CAS-based aggregate coordination (mirrors SkipListNode pattern) ---
    Cluster* find_root_with_cas();
    void recompute_aggs_topdown(int fork_levels);
    void clear_cas_flags();
    // Flush buffered updates (no-op for UFO tree, kept for interface compat)
    void process_updates() {};

    // --- BatchTiers ETT-compatibility methods ---
    // For UFO, the cluster itself IS the handle.
    Cluster* get_allowed_caller() { return this; }

    // Apply delta atomically up to `level` parent levels, return the last node touched.
    Cluster* update_sketch_atomic_to_level(const ColumnEntryDelta &delta, uint32_t level) {
        Cluster* current = this;
        current->sketch_agg.atomic_apply_entry_delta(delta);
        for (uint32_t i = 0; i < level && current->parent != nullptr; ++i) {
            current = current->parent;
            current->sketch_agg.atomic_apply_entry_delta(delta);
        }
        return current;
    }
};

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
UFOCluster<SketchClass>* UFOCluster<SketchClass>::get_root() {
    Cluster* curr = this;
    while (curr->parent) curr = curr->parent;
    return curr;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool UFOCluster<SketchClass>::contracts() {
    assert(get_degree() <= UFO_ARRAY_MAX);
    for (auto neighborp : neighbors) {
        auto neighbor = UNTAG(neighborp);
        if (neighbor && neighbor->parent == parent) return true;
    }
    return false;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
int UFOCluster<SketchClass>::get_degree() {
    int tag = GET_TAG(neighbors[UFO_ARRAY_MAX-1]);
    if (tag <= 3) [[likely]] return tag;
    return 2 + get_neighbor_set()->size();
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool UFOCluster<SketchClass>::has_neighbor_set() {
    int tag = GET_TAG(neighbors[UFO_ARRAY_MAX-1]);
    if (tag <= 3) [[likely]] return false;
    return true;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
absl::flat_hash_set<UFOCluster<SketchClass>*>* UFOCluster<SketchClass>::get_neighbor_set() {
    return (NeighborSet*) UNTAG(neighbors[UFO_ARRAY_MAX-1]);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool UFOCluster<SketchClass>::parent_high_fanout() {
    assert(parent);
    int parent_degree = parent->get_degree();
    if (get_degree() == 1) {
        auto neighbor = neighbors[0];
        if (neighbor->parent == parent)
        if (neighbor->get_degree() - parent_degree > 2) return true;
    } else {
        if (get_degree() - parent_degree > 2) return true;
    }
    return false;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool UFOCluster<SketchClass>::contains_neighbor(Cluster* c) {
    for (auto neighbor : neighbors) if (UNTAG(neighbor) == c) return true;
    if (has_neighbor_set() && get_neighbor_set()->find(c) != get_neighbor_set()->end()) return true;
    return false;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void UFOCluster<SketchClass>::insert_neighbor(Cluster* c) {
    assert(!contains_neighbor(c));
    for (int i = 0; i < UFO_ARRAY_MAX; ++i) {
        if (UNTAG(neighbors[i]) == nullptr) [[likely]] {
            int deg = GET_TAG(neighbors[UFO_ARRAY_MAX-1]);
            neighbors[i] = c;
            neighbors[UFO_ARRAY_MAX - 1] = TAG(UNTAG(neighbors[UFO_ARRAY_MAX - 1]), deg + 1);
            return;
        }
    }
    if (!has_neighbor_set()) {
        auto neighbor_set = new NeighborSet();
        neighbor_set->insert(UNTAG(neighbors[UFO_ARRAY_MAX-1]));
        neighbors[UFO_ARRAY_MAX-1] = TAG(neighbor_set, 4);
    }
    get_neighbor_set()->insert(c);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void UFOCluster<SketchClass>::remove_neighbor(Cluster* c) {
    assert(contains_neighbor(c));
    for (int i = 0; i < UFO_ARRAY_MAX; ++i) {
        if (UNTAG(neighbors[i]) == c) {
            neighbors[i] = TAG(nullptr, GET_TAG(neighbors[i]));
            if (has_neighbor_set()) [[unlikely]] { // Put an element from the set into the array
                auto neighbor_set = get_neighbor_set();
                auto replacement = *neighbor_set->begin();
                neighbors[i] = replacement;
                neighbor_set->erase(replacement);
                if (neighbor_set->size() == 1) {
                    auto temp = *neighbor_set->begin();
                    delete neighbor_set;
                    neighbors[UFO_ARRAY_MAX-1] = TAG(temp, 3);
                }
            } else [[likely]] {
                for (int j = UFO_ARRAY_MAX-1; j > i; --j) {
                    if (UNTAG(neighbors[j])) [[unlikely]] {
                        neighbors[i] = UNTAG(neighbors[j]);
                        neighbors[j] = TAG(nullptr, GET_TAG(neighbors[j]));
                        break;
                    }
                }
                neighbors[UFO_ARRAY_MAX-1] = TAG(UNTAG(neighbors[UFO_ARRAY_MAX-1]), GET_TAG(neighbors[UFO_ARRAY_MAX-1])-1);
            }
            return;
        }
    }
    auto neighbor_set = get_neighbor_set();
    neighbor_set->erase(c);
    if (neighbor_set->size() == 1) {
        auto temp = *neighbor_set->begin();
        delete neighbor_set;
        neighbors[UFO_ARRAY_MAX-1] = TAG(temp, 3);
    }
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
size_t UFOCluster<SketchClass>::calculate_size() {
    size_t memory = sizeof(UFOCluster<SketchClass>);
    if (has_neighbor_set()) memory += get_neighbor_set()->bucket_count() * sizeof(Cluster*);
    return memory;
}

// --- CAS-based aggregate coordination ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
UFOCluster<SketchClass>* UFOCluster<SketchClass>::find_root_with_cas() {
    Cluster* current = this;
    while (current->parent != nullptr) {
        current = current->parent;
        std::atomic_ref<int8_t> atomic_needs_update(current->needs_update);
        int8_t expected = 0; // NORMAL
        bool cas_succeed = atomic_needs_update.compare_exchange_strong(
            expected,
            1, // NEEDS_UPDATE
            std::memory_order_seq_cst
        );
        if (!cas_succeed) {
            return nullptr;
        }
    }
    return current;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void UFOCluster<SketchClass>::recompute_aggs_topdown(int /*fork_levels*/) {
    this->needs_update = 0;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void UFOCluster<SketchClass>::clear_cas_flags() {
    this->needs_update = 0;
}

}
