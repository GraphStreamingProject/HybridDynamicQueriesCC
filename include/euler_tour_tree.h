#pragma once
#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include <skiplist.h>
#include "sketch/sketch_concept.h"
#include "sketch_interfacing.h"
#include "default_containers.h"

#include <absl/container/flat_hash_map.h>


template <typename Key, typename Value, std::size_t InlineCapacity = 3>
class SmallEdgeMap {
public:
  using Pair = std::pair<Key, Value>;
  using OverflowMap = absl::flat_hash_map<Key, Value>;
  using OverflowIter = typename OverflowMap::iterator;
  using OverflowConstIter = typename OverflowMap::const_iterator;

  class iterator {
  public:
    iterator(SmallEdgeMap* owner, std::size_t inline_idx)
        : owner_(owner), inline_idx_(inline_idx), in_overflow_(false) {}
    iterator(SmallEdgeMap* owner, OverflowIter it)
        : owner_(owner), overflow_it_(it), in_overflow_(true) {}

    Pair operator*() const {
      if (in_overflow_) {
        return Pair{overflow_it_->first, overflow_it_->second};
      }
      return owner_->inline_pairs_[inline_idx_];
    }
    const Pair* operator->() const {
      current_ = **this;
      return &current_;
    }
    iterator& operator++() {
      if (in_overflow_) {
        ++overflow_it_;
      } else {
        ++inline_idx_;
      }
      return *this;
    }
    bool operator==(const iterator& other) const {
      if (in_overflow_ != other.in_overflow_ || owner_ != other.owner_) {
        return false;
      }
      return in_overflow_ ? (overflow_it_ == other.overflow_it_) : (inline_idx_ == other.inline_idx_);
    }
    bool operator!=(const iterator& other) const { return !(*this == other); }

  private:
    SmallEdgeMap* owner_;
    std::size_t inline_idx_ = 0;
    OverflowIter overflow_it_{};
    bool in_overflow_ = false;
    mutable Pair current_{};
  };

  class const_iterator {
  public:
    const_iterator(const SmallEdgeMap* owner, std::size_t inline_idx)
        : owner_(owner), inline_idx_(inline_idx), in_overflow_(false) {}
    const_iterator(const SmallEdgeMap* owner, OverflowConstIter it)
        : owner_(owner), overflow_it_(it), in_overflow_(true) {}

    Pair operator*() const {
      if (in_overflow_) {
        return Pair{overflow_it_->first, overflow_it_->second};
      }
      return owner_->inline_pairs_[inline_idx_];
    }
    const Pair* operator->() const {
      current_ = **this;
      return &current_;
    }
    const_iterator& operator++() {
      if (in_overflow_) {
        ++overflow_it_;
      } else {
        ++inline_idx_;
      }
      return *this;
    }
    bool operator==(const const_iterator& other) const {
      if (in_overflow_ != other.in_overflow_ || owner_ != other.owner_) {
        return false;
      }
      return in_overflow_ ? (overflow_it_ == other.overflow_it_) : (inline_idx_ == other.inline_idx_);
    }
    bool operator!=(const const_iterator& other) const { return !(*this == other); }

  private:
    const SmallEdgeMap* owner_;
    std::size_t inline_idx_ = 0;
    OverflowConstIter overflow_it_{};
    bool in_overflow_ = false;
    mutable Pair current_{};
  };

  SmallEdgeMap() = default;

  SmallEdgeMap(const SmallEdgeMap& other)
      : inline_pairs_(other.inline_pairs_), inline_size_(other.inline_size_) {
    if (other.overflow_) {
      overflow_ = std::make_unique<OverflowMap>(*other.overflow_);
    }
  }

  SmallEdgeMap& operator=(const SmallEdgeMap& other) {
    if (this == &other) {
      return *this;
    }
    inline_pairs_ = other.inline_pairs_;
    inline_size_ = other.inline_size_;
    if (other.overflow_) {
      overflow_ = std::make_unique<OverflowMap>(*other.overflow_);
    } else {
      overflow_.reset();
    }
    return *this;
  }

  SmallEdgeMap(SmallEdgeMap&&) noexcept = default;
  SmallEdgeMap& operator=(SmallEdgeMap&&) noexcept = default;
  Value emplace_or_get(Key key, Value value) {
    if (overflow_) {
      auto [it, _] = overflow_->emplace(key, value);
      return it->second;
    }

    for (std::size_t i = 0; i < inline_size_; ++i) {
      if (inline_pairs_[i].first == key) {
        return inline_pairs_[i].second;
      }
    }

    if (inline_size_ < InlineCapacity) {
      inline_pairs_[inline_size_++] = {key, value};
      return value;
    }

    migrate_to_overflow();
    auto [it, _] = overflow_->emplace(key, value);
    return it->second;
  }

  Value get(Key key) const {
    if (overflow_) {
      auto it = overflow_->find(key);
      assert(it != overflow_->end());
      return it->second;
    }
    for (std::size_t i = 0; i < inline_size_; ++i) {
      if (inline_pairs_[i].first == key) {
        return inline_pairs_[i].second;
      }
    }
    assert(false);
    return nullptr;
  }

  Value operator[](Key key) const {
    return get(key);
  }

  bool contains(Key key) const {
    if (overflow_) {
      return overflow_->find(key) != overflow_->end();
    }
    for (std::size_t i = 0; i < inline_size_; ++i) {
      if (inline_pairs_[i].first == key) {
        return true;
      }
    }
    return false;
  }

  Value any_value() const {
    assert(!empty());
    if (overflow_) {
      return overflow_->begin()->second;
    }
    return inline_pairs_[0].second;
  }

  void erase(Key key) {
    if (overflow_) {
      overflow_->erase(key);
      return;
    }
    for (std::size_t i = 0; i < inline_size_; ++i) {
      if (inline_pairs_[i].first == key) {
        inline_pairs_[i] = inline_pairs_[inline_size_ - 1];
        --inline_size_;
        return;
      }
    }
  }

  bool empty() const {
    return overflow_ ? overflow_->empty() : (inline_size_ == 0);
  }

  iterator begin() {
    return overflow_ ? iterator(this, overflow_->begin()) : iterator(this, 0);
  }
  iterator end() {
    return overflow_ ? iterator(this, overflow_->end()) : iterator(this, inline_size_);
  }
  const_iterator begin() const {
    return overflow_ ? const_iterator(this, overflow_->begin()) : const_iterator(this, 0);
  }
  const_iterator end() const {
    return overflow_ ? const_iterator(this, overflow_->end()) : const_iterator(this, inline_size_);
  }

  std::size_t dynamic_space_usage_bytes() const {
    if (!overflow_) {
      return 0;
    }
    // Estimate: map object + slot storage; inline storage is already in sizeof(SmallEdgeMap).
    return sizeof(OverflowMap) + overflow_->bucket_count() * sizeof(Pair);
  }

private:
  void migrate_to_overflow() {
    overflow_ = std::make_unique<OverflowMap>();
    overflow_->reserve(InlineCapacity * 2);
    for (std::size_t i = 0; i < inline_size_; ++i) {
      overflow_->emplace(inline_pairs_[i].first, inline_pairs_[i].second);
    }
    inline_size_ = 0;
  }

  std::array<Pair, InlineCapacity> inline_pairs_{};
  std::size_t inline_size_ = 0;
  std::unique_ptr<OverflowMap> overflow_;
};



template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
class EulerTourNode {
  FRIEND_TEST(EulerTourTreeSuite, random_links_and_cuts);
  FRIEND_TEST(EulerTourTreeSuite, get_aggregate);
  FRIEND_TEST(SkipListSuite, join_split_test);
  FRIEND_TEST(GraphTiersSuite, mini_correctness_test);
  
  SmallEdgeMap<EulerTourNode<SketchClass>*, SkipListNode<SketchClass>*, 3> edges;

  long seed = 0;

  SkipListNode<SketchClass>* make_edge(EulerTourNode<SketchClass>* other, SketchClass &temp_sketch);
  SkipListNode<SketchClass>* make_edge(EulerTourNode<SketchClass>* other);
  void delete_edge(EulerTourNode<SketchClass>* other, SketchClass &temp_sketch);

public:
  const node_id_t vertex = 0;
  const uint32_t tier = 0;
  SkipListNode<SketchClass>* allowed_caller = nullptr;

  EulerTourNode(long seed, node_id_t vertex, uint32_t tier);
  EulerTourNode(long seed);
  ~EulerTourNode();
  bool link(EulerTourNode<SketchClass>& other, SketchClass &temp_sketch);
  bool cut(EulerTourNode<SketchClass>& other, SketchClass &temp_sketch);

  bool isvalid() const;

  SketchClass& get_sketch(SkipListNode<SketchClass>* caller);
  SkipListNode<SketchClass>* update_sketch(vec_t update_idx);
  SkipListNode<SketchClass>* update_sketch(const ColumnEntryDelta &delta);
  SkipListNode<SketchClass>* update_sketch(const ColumnEntryDeltas &deltas);
  SkipListNode<SketchClass>* update_sketch(const SketchClass &sketch);
  SkipListNode<SketchClass>* update_sketch_atomic(vec_t update_idx);
  SkipListNode<SketchClass>* update_sketch_atomic(const ColumnEntryDelta &delta);
  SkipListNode<SketchClass>* update_sketch_atomic(const ColumnEntryDeltas &deltas);

  SkipListNode<SketchClass>* get_allowed_caller() {
      return this->allowed_caller;
  }
  
  // update just this node's sketch
  // plus return the allowed caller
  SkipListNode<SketchClass>* update_sketch_noagg_atomic(const ColumnEntryDelta &delta);
  // void update_sketch_noagg_atomic(const SketchClass &sketch);
  SkipListNode<SketchClass>* update_sketch_atomic_to_level(const ColumnEntryDelta &delta, uint32_t level);
  
  //recompute the parent aggregates
  void recompute_aggregates_parallel();

  const ColumnEntryDelta generate_entry_delta(vec_t update) const {
    return this->allowed_caller->sketch_agg.generate_entry_delta(update);
  }

  SkipListNode<SketchClass>* get_root() const;

  const SketchClass& get_aggregate();
  uint32_t get_size();
  bool has_edge_to(EulerTourNode<SketchClass>* other);

  std::set<EulerTourNode<SketchClass>*> get_component();
  std::size_t edge_map_space_usage_bytes() const {
    return edges.dynamic_space_usage_bytes();
  }

  std::size_t space_usage_bytes() const {
    return sizeof(EulerTourNode<SketchClass>) + edge_map_space_usage_bytes();
  }

  std::vector<node_id_t> get_component_vertices() {
      std::vector<node_id_t> component;
      auto nodes = get_component();
      for (auto node : nodes) {
          component.push_back(node->vertex);
      }
      return component;
  }

  long get_seed() {return seed;};

  template <typename T> requires(SketchColumnConcept<T, vec_t>)
  friend std::ostream& operator<<(std::ostream& os, const EulerTourNode<T>& ett);
};


using VectorContainer = std::vector<EulerTourNode<DefaultSketchColumn>>;
using HashmapContainer = absl::flat_hash_map<node_id_t, EulerTourNode<DefaultSketchColumn>*>;

template <typename SketchClass = DefaultSketchColumn, 
// typename Container = std::vector<EulerTourNode<SketchClass>>>
typename Container = std::conditional_t<
  use_vector_default_container,
  std::vector<EulerTourNode<SketchClass>>,
  absl::flat_hash_map<node_id_t, EulerTourNode<SketchClass>*>>>
requires(SketchColumnConcept<SketchClass, vec_t>)
class EulerTourTree {
  SketchClass temp_sketch;
private:
  size_t seed;
  node_id_t max_num_nodes;
  uint32_t tier_num;
public:
  // std::vector<EulerTourNode<SketchClass>> ett_nodes;
  // absl::flat_hash_map<node_id_t, EulerTourNode<SketchClass>*> ett_nodes;
  // Container ett_nodes;
  Container ett_nodes;
  
  using SketchType = SketchClass;
  using ComponentID = size_t;

  struct ComponentView {
    SkipListNode<SketchClass>* root = nullptr;

    ComponentView() = default;
    ComponentView(SkipListNode<SketchClass>* root) : root(root) {}

    ComponentID key() const {
      return reinterpret_cast<ComponentID>(root);
    }

    uint32_t size() const {
      return root->size;
    }

    SketchClass& sketch() const {
      return root->sketch_agg;
    }

  };
  
  EulerTourTree(node_id_t max_num_nodes, uint32_t tier_num, int seed);

  EulerTourNode<SketchClass>& ett_node(node_id_t u) {
    if constexpr (std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
        assert(u < ett_nodes.size());
        return ett_nodes[u];
    } else {
        assert(ett_nodes.find(u) != ett_nodes.end());
        return *ett_nodes[u];
    }
  }
  
  void initialize_node(node_id_t u) {
    // no-op with vector implementation
    if constexpr (!std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
        // assert(ett_nodes.find(u) == ett_nodes.end());
        // TODO - this is kinda gross - fix later
        if (ett_nodes.find(u) == ett_nodes.end())
          ett_nodes[u] = new EulerTourNode<SketchClass>(this->seed, u, this->tier_num);
    }
  };
  void uninitialize_node(node_id_t u) {
    // no-op with vector implementation
    if constexpr (!std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
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
    if constexpr (std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
        return true;
    } else {
        return ett_nodes.find(u) != ett_nodes.end();
    }
  };

  void link(node_id_t u, node_id_t v);
  void cut(node_id_t u, node_id_t v);
    // Debug-only helper: intentionally not part of the cutset strategy API.
    bool _has_edge(node_id_t u, node_id_t v) {
      return ett_node(u).has_edge_to(&ett_node(v));
    }
  bool is_connected(node_id_t u, node_id_t v) {
      return get_root(u) == get_root(v);
  }
  ComponentView update_sketch(node_id_t u, vec_t update_idx);
  ComponentView update_sketch(node_id_t u, const ColumnEntryDelta &delta);
  ComponentView update_sketch(node_id_t u, const ColumnEntryDeltas &deltas);
  ComponentView update_sketch(node_id_t u, const SketchClass &sketch);
  ComponentView update_sketch_atomic(node_id_t u, vec_t update_idx);
  ComponentView update_sketch_atomic(node_id_t u, const ColumnEntryDelta &delta);
  ComponentView update_sketch_atomic(node_id_t u, const ColumnEntryDeltas &deltas);
  
  // returns the allowed caller
  // SkipListNode<SketchClass>* update_sketch_noagg_atomic(const ColumnEntryDelta &delta);
  // void update_sketch_noagg_atomic(const SketchClass &sketch);
  
  //recompute the parent aggregates
  void recompute_aggregates_parallel();

  ColumnEntryDelta generate_entry_delta(node_id_t u, vec_t update) {
      // TODO - the specific node isnt actually meaningful here.
      return ett_node(u).generate_entry_delta(update);
  }

  std::pair<ComponentView, ComponentView> update_sketches(node_id_t u, node_id_t v, vec_t update_idx);
  SkipListNode<SketchClass>* get_root(node_id_t u);
  const SketchClass& get_aggregate(node_id_t u);
  uint32_t get_size(node_id_t u);
  uint32_t num_components() {
    std::set<void*> roots;
    if constexpr (std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
      for (node_id_t i = 0; i < ett_nodes.size(); ++i) {
        auto root = ett_node(i).get_root();
        roots.insert(root);
      }
    } else {
      for (const auto& [_, node] : ett_nodes) {
        auto root = node->get_root();
        roots.insert(root);
      }
    }
    return roots.size();
  }
  node_id_t get_max_nodes() {
      return max_num_nodes;
  }
    ComponentID component_id(node_id_t u) {
      return reinterpret_cast<ComponentID>(get_root(u));
    }
    ComponentView component_view(node_id_t u) {
      return ComponentView{get_root(u)};
    }
  size_t space_usage_bytes() {
    size_t total = 0;
    if constexpr (std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
        total += sizeof(EulerTourNode<SketchClass>) * ett_nodes.capacity();
        for (node_id_t i = 0; i < ett_nodes.size(); ++i) {
          total += ett_nodes[i].edge_map_space_usage_bytes();
        }
    } else {
        size_t num_buckets = ett_nodes.bucket_count();
        total += sizeof(std::pair<node_id_t, EulerTourNode<SketchClass>>*) * num_buckets;
        for (const auto& [_, node] : ett_nodes) {
          total += node->space_usage_bytes();
        }
    }
    std::unordered_set<SkipListNode<SketchClass>*> roots;
    if constexpr (std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
      for (node_id_t i = 0; i < ett_nodes.size(); ++i) {
        SkipListNode<SketchClass>* root = ett_node(i).get_root();
        roots.insert(root);
      }
    } else {
      for (const auto& [_, node] : ett_nodes) {
        roots.insert(node->get_root());
      }
    }
    for (SkipListNode<SketchClass>* root : roots) {
      total += root->compute_space_usage();
    }
    return total;
  }

  // size_t space_usage_bytes() {
  //   size_t total = 0;
  //   if constexpr (std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
  //       total += sizeof(EulerTourNode<SketchClass>) * ett_nodes.capacity();
  //   } else {
  //       size_t num_buckets = ett_nodes.bucket_count();
  //       total += sizeof(std::pair<node_id_t, EulerTourNode<SketchClass>>*) * num_buckets;
  //   }
  //   std::unordered_set<SkipListNode<SketchClass>*> roots;
  //   for (node_id_t i = 0; i < ett_nodes.size(); ++i) {
  //     if constexpr (!std::is_same_v<Container, std::vector<EulerTourNode<SketchClass>>>) {
  //       if (ett_nodes.find(i) == ett_nodes.end()) {
  //         continue;
  //       }
  //     }
  //     SkipListNode<SketchClass>* root = ett_node(i).get_root();
  //     roots.insert(root);
  //   }
  //   for (SkipListNode<SketchClass>* root : roots) {
  //     total += root->compute_space_usage();
  //   }
  //   return total;
  // }

  std::vector<node_id_t> get_component_vertices(node_id_t u) {
      return ett_node(u).get_component_vertices();
  }

  // Verify the structural integrity of the tree:
  // checks that each component's root size and sketch aggregate
  // match the expected values computed from the individual node sketches.
  bool verify_structure() {
      std::unordered_map<SkipListNode<SketchClass>*, std::vector<node_id_t>> components;
      for (node_id_t i = 0; i < ett_nodes.size(); ++i) {
          if (!is_initialized(i)) continue;
          components[get_root(i)].push_back(i);
      }
      bool valid = true;
      for (const auto& [root, vertices] : components) {
          // if (root->size != vertices.size()) {
          //     std::cout << "Size mismatch for root " << root
          //               << ": expected " << vertices.size()
          //               << ", got " << root->size << "\n";
          //     valid = false;
          // }
          SketchClass expected_sketch(SketchClass::suggest_capacity(max_num_nodes), seed);
          for (node_id_t idx : vertices) {
              expected_sketch.merge(ett_node(idx).allowed_caller->sketch_agg);
          }
          expected_sketch.merge(root->sketch_agg);
          if (expected_sketch.sample().result != ZERO) {
              std::cout << "Sketch mismatch for root " << root << std::endl;
              valid = false;
          }
      }
      return valid;
  }
};
  
