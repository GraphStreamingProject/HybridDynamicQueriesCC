#pragma once
#include "ufo_types.h"
#include "ufo_tree/util.h"
#include "ufo_tree/ufo_cluster.h"
#include "sketch_interfacing.h"
// #include "types.h"
#include <absl/container/flat_hash_set.h>
#include <unordered_set>
#include <unordered_map>


namespace ufo {

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class CutsetUFOTree {
using Cluster = UFOCluster<SketchClass>;
public:
    // --- CutsetDataStructure type aliases ---
    using SketchType = SketchClass;
    using Handle = Cluster*;

    // Cutset-mode constructor
    CutsetUFOTree(node_id_t max_num_nodes, uint32_t tier_num, size_t seed);

    // Non-copyable (copy would duplicate buffer and break internal cross-pointers)
    CutsetUFOTree(const CutsetUFOTree&) = delete;
    CutsetUFOTree& operator=(const CutsetUFOTree&) = delete;

    // Default move is safe: vector move transfers the buffer pointer,
    // so all internal Cluster* pointers remain valid.
    CutsetUFOTree(CutsetUFOTree&&) = default;
    CutsetUFOTree& operator=(CutsetUFOTree&&) = default;

    ~CutsetUFOTree();
    void link(vertex_t u, vertex_t v);
    void cut(vertex_t u, vertex_t v);
    bool connected(vertex_t u, vertex_t v);
    // Testing helpers
    size_t space();
    size_t count_nodes();
    size_t get_height();
    bool is_valid();
    void print_tree();
    bool verify_structure();

    // --- CutsetDataStructure concept methods ---

    // Connectivity
    bool is_connected(node_id_t u, node_id_t v) {
        return connected(static_cast<vertex_t>(u), static_cast<vertex_t>(v));
    }
    bool has_edge(node_id_t u, node_id_t v) {
        return leaves[u].contains_neighbor(&leaves[v]);
    }

    // Sketch updates
    Handle update_sketch(node_id_t u, vec_t update_idx);
    Handle update_sketch(node_id_t u, const ColumnEntryDelta &delta);
    Handle update_sketch_atomic(node_id_t u, vec_t update_idx);
    Handle update_sketch_atomic(node_id_t u, const ColumnEntryDelta &delta);
    ColumnEntryDelta generate_entry_delta(node_id_t u, vec_t update) {
        return leaves[u].sketch_agg.generate_entry_delta(update);
    }

    // Querying
    uint32_t get_size(node_id_t u) {
        return get_root(u)->size;
    }
    node_id_t get_max_nodes() {
        return static_cast<node_id_t>(leaves.size());
    }
    Handle get_root(node_id_t u) {
        return leaves[u].get_root();
    }
    std::vector<node_id_t> get_component_vertices(node_id_t u);

    // Maintenance
    bool is_initialized(node_id_t u) {
        return u < leaves.size();
    }
    void initialize_node(node_id_t u) {
        // no-op: leaves are pre-allocated in constructor
    }
    void uninitialize_node(node_id_t u) {
        // no-op for vector-based storage
    }
    void initialize_all_nodes() {
        // no-op: leaves are pre-allocated in constructor
    }
    void initialize_all_nodes(node_id_t until) {
        // no-op: leaves are pre-allocated in constructor
    }
    size_t space_usage_bytes() {
        return space();
    }

    // Direct leaf access (mirrors ETT's ett_node for BatchTiers compatibility)
    Cluster& ett_node(node_id_t u) {
        return leaves[u];
    }

    // Container for BatchTiers compatibility (size query)
    struct {
        size_t sz = 0;
        size_t size() const { return sz; }
    } ett_nodes;

private:
    // Class data and parameters
    std::vector<Cluster> leaves;
    std::vector<std::vector<Cluster*>> root_clusters;
    int max_level;
    std::vector<std::pair<Cluster*,vertex_t>> lower_deg[2]; // lower_deg helps to identify clusters who became low degree during a deletion update
    QueryType query_type;
    uint32_t tier_num_ = 0;
    size_t seed_ = 0;

    // We preallocate UFO clusters and store unused clusters in free_clusters
    std::vector<Cluster*> free_clusters;
    Cluster* allocate_cluster();
    void free_cluster(Cluster* c);
    // Helper functions
    void remove_ancestors(Cluster* c, int start_level = 0);
    void recluster_tree();
    bool is_high_degree_or_high_fanout(Cluster* cluster, Cluster* child, int level);
    void disconnect_siblings(Cluster* c, int level);
    void insert_adjacency(Cluster* u, Cluster* v);
    void remove_adjacency(Cluster* u, Cluster* v);

    // Sketch aggregate recomputation helpers
    void recompute_component_sketch(Cluster* root);
    // Top-down traversal helpers (via center pointers)
    void walk_down_recompute(Cluster* c, Cluster* root);
    void walk_down_vertices(Cluster* c, std::vector<node_id_t>& vertices);
};

// --- Constructor / Destructor ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
CutsetUFOTree<SketchClass>::CutsetUFOTree(node_id_t max_num_nodes, uint32_t tier_num, size_t seed)
    : query_type(CONNECTIVITY), tier_num_(tier_num), seed_(static_cast<size_t>(seed)) {
    leaves.reserve(max_num_nodes);
    for (node_id_t i = 0; i < max_num_nodes; ++i) {
        leaves.emplace_back(seed_);
        leaves.back().size = 1; // Leaves represent 1 vertex
    }
    root_clusters.resize(max_tree_height(max_num_nodes));
    for (int i = 0; i < static_cast<int>(max_num_nodes); ++i)
        free_clusters.push_back(new Cluster(seed_));
    ett_nodes.sz = max_num_nodes;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
CutsetUFOTree<SketchClass>::~CutsetUFOTree() {
    // Clear all memory
    std::unordered_set<Cluster*> clusters;
    for (auto& leaf : leaves) {
        auto curr = leaf.parent;
        while (curr) {
            clusters.insert(curr);
            curr = curr->parent;
        }
    }
    for (auto cluster : clusters) delete cluster;
    for (auto cluster : free_clusters) delete cluster;
    #ifdef COLLECT_ROOT_CLUSTER_STATS
    std::cout << "Number of root clusters: Frequency" << std::endl;
        for (auto entry : root_clusters_histogram)
            std::cout << entry.first << "\t" << entry.second << std::endl;
    #endif
}

// --- Cluster allocation ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
UFOCluster<SketchClass>* CutsetUFOTree<SketchClass>::allocate_cluster() {
    if (!free_clusters.empty()) {
        auto c = free_clusters.back();
        free_clusters.pop_back();
        // c->size is already 0 from free_cluster
        return c;
    }
    auto c = new Cluster(seed_);
    c->size = 0; // Explicitly enforce 0 even if constructor default changes
    return c;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::free_cluster(UFOCluster<SketchClass>* c) {
    c->parent = nullptr;
    c->center = nullptr;
    if (c->has_neighbor_set()) [[unlikely]] delete c->get_neighbor_set();
    for (int i = 0; i < UFO_ARRAY_MAX; ++i)
        c->neighbors[i] = nullptr;
    c->degree = 0;
    c->fanout = 0;
    c->size = 0;
    c->sketch_agg.clear();
    free_clusters.push_back(c);
}

// --- Space / stats helpers ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
size_t CutsetUFOTree<SketchClass>::space() {
    std::unordered_set<Cluster*> visited;
    size_t memory = sizeof(CutsetUFOTree<SketchClass>);
    for (auto& cluster : leaves) {
        memory += cluster.calculate_size();
        auto parent = cluster.parent;
        while (parent != nullptr && visited.count(parent) == 0) {
            memory += parent->calculate_size();
            visited.insert(parent);
            parent = parent->parent;
        }
    }
    return memory;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
size_t CutsetUFOTree<SketchClass>::count_nodes() {
    std::unordered_set<Cluster*> visited;
    size_t node_count = 0;
    for(auto& cluster : leaves){
        node_count += 1;
        auto parent = cluster.parent;
        while(parent != nullptr && visited.count(parent) == 0){
            node_count += 1;
            visited.insert(parent);
            parent = parent->parent;
        }
    }
    return node_count;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
size_t CutsetUFOTree<SketchClass>::get_height() {
    size_t max_height = 0;
    for (vertex_t v = 0; v < leaves.size(); ++v) {
        size_t height = 0;
        Cluster* curr = &leaves[v];
        while (curr) {
            height++;
            curr = curr->parent;
        }
        max_height = std::max(max_height, height);
    }
    return max_height;
}

// --- Link / Cut / Connected ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::link(vertex_t u, vertex_t v) {
    assert(u >= 0 && u < leaves.size() && v >= 0 && v < leaves.size());
    assert(u != v && !connected(u,v));
    max_level = 0;
    remove_ancestors(&leaves[u]);
    remove_ancestors(&leaves[v]);
    insert_adjacency(&leaves[u], &leaves[v]);
    recluster_tree();
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::cut(vertex_t u, vertex_t v) {
    assert(u >= 0 && u < leaves.size() && v >= 0 && v < leaves.size());
    assert(leaves[u].contains_neighbor(&leaves[v]));
    max_level = 0;
    auto curr_u = &leaves[u];
    auto curr_v = &leaves[v];
    while (curr_u != curr_v) {
        lower_deg[0].push_back({curr_u, curr_u->get_degree()-1});
        lower_deg[1].push_back({curr_v, curr_v->get_degree()-1});
        curr_u->degree = curr_u->get_degree()-1;
        curr_v->degree = curr_v->get_degree()-1;
        curr_u = curr_u->parent;
        curr_v = curr_v->parent;
    }
    remove_ancestors(&leaves[u]);
    remove_ancestors(&leaves[v]);
    for (auto cluster: lower_deg[0]) cluster.first->degree = 0;
    for (auto cluster: lower_deg[1]) cluster.first->degree = 0;
    lower_deg[0].clear();
    lower_deg[1].clear();
    remove_adjacency(&leaves[u], &leaves[v]);
    recluster_tree();
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetUFOTree<SketchClass>::connected(vertex_t u, vertex_t v) {
    return leaves[u].get_root() == leaves[v].get_root();
}

// --- Remove ancestors ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::remove_ancestors(Cluster* c, int start_level) {
    int level = start_level; // level is always the level of cluster prev, 0 being the leaves
    auto prev = c;
    auto curr = c->parent;
    bool del = false;
    while (curr) {
        // Different cases for if curr will or will not be deleted later
        if (!is_high_degree_or_high_fanout(curr, prev, level)) [[likely]] { // We will delete curr next round
            disconnect_siblings(prev, level);
            if (del) [[likely]] { // Possibly delete prev
                assert(prev->get_degree() <= UFO_ARRAY_MAX);
                for (auto neighborp : prev->neighbors) {
                    auto neighbor = UNTAG(neighborp);
                    if (neighbor) neighbor->remove_neighbor(prev); // Remove prev from adjacency
                }
                auto position = std::find(root_clusters[level].begin(), root_clusters[level].end(), prev);
                if (position != root_clusters[level].end()) root_clusters[level].erase(position);
                free_cluster(prev);
            } else [[unlikely]] {
                prev->parent = nullptr;
                curr->fanout--;
                root_clusters[level].push_back(prev);
            }
            del = true;
        } else [[unlikely]] { // We will not delete curr next round
            if (del) [[likely]] { // Possibly delete prev
                assert(prev->get_degree() <= UFO_ARRAY_MAX);
                for (auto neighborp : prev->neighbors) {
                    auto neighbor = UNTAG(neighborp);
                    if (neighbor) neighbor->remove_neighbor(prev); // Remove prev from adjacency
                }
                auto position = std::find(root_clusters[level].begin(), root_clusters[level].end(), prev);
                if (position != root_clusters[level].end()) root_clusters[level].erase(position);
                free_cluster(prev);
                curr->fanout--;
            } else [[unlikely]] if (prev->get_degree() <= 1) {
                prev->parent = nullptr;
                curr->fanout--;
                root_clusters[level].push_back(prev);
            }
            del = false;
        }
        // Update pointers
        prev = curr;
        curr = prev->parent;
        level++;
    }
    // DO LAST DELETIONS
    if (del) [[likely]] { // Possibly delete prev
        assert(prev->get_degree() <= UFO_ARRAY_MAX);
        for (auto neighborp : prev->neighbors) {
            auto neighbor = UNTAG(neighborp);
            if (neighbor) neighbor->remove_neighbor(prev); // Remove prev from adjacency
        }
        auto position = std::find(root_clusters[level].begin(), root_clusters[level].end(), prev);
        if (position != root_clusters[level].end()) root_clusters[level].erase(position);
        free_cluster(prev);
    } else [[unlikely]] root_clusters[level].push_back(prev);
    if (level > max_level) max_level = level;
}

// --- Recluster tree ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::recluster_tree() {
    for (int level = 0; level <= max_level; level++) {
        if (root_clusters[level].empty()) [[unlikely]] continue;
        // Update root cluster stats if we are collecting them
        #ifdef COLLECT_ROOT_CLUSTER_STATS
            if (root_clusters_histogram.find(root_clusters[level].size()) == root_clusters_histogram.end())
                root_clusters_histogram[root_clusters[level].size()] = 1;
            else
                root_clusters_histogram[root_clusters[level].size()] += 1;
        #endif
        // Merge deg 3-5 root clusters with all of its deg 1 neighbors
        for (auto cluster : root_clusters[level]) {
            if (!cluster->parent && cluster->get_degree() > 2) [[unlikely]] {
                assert(cluster->get_degree() <= 5);
                auto parent = allocate_cluster();
                parent->size = 0; // Fix: internal node starts empty (allocated as 1)
                parent->fanout = 1;
                cluster->parent = parent;
                parent->center = cluster; // high-degree cluster is the star center
                parent->size += cluster->size; // accumulate child size
                parent->sketch_agg.merge(cluster->sketch_agg);
                root_clusters[level+1].push_back(parent);
                assert(UFO_ARRAY_MAX >= 3);
                if (!cluster->has_neighbor_set()) [[likely]] {
                    for (int i = 0; i < UFO_ARRAY_MAX; ++i) {
                        auto neighbor = UNTAG(cluster->neighbors[i]);
                        if (neighbor->get_degree() == 1) [[unlikely]] {
                            auto curr = neighbor->parent;
                            int lev = level+1;
                            while (curr) {
                                auto temp = curr;
                                curr = curr->parent;
                                auto position = std::find(root_clusters[lev].begin(), root_clusters[lev].end(), temp);
                                if (position != root_clusters[lev].end()) root_clusters[lev].erase(position);
                                free_cluster(temp);
                                lev++;
                            }
                            neighbor->parent = cluster->parent;
                            parent->fanout++;
                        } else if (neighbor->parent) { // Populate new parent's neighbors
                            parent->insert_neighbor(neighbor->parent);
                            neighbor->parent->insert_neighbor(parent);
                        }
                    }
                } else [[unlikely]] {
                    for (int i = 0; i < UFO_ARRAY_MAX-1; ++i) {
                        auto neighbor = cluster->neighbors[i];
                        if (neighbor->get_degree() == 1) [[unlikely]] {
                            auto curr = neighbor->parent;
                            int lev = level+1;
                            while (curr) {
                                auto temp = curr;
                                curr = curr->parent;
                                auto position = std::find(root_clusters[lev].begin(), root_clusters[lev].end(), temp);
                                if (position != root_clusters[lev].end()) root_clusters[lev].erase(position);
                                free_cluster(temp);
                                lev++;
                            }
                            neighbor->parent = cluster->parent;
                            parent->fanout++;
                        } else if (neighbor->parent) { // Populate new parent's neighbors
                            parent->insert_neighbor(neighbor->parent);
                            neighbor->parent->insert_neighbor(parent);
                        }
                    }
                    for (auto neighbor : *cluster->get_neighbor_set()) {
                        if (neighbor->get_degree() == 1) [[unlikely]] {
                            auto curr = neighbor->parent;
                            int lev = level+1;
                            while (curr) {
                                auto temp = curr;
                                curr = curr->parent;
                                auto position = std::find(root_clusters[lev].begin(), root_clusters[lev].end(), temp);
                                if (position != root_clusters[lev].end()) root_clusters[lev].erase(position);
                                free_cluster(temp);
                                lev++;
                            }
                            neighbor->parent = cluster->parent;
                            parent->fanout++;
                            parent->size += neighbor->size; // accumulate absorbed neighbor's size
                            parent->sketch_agg.merge(neighbor->sketch_agg);
                        } else if (neighbor->parent) { // Populate new parent's neighbors
                            parent->insert_neighbor(neighbor->parent);
                            neighbor->parent->insert_neighbor(parent);
                        }
                    }
                }
            }
        }
        // This loop handles all deg 2 and 1 root clusters
        for (auto cluster : root_clusters[level]) {
            // Combine deg 2 root clusters with deg 2 root clusters
            if (!cluster->parent && cluster->get_degree() == 2) [[unlikely]] {
                assert(UFO_ARRAY_MAX >= 2);
                for (int i = 0; i < 2; ++i) {
                    auto neighbor = cluster->neighbors[i];
                    if (!neighbor->parent && (neighbor->get_degree() == 2)) [[unlikely]] {
                        auto parent = allocate_cluster();
                        cluster->parent = parent;
                        neighbor->parent = parent;
                        parent->fanout = 2;
                        parent->center = cluster; // arbitrary choice for pair merge
                        parent->size = cluster->size + neighbor->size; // sum both children
                        parent->sketch_agg.merge(cluster->sketch_agg);
                        parent->sketch_agg.merge(neighbor->sketch_agg);
                        root_clusters[level+1].push_back(parent);
                        for (int i = 0; i < 2; ++i) { // Populate new parent's neighbors
                            if (cluster->neighbors[i]->parent && cluster->neighbors[i]->parent != parent) {
                                parent->insert_neighbor(cluster->neighbors[i]->parent);
                                cluster->neighbors[i]->parent->insert_neighbor(parent);
                            }
                            if (neighbor->neighbors[i]->parent && neighbor->neighbors[i]->parent != parent) {
                                parent->insert_neighbor(neighbor->neighbors[i]->parent);
                                neighbor->neighbors[i]->parent->insert_neighbor(parent);
                            }
                        }
                        break;
                    }
                }
                // Combine deg 2 root clusters with deg 1 or 2 non-root clusters
                if (!cluster->parent) [[unlikely]] {
                    assert(UFO_ARRAY_MAX >= 2);
                    for (int i = 0; i < 2; ++i) {
                        auto neighbor = cluster->neighbors[i];
                        if (neighbor->parent && (neighbor->get_degree() == 1 || neighbor->get_degree() == 2)) [[unlikely]] {
                            if (neighbor->contracts()) continue;
                            cluster->parent = neighbor->parent;
                            neighbor->parent->fanout++;
                            neighbor->parent->center = cluster; // update center to higher-degree node
                            neighbor->parent->size += cluster->size; // accumulate new child's size
                            neighbor->parent->sketch_agg.merge(cluster->sketch_agg);
                            remove_ancestors(cluster->parent, level+1); // Recursive remove ancestor call
                            auto other_neighbor = cluster->neighbors[!i]; // Populate neighbors
                            if (other_neighbor->parent) {
                                insert_adjacency(cluster->parent, other_neighbor->parent);
                            }
                            break;
                        }
                    }
                }
            // Always combine deg 1 root clusters with its neighboring cluster
            } else if (!cluster->parent && cluster->get_degree() == 1) [[unlikely]] {
                auto neighbor = cluster->neighbors[0];
                if (neighbor->parent) {
                    if (neighbor->get_degree() == 2 && neighbor->contracts()) continue;
                    cluster->parent = neighbor->parent;
                    neighbor->parent->fanout++;
                    neighbor->parent->size += cluster->size; // accumulate new child's size
                    neighbor->parent->sketch_agg.merge(cluster->sketch_agg);
                    remove_ancestors(cluster->parent, level+1);
                } else {
                    auto parent = allocate_cluster();
                    cluster->parent = parent;
                    neighbor->parent = parent;
                    parent->fanout = 2;
                    parent->center = neighbor; // neighbor has higher degree
                    parent->size = cluster->size + neighbor->size; // sum both children
                    parent->sketch_agg.merge(cluster->sketch_agg);
                    parent->sketch_agg.merge(neighbor->sketch_agg);
                    for (int i = 0; i < 2; ++i) { // Populate new parent's neighbors
                        if (neighbor->neighbors[i] && neighbor->neighbors[i] != cluster && neighbor->neighbors[i]->parent) {
                            parent->insert_neighbor(neighbor->neighbors[i]->parent);
                            neighbor->neighbors[i]->parent->insert_neighbor(parent);
                        }
                    }
                    root_clusters[level+1].push_back(parent);
                }
            }
        }
        // Add remaining uncombined clusters to the next level
        for (auto cluster : root_clusters[level]) {
            if (!cluster->parent && cluster->get_degree() > 0) [[unlikely]] {
                auto parent = allocate_cluster();
                cluster->parent = parent;
                parent->fanout = 1;
                parent->center = cluster;
                parent->size = cluster->size; // single child
                parent->sketch_agg.merge(cluster->sketch_agg);
                for (int i = 0; i < 2; ++i) { // Populate new parent's neighbors
                    if (cluster->neighbors[i] && cluster->neighbors[i]->parent) {
                        parent->insert_neighbor(cluster->neighbors[i]->parent);
                        cluster->neighbors[i]->parent->insert_neighbor(parent);
                    }
                }
                root_clusters[level+1].push_back(parent);
            }
        }
        // Clear the contents of this level
        root_clusters[level].clear();
        if (level == max_level && !root_clusters[max_level+1].empty()) max_level++;
    }
}

// --- Helper functions ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetUFOTree<SketchClass>::is_high_degree_or_high_fanout(Cluster* cluster, Cluster* child, int level) {
    int cluster_degree = cluster->degree > 0 ? cluster->degree : cluster->get_degree();
    if (cluster_degree > 2) [[unlikely]] return true;
    if (!child->neighbors[1] && cluster->fanout > 2) [[unlikely]] return true;
    int child_degree = child->degree > 0 ? child->degree : child->get_degree();
    if (child_degree - cluster_degree > 2) [[unlikely]] return true;
    return false;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::disconnect_siblings(Cluster* c, int level) {
    if (c->get_degree() == 1) {
        auto center = c->neighbors[0];
        if (center->parent && c->parent != center->parent) return;
        assert(center->get_degree() <= 5);
        if (!center->has_neighbor_set()) [[likely]] {
            for (auto neighborp : center->neighbors) {
                Cluster* neighbor = UNTAG(neighborp);
                if (neighbor && neighbor->parent == c->parent && neighbor != c) {
                    neighbor->parent = nullptr; // Set sibling parent pointer to null
                    root_clusters[level].push_back(neighbor); // Keep track of root clusters
                }
            }
        } else [[unlikely]] {
            for (int i = 0; i < UFO_ARRAY_MAX-1; ++i) {
                Cluster* neighbor = center->neighbors[i];
                if (neighbor->parent == c->parent && neighbor != c) {
                    neighbor->parent = nullptr; // Set sibling parent pointer to null
                    root_clusters[level].push_back(neighbor); // Keep track of root clusters
                }
            }
            for (auto neighbor : *center->get_neighbor_set()) {
                if (neighbor && neighbor->parent == c->parent && neighbor != c) {
                    neighbor->parent = nullptr; // Set sibling parent pointer to null
                    root_clusters[level].push_back(neighbor); // Keep track of root clusters
                }
            }
        }
        center->parent = nullptr;
        root_clusters[level].push_back(center);
    } else {
        assert(c->get_degree() <= 5);
        if (!c->has_neighbor_set()) [[likely]] {
            for (auto neighborp : c->neighbors) {
                Cluster* neighbor = UNTAG(neighborp);
                if (neighbor && neighbor->parent == c->parent) {
                    neighbor->parent = nullptr; // Set sibling parent pointer to null
                    root_clusters[level].push_back(neighbor); // Keep track of root clusters
                }
            }
        } else [[unlikely]] {
            for (int i = 0; i < UFO_ARRAY_MAX-1; ++i) {
                Cluster* neighbor = c->neighbors[i];
                if (neighbor->parent == c->parent) {
                    neighbor->parent = nullptr; // Set sibling parent pointer to null
                    root_clusters[level].push_back(neighbor); // Keep track of root clusters
                }
            }
            for (auto neighbor : *c->get_neighbor_set()) {
                if (neighbor && neighbor->parent == c->parent) {
                    neighbor->parent = nullptr; // Set sibling parent pointer to null
                    root_clusters[level].push_back(neighbor); // Keep track of root clusters
                }
            }
        }
    }
}

// --- Adjacency helpers ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::insert_adjacency(Cluster* u, Cluster* v) {
    auto curr_u = u;
    auto curr_v = v;
    while (curr_u && curr_v && curr_u != curr_v) {
        curr_u->insert_neighbor(curr_v);
        curr_v->insert_neighbor(curr_u);
        curr_u = curr_u->parent;
        curr_v = curr_v->parent;
    }
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::remove_adjacency(Cluster* u, Cluster* v) {
    auto curr_u = u;
    auto curr_v = v;
    while (curr_u && curr_v && curr_u != curr_v) {
        curr_u->remove_neighbor(curr_v);
        curr_v->remove_neighbor(curr_u);
        curr_u = curr_u->parent;
        curr_v = curr_v->parent;
    }
}

// --- Sketch update methods ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetUFOTree<SketchClass>::Handle
CutsetUFOTree<SketchClass>::update_sketch(node_id_t u, vec_t update_idx) {
    ColumnEntryDelta delta = generate_entry_delta(u, update_idx);
    return update_sketch(u, delta);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetUFOTree<SketchClass>::Handle
CutsetUFOTree<SketchClass>::update_sketch(node_id_t u, const ColumnEntryDelta &delta) {
    // Apply delta to leaf, then walk up parent chain merging into each ancestor
    Cluster* current = &leaves[u];
    current->sketch_agg.apply_entry_delta(delta);
    while (current->parent != nullptr) {
        current = current->parent;
        current->sketch_agg.apply_entry_delta(delta);
    }
    return current; // return root
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetUFOTree<SketchClass>::Handle
CutsetUFOTree<SketchClass>::update_sketch_atomic(node_id_t u, vec_t update_idx) {
    ColumnEntryDelta delta = generate_entry_delta(u, update_idx);
    return update_sketch_atomic(u, delta);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetUFOTree<SketchClass>::Handle
CutsetUFOTree<SketchClass>::update_sketch_atomic(node_id_t u, const ColumnEntryDelta &delta) {
    // Apply delta atomically to leaf, then walk up parent chain
    Cluster* current = &leaves[u];
    current->sketch_agg.atomic_apply_entry_delta(delta);
    while (current->parent != nullptr) {
        current = current->parent;
        current->sketch_agg.atomic_apply_entry_delta(delta);
    }
    return current; // return root
}

// --- Component vertex enumeration ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
std::vector<node_id_t> CutsetUFOTree<SketchClass>::get_component_vertices(node_id_t u) {
    Cluster* root = leaves[u].get_root();
    std::vector<node_id_t> vertices;
    // Top-down traversal via center pointers: O(component_size)
    walk_down_vertices(root, vertices);
    return vertices;
}

// --- Sketch aggregate recomputation ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::recompute_component_sketch(Cluster* root) {
    // Top-down traversal via center pointers: O(component_size)
    root->sketch_agg = SketchClass();
    root->size = 0;
    walk_down_recompute(root, root);
}

// --- Top-down traversal helpers ---

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::walk_down_recompute(Cluster* c, Cluster* root) {
    if (c->center == nullptr) { // leaf cluster
        root->sketch_agg.merge(c->sketch_agg);
        root->size++;
        return;
    }
    // Walk center child
    walk_down_recompute(c->center, root);
    // Walk siblings: center's neighbors whose parent is c
    if (!c->center->has_neighbor_set()) [[likely]] {
        for (auto neighborp : c->center->neighbors) {
            auto neighbor = UNTAG(neighborp);
            if (neighbor && neighbor->parent == c) {
                walk_down_recompute(neighbor, root);
            }
        }
    } else [[unlikely]] {
        for (int i = 0; i < UFO_ARRAY_MAX-1; ++i) {
            auto neighbor = c->center->neighbors[i];
            if (neighbor && neighbor->parent == c) {
                walk_down_recompute(neighbor, root);
            }
        }
        auto& extra_neighbors = *(c->center->get_neighbor_set());
        for (auto neighbor : extra_neighbors) {
            if (neighbor && neighbor->parent == c) {
                walk_down_recompute(neighbor, root);
            }
        }
    }
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetUFOTree<SketchClass>::walk_down_vertices(Cluster* c, std::vector<node_id_t>& vertices) {
    if (c->center == nullptr) { // leaf cluster — find its index
        size_t idx = static_cast<size_t>(c - &leaves[0]);
        if (idx < leaves.size()) {
            vertices.push_back(static_cast<node_id_t>(idx));
        }
        return;
    }
    walk_down_vertices(c->center, vertices);
    if (!c->center->has_neighbor_set()) [[likely]] {
        for (auto neighborp : c->center->neighbors) {
            auto neighbor = UNTAG(neighborp);
            if (neighbor && neighbor->parent == c) {
                walk_down_vertices(neighbor, vertices);
            }
        }
    } else [[unlikely]] {
        for (int i = 0; i < UFO_ARRAY_MAX-1; ++i) {
            auto neighbor = c->center->neighbors[i];
            if (neighbor && neighbor->parent == c) {
                walk_down_vertices(neighbor, vertices);
            }
        }
        auto& extra_neighbors = *c->center->get_neighbor_set();
        for (auto neighbor : extra_neighbors) {
            if (neighbor && neighbor->parent == c) {
                walk_down_vertices(neighbor, vertices);
            }
        }
    }
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetUFOTree<SketchClass>::verify_structure() {
    std::unordered_map<Cluster*, std::vector<node_id_t>> components;
    for (node_id_t i = 0; i < leaves.size(); ++i) {
        components[leaves[i].get_root()].push_back(i);
    }

    bool valid = true;
    for (const auto& [root, leaf_indices] : components) {
        if (root->size != leaf_indices.size()) {
            std::cout << "Size mismatch for root " << root
                      << ": expected " << leaf_indices.size()
                      << ", got " << root->size << "\n";
            std::cout << "Root address: " << root << " Leaf count: " << leaf_indices.size() << "\n";
            valid = false;
        }

        SketchClass expected_sketch(SketchClass::suggest_capacity(leaves.size()), seed_);
        for (node_id_t idx : leaf_indices) {
            expected_sketch.merge(leaves[idx].sketch_agg);
        }

        // Assuming SketchClass supports != or similar comparison
        // If not, we might need a workaround, but sketches usually do.
        // For now, let's assume DefaultSketchColumn (FixedSizeSketchColumn) supports equality check
        // If compilation fails, I will inspect sketch interface.
        // Wait, sketch_interfacing.h defines DefaultSketchColumn. 
        // Let's rely on standard operators or add a check if needed.
        // Actually, FixedSizeSketchColumn usually has operator==.
        if (root->sketch_agg != expected_sketch) {
             std::cout << "Sketch mismatch for root " << root << std::endl;
             valid = false;
        }
    }
    return valid;
}

}

using CutsetUFOTree = ufo::CutsetUFOTree<DefaultSketchColumn>;
