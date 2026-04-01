#include <cassert>
#include <xxhash.h>
#include "skiplist.h"
#include "euler_tour_tree.h"
#include <atomic>


double height_factor;
long skiplist_seed = time(NULL);
vec_t sketch_len;
vec_t sketch_err;

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>::SkipListNode(EulerTourNode<SketchClass>* node, long seed, bool has_sketch) : node(node), sketch_agg(0, seed) {
  // if (has_sketch) sketch_agg = new Sketch(sketch_len, seed, 1, sketch_err);
  // TODO - FIGURE OUT HOW TO DO SEEDING PROPERLY
  // if (has_sketch)
  //   sketch_agg = new SketchClass(
  //       SketchClass::suggest_capacity(sketch_len), seed);
  if (has_sketch)
    this->sketch_agg = SketchClass(SketchClass::suggest_capacity(sketch_len), seed);
  return;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>::~SkipListNode() {
	// if (sketch_agg) delete sketch_agg;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void SkipListNode<SketchClass>::uninit_element(bool delete_bdry) {
	SkipListNode<SketchClass>* list_curr = this;
	SkipListNode* list_prev;
	SkipListNode* bdry_curr = this->left;
	SkipListNode* bdry_prev;
	while (list_curr) {
		list_prev = list_curr;
		list_curr = list_prev->get_up_link();
		delete list_prev;
	}
	if (delete_bdry) {
		while (bdry_curr) {
			bdry_prev = bdry_curr;
			bdry_curr = bdry_prev->get_up_link();
			delete bdry_prev;
		}
	}
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::init_element(EulerTourNode<SketchClass>* node, bool is_allowed_caller) {
	long seed = node->get_seed();
	// NOTE: WE SHOULD MAKE IT SO DIFFERENT SKIPLIST NODES FOR THE SAME ELEMENT CAN BE DIFFERENT HEIGHTS
	uint64_t element_height = height_factor*__builtin_ctzll(XXH3_64bits_withSeed(&node->vertex, sizeof(node_id_t), skiplist_seed))+1;
	SkipListNode* list_node, *bdry_node, *list_prev, *bdry_prev;
	list_node = bdry_node = list_prev = bdry_prev = nullptr;
	// Add skiplist and boundary nodes up to the random height
	for (uint64_t i = 0; i < element_height; i++) {
		if (i == 0) {
			list_node = new SkipListNode(node, seed, is_allowed_caller);
			bdry_node = new SkipListNode(nullptr, seed, false);
		} else {
			list_node = new SkipListNode(node, seed, true);
			bdry_node = new SkipListNode(nullptr, seed, true);
		}
		list_node->left = bdry_node;
		bdry_node->right = list_node;
		if (list_prev) {
			list_node->down = list_prev;
			list_prev->set_up_link(list_node);
		}
		if (bdry_prev) {
			bdry_node->down = bdry_prev;
			bdry_prev->set_up_link(bdry_node);
		}
		list_prev = list_node;
		bdry_prev = bdry_node;
	}
	// Add one more boundary node at height+1
	SkipListNode* root = new SkipListNode(nullptr, seed, true);
	root->down = bdry_prev;
	bdry_prev->set_up_link(root);
	list_prev->set_parent_link(root);
	root->size = 2;
	return root->get_last();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::get_parent() const {
	// SkipListNode* curr = this;
	// while (curr && !curr->up) {
	// 	curr = curr->left;
	// }
	// return curr ? curr->up : nullptr;
	return get_parent_link();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::get_root() const {
	const SkipListNode* prev = nullptr;
	const SkipListNode* curr = this;
	while (curr) {
		prev = curr;
		curr = prev->get_parent();
	}
	return (SkipListNode*) prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::get_first() const {
	// Go to the root first and then down to the first element, because if we start at some lower level
	// we may have to travel right a lot more on that level, takes log time instead of linear time
	SkipListNode* prev = nullptr;
	SkipListNode* curr = this->get_root();
	while (curr) {
		prev = curr;
		curr = prev->down;
	}
	return prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::get_last() const {
	// Go to the root first and then down to the last element, because if we start at some lower level
	// we may have to travel left a lot more on that level, takes log time instead of linear time
	SkipListNode* prev = nullptr;
	SkipListNode* curr = this->get_root();
	while (curr) {
		prev = curr;
		curr = prev->right ? prev->right : prev->down;
	}
	return prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
uint32_t SkipListNode<SketchClass>::get_list_size() {
	return this->get_root()->size;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
const SketchClass& SkipListNode<SketchClass>::get_list_aggregate() {
	return this->get_root()->sketch_agg;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void SkipListNode<SketchClass>::update_agg(vec_t update_idx) {
	if (!this->sketch_agg.is_initialized()) // Only do something if this node has a sketch
		return;
	this->sketch_agg.update(update_idx);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void SkipListNode<SketchClass>::update_agg_atomic(vec_t update_idx) {
	if (!this->sketch_agg.is_initialized()) // Only do something if this node has a sketch
		return;
	// TODO - do we need to do batchiing here too?
	this->sketch_agg.atomic_update(update_idx);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void SkipListNode<SketchClass>::process_updates() {
	// No-op: buffering removed, updates applied directly in update_agg
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg(vec_t update_idx) {
	SkipListNode* curr = this;
	SkipListNode* prev;
	while (curr) {
		curr->update_agg(update_idx);
		prev = curr;
		curr = prev->get_parent();
	}
	return prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg_atomic(vec_t update_idx) {
	SkipListNode* curr = this;
	SkipListNode* prev;
	while (curr) {
		curr->update_agg_atomic(update_idx);
		prev = curr;
		curr = prev->get_parent();
	}
	return prev;
}
template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg(const ColumnEntryDelta &delta) {
	SkipListNode* curr = this;
	SkipListNode* prev;
	while (curr) {
		// __builtin_prefetch(curr->get_parent());
		curr->update_agg_entry_delta(delta);
		prev = curr;
		curr = prev->get_parent();
	}
	return prev;
}
template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg_atomic(const ColumnEntryDelta &delta) {
	SkipListNode* curr = this;
	SkipListNode* prev;
	while (curr) {
		// __builtin_prefetch(curr->get_parent());
		curr->update_agg_atomic_entry_delta(delta);
		prev = curr;
		curr = prev->get_parent();
	}
	return prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg(const ColumnEntryDeltas &deltas) {
	SkipListNode* curr = this;
	SkipListNode* prev;
	while (curr) {
		curr->update_agg_entry_deltas(deltas);
		prev = curr;
		curr = prev->get_parent();
	}
	return prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg_atomic(const ColumnEntryDeltas &deltas) {
	SkipListNode* curr = this;
	SkipListNode* prev;
	while (curr) {
		curr->update_agg_atomic_entry_deltas(deltas);
		prev = curr;
		curr = prev->get_parent();
	}
	return prev;
}


template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg(SketchClass &sketch) {
	// returns the last node that was updated
	SkipListNode* curr = this;
	SkipListNode* prev;
	if (!this->sketch_agg.is_initialized()) {
		this->sketch_agg = std::move(sketch);
		prev = curr;
		curr = prev->get_parent();
		while (curr)
		{
			curr->sketch_agg.merge(this->sketch_agg);
			prev = curr;
			curr = prev->get_parent();
		}
		// this->sketch_agg.zero_contents();
	} else {
	  while (curr) {
		curr->sketch_agg.merge(sketch);
		prev = curr;
		curr = prev->get_parent();
	  }
	}
	return prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::update_path_agg(const SketchClass &sketch) {
	// returns the last node that was updated
	SkipListNode* curr = this;
	SkipListNode* prev;
	if (!this->sketch_agg.is_initialized()) {
		assert(false);
		// NOTE - SHOULD NOT USE IN THIS CASE
		// TODO - make this code less confusing if possible.
	} else {
	  while (curr) {
		curr->sketch_agg.merge(sketch);
		prev = curr;
		curr = prev->get_parent();
	  }
	}
	return prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
std::set<EulerTourNode<SketchClass>*> SkipListNode<SketchClass>::get_component() {
	std::set<EulerTourNode<SketchClass>*> nodes;
	SkipListNode* curr = this->get_first()->right; //Skip over the boundary node
	while (curr) {
		nodes.insert(curr->node);
		curr = curr->right;
	}
	return nodes;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void SkipListNode<SketchClass>::uninit_list() {
	SkipListNode* curr = this->get_first();
	SkipListNode* prev;
	while (curr) {
		prev = curr;
		curr = prev->right;
		prev->uninit_element(false);
	}
	prev->uninit_element(false);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::join(SkipListNode<SketchClass>* left, SkipListNode<SketchClass>* right) {
	assert(left || right);
	if (!left) return right->get_root();
	if (!right) return left->get_root();

	long seed = left->sketch_agg.is_initialized() ? left->sketch_agg.get_seed()
	 : left->get_parent()->sketch_agg.get_seed();

	SkipListNode* l_curr = left->get_last();
	SkipListNode* r_curr = right->get_first(); // this is the bottom boundary node
	SkipListNode* r_first = r_curr->right;
	SkipListNode* l_prev = nullptr;
	SkipListNode* r_prev = nullptr;
	
	// Go up levels. link pointers, add aggregates
	while (l_curr && r_curr) {
		// Fix right pointer and add agg
		l_curr->right = r_curr->right; // skip over boundary node
		if (r_curr->right) r_curr->right->left = l_curr; // skip over boundary node, but to the left
		// r_curr->process_updates();
		if (l_curr->sketch_agg.is_initialized() && r_curr->sketch_agg.is_initialized()) // Only if that skiplist node has a sketch
			l_curr->sketch_agg.merge(r_curr->sketch_agg);
		l_curr->size += r_curr->size-1;

		if (r_prev) delete r_prev; // Delete old boundary nodes
		l_prev = l_curr;
		r_prev = r_curr;
		l_curr = l_prev->get_parent();
		r_curr = r_prev->get_up_link();
	}

	// If left list was taller add the root agg in right to the rest in left
	while (l_curr) {
		l_curr->sketch_agg.merge(r_prev->sketch_agg);
		l_curr->size += r_prev->size-1;
		l_prev = l_curr;
		l_curr = l_prev->get_parent();
	}

	// If right list was taller add new boundary nodes to left list
	if (r_curr) {
		// Cache the left root to initialize the new boundary nodes
		// Sketch* l_root_agg = new Sketch(sketch_len, seed, 1, sketch_err);
		SketchClass l_root_agg = SketchClass(
			SketchClass::suggest_capacity(sketch_len), seed);
		// l_prev->process_updates();
		l_root_agg.merge(l_prev->sketch_agg);
		l_root_agg.merge(r_prev->sketch_agg);
		uint32_t l_root_size = l_prev->size - (r_prev->size-1);
		while (r_curr) {
			l_curr = new SkipListNode(nullptr, seed, true);
			l_curr->down = l_prev;
			l_prev->set_up_link(l_curr);
			l_curr->right = r_curr->right;
			if (r_curr->right) r_curr->right->left = l_curr;

			l_curr->sketch_agg.merge(l_root_agg);
			l_curr->size = l_root_size;
			// r_curr->process_updates();
			l_curr->sketch_agg.merge(r_curr->sketch_agg);
			l_curr->size += r_curr->size-1;

			if (r_prev) delete r_prev; // Delete old boundary nodes
			l_prev = l_curr;
			r_prev = r_curr;
			r_curr = r_prev->get_up_link();
		}
		// delete l_root_agg;
	}
	delete r_prev;
	// Update parent pointers in right list
	while (r_first) {
		while (r_first && !r_first->get_up_link()) {
			r_first->set_parent_link(r_first->left->get_parent());
			r_first = r_first->right;
		}
		if (r_first)
			r_first = r_first->get_up_link();
	}
	// Returns the root of the joined list
	return l_prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::split_left(SkipListNode<SketchClass>* node) {
	assert(node && node->left && !node->down);
	// If just splitting off the boundary nodes do nothing instead
	if (!node->left->left) {
		return nullptr;
	}
	long seed = node->node->get_seed();
	// Construct new boundary nodes with correct aggregates for the right component
	// New aggs will be sum of all aggs on each level in the right path
	// Subtract those new aggregates from the "corners" of the left path
	// And unlink the nodes and link with the  new boundary nodes
	SkipListNode* r_curr = node;
	SkipListNode* l_curr = node->left;
	SkipListNode* bdry = new SkipListNode(nullptr, seed, false);
	SkipListNode* new_bdry;
	while (r_curr) {
		r_curr->left = bdry;
		bdry->right = r_curr;
		l_curr->right = nullptr;
		if (l_curr->sketch_agg.is_initialized() && bdry->sketch_agg.is_initialized()) // Only if its not the bottom sketchless node
			l_curr->sketch_agg.merge(bdry->sketch_agg); // XOR addition same as subtraction
		l_curr->size -= bdry->size-1;
		// Get next l_curr, r_curr, and bdry
		l_curr = l_curr->get_parent();
		new_bdry = new SkipListNode(nullptr, seed, true);
		if (bdry->sketch_agg.is_initialized()) // Only if its not the bottom sketchless node
			new_bdry->sketch_agg.merge(bdry->sketch_agg);
		new_bdry->size = bdry->size;
		while (r_curr && !r_curr->get_up_link()) {
			// r_curr->process_updates();
			if (r_curr->sketch_agg.is_initialized()) // Only if that skiplist node has a sketch
				new_bdry->sketch_agg.merge(r_curr->sketch_agg);
			new_bdry->size += r_curr->size;
			r_curr->set_parent_link(new_bdry);
			r_curr = r_curr->right;
		}
		r_curr = r_curr ? r_curr->get_up_link() : nullptr;
		new_bdry->down = bdry;
		bdry->set_up_link(new_bdry);
		bdry = new_bdry;
	}
	// Subtract the final right agg from the rest of the aggs on left path
	SkipListNode* l_prev = nullptr;
	while (l_curr) {
		l_curr->sketch_agg.merge(bdry->sketch_agg); // XOR addition same as subtraction
		l_curr->size -= bdry->size-1;
		l_prev  = l_curr;
		l_curr = l_curr->get_parent();
	}
	// Trim extra boundary nodes on the left list
	l_curr = l_prev->down;
	while (!l_curr->right) {
		delete l_prev;
		l_prev = l_curr;
		l_curr = l_prev->down;
	}
	l_prev->clear_vertical_link();
	// Returns the root of left list
	return l_prev;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::split_right(SkipListNode<SketchClass>* node) {
	assert(node);
	SkipListNode* right = node->right;
	if (!right) return nullptr;
	SkipListNode::split_left(right);
	return right->get_root();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* SkipListNode<SketchClass>::next() {
	return this->right;
}


template class SkipListNode<DefaultSketchColumn>;