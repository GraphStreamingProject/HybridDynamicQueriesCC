#pragma once
#include <iostream>
#include <unordered_map>
#include <set>

#include <sketchless_skiplist.h>
#include "types.h"


#include <absl/container/flat_hash_map.h>

class SketchlessEulerTourNode {

  std::unordered_map<SketchlessEulerTourNode*, SketchlessSkipListNode*> edges;

  SketchlessSkipListNode* allowed_caller = nullptr;
  long seed = 0;

  SketchlessSkipListNode* make_edge(SketchlessEulerTourNode* other);
  void delete_edge(SketchlessEulerTourNode* other);

public:
  const node_id_t vertex = 0;
  const uint32_t tier = 0;

  SketchlessEulerTourNode(long seed, node_id_t vertex, uint32_t tier);
  SketchlessEulerTourNode(long seed);
  ~SketchlessEulerTourNode();
  bool link(SketchlessEulerTourNode& other);
  bool cut(SketchlessEulerTourNode& other);

  bool isvalid() const;

  SketchlessSkipListNode* get_root();

  bool has_edge_to(SketchlessEulerTourNode* other);

  std::set<SketchlessEulerTourNode*> get_component();

  long get_seed() {return seed;};

  friend std::ostream& operator<<(std::ostream& os, const SketchlessEulerTourNode& ett);
};


template <
// typename Container = std::vector<SketchlessEulerTourNode>>
typename Container = absl::flat_hash_map<node_id_t, SketchlessEulerTourNode*>>
class SketchlessEulerTourTree {
  // TODO - packing order fixes
  size_t seed = 0;
  uint32_t tier_num = 0;
public:
  node_id_t max_num_nodes;
  Container ett_nodes;

  SketchlessEulerTourTree(node_id_t max_num_nodes, uint32_t tier_num, size_t seed);
  
  void link(node_id_t u, node_id_t v);
  void cut(node_id_t u, node_id_t v);
  bool has_edge(node_id_t u, node_id_t v);
  SketchlessSkipListNode* get_root(node_id_t u);
  bool is_connected(node_id_t u, node_id_t v);
  std::vector<std::set<node_id_t>> cc_query();
  
  SketchlessEulerTourNode& ett_node(node_id_t u) {
    if constexpr (std::is_same_v<Container, std::vector<SketchlessEulerTourNode>>) {
        assert(u < ett_nodes.size());
        return ett_nodes[u];
    } else {
      // if (ett_nodes.find(u) == ett_nodes.end()) {
      //     std::cout << "ruh oh" << std::endl;
      // }
        assert(ett_nodes.find(u) != ett_nodes.end());
        return *ett_nodes[u];
    }
  }

  size_t space_usage_bytes() const {
      size_t total = sizeof(*this);
      if constexpr (std::is_same_v<Container, std::vector<SketchlessEulerTourNode>>) {
          total += max_num_nodes * sizeof(SketchlessEulerTourNode);
      } else {
          total += max_num_nodes * (sizeof(SketchlessEulerTourNode*) + sizeof(SketchlessEulerTourNode));
      }
      return total;
  }
  
  void initialize_node(node_id_t u) {
    // no-op with vector implementation
    if constexpr (!std::is_same_v<Container, std::vector<SketchlessEulerTourNode>>) {
        ett_nodes[u] = new SketchlessEulerTourNode(this->seed, u, this->tier_num);
    }
  };
  void uninitialize_node(node_id_t u) {
    // no-op with vector implementation
    if constexpr (!std::is_same_v<Container, std::vector<SketchlessEulerTourNode>>) {
        assert(ett_nodes.find(u) != ett_nodes.end());
        delete ett_nodes[u];
        ett_nodes.erase(u);
    }
  };
  
  void initialize_all_nodes() {
    for (node_id_t i = 0; i < max_num_nodes; ++i) {
        initialize_node(i);
    }
  };
  void initialize_all_nodes(node_id_t until) {
    assert(until <= max_num_nodes);
    for (node_id_t i = 0; i < until; ++i) {
        initialize_node(i);
    }
  }
  bool is_initialized(node_id_t u) {
    // no-op with vector implementation
    if constexpr (std::is_same_v<Container, std::vector<SketchlessEulerTourNode>>) {
        return true;
    } else {
        return ett_nodes.find(u) != ett_nodes.end();
    }
  };
};
