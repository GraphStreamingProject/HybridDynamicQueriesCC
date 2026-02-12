#pragma once
#include <stdint.h>
#include <parlay/sequence.h>


namespace ufo {

typedef uint32_t vertex_t;
static vertex_t NONE = -1;
using node_id_t = vertex_t;

struct empty_t {
};
static empty_t empty;

typedef uint64_t edge_t;

enum UpdateType {
  INSERT,
  DELETE
};

struct Edge {
public:
  vertex_t src;
  vertex_t dst;

  bool operator==(const Edge& other) const {
    return src == other.src && dst == other.dst;
  }
};

struct Update {
  UpdateType type;
  Edge edge;
};

struct UpdateBatch {
  UpdateType type;
  parlay::sequence<std::pair<int, int>> edges;
};

struct UpdateBatchWithWeights{
  UpdateType type;
  parlay::sequence<std::tuple<int,int,int>> insert_edges;
  parlay::sequence<std::pair<int,int>> delete_edges;
};

enum QueryType {
  CONNECTIVITY,
  PATH,
  SUBTREE
};

struct Query {
  vertex_t u;
  vertex_t v;
};

}
