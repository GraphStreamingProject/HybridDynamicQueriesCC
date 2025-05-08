#pragma once

#include <gtest/gtest.h>
#include "sketch.h"
#include "sketch/sketch_columns.h"

template <typename SketchClass = FixedSizeSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
class EulerTourNode;

constexpr int skiplist_buffer_cap = 25;
extern long skiplist_seed;
extern double height_factor;
extern vec_t sketch_len;
extern vec_t sketch_err;

template <typename SketchClass = FixedSizeSketchColumn> requires(SketchColumnConcept<SketchClass, vec_t>)
class SkipListNode {

  SkipListNode* left = nullptr;
  SkipListNode* right = nullptr;
  SkipListNode* up = nullptr;
  SkipListNode* down = nullptr;
  // Store the first node to the left on the next level up
  SkipListNode* parent = nullptr;

  vec_t update_buffer[skiplist_buffer_cap];
  int buffer_size = 0;
  int buffer_capacity;

public:
  SketchClass* sketch_agg = nullptr;

  uint32_t size = 1;

  EulerTourNode<SketchClass>* node;

  SkipListNode(EulerTourNode<SketchClass>* node, long seed, bool has_sketch);
  ~SkipListNode();
  static SkipListNode* init_element(EulerTourNode<SketchClass>* node, bool is_allowed_caller);
  void uninit_element(bool delete_bdry);
  void uninit_list();

  // Returns the closest node on the next level up at or left of the current
  SkipListNode<SketchClass>* get_parent();
  // Returns the top left root node of the skiplist
  SkipListNode<SketchClass>* get_root();
  // Returns the bottom left boundary node of the skiplist
  SkipListNode<SketchClass>* get_first();
  // Returns the bottom right node of the skiplist
  SkipListNode<SketchClass>* get_last();

  // Return the aggregate size at the root of the list
  uint32_t get_list_size();
  // Return the aggregate sketch at the root of the list
  SketchClass* get_list_aggregate();
  // Update all the aggregate sketches with the input vector from the current node to its root
  SkipListNode<SketchClass>* update_path_agg(vec_t update_idx);
  // Add the given sketch to all aggregate sketches from the current node to its root
  SkipListNode<SketchClass>* update_path_agg(SketchClass* sketch);

  // Update just this node's aggregate sketch
  void update_agg(vec_t update_idx);

  // Apply all the sketch updates currently in the update buffer
  void process_updates();

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
  SkipListNode* next();
  int print_list();
};

// template <typename... Tail> requires((std::is_same_v<SkipListNode<SketchClass>*, Tail> && ...))
// SkipListNode<SketchClass>* SkipListNode<SketchClass>::join(SkipListNode<SketchClass>* head, Tail... tail) {
//   return join(head, join(tail...));
// }
