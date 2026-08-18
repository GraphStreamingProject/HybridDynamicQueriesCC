#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

#include "types.h"
#include "default_containers.h"
#include "util.h"

#include <absl/container/flat_hash_map.h>


template <typename WeightT>
class NodeMaxLCT {
  public:
    using NodePair = std::pair<NodeMaxLCT<WeightT>*, NodeMaxLCT<WeightT>*>;
    // static_assert(std::is_integral_v<WeightT>, "WeightT must be an integral type");

   NodeMaxLCT(node_id_t node_id);
 
   void link(NodeMaxLCT* child, WeightT weight);
   void cut(NodeMaxLCT* neighbor);
   void evert(); // reroot
   node_id_t get_node_id() const { return node_id; };
   NodeMaxLCT* get_root();
  std::pair<NodePair, WeightT> path_query(NodeMaxLCT<WeightT>* other);
 
  private:

   static constexpr WeightT sentinel() {
     return std::numeric_limits<WeightT>::lowest();
   }

   NodeMaxLCT<WeightT>* par; // parent
   NodeMaxLCT<WeightT>* c[2]; // children
   WeightT w[2]; // store the weights of the up and down preferred edges
   WeightT max; // maintain the maximum edge weight in the splay tree subtree rooted at this
   node_id_t node_id;
   bool head; // whether the node is a head of a path, so don't use value w[0]
   bool flip; // whether children are reversed; used for evert()

   NodeMaxLCT<WeightT>* get_real_par();
   NodeMaxLCT<WeightT>* get_leftmost();
   NodeMaxLCT<WeightT>* get_predecessor();
   NodeMaxLCT<WeightT>* get_successor();
   NodePair get_edge_with_weight(WeightT weight);
   void rot();
   void splay();
   NodeMaxLCT* expose();
   void fix_c();
   void recompute_max();
   void push_flip();

 };


template <
typename WeightT,
typename Container = std::conditional_t<
  use_vector_default_container,
  std::vector<NodeMaxLCT<WeightT>>,
  absl::flat_hash_map<node_id_t, NodeMaxLCT<WeightT>*>>
>
class LinkCutTreeMaxAgg {
 public:
  static_assert(std::is_integral_v<WeightT>, "WeightT must be an integral type");

  explicit LinkCutTreeMaxAgg(int _num_verts);
  LinkCutTreeMaxAgg(node_id_t n) : LinkCutTreeMaxAgg(static_cast<int>(n)) {}
  ~LinkCutTreeMaxAgg();
  
  void link(node_id_t u, node_id_t v, WeightT weight = WeightT{});
  void link(node_id_t u, node_id_t v, std::pair<Edge, WeightT> weight) { link(u, v, weight.second); }
  void cut(node_id_t u, node_id_t v);
  bool connected(node_id_t u, node_id_t v);
  bool has_edge(node_id_t u, node_id_t v);
  std::pair<Edge, WeightT> path_query(node_id_t u, node_id_t v);
  size_t space_usage_bytes() const;
 private:
  Container verts;
  size_t num_verts;
  NodeMaxLCT<WeightT>& vert(node_id_t id) { 
    if constexpr (std::is_same_v<Container, std::vector<NodeMaxLCT<WeightT>>>) {
        assert(id < verts.size());
        return verts[id];
    } else {
        assert(verts.find(id) != verts.end());
        return *verts[id];
    }
  }
  NodeMaxLCT<WeightT>* vert_ptr(node_id_t id) { 
    if constexpr (std::is_same_v<Container, std::vector<NodeMaxLCT<WeightT>>>) {
        assert(id < verts.size());
        return &verts[id];
    } else {
        assert(verts.find(id) != verts.end());
        return verts[id];
    }
  }
public:
  void initialize_node(node_id_t v) {
      // no-op with vector implementation
      if constexpr (!std::is_same_v<Container, std::vector<NodeMaxLCT<WeightT>>>) {
          assert(verts.find(v) == verts.end());
          verts[v] = new NodeMaxLCT<WeightT>(v);
      }
  }
  void uninitialize_node(node_id_t v) {
      // no-op with vector implementation
      if constexpr (!std::is_same_v<Container, std::vector<NodeMaxLCT<WeightT>>>) {
          assert(verts.find(v) != verts.end());
          delete verts[v];
        verts.erase(v);
      }
  }
  void initialize_all_nodes() {
      for (node_id_t i = 0; i < num_verts; ++i) {
          initialize_node(i);
      }
  }
  void initialize_all_nodes(node_id_t until) {
      assert(until <= num_verts);
      for (node_id_t i = 0; i < until; ++i) {
          initialize_node(i);
      }
  }
};
 
template <typename WeightT>
NodeMaxLCT<WeightT>::NodeMaxLCT(node_id_t node_id) : par(nullptr), c{nullptr, nullptr}, w{sentinel(), sentinel()},
     max(sentinel()), node_id(node_id), head(true), flip(false) {}
 
template <typename WeightT>
NodeMaxLCT<WeightT>* NodeMaxLCT<WeightT>::get_real_par() {
   return par != nullptr && this != par->c[0] && this != par->c[1] ? nullptr : par;
 }
 
template <typename WeightT>
NodeMaxLCT<WeightT>* NodeMaxLCT<WeightT>::get_leftmost() {
   NodeMaxLCT* left = this;
   push_flip();
   while (left->c[0] != nullptr) {
     left = left->c[0];
     left->push_flip();
   }
   left->splay();
   return left;
 }
 
template <typename WeightT>
NodeMaxLCT<WeightT>* NodeMaxLCT<WeightT>::get_predecessor() {
   push_flip();
   NodeMaxLCT* curr = c[0];
   curr->push_flip();
   while (curr->c[1] != nullptr) {
     curr = curr->c[1];
     curr->push_flip();
   }
   curr->splay();
   return curr;
 }
 
template <typename WeightT>
NodeMaxLCT<WeightT>* NodeMaxLCT<WeightT>::get_successor() {
   push_flip();
   NodeMaxLCT* curr = c[1];
   curr->push_flip();
   while (curr->c[0] != nullptr) {
     curr = curr->c[0];
     curr->push_flip();
   }
   curr->splay();
   return curr;
 }
 
template <typename WeightT>
typename NodeMaxLCT<WeightT>::NodePair NodeMaxLCT<WeightT>::get_edge_with_weight(WeightT weight) {
   NodeMaxLCT* node = this;
   while (node->w[0] != weight && node->w[1] != weight) {
     for (int i = 0; i < 2; i++)
       if (node->c[i] != nullptr && node->c[i]->max == weight)
         node = node->c[i];
   }
   node->splay();
   if (node->w[0] == weight)
     return {node, node->get_predecessor()};
   return {node, node->get_successor()};
 }
 
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::fix_c() {
   for (int i = 0; i < 2; i++)
     if (c[i] != nullptr)
       c[i]->par = this;
 }
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::recompute_max() {
   max = head ? w[1]: std::max(w[0], w[1]);
   for (int i = 0; i < 2; i++)
     if (c[i] != nullptr)
       max = std::max(max, c[i]->max);
 }
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::push_flip() {
   if (flip) {
     flip = false;
     std::swap(c[0], c[1]);
     std::swap(w[0], w[1]);
     for (int i = 0; i < 2; i++)
       if (c[i] != nullptr)
         c[i]->flip = !c[i]->flip;
   }
 }
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::rot() { // rotate v towards its parent; v must have real parent
   NodeMaxLCT* p = get_real_par();
   par = p->par;
   if (par != nullptr)
     for (int i = 0; i < 2; i++)
       if (par->c[i] == p) {
         par->c[i] = this;
         par->fix_c();
       }
   const bool rot_dir = this == p->c[0];
   p->c[!rot_dir] = c[rot_dir];
   c[rot_dir] = p;
   p->fix_c();
   p->recompute_max();
   fix_c();
   recompute_max();
 }
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::splay() {
   NodeMaxLCT* p, * gp;
   push_flip(); // guarantee flip bit isn't set after calling splay()
   while ((p = get_real_par()) != nullptr) {
     gp = p->get_real_par();
     if (gp != nullptr)
       gp->push_flip();
     p->push_flip();
     push_flip();
     if (gp != nullptr)
       ((gp->c[0] == p) == (p->c[0] == this) ? p : this)->rot();
     rot();
   }
 }
 
// returns the root of the tree
template <typename WeightT>
NodeMaxLCT<WeightT>* NodeMaxLCT<WeightT>::expose() {
   NodeMaxLCT* curr = this;
   NodeMaxLCT* prev = nullptr;
   while (curr) {
     curr->splay();
     NodeMaxLCT* lower = curr->c[1];
     curr->c[1] = prev;
     curr->w[1] = sentinel();
     if (prev) {
       curr->w[1] = prev->w[0];
       prev->head = false;
       prev->recompute_max();
     }
     curr->recompute_max();
     if (lower) {
       NodeMaxLCT* left = lower->get_leftmost();
       left->head = true;
       left->recompute_max();
     }
     prev = curr->get_leftmost();
     curr = prev->par;
   }
   return prev;
 }
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::evert() {
   NodeMaxLCT* head_node = expose();
   head_node->flip = !head_node->flip;
   head_node->push_flip();
 }
 
template <typename WeightT>
NodeMaxLCT<WeightT>* NodeMaxLCT<WeightT>::get_root() {
   return expose();
 }

template <typename WeightT>
auto NodeMaxLCT<WeightT>::path_query(NodeMaxLCT<WeightT>* other) -> std::pair<std::pair<NodeMaxLCT<WeightT>*, NodeMaxLCT<WeightT>*>, WeightT> {
  evert();
  NodeMaxLCT<WeightT>* path_root = other->expose();

  std::pair<std::pair<NodeMaxLCT<WeightT>*, NodeMaxLCT<WeightT>*>, WeightT> max_edge;
  max_edge.second = path_root->max;
  if (max_edge.second == sentinel()) {
    return {{nullptr, nullptr}, sentinel()};
  }
  max_edge.first = path_root->get_edge_with_weight(max_edge.second);
  return max_edge;
 }
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::cut(NodeMaxLCT* neighbor) {
   neighbor->evert();
   evert();
   neighbor->push_flip();
   push_flip();
   neighbor->c[0] = nullptr;
   neighbor->w[0] = sentinel();
   neighbor->head = true;
   neighbor->recompute_max();
   par = nullptr;
   w[1] = sentinel();
   head = true;
   recompute_max();
 }
 
template <typename WeightT>
void NodeMaxLCT<WeightT>::link(NodeMaxLCT* child, WeightT weight) {
   child->evert();
   child->splay();
   child->par = this;
   child->w[0] = weight;
   child->head = false;
   child->recompute_max();
 }
 
template <typename WeightT, typename Container>
LinkCutTreeMaxAgg<WeightT, Container>::LinkCutTreeMaxAgg(int _num_verts) : num_verts(static_cast<size_t>(_num_verts)) {
    if constexpr (std::is_same_v<Container, std::vector<NodeMaxLCT<WeightT>>>) {
        verts.resize(static_cast<size_t>(_num_verts), NodeMaxLCT<WeightT>(0));
        for (node_id_t i = 0; i < num_verts; ++i) {
            verts[static_cast<size_t>(i)] = NodeMaxLCT<WeightT>(i);
        }
    }
}

template <typename WeightT, typename Container>
LinkCutTreeMaxAgg<WeightT, Container>::~LinkCutTreeMaxAgg() {
  if constexpr (std::is_same_v<Container, std::vector<NodeMaxLCT<WeightT>>>) {
      verts.clear();
  }
}

template <typename WeightT, typename Container>
void LinkCutTreeMaxAgg<WeightT, Container>::link(node_id_t u, node_id_t v, WeightT weight) {
  assert(u != v);
  assert(!connected(u, v));
  const auto u_idx = static_cast<size_t>(u);
  const auto v_idx = static_cast<size_t>(v);
  vert(u_idx).link(vert_ptr(v_idx), weight);
}

template <typename WeightT, typename Container>
void LinkCutTreeMaxAgg<WeightT, Container>::cut(node_id_t u, node_id_t v) {
  assert(u != v);
  assert(connected(u, v));
#ifndef NDEBUG
  const auto cut_edge = path_query(u, v);
  assert(cut_edge.second != std::numeric_limits<WeightT>::lowest());
  assert(VERTICES_TO_EDGE(cut_edge.first.src, cut_edge.first.dst) == VERTICES_TO_EDGE(u, v));
#endif
  const auto u_idx = static_cast<size_t>(u);
  const auto v_idx = static_cast<size_t>(v);
  vert(u_idx).cut(vert_ptr(v_idx));
}

template <typename WeightT, typename Container>
bool LinkCutTreeMaxAgg<WeightT, Container>::connected(node_id_t u, node_id_t v) {
  const auto u_idx = static_cast<size_t>(u);
  const auto v_idx = static_cast<size_t>(v);
  return vert(u_idx).get_root() == vert(v_idx).get_root();
}

template <typename WeightT, typename Container>
bool LinkCutTreeMaxAgg<WeightT, Container>::has_edge(node_id_t u, node_id_t v) {
  if (u == v) {
    return false;
  }
  if (!connected(u, v)) {
    return false;
  }
  const auto q = path_query(u, v);
  if (q.second == std::numeric_limits<WeightT>::lowest()) {
    return false;
  }
  return VERTICES_TO_EDGE(q.first.src, q.first.dst) == VERTICES_TO_EDGE(u, v);
}

template <typename WeightT, typename Container>
std::pair<Edge, WeightT> LinkCutTreeMaxAgg<WeightT, Container>::path_query(node_id_t u, node_id_t v) {
  assert(u < num_verts && v < num_verts);
  const auto u_idx = static_cast<size_t>(u);
  const auto v_idx = static_cast<size_t>(v);
  if (u == v) {
    return {{static_cast<node_id_t>(-1), static_cast<node_id_t>(-1)}, std::numeric_limits<WeightT>::lowest()};
  }
  if (!connected(u, v)) {
    return {{static_cast<node_id_t>(-1), static_cast<node_id_t>(-1)}, std::numeric_limits<WeightT>::lowest()};
  }
  auto pointer_edge = vert(u_idx).path_query(vert_ptr(v_idx));
  std::pair<Edge, WeightT> edge;
  if (pointer_edge.first.first == nullptr) {
    edge.first.src = static_cast<node_id_t>(-1);
    edge.first.dst = static_cast<node_id_t>(-1);
  } else {
    edge.first.src = static_cast<node_id_t>(pointer_edge.first.first->get_node_id());
    edge.first.dst = static_cast<node_id_t>(pointer_edge.first.second->get_node_id());
  }
  edge.second = pointer_edge.second;
  return edge;
}

template <typename WeightT, typename Container>
size_t LinkCutTreeMaxAgg<WeightT, Container>::space_usage_bytes() const {
    size_t max_space = sizeof(LinkCutTreeMaxAgg<WeightT, Container>) +
                       (num_verts * (sizeof(NodeMaxLCT<WeightT>*) + sizeof(NodeMaxLCT<WeightT>)));
    return max_space;
}

template class LinkCutTreeMaxAgg<int8_t>;