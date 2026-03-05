#pragma once
#include <algorithm>
#include "cutsets/ufo_types.h"
#include "sketch_interfacing.h"


namespace cutset_lct {

using namespace ufo;

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class Node;

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class CutsetLCT {
public:
    CutsetLCT(int n, uint64_t seed);
    ~CutsetLCT();
    
    void link(vertex_t u, vertex_t v);
    void cut(vertex_t u, vertex_t v);
    bool is_connected(vertex_t u, vertex_t v);

    bool verify_structure();
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
bool CutsetLCT<SketchClass>::verify_structure() {
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
            expected_sketch.merge(verts[idx].sketch_agg);
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
Node<SketchClass>::Node(uint64_t seed) : parent(nullptr), children(nullptr, nullptr), weight(1), flip(false),
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
    if (p->children[!dir]) this->weight -= p->children[!dir]->weight;
    p->weight -= this->weight;
    this->weight += p->weight;
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
}

template<typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void Node<SketchClass>::link(Node* child) {
    child->evert();
    this->expose();
    child->parent = this;
    this->children[1] = child;
    this->weight += child->weight;
}

}
