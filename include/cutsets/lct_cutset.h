#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "cutsets/ufo_types.h"
#include "sketch_interfacing.h"
#include "types.h"
#include "util.h"


namespace cutset_lct {

using namespace ufo;

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class Node;

template<typename SketchClass = DefaultSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
class CutsetLCT {
public:
    using SketchType = SketchClass;
    using ComponentID = size_t;

    struct ComponentView {
        Node<SketchClass>* representative = nullptr;
                Node<SketchClass>* aggregate_root = nullptr;
        ComponentID component_key = 0;

        ComponentView() = default;
                ComponentView(Node<SketchClass>* representative, Node<SketchClass>* aggregate_root)
            : representative(representative),
                            aggregate_root(aggregate_root),
              component_key(reinterpret_cast<ComponentID>(representative)) {}

        ComponentID key() const {
            return component_key;
        }

        uint32_t size() const {
            return aggregate_root ? aggregate_root->weight : 0u;
        }

        SketchClass& sketch() const {
            return aggregate_root->sketch_agg;
        }
    };

    CutsetLCT(int n, uint64_t seed);
    ~CutsetLCT();
    
    void link(vertex_t u, vertex_t v);
    void cut(vertex_t u, vertex_t v);

    bool is_connected(vertex_t u, vertex_t v);

    ComponentView update_sketch(vertex_t v, vec_t update_idx);
    ComponentView update_sketch(vertex_t v, const ColumnEntryDelta& delta);
    ComponentView update_sketch_atomic(vertex_t v, vec_t update_idx);
    ComponentView update_sketch_atomic(vertex_t v, const ColumnEntryDelta& delta);

    ColumnEntryDelta generate_entry_delta(node_id_t u, vec_t update_idx) {
        return verts[static_cast<size_t>(u)].sketch_agg.generate_entry_delta(update_idx);
    }

    uint32_t get_size(node_id_t u) {
        return component_view(u).size();
    }

    node_id_t get_max_nodes() {
        return static_cast<node_id_t>(verts.size());
    }

    ComponentID component_id(node_id_t u) {
        return component_view(u).key();
    }

    ComponentView component_view(node_id_t u) {
        Node<SketchClass>* node = &verts[static_cast<size_t>(u)];
        Node<SketchClass>* representative = node->get_representative();
        Node<SketchClass>* aggregate_root = node->get_root();
        return ComponentView{representative, aggregate_root};
    }

    bool is_initialized(node_id_t u) {
        return static_cast<size_t>(u) < verts.size();
    }
    void initialize_node(node_id_t) {}
    void uninitialize_node(node_id_t) {}
    void initialize_all_nodes() {}
    void initialize_all_nodes(node_id_t) {}

    size_t space();
    size_t space_usage_bytes() {
        return space();
    }
    bool verify_structure(const std::vector<SketchClass>& base_sketches);
private:
    uint64_t seed;
    std::vector<Node<SketchClass>> verts;
};

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
CutsetLCT<SketchClass>::CutsetLCT(int n, uint64_t seed) : seed(seed), verts(n, seed) {}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
CutsetLCT<SketchClass>::~CutsetLCT() {}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetLCT<SketchClass>::link(vertex_t u, vertex_t v) {
    verts[u].link(&verts[v]);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void CutsetLCT<SketchClass>::cut(vertex_t u, vertex_t v) {
    verts[u].cut(&verts[v]);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetLCT<SketchClass>::is_connected(vertex_t u, vertex_t v) {
    return verts[u].get_representative() == verts[v].get_representative();
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass>::ComponentView
CutsetLCT<SketchClass>::update_sketch(vertex_t v, vec_t update_idx) {
    ColumnEntryDelta delta = verts[v].sketch_agg.generate_entry_delta(update_idx);
    return update_sketch(v, delta);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass>::ComponentView
CutsetLCT<SketchClass>::update_sketch(vertex_t v, const ColumnEntryDelta& delta) {
    Node<SketchClass>* curr = &verts[v];
    while (curr) {
        curr->sketch_agg.apply_entry_delta(delta);
        curr = curr->parent;
    }
    return component_view(static_cast<node_id_t>(v));
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass>::ComponentView
CutsetLCT<SketchClass>::update_sketch_atomic(vertex_t v, vec_t update_idx) {
    return update_sketch(v, update_idx);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
typename CutsetLCT<SketchClass>::ComponentView
CutsetLCT<SketchClass>::update_sketch_atomic(vertex_t v, const ColumnEntryDelta& delta) {
    return update_sketch(v, delta);
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
size_t CutsetLCT<SketchClass>::space() {
    size_t mem = sizeof(CutsetLCT<SketchClass>);
    for (auto& v : verts) {
        mem += v.space();
    }
    return mem;
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool CutsetLCT<SketchClass>::verify_structure(const std::vector<SketchClass>& base_sketches) {
    std::unordered_map<Node<SketchClass>*, std::vector<node_id_t>> components;
    for (node_id_t i = 0; i < verts.size(); ++i) {
        components[verts[i].get_representative()].push_back(i);
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
        SketchClass expected_sketch(SketchClass::suggest_capacity(verts.size()), seed);
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
