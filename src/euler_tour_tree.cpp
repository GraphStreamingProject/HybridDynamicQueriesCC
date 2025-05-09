#include <cassert>

#include <euler_tour_tree.h>

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
EulerTourTree<SketchClass>::EulerTourTree(node_id_t num_nodes, uint32_t tier_num, int seed) {
  // Initialize all the ETT node
    ett_nodes.reserve(num_nodes);
    for (node_id_t i = 0; i < num_nodes; ++i) {
        ett_nodes.emplace_back(seed, i, tier_num);
    }
    // Initialize the temp_sketch
    // this->temp_sketch = new Sketch(sketch_len, seed, 1, sketch_err);
    // this-> temp_sketch = new SketchClass(4, 0);
    this -> temp_sketch = new SketchClass(
        SketchClass::suggest_capacity(sketch_len), seed % (1 << 16));
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void EulerTourTree<SketchClass>::link(node_id_t u, node_id_t v) {
  ett_nodes[u].link(ett_nodes[v], temp_sketch);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void EulerTourTree<SketchClass>::cut(node_id_t u, node_id_t v) {
  ett_nodes[u].cut(ett_nodes[v], temp_sketch);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool EulerTourTree<SketchClass>::has_edge(node_id_t u, node_id_t v) {
  return ett_nodes[u].has_edge_to(&ett_nodes[v]);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* EulerTourTree<SketchClass>::update_sketch(node_id_t u, vec_t update_idx) {
  return ett_nodes[u].update_sketch(update_idx);
}

template <typename SketchClass>
  requires(SketchColumnConcept<SketchClass, vec_t>)
std::pair<SkipListNode<SketchClass> *, SkipListNode<SketchClass> *>
EulerTourTree<SketchClass>::update_sketches(node_id_t u, node_id_t v,
                                            vec_t update_idx) {
  // Update the paths in lockstep, stopping at the first common node
  SkipListNode<SketchClass>* curr1 = ett_nodes[u].allowed_caller;
  SkipListNode<>* curr2 = ett_nodes[v].allowed_caller;
	SkipListNode<> *prev1, *prev2;
	while (curr1 || curr2) {
    if (curr1 == curr2) {
      SkipListNode<>* root  = curr1->get_root();
      return {root, root};
    }
    if (curr1) {
      curr1->update_agg(update_idx);
      prev1 = curr1;
      curr1 = prev1->get_parent();
    }
    if (curr2) {
      curr2->update_agg(update_idx);
      prev2 = curr2;
      curr2 = prev2->get_parent();
    }
	}
	return {prev1, prev2};
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* EulerTourTree<SketchClass>::get_root(node_id_t u) {
  return ett_nodes[u].get_root();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SketchClass* EulerTourTree<SketchClass>::get_aggregate(node_id_t u) {
  return ett_nodes[u].get_aggregate();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
uint32_t EulerTourTree<SketchClass>::get_size(node_id_t u) {
  return ett_nodes[u].get_size();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
EulerTourNode<SketchClass>::EulerTourNode(long seed, node_id_t vertex, uint32_t tier) : seed(seed), vertex(vertex), tier(tier) {
  // Initialize sentinel
  this->make_edge(nullptr, nullptr);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
EulerTourNode<SketchClass>::EulerTourNode(long seed) : seed(seed) {
  // Initialize sentinel
  this->make_edge(nullptr, nullptr);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
EulerTourNode<SketchClass>::~EulerTourNode() {
  // Final boundary nodes are a memory leak
  // Need to somehow delete all the skiplist nodes at the end
  // for (auto edge : edges)
  //   edge.second->uninit_element(false);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* EulerTourNode<SketchClass>::make_edge(EulerTourNode<SketchClass>* other, SketchClass* temp_sketch) {
  assert(!other || this->tier == other->tier);
  //Constructing a new SkipListNode with pointer to this ETT object
  SkipListNode<SketchClass>* node;
  if (allowed_caller == nullptr) {
    node = SkipListNode<SketchClass>::init_element(this, true);
    allowed_caller = node;
    if (temp_sketch != nullptr) {
      node->update_path_agg(temp_sketch);
      temp_sketch->zero_contents();
    }
  } else {
    node = SkipListNode<SketchClass>::init_element(this, false);
  }
  //Add the new SkipListNode to the edge list
  return this->edges.emplace(std::make_pair(other, node)).first->second;
  //Returns the new node pointer or the one that already existed if it did
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void EulerTourNode<SketchClass>::delete_edge(EulerTourNode<SketchClass>* other, SketchClass* temp_sketch) {
  assert(!other || this->tier == other->tier);
  SkipListNode<SketchClass>* node_to_delete = this->edges[other];
  this->edges.erase(other);
  if (node_to_delete == allowed_caller) {
    if (this->edges.empty()) {
      allowed_caller = nullptr;
      node_to_delete->process_updates();
      // std::cout << node_to_delete << std::endl;
      temp_sketch->merge(*node_to_delete->sketch_agg);
      node_to_delete->sketch_agg = nullptr;
    } else {
      allowed_caller = this->edges.begin()->second;
      node_to_delete->process_updates();
      allowed_caller->update_path_agg(node_to_delete->sketch_agg);
      node_to_delete->sketch_agg = nullptr; // We just gave the sketch to new allowed caller
    }
  }
  node_to_delete->uninit_element(true);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* EulerTourNode<SketchClass>::update_sketch(vec_t update_idx) {
  assert(allowed_caller);
  return this->allowed_caller->update_path_agg(update_idx);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SkipListNode<SketchClass>* EulerTourNode<SketchClass>::get_root() {
  return this->allowed_caller->get_root();
}

//Get the aggregate sketch at the root of the ETT for this node
template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
SketchClass* EulerTourNode<SketchClass>::get_aggregate() {
  assert(allowed_caller);
  return this->allowed_caller->get_list_aggregate();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
uint32_t EulerTourNode<SketchClass>::get_size() {
  return this->allowed_caller->get_list_size();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool EulerTourNode<SketchClass>::has_edge_to(EulerTourNode<SketchClass>* other) {
  return !(this->edges.find(other) == this->edges.end());
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
std::set<EulerTourNode<SketchClass>*> EulerTourNode<SketchClass>::get_component() {
  return this->allowed_caller->get_component();
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool EulerTourNode<SketchClass>::link(EulerTourNode<SketchClass>& other, SketchClass* temp_sketch) {
  assert(this->tier == other.tier);
  SkipListNode<SketchClass>* this_sentinel = this->edges.begin()->second->get_last();
  SkipListNode<SketchClass>* other_sentinel = other.edges.begin()->second->get_last();

  // There should always be a sentinel
  assert(this_sentinel == this_sentinel->node->edges.at(nullptr));
  assert(other_sentinel == other_sentinel->node->edges.at(nullptr));

  // If the nodes are already part of the same tree, don't link
  if (this_sentinel == other_sentinel) {
    return false;
  }

  // linking BD, ABA with CDEDC
  // ABA split on B gives A BA
  // CDEDC removing sentinel gives CDED
  //                               ^ might be null
  // CDED split on D gives C DED
  // A, construct B, DED, C, construct D, BA
  // ^                    ^
  // '--------------------'--- might be null

  SkipListNode<SketchClass>* aux_this_right = this->edges.begin()->second;
  SkipListNode<SketchClass>* aux_this_left = SkipListNode<SketchClass>::split_left(aux_this_right);

  // Unlink and destroy other_sentinel
  SkipListNode<SketchClass>* aux_other = SkipListNode<SketchClass>::split_left(other_sentinel);
  other_sentinel->node->delete_edge(nullptr, temp_sketch);

  SkipListNode<SketchClass>* aux_other_left, *aux_other_right;
  if (aux_other == nullptr) {
    aux_other_right = aux_other_left = nullptr;
  } else {
    aux_other_right = other.edges.begin()->second;
    aux_other_left = SkipListNode<SketchClass>::split_left(aux_other_right);
  }

  // reroot other tree
  // A, construct B, DED, C, construct D, BA
  // R  LR           L    R  LR           L
  // N                    N

  SkipListNode<SketchClass>* aux_edge_left = this->make_edge(&other, temp_sketch);
  SkipListNode<SketchClass>* aux_edge_right = other.make_edge(this, temp_sketch);

  SkipListNode<SketchClass>::join(aux_this_left, aux_edge_left, aux_other_right,
      aux_other_left, aux_edge_right, aux_this_right);

  return true;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool EulerTourNode<SketchClass>::cut(EulerTourNode<SketchClass>& other, SketchClass* temp_sketch) {
  assert(this->tier == other.tier);
  if (this->edges.find(&other) == this->edges.end()) {
    assert(other.edges.find(this) == other.edges.end());
    return false;
  }
  SkipListNode<SketchClass>* e1 = this->edges[&other];
  SkipListNode<SketchClass>* e2 = other.edges[this];

  SkipListNode<SketchClass>* frag1r = SkipListNode<SketchClass>::split_right(e1);
  bool order_is_e1e2 = e2->get_last() != e1;
  SkipListNode<SketchClass>* frag1l = SkipListNode<SketchClass>::split_left(e1);
  this->delete_edge(&other, temp_sketch);
  SkipListNode<SketchClass>* frag2r = SkipListNode<SketchClass>::split_right(e2);
  SkipListNode<SketchClass>* frag2l = SkipListNode<SketchClass>::split_left(e2);
  other.delete_edge(this, temp_sketch);

  if (order_is_e1e2) {
    // e1 is to the left of e2
    // e2 should be made into a sentinel
    SkipListNode<SketchClass>* sentinel = other.make_edge(nullptr, temp_sketch);
    SkipListNode<SketchClass>::join(frag2l, sentinel);
    SkipListNode<SketchClass>::join(frag1l, frag2r);
  } else {
    // e2 is to the left of e1
    // e1 should be made into a sentinel
    SkipListNode<SketchClass>* sentinel = this->make_edge(nullptr, temp_sketch);
    SkipListNode<SketchClass>::join(frag2r, sentinel);
    SkipListNode<SketchClass>::join(frag2l, frag1r);
  }

  return true;
}

template class EulerTourNode<DefaultSketchColumn>;
template class EulerTourTree<DefaultSketchColumn>;

// template std::ostream& operator<<(std::ostream&, const EulerTourNode<FixedSizeSketchColumn>&);