#pragma once
#include <algorithm>
#include <cassert>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "cutsets/ufo_types.h"
#include "sketch_interfacing.h"
#include "types.h"
#include "default_containers.h"
#include <absl/container/flat_hash_map.h>
#include "util.h"


namespace cutset_lct {

using namespace ufo;

// LCT_QUERY_MODE is set by CMake:
//   0 = EAGER   — full splay on every query (always correct aggregates)
//   1 = LAZY    — read-only parent walk (no structural mutation)
//   2 = WORST_CASE — read-only with budget, fallback to eager splay
#ifndef LCT_QUERY_MODE
#define LCT_QUERY_MODE 0
#endif

static constexpr int LCT_QUERY_MODE_EAGER      = 0;
static constexpr int LCT_QUERY_MODE_LAZY        = 1;
static constexpr int LCT_QUERY_MODE_WORST_CASE  = 2;

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class Node;

template<typename SketchClass = DefaultSketchColumn,
        //  typename Container = std::vector<Node<SketchClass>>>
typename Container = std::conditional_t<
    use_vector_default_container,
    std::vector<Node<SketchClass>>,
    absl::flat_hash_map<node_id_t, Node<SketchClass>*>>>
requires(SketchColumnConcept<SketchClass, vec_t>)
class CutsetLCT {
public:
    using SketchType = SketchClass;
    using ComponentID = size_t;

    struct ComponentView {
        Node<SketchClass>* node = nullptr;

        ComponentView() = default;

        explicit ComponentView(Node<SketchClass>* node)
            : node(node) {}


        ComponentID key() const {
            assert(node != nullptr);
            if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_LAZY) {
                return reinterpret_cast<ComponentID>(node->get_representative_read_only());
            } else if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_WORST_CASE) {
                bool exceeded = false;
                Node<SketchClass>* rep = node->get_representative_read_only_with_limit(
                    CutsetLCT::query_budget_steps(), exceeded);
                if (exceeded) rep = node->get_representative();
                return reinterpret_cast<ComponentID>(rep);
            } else {
                return reinterpret_cast<ComponentID>(node->get_representative());
            }
        }

        uint32_t size() const {
            assert(node != nullptr);
            if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_LAZY) {
                return node->get_root_read_only()->weight;
            } else if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_WORST_CASE) {
                bool exceeded = false;
                Node<SketchClass>* r = node->get_root_read_only_with_limit(
                    CutsetLCT::query_budget_steps(), exceeded);
                if (exceeded) r = node->get_root();
                return r->weight;
            } else {
                return node->get_root()->weight;
            }
        }

        SketchClass& sketch() const {
            assert(node != nullptr);
            if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_LAZY) {
                return node->get_root_read_only()->sketch_agg;
            } else if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_WORST_CASE) {
                bool exceeded = false;
                Node<SketchClass>* r = node->get_root_read_only_with_limit(
                    CutsetLCT::query_budget_steps(), exceeded);
                if (exceeded) r = node->get_root();
                return r->sketch_agg;
            } else {
                return node->get_root()->sketch_agg;
            }
        }
    };

    CutsetLCT(int n, uint64_t seed);
    CutsetLCT(node_id_t n, uint32_t tier_num, int seed);
    ~CutsetLCT();
    
    void link(vertex_t u, vertex_t v);
    void cut(vertex_t u, vertex_t v);

    bool is_connected(vertex_t u, vertex_t v);

    ComponentView update_sketch(vertex_t v, vec_t update_idx);
    ComponentView update_sketch(vertex_t v, const ColumnEntryDelta& delta);
    ComponentView update_sketch_atomic(vertex_t v, vec_t update_idx);
    ComponentView update_sketch_atomic(vertex_t v, const ColumnEntryDelta& delta);
    std::pair<ComponentView, ComponentView> update_sketches(node_id_t u, node_id_t v, vec_t update_idx);

    ColumnEntryDelta generate_entry_delta(node_id_t u, vec_t update_idx) {
        ensure_initialized(u);
        return lct_node(u).sketch_agg.generate_entry_delta(update_idx);
    }

    uint32_t get_size(node_id_t u) {
        ensure_initialized(u);
        return component_view(u).size();
    }

    node_id_t get_max_nodes() {
        return max_num_nodes;
    }

    uint32_t num_components() {
        std::unordered_set<Node<SketchClass>*> reps;
        if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            for (node_id_t i = 0; i < max_num_nodes; ++i) {
                reps.insert(lct_node(i).get_representative());
            }
        } else {
            for (const auto& [_, node] : verts) {
                reps.insert(node->get_representative());
            }
        }
        return static_cast<uint32_t>(reps.size());
    }

    ComponentID component_id(node_id_t u) {
        ensure_initialized(u);
        return component_view(u).key();
    }

    static size_t query_budget_steps() {
        if (sketch_len <= 1) return 0;
        // approx 8 * log(num vertices)
        return 4 * (std::bit_width(static_cast<size_t>(sketch_len)) - 1);
    }

    Node<SketchClass>* representative_for_query(Node<SketchClass>* node) {
        if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_EAGER) {
            return node->get_representative();
        } else if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_LAZY) {
            return node->get_representative_read_only();
        } else {
            bool exceeded_budget = false;
            Node<SketchClass>* rep = node->get_representative_read_only_with_limit(query_budget_steps(), exceeded_budget);
            if (exceeded_budget) {
                return node->get_representative();
            }
            return rep;
        }
    }

    ComponentView component_view(node_id_t u) {
        ensure_initialized(u);
        return ComponentView{&lct_node(u)};
    }

    bool is_connected_read_only(vertex_t u, vertex_t v) const {
        assert(in_bounds(u) && in_bounds(v));
        if constexpr (!std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            auto it_u = verts.find(static_cast<node_id_t>(u));
            auto it_v = verts.find(static_cast<node_id_t>(v));
            if (it_u == verts.end() || it_v == verts.end()) return false;
            return it_u->second->get_representative_read_only() == it_v->second->get_representative_read_only();
        } else {
            // For vector container we need const access
            Node<SketchClass>* nu = const_cast<Node<SketchClass>*>(&lct_node_const(u));
            Node<SketchClass>* nv = const_cast<Node<SketchClass>*>(&lct_node_const(v));
            return nu->get_representative_read_only() == nv->get_representative_read_only();
        }
    }

    bool is_initialized(node_id_t u) {
        if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            return u < verts.size();
        } else {
            return verts.find(u) != verts.end();
        }
    }
    void initialize_node(node_id_t u) {
        if constexpr (!std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            if (verts.find(u) == verts.end()) {
                verts[u] = new Node<SketchClass>(seed);
            }
        }
    }
    void uninitialize_node(node_id_t u) {
        if constexpr (!std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            assert(verts.find(u) != verts.end());
            delete verts[u];
            verts.erase(u);
        }
    }
    void initialize_all_nodes() {
        for (node_id_t i = 0; i < max_num_nodes; ++i) {
            initialize_node(i);
        }
    }
    void initialize_all_nodes(node_id_t until) {
        for (node_id_t i = 0; i < until; ++i) {
            initialize_node(i);
        }
    }
    
    Node<SketchClass>& lct_node(node_id_t u) {
        if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            assert(u < verts.size());
            return verts[u];
        } else {
            assert(verts.find(u) != verts.end());
            return *verts[u];
        }
    }

    const Node<SketchClass>& lct_node_const(node_id_t u) const {
        if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            assert(u < verts.size());
            return verts[u];
        } else {
            auto it = verts.find(u);
            assert(it != verts.end());
            return *it->second;
        }
    }

    uint32_t get_size_read_only(node_id_t u) const {
        assert(in_bounds(u));
        return lct_node_const(u).get_root_read_only()->weight;
    }

    const SketchClass& get_sketch_read_only(node_id_t u) const {
        assert(in_bounds(u));
        return lct_node_const(u).get_root_read_only()->sketch_agg;
    }

    size_t space();
    size_t space_usage_bytes() {
        return space();
    }
    bool verify_structure();
    bool verify_structure(const std::vector<SketchClass>& base_sketches);
private:
    bool in_bounds(vertex_t v) const {
        return v >= 0 && static_cast<node_id_t>(v) < max_num_nodes;
    }

    void ensure_initialized(node_id_t u) {
        assert(u < max_num_nodes);
        if constexpr (!std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
            if (!is_initialized(u)) initialize_node(u);
        }
    }

    node_id_t max_num_nodes;
    uint64_t seed;
    Container verts;
};

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
CutsetLCT<SketchClass, Container>::CutsetLCT(int n, uint64_t seed) : max_num_nodes(n), seed(seed) {
    if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
        verts.reserve(n);
        for (int i = 0; i < n; ++i) verts.emplace_back(seed);
    }
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
CutsetLCT<SketchClass, Container>::CutsetLCT(node_id_t n, uint32_t tier_num, int seed)
    : CutsetLCT(static_cast<int>(n), static_cast<uint64_t>(seed)) {
    (void)tier_num;
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
CutsetLCT<SketchClass, Container>::~CutsetLCT() {
    if constexpr (!std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
        for (const auto& [_, node] : verts) {
            delete node;
        }
    }
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetLCT<SketchClass, Container>::link(vertex_t u, vertex_t v) {
    assert(in_bounds(u) && in_bounds(v));
    if constexpr (!std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
        ensure_initialized(static_cast<node_id_t>(u));
        ensure_initialized(static_cast<node_id_t>(v));
    }
    lct_node(u).link(&lct_node(v));
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetLCT<SketchClass, Container>::cut(vertex_t u, vertex_t v) {
    assert(in_bounds(u) && in_bounds(v));
    assert(is_initialized(static_cast<node_id_t>(u)) && is_initialized(static_cast<node_id_t>(v)));
    lct_node(u).cut(&lct_node(v));
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetLCT<SketchClass, Container>::is_connected(vertex_t u, vertex_t v) {
    assert(in_bounds(u) && in_bounds(v));
    if constexpr (!std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
        if (!is_initialized(static_cast<node_id_t>(u)) || !is_initialized(static_cast<node_id_t>(v))) {
            return false;
        }
    }
    if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_LAZY) {
        return is_connected_read_only(u, v);
    }
    return representative_for_query(&lct_node(u)) == representative_for_query(&lct_node(v));
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass, Container>::ComponentView
CutsetLCT<SketchClass, Container>::update_sketch(vertex_t v, vec_t update_idx) {
    assert(in_bounds(v));
    ensure_initialized(static_cast<node_id_t>(v));
    ColumnEntryDelta delta = lct_node(v).sketch_agg.generate_entry_delta(update_idx);
    return update_sketch(v, delta);
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass, Container>::ComponentView
CutsetLCT<SketchClass, Container>::update_sketch(vertex_t v, const ColumnEntryDelta& delta) {
    assert(in_bounds(v));
    ensure_initialized(static_cast<node_id_t>(v));

    Node<SketchClass>* last = nullptr;

    if constexpr (LCT_QUERY_MODE == LCT_QUERY_MODE_WORST_CASE) {
        // Two-pass approach:
        // Pass 1: count path length and prefetch sketches.
        Node<SketchClass>* node = &lct_node(v);
        size_t budget = query_budget_steps();
        size_t path_len = 0;
        Node<SketchClass>* curr = node;
        while (curr) {
            curr->sketch_agg.prefetch();
            ++path_len;
            last = curr;
            curr = curr->parent;
        }

        if (path_len > budget) {
            // Path too long — do an eager splay to restructure, then walk.
            node->get_representative();
            last = nullptr;
        }

        // Pass 2: apply deltas along the (potentially restructured) path.
        curr = node;
        while (curr) {
            curr->sketch_agg.apply_entry_delta(delta);
            if (!last) last = curr;
            curr = curr->parent;
        }
    } else {
        // EAGER and LAZY: simple parent walk.
        // TODO - this is actually incorrect for eager.
        // but idrc since i dont think thats the right way
        Node<SketchClass>* curr = &lct_node(v);
        while (curr) {
            curr->sketch_agg.apply_entry_delta(delta);
            last = curr;
            curr = curr->parent;
        }
    }

    return ComponentView{last};
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass, Container>::ComponentView
CutsetLCT<SketchClass, Container>::update_sketch_atomic(vertex_t v, vec_t update_idx) {
    return update_sketch(v, update_idx);
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass, Container>::ComponentView
CutsetLCT<SketchClass, Container>::update_sketch_atomic(vertex_t v, const ColumnEntryDelta& delta) {
    return update_sketch(v, delta);
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
std::pair<typename CutsetLCT<SketchClass, Container>::ComponentView,
          typename CutsetLCT<SketchClass, Container>::ComponentView>
CutsetLCT<SketchClass, Container>::update_sketches(node_id_t u, node_id_t v, vec_t update_idx) {
    ColumnEntryDelta delta = generate_entry_delta(u, update_idx);
    ComponentView view_u = update_sketch(static_cast<vertex_t>(u), delta);
    ComponentView view_v = update_sketch(static_cast<vertex_t>(v), delta);
    return {view_u, view_v};
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
size_t CutsetLCT<SketchClass, Container>::space() {
    size_t mem = sizeof(CutsetLCT<SketchClass, Container>);
    if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
        mem += sizeof(Node<SketchClass>) * verts.capacity();
        for (node_id_t i = 0; i < max_num_nodes; ++i) {
            mem += lct_node(i).space() - sizeof(Node<SketchClass>);
        }
    } else {
        // Bucket metadata/controls in flat hash map.
        mem += sizeof(std::pair<node_id_t, Node<SketchClass>*>*) * verts.bucket_count();
        for (const auto& [_, node] : verts) {
            mem += node->space();
        }
    }
    return mem;
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetLCT<SketchClass, Container>::verify_structure() {
    std::unordered_map<Node<SketchClass>*, std::vector<node_id_t>> components;
    if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
        for (node_id_t i = 0; i < max_num_nodes; ++i) {
            components[lct_node(i).get_representative()].push_back(i);
        }
    } else {
        for (const auto& [idx, node] : verts) {
            components[node->get_representative()].push_back(idx);
        }
    }
    bool valid = true;
    for (const auto& [root, leaf_indices] : components) {
        if (root->weight != leaf_indices.size()) {
            std::cout << "Size mismatch for root " << root
                      << ": expected " << leaf_indices.size()
                      << ", got " << root->weight << "\n";
            valid = false;
        }
    }
    return valid;
}

template<typename SketchClass, typename Container> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetLCT<SketchClass, Container>::verify_structure(const std::vector<SketchClass>& base_sketches) {
    std::unordered_map<Node<SketchClass>*, std::vector<node_id_t>> components;
    if constexpr (std::is_same_v<Container, std::vector<Node<SketchClass>>>) {
        for (node_id_t i = 0; i < max_num_nodes; ++i) {
            components[lct_node(i).get_representative()].push_back(i);
        }
    } else {
        for (const auto& [idx, node] : verts) {
            components[node->get_representative()].push_back(idx);
        }
    }
    bool valid = true;
    for (const auto& [root, leaf_indices] : components) {
        if (root->weight != leaf_indices.size()) {
            std::cout << "Size mismatch for root " << root
                      << ": expected " << leaf_indices.size()
                      << ", got " << root->weight << "\n";
            std::cout << "Root address: " << root << " Leaf count: " << leaf_indices.size() << "\n";
            valid = false;
        }
        SketchClass expected_sketch(SketchClass::suggest_capacity(max_num_nodes), seed);
        for (node_id_t idx : leaf_indices) {
            expected_sketch.merge(base_sketches[idx]);
        }
        if (root->sketch_agg != expected_sketch) {
             std::cout << "Sketch mismatch for root " << root << std::endl;
             valid = false;
        }
    }
    return valid;
}



template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class Node {
friend class CutsetLCT<SketchClass>;
public:
    Node(uint64_t seed);
    Node* get_root();
    Node* get_representative();
    void link(Node* child);
    void cut(Node* neighbor);
    void evert(); // reroot

    Node* get_root_read_only() {
        Node* curr = this;
        while (curr->parent) curr = curr->parent;
        return curr;
    }
    const Node* get_root_read_only() const {
        const Node* curr = this;
        while (curr->parent) curr = curr->parent;
        return curr;
    }
    Node* get_representative_read_only() {
        // Walk to absolute root of the splay forest
        Node* curr = this;
        while (curr->parent) curr = curr->parent;
        // Walk to leftmost node, tracking cumulative flip without mutating
        bool flipped = false;
        while (true) {
            flipped ^= curr->flip;
            Node* left = flipped ? curr->children[1] : curr->children[0];
            if (!left) break;
            curr = left;
        }
        return curr;
    }
    Node* get_representative_read_only_with_limit(size_t max_steps, bool& exceeded_budget) {
        // TODO - test whether this is actually worth doing. methinks maybe not..
        // Walk to absolute root of the splay forest
        Node* curr = this;
        size_t steps = 0;
        while (curr->parent) {
            if (steps >= max_steps) {
                exceeded_budget = true;
                return nullptr;
            }
            curr = curr->parent;
            ++steps;
        }
        // Walk to leftmost node, tracking cumulative flip without mutating
        bool flipped = false;
        while (true) {
            flipped ^= curr->flip;
            Node* left = flipped ? curr->children[1] : curr->children[0];
            if (!left) break;
            if (steps >= max_steps) {
                exceeded_budget = true;
                return nullptr;
            }
            curr = left;
            ++steps;
        }
        exceeded_budget = false;
        return curr;
    }
    Node* get_root_read_only_with_limit(size_t max_steps, bool& exceeded_budget) {
        Node* curr = this;
        size_t steps = 0;
        while (curr->parent) {
            if (steps >= max_steps) {
                exceeded_budget = true;
                return curr;
            }
            curr = curr->parent;
            ++steps;
        }
        exceeded_budget = false;
        return curr;
    }
 
    size_t space() {
        size_t mem = sizeof(Node<SketchClass>);
        mem += sketch_agg.space_usage_bytes();
        mem -= sizeof(SketchClass);
        return mem;
    }
private:
    Node* parent; // parent
    Node* children[2]; // children
    uint32_t weight; // lct weight is 1 + sum(npchild.size)
    bool flip; // whether children are reversed; used for evert()
    SketchClass sketch_agg;
    
    Node* get_real_parent();
    void rotate_up();
    void splay();
    void expose();
    void push_flip();
};

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
Node<SketchClass>::Node(uint64_t seed) : parent(nullptr), children{nullptr, nullptr}, weight(1), flip(false),
    sketch_agg(SketchClass::suggest_capacity(sketch_len), seed) {}
 
template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
Node<SketchClass>* Node<SketchClass>::get_real_parent() {
    return parent != nullptr && this != parent->children[0] && this != parent->children[1] ? nullptr : parent;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::push_flip() {
    if (flip) {
        flip = 0;
        std::swap(children[0], children[1]);
        for (int i = 0; i < 2; i++)
            if (children[i] != nullptr)
                children[i]->flip ^= 1;
    }
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::rotate_up() { // rotate v towards its parent; v must have real parent
    Node* p = get_real_parent();
    // Link v to GP
    this->parent = p->parent;
    if (this->parent)
        for (int i = 0; i < 2; i++)
            if (this->parent->children[i] == p)
                this->parent->children[i] = this;
    // Link P with v's other child
    bool dir = this == p->children[0];
    p->children[!dir] = this->children[dir];
    if (this->children[dir]) children[dir]->parent = p;
    // Link v with P
    this->children[dir] = p;
    p->parent = this;
    // Update aggregates
    if (p->children[!dir]) {
        this->weight -= p->children[!dir]->weight;
        this->sketch_agg.merge(p->children[!dir]->sketch_agg);
    }
    p->weight -= this->weight;
    p->sketch_agg.merge(this->sketch_agg);
    this->weight += p->weight;
    this->sketch_agg.merge(p->sketch_agg);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::splay() {
    Node* p, * gp;
    this->push_flip(); // guarantee flip bit isn't set after calling splay()
    while ((p = get_real_parent()) != nullptr) {
        gp = p->get_real_parent();
        if (gp != nullptr) gp->push_flip();
        p->push_flip();
        this->push_flip();
        if (gp != nullptr)
            ((gp->children[0] == p) == (p->children[0] == this) ? p : this)->rotate_up();
        this->rotate_up();
    }
}

// Guarantees that `this` is the root of the splay tree, and the flip bit is false.
template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::expose() {
    Node* curr = this;
    while (curr != nullptr) {
        curr->splay();
        if (curr != this) {
            curr->children[1] = this;
            this->splay();
        } else curr->children[1] = nullptr;
        curr = this->parent;
    }
}

// Guarantees that `this` is the root of the splay tree, and the flip bit is false.
template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::evert() {
    this->expose();
    this->flip ^= 1;
    this->push_flip();
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
Node<SketchClass>* Node<SketchClass>::get_root() {
    this->expose();
    return this;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
Node<SketchClass>* Node<SketchClass>::get_representative() {
    this->expose();
    Node* curr = this;
    while (curr->children[0] != nullptr) {
        curr = curr->children[0];
        curr->push_flip();
    }
    curr->splay();
    return curr;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::cut(Node* neighbor) {
    neighbor->evert();
    this->evert();
    neighbor->parent = nullptr;
    for (int i = 0; i < 2; i++)
        if (children[i] == neighbor)
            children[i] = nullptr;
    this->weight -= neighbor->weight;
    this->sketch_agg.merge(neighbor->sketch_agg);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::link(Node* child) {
    child->evert();
    this->expose();
    child->parent = this;
    this->children[1] = child;
    this->weight += child->weight;
    this->sketch_agg.merge(child->sketch_agg);
}

}
