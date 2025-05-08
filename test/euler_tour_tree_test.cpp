#include <algorithm>
#include <cstdint>
#include <unordered_set>

#include <gtest/gtest.h>

#include <euler_tour_tree.h>

#include "sketch_interfacing.h"

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool EulerTourNode<SketchClass>::isvalid() const {
  bool invalid = false;
  // validate allowed_caller is null iff edges is empty
  EXPECT_EQ(allowed_caller == nullptr, this->edges.empty()) << (invalid = true, "");
  if (invalid) return false;
  // make sure allowed_caller corresponds to one of the nodes
  bool allowed_valid = false;
  // validate each edge
  for (const auto& [k, v] : this->edges) {
    // validate node's back ptr
    EXPECT_EQ(v->node, this) << (invalid = true, "");
    if (invalid) return false;
    // validate node itself
    EXPECT_TRUE(v->isvalid()) << (invalid = true, "");
    if (invalid) return false;
    if (k == nullptr) {
      // node is sentinel, expect next to be null
      EXPECT_EQ(v->next(), nullptr) << (invalid = true, "");
      if (invalid) return false;
    } else {
      // node is not sentinel, expect next to be valid
      EXPECT_EQ(v->next()->node, k) << (invalid = true, "");
      if (invalid) return false;
    }
    // check allowed_caller
    if (v == allowed_caller) {
      // make sure there's only one allowed
      EXPECT_FALSE(allowed_valid) << (invalid = true, "");
      if (invalid) return false;
      allowed_valid = true;
    }
  }
  // check that there was one allowed_caller
  EXPECT_TRUE(allowed_valid) << (invalid = true, "");
  if (invalid) return false;

  return true;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
std::ostream& operator<<(std::ostream& os, const EulerTourNode<SketchClass>& ett) {
  os << "EulerTourNode " << &ett << std::endl;
  for (const auto& [k, v] : ett.edges) {
    os << "to EulerTourNode " << k << " is " << &v << std::endl;
    os << v << std::endl;
  }
  os << std::endl;
  return os;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
std::ostream& operator<<(std::ostream& os,
    const std::vector<EulerTourNode<SketchClass>>& nodes) {
  for (const auto& node : nodes) {
    os << node;
  }
  return os;
}

TEST(EulerTourTreeSuite, stress_test) {
  // global sketch variables
  sketch_len = 1000;
  sketch_err = 100;

  int nodecount = 1000;
  int n = 100000;
  int seed = time(NULL);
  srand(seed);
  std::cout << "Seeding stress test with " << seed << std::endl;
  EulerTourTree ett(nodecount, 0, seed);

  for (int i = 0; i < n; i++) {
    int a = rand() % nodecount, b = rand() % nodecount;
    if (rand() % 100 < 15) {
      ett.link(a,b);
    } else {
      ett.cut(a,b);
    }
    if (i % n/100 == 0)
    {
      ASSERT_TRUE(std::all_of(ett.ett_nodes.begin(), ett.ett_nodes.end(),
            [](auto& node){return node.isvalid();}))
          << "Stress test validation failed, final state:"
          << std::endl
          << ett.ett_nodes;
    }
  }
}

TEST(EulerTourTreeSuite, random_links_and_cuts) {
  // sketch variables
  sketch_len = 1000;
  sketch_err = 100;

  int nodecount = 1000;
  int n = 500;
  int seed = time(NULL);
  srand(seed);
  std::cout << "Seeding random links and cuts test with " << seed << std::endl;
  EulerTourTree<DefaultSketchColumn> ett(nodecount, 0, seed);
  for (int i = 0; i < nodecount; i++)
    ett.update_sketch(i, (vec_t)i);

  // Do random links and cuts
  for (int i = 0; i < n; i++) {
    int a = rand() % nodecount, b = rand() % nodecount;
    if (rand() % 100 < 10) {
      ett.link(a,b);
    } else {
      ett.cut(a,b);
    }
    ASSERT_TRUE(std::all_of(ett.ett_nodes.begin(), ett.ett_nodes.end(),
          [](auto& node){return node.isvalid();}))
        << "Stress test validation failed, final state:"
        << std::endl
        << ett.ett_nodes;
  }

  std::unordered_set<SkipListNode<DefaultSketchColumn>*> sentinels;
  for (int i = 0; i < nodecount; i++)
  {
    SkipListNode<DefaultSketchColumn> *sentinel = ett.ett_nodes[i].edges.begin()->second->get_last();
    sentinels.insert(sentinel);
  }

  // Walk up from an occurrence of each node to the root of its auxiliary tre
  std::unordered_map<SkipListNode<DefaultSketchColumn>*, DefaultSketchColumn*> aggs;
  std::unordered_map<SkipListNode<DefaultSketchColumn>*, uint32_t> sizes;
  for (int i = 0; i < nodecount; i++)
  {
    SkipListNode<DefaultSketchColumn>* sentinel = ett.ett_nodes[i].edges.begin()->second->get_last();
    if (aggs.find(sentinel) == aggs.end())
    {
      // DefaultSketchColumn* agg = new Sketch(sketch_len, seed, 1, sketch_err);
      DefaultSketchColumn *agg = new DefaultSketchColumn(4, 0);
      aggs.insert({sentinel, agg});
      SkipListNode<DefaultSketchColumn>* sentinel_root = sentinel->get_root();
      
      sentinel_root->process_updates();
      aggs[sentinel]->merge(*sentinel->get_list_aggregate());
      sizes[sentinel] = sentinel->get_list_size();
    }
  }

  std::unordered_map<SkipListNode<DefaultSketchColumn>*, DefaultSketchColumn*> naive_aggs;
  std::unordered_map<SkipListNode<DefaultSketchColumn>*, uint32_t> naive_sizes;
  // Naively compute aggregates for each connected component
  for (int i = 0; i < nodecount; i++)
  {
    SkipListNode<DefaultSketchColumn>* sentinel = ett.ett_nodes[i].edges.begin()->second->get_last();
    sentinel->process_updates();
    if (naive_aggs.find(sentinel) != naive_aggs.end())
    {
      naive_aggs[sentinel]->merge(*ett.ett_nodes[i].allowed_caller->sketch_agg);
      naive_sizes[sentinel] += 1;
    }
    else
    {
      // Sketch* agg = new Sketch(sketch_len, seed, 1, sketch_err);
      DefaultSketchColumn *agg = new DefaultSketchColumn(4, 0);
      naive_aggs.insert({sentinel, agg});
      naive_aggs[sentinel]->merge(*ett.ett_nodes[i].allowed_caller->sketch_agg);
      naive_sizes[sentinel] = 1;
    }
  }
  for (auto agg : aggs)
  {
    ASSERT_EQ(*(agg.second), *(naive_aggs[agg.first])) 
      << *agg.second << "\n\n\n" << *naive_aggs[agg.first] << std::endl;
  }
  for (auto size: sizes) {
    // Euler tour has length 2n-1
    ASSERT_EQ(size.second-1, 2*naive_sizes[size.first]-1);
  }
}

TEST(EulerTourTreeSuite, get_aggregate) {
  // Sketch variables
  sketch_len = 1000;
  sketch_err = 4;

  int seed = time(NULL);
  srand(seed);
  std::cout << "Seeding get aggregate test with " << seed << std::endl;

  // Keep a manual aggregate of all the sketches
  // DefaultSketchColumn true_aggregate(sketch_len, seed, 1, sketch_err);
  DefaultSketchColumn true_aggregate(4, 0);

  int nodecount = 1000;
  EulerTourTree<DefaultSketchColumn> ett(nodecount, 0, seed);

  // Add value to each sketch, update the manual aggregate
  for (int i = 0; i < nodecount; i++)
  {
    ett.update_sketch(i, (vec_t)i);
    true_aggregate.update((vec_t)i);
  }

  // Link all the ETT nodes
  for (int i = 0; i < nodecount-1; i++) {
    ett.link(i, i+1);
  }

  // Check that the ETT aggregate is properly maintained and gotten
  DefaultSketchColumn* aggregate = ett.get_aggregate(0);
  ASSERT_TRUE(*aggregate == true_aggregate);
}
