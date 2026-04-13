#pragma once

#include <gtest/gtest.h>
#include <algorithm>
#include <type_traits>
#include "types.h"
#include "util.h"
#include "default_containers.h"

#include <absl/container/flat_hash_map.h>

#define MAX_UINT64 (std::numeric_limits<uint64_t>::max())
// class LinkCutTree<>;
// class SplayTree;

class LinkCutNode {
  FRIEND_TEST(LinkCutTreeSuite, random_links_and_cuts);

  LinkCutNode* parent;
  LinkCutNode* dparent;
  LinkCutNode* left;
  LinkCutNode* right;
  
  LinkCutNode* head = this;
  LinkCutNode* tail = this;

  //Keep a list of edges with weights and up to two preferred edges
  std::pair<edge_id_t, edge_id_t> preferred_edges = {MAX_UINT64, MAX_UINT64};
  std::unordered_map<edge_id_t, uint32_t> edges = {};
  //Maintain an aggregate maximum of the edge weights in the auxilliary tree
  uint32_t max = 0;
  edge_id_t max_edge = MAX_UINT64;
  //Recompute the maximum just for this single node
  void rebuild_max();

  //Indicates that the meanings of left and right are reversed at all nodes in this subtree
  bool reversed;
  //Reverses all of appropriate nodes on the path from the root to this node
  void correct_reversals();

  void rotate_up();

  public:
    // delete copy constructor and assignment operator
    // LinkCutNode(const LinkCutNode&) = delete;
    // LinkCutNode& operator=(const LinkCutNode&) = delete;
    
    LinkCutNode* splay();

    void link_left(LinkCutNode* left);
    void link_right(LinkCutNode* right);

    void set_parent(LinkCutNode* parent);
    void set_dparent(LinkCutNode* dparent);
    
    void make_preferred_edge(edge_id_t e);
    void unmake_preferred_edge(edge_id_t e);
    void insert_edge(edge_id_t e, uint32_t weight);
    void remove_edge(edge_id_t e);
    bool has_edge(edge_id_t e);
    uint32_t get_edge_weight(edge_id_t e);
    void set_max(uint32_t weight);

    void set_reversed(bool reversed);
    void reverse();
    void set_use_edge_up(bool use_edge_up);
    void set_use_edge_down(bool use_edge_down);
    
    LinkCutNode* get_left();
    LinkCutNode* get_right();
    LinkCutNode* get_parent();
    LinkCutNode* get_dparent();

    LinkCutNode* get_head();
    LinkCutNode* get_tail();
    LinkCutNode* recompute_head();
    LinkCutNode* recompute_tail();
    std::pair<edge_id_t, uint32_t> get_max_edge();
    bool get_reversed();
};

template <
// typename Container = std::vector<LinkCutNode>>
typename Container = std::conditional_t<
  use_vector_default_container,
  std::vector<LinkCutNode>,
  absl::flat_hash_map<node_id_t, LinkCutNode*>>>
class LinkCutTree {
  FRIEND_TEST(LinkCutTreeSuite, join_split_test);
  FRIEND_TEST(LinkCutTreeSuite, expose_simple_test);
  FRIEND_TEST(LinkCutTreeSuite, random_links_and_cuts);
  
  Container nodes;
  node_id_t max_nodes;

  // Concatenate the paths with aux trees rooted at v and w and return the root of the combined aux tree
  LinkCutNode* join(LinkCutNode* v, LinkCutNode* w);
  // Split the aux tree of the path containing v right after v, and return the roots of the two new aux trees
  std::pair<LinkCutNode*, LinkCutNode*> split(LinkCutNode* v);
  
  LinkCutNode* splice(LinkCutNode* p);
  LinkCutNode* expose(LinkCutNode* v);
  // Make v the new root of the represented tree by "turning the tree inside out"
  LinkCutNode* evert(LinkCutNode* v);

  public:
    LinkCutTree(node_id_t num_nodes);
    
    // Given nodes v and w, link the trees containing v and w by adding the edge(v, w)
    void link(node_id_t v, node_id_t w, uint32_t weight);
    // Given nodes v and w, divide the tree containing v and w by deleting the edge(v, w)
    void cut(node_id_t v, node_id_t w);

    void* find_root(node_id_t v);

    // Given node v and w return the edge with the maximum weight on the path from v to w and the weight itself
    std::pair<edge_id_t, uint32_t> path_aggregate(node_id_t v, node_id_t w);
    bool has_edge(node_id_t v1, node_id_t v2);
    uint32_t get_edge_weight(node_id_t v1, node_id_t v2);

    // Query for the CC algorithm
    std::vector<std::set<node_id_t>> get_cc();
    
    LinkCutNode& node(node_id_t u) {
        if constexpr (std::is_same_v<Container, std::vector<LinkCutNode>>) {
            assert(u < nodes.size());
            return nodes[u];
        } else {
            assert(nodes.find(u) != nodes.end());
            return *nodes[u];
        }
    }
    
    LinkCutNode* get_node_ptr(node_id_t u) {
        if constexpr (std::is_same_v<Container, std::vector<LinkCutNode>>) {
            assert(u < nodes.size());
            return &nodes[u];
        } else {
            assert(nodes.find(u) != nodes.end());
            return nodes[u];
        }
    }
    
    void initialize_node(node_id_t u) {
      // no-op with vector implementation
      if constexpr (!std::is_same_v<Container, std::vector<LinkCutNode>>) {
          nodes[u] = new LinkCutNode();
      }
    };
    void uninitialize_node(node_id_t u) {
      // no-op with vector implementation
      if constexpr (!std::is_same_v<Container, std::vector<LinkCutNode>>) {
          assert(nodes.find(u) != nodes.end());
          delete nodes[u];
      }
    };

    void initialize_all_nodes() {
      for (node_id_t i = 0; i < max_nodes; ++i) {
          initialize_node(i);
      }
    };

    void initialize_all_nodes(node_id_t upto) {
      for (node_id_t i = 0; i < upto; ++i) {
          initialize_node(i);
      }
    };

    bool is_initialized(node_id_t u) {
      // no-op with vector implementation
      if constexpr (std::is_same_v<Container, std::vector<LinkCutNode>>) {
          return true;
      } else {
          return nodes.find(u) != nodes.end();
      }
    };
    
};
