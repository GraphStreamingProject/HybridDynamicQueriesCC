#pragma once

#include <gtest/gtest.h>
#include "sketch.h"
#include "sketch/sketch_columns.h"
#include "sketch_interfacing.h"

#include <parlay/sequence.h>
#include <tbb/tbb.h>

// using ColumnEntryDeltas = parlay::sequence<ColumnEntryDelta>::const_view_type;
using ColumnEntryDeltas = parlay::sequence<ColumnEntryDelta>::view_type;



#ifndef SKETCH_BUFFER_SIZE
  #define SKETCH_BUFFER_SIZE 25
#endif

enum AggUpdateState {
    NORMAL = 0,
    // needs to be updated (normal cas logic)
    NEEDS_UPDATE = 1,
    // this one was updated but its parent needs to be FULLY updated
    // since we changed in some non-trackable way (ie we atomically updated)
    PARENT_IS_STALE = 2,
    // this is applied to nodes where we don't need to reapply the aggregation
    LEAVE_ALONE = 3
};

template <typename SketchClass = DefaultSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
class EulerTourNode;

extern long skiplist_seed;
extern double height_factor;
// extern vec_t sketch_len;

template <typename SketchClass = DefaultSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
class SkipListNode {
  friend class EulerTourNode<SketchClass>;

  SkipListNode<SketchClass>* left = nullptr;
  SkipListNode<SketchClass>* right = nullptr;
  SkipListNode<SketchClass>* up = nullptr;
  SkipListNode<SketchClass>* down = nullptr;
  // Store the first node to the left on the next level up
  SkipListNode<SketchClass>* parent = nullptr;

  int buffer_size = 0;
  int buffer_capacity;
  vec_t update_buffer[SKETCH_BUFFER_SIZE];
  int8_t needs_update = AggUpdateState::NORMAL;

public:
  EulerTourNode<SketchClass>* node;
  SketchClass sketch_agg;

  uint32_t size = 1;
  

  SkipListNode(EulerTourNode<SketchClass>* node, long seed, bool has_sketch);
  ~SkipListNode();
  static SkipListNode* init_element(EulerTourNode<SketchClass>* node, bool is_allowed_caller);
  void uninit_element(bool delete_bdry);
  void uninit_list();

  // Returns the closest node on the next level up at or left of the current
  SkipListNode<SketchClass>* get_parent() const;
  // Returns the top left root node of the skiplist
  SkipListNode<SketchClass>* get_root() const;
  // Returns the bottom left boundary node of the skiplist
  SkipListNode<SketchClass>* get_first() const;
  // Returns the bottom right node of the skiplist
  SkipListNode<SketchClass>* get_last() const;

  // Return the aggregate size at the root of the list
  uint32_t get_list_size();
  // Return the aggregate sketch at the root of the list
  const SketchClass& get_list_aggregate();
  // Update all the aggregate sketches with the input vector from the current node to its root
  SkipListNode<SketchClass>* update_path_agg(vec_t update_idx);
  // // same, but atomically
  SkipListNode<SketchClass>* update_path_agg_atomic(vec_t update_idx);
  // SkipListNode<SketchClass>* update_path_agg_atomic(vec_t update_idx)
  // Add the given sketch to all aggregate sketches from the current node to its root
  SkipListNode<SketchClass>* update_path_agg(const SketchClass &sketch);
  SkipListNode<SketchClass>* update_path_agg(SketchClass &sketch);
  
  SkipListNode<SketchClass>* update_path_agg(const ColumnEntryDelta &delta);
  SkipListNode<SketchClass>* update_path_agg(const ColumnEntryDeltas &deltas);

  SkipListNode<SketchClass>* update_path_agg_atomic(const ColumnEntryDelta &delta);
  SkipListNode<SketchClass>* update_path_agg_atomic(const ColumnEntryDeltas &deltas);

  // Update just this node's aggregate sketch
  void update_agg(vec_t update_idx);
  // Same but atomically
  void update_agg_atomic(vec_t update_idx);
  //Just apply the delta
  void update_agg_entry_delta(const ColumnEntryDelta& delta) {
      if (!this->sketch_agg.is_initialized())  // Only do something if this node has a sketch
          return;
      this->sketch_agg.apply_entry_delta(delta);
  }

  void update_agg_entry_deltas(const ColumnEntryDeltas &deltas) {
      if (!this->sketch_agg.is_initialized())  // Only do something if this node has a sketch
          return;
      size_t sz = deltas.size();
      for (const auto& delta : deltas)
          this->sketch_agg.apply_entry_delta(delta);
  }
  // and the atomic versions:
  void update_agg_atomic_entry_delta(const ColumnEntryDelta &delta) {
      if (!this->sketch_agg.is_initialized())  // Only do something if this node has a sketch
          return;
      this->sketch_agg.atomic_apply_entry_delta(delta);
  }
  void update_agg_atomic_entry_deltas(const ColumnEntryDeltas &deltas) {
      if (!this->sketch_agg.is_initialized())  // Only do something if this node has a sketch
          return;
      size_t sz = deltas.size();
      for (const auto& delta : deltas)
          this->sketch_agg.atomic_apply_entry_delta(delta);
  }

  // Apply all the sketch updates currently in the update buffer
  void process_updates();
  
  bool _needs_full_recompute() {
    if (this->down == nullptr)
      return false;
    SkipListNode<SketchClass>* current = this->down;
    do {
      if (current->needs_update == AggUpdateState::PARENT_IS_STALE) {
        return true;
      }
      current = current->right;
    } while (current != nullptr && current != this->down && current->up == nullptr);
    return false;
  }

  void _do_full_prefetch() {
    if (this->down == nullptr)
      return;
    SkipListNode<SketchClass>* current = this->down;
    do {
      if (current->sketch_agg.is_initialized()) {
          this->sketch_agg.prefetch();
      }
      current = current->right;
    } while (current != nullptr && current != this->down && current->up == nullptr);
  }

  void _subtract_stale_children() {
    // subtract the agg for any sketches that need to be updated.
    if (this->down == nullptr)
      return;
    SkipListNode<SketchClass>* current = this->down;
    do {
      assert(current->needs_update != AggUpdateState::PARENT_IS_STALE);
      if (current->needs_update == AggUpdateState::NEEDS_UPDATE) {
        if (current->sketch_agg.is_initialized()) {
            this->sketch_agg.merge(current->sketch_agg);
        }
      }
      else {
        current->needs_update = AggUpdateState::LEAVE_ALONE;
      }
      current = current->right;
    } while (current != nullptr && current != this->down && current->up == nullptr);
  }
  
  void _do_full_reagg() {
    if (this->down == nullptr)
      return;
    this->sketch_agg.clear();
    SkipListNode<SketchClass>* current = this->down;
    do {
        if (current->sketch_agg.is_initialized()) {
            this->sketch_agg.merge(current->sketch_agg);
        }
        current->needs_update = AggUpdateState::NORMAL;
        current = current->right;
    } while (current != nullptr && current != this->down && current->up == nullptr);
  }
  
  void _full_recompute_aggs_topdown(int fork_levels) {
    if (!this->sketch_agg.is_initialized())
      return;
    if (this->down == nullptr)
      return;
    SkipListNode<SketchClass>* current = this->down;
    this->sketch_agg.clear();
    if (fork_levels > 0) {
      tbb::task_group tg;
      do {
        if (current->needs_update == AggUpdateState::NEEDS_UPDATE) {
          tg.run([current, fork_levels]() {
            current->recompute_aggs_topdown(fork_levels-1);
          });
        }
        current = current->right;
      } while (current != nullptr && current != this->down && current->up == nullptr);
      tg.wait();
      // _do_full_prefetch();
      _do_full_reagg();
    }
    else {
        do {
            if (current->needs_update == AggUpdateState::NEEDS_UPDATE) {
                current->recompute_aggs_topdown(fork_levels - 1);
            }
        } while (current != nullptr && current != this->down && current->up == nullptr);
        // _do_full_prefetch();
        _do_full_reagg();
    }
    this->needs_update = false;
  }

  void _recursive_recompute_children(int fork_levels) {
    if (this->down == nullptr)
      return;
    SkipListNode<SketchClass>* current = this->down;
    if (fork_levels > 0) {
      tbb::task_group tg;
      do {
        if (current->needs_update == AggUpdateState::NEEDS_UPDATE) {
        tg.run([current, fork_levels]() {
            current->recompute_aggs_topdown(fork_levels-1);
          });
        }
        current = current->right;
      } while (current != nullptr && current != this->down && current->up == nullptr);
      tg.wait();
    }
    else {
      if (current->needs_update == AggUpdateState::NEEDS_UPDATE) {
        do {
            current->recompute_aggs_topdown(fork_levels - 1);
            current = current->right;
        } while (current != nullptr && current != this->down && current->up == nullptr);
      }
    }
  }
  
  // recompute your aggregate from your children.
  void recompute_aggs_topdown(int fork_levels) {
    assert(this != nullptr);
    if (!this->sketch_agg.is_initialized())
      return;
    // do not recompute for bottom level nodes
    if (this->down == nullptr) 
      return;
    // _full_recompute_aggs_topdown(fork_levels);
    if (_needs_full_recompute()) {
      // prefetch all the children
      _full_recompute_aggs_topdown(fork_levels);
    }
    else {
      _subtract_stale_children();
      _recursive_recompute_children(fork_levels);
      SkipListNode<SketchClass>* current = this->down;
      do {
        if (current->needs_update == AggUpdateState::LEAVE_ALONE) {
          // do nothing
        }
        else {
          if (current->sketch_agg.is_initialized()) {
              this->sketch_agg.merge(current->sketch_agg);
          }
        }
        current->needs_update = AggUpdateState::NORMAL;
        current = current->right;
      } while (current != nullptr && current != this->down && current->up == nullptr);
    }
    this->needs_update = AggUpdateState::NORMAL;
  }
  size_t compute_space_usage() {
    size_t total = sizeof(SkipListNode<SketchClass>);
    if (this->sketch_agg.is_initialized())
      total += sketch_agg.space_usage_bytes();
    if (this->down != nullptr) {
      SkipListNode<SketchClass>* current = this->down;
      do {
        total += current->compute_space_usage();
        current = current->right;
      } while (current != nullptr && current != this->down && current->up == nullptr);
    }
    return total;
  }

  // we have to barrier on all of these finishing
  SkipListNode<SketchClass>* find_root_with_cas() {
    SkipListNode<SketchClass>* current = this;
    while (current->parent != nullptr) {
      current = current->parent;
      std::atomic_ref<int8_t> atomic_needs_update(current->needs_update);
      int8_t expected = static_cast<int8_t>(AggUpdateState::NORMAL);
      bool cas_succeed =  atomic_needs_update.compare_exchange_strong(
        expected,
        static_cast<int8_t>(AggUpdateState::NEEDS_UPDATE),
        std::memory_order_seq_cst
      );
      //  __sync_bool_compare_and_swap(
      //   (bool *)&current->needs_update,
      //   false,
      //   true
      // );
      if (!cas_succeed) {
        // someone else already set needs_update to true, so we can stop
        return nullptr;
      }
    }
    // TODO - dont make this hard-coded
    return current;
  }
  
  void clear_cas_flags() {
    assert(this != nullptr);
    this->needs_update = AggUpdateState::NORMAL;  
    SkipListNode<SketchClass>* current = this->down;
    do {
      if (current->needs_update == AggUpdateState::NEEDS_UPDATE) {
          current->clear_cas_flags();
      }
      current = current->right;
    } while (current != nullptr && current != this->down && current->up == nullptr);
  }

  std::set<EulerTourNode<SketchClass>*> get_component();

  // Returns the root of a new skiplist formed by joining the lists containing left and right
  static SkipListNode<SketchClass>* join(SkipListNode<SketchClass>* left, SkipListNode<SketchClass>* right);
  
  template <typename... Tail> requires((std::is_same_v<SkipListNode<SketchClass>*, Tail> && ...))
  static SkipListNode<SketchClass>* join(SkipListNode<SketchClass>* head, Tail... tail) {
    return join(head, join(tail...));
  };
  // Returns the root of the left list after splitting to the left of the given node
  static SkipListNode<SketchClass>* split_left(SkipListNode<SketchClass>* node);
  // Returns the root of the right list after splitting to the right of the given node
  static SkipListNode<SketchClass>* split_right(SkipListNode<SketchClass>* node);

  bool isvalid();
  SkipListNode<SketchClass>* next();
  int print_list();
};

// template <typename... Tail> requires((std::is_same_v<SkipListNode<SketchClass>*, Tail> && ...))
// SkipListNode<SketchClass>* SkipListNode<SketchClass>::join(SkipListNode<SketchClass>* head, Tail... tail) {
//   return join(head, join(tail...));
// }
