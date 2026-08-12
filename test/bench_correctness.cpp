#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "binary_graph_stream.h"
#include "utils/graph_util.h"
#include "util.h"

#include "../src/euler_tour_tree.cpp"
#include "cutsets/lct_cutset.h"
#include "../src/link_cut_tree.cpp"
#include "cutsets/ufo_cutset.h"

#if defined(CUPCAKE_ALGO_GRAPH_TIERS)
#include "../src/graph_tiers.cpp"
#elif defined(CUPCAKE_ALGO_BATCH_TIERS)
#include "../src/batch_tiers.cpp"
#elif defined(CUPCAKE_ALGO_MPI_TIERS)
#include "mpi_nodes.h"
#include "../src/input_node.cpp"
#include "../src/tier_node.cpp"
#elif defined(CUPCAKE_ALGO_MPI_BATCH_TIERS)
#include "mpi_batch_nodes.h"
#include "../src/batch_input_node.cpp"
#include "../src/batch_tier_node.cpp"
#endif

#if defined(USE_HYBRID) && USE_HYBRID
  #if defined(USE_PARALLEL_HYBRID) && USE_PARALLEL_HYBRID
    #include "parallel_hybrid_conn.h"
    template <typename T = InputNode>
    using HybridConnManager = ParallelConnectivityManager<T>;
  #else
    #include "serial_hybrid_conn.h"
    template <typename T = InputNode>
    using HybridConnManager = SerialConnectivityManager<T>;
  #endif
#endif

#if defined(CUTSET_DS_ETT)
#define CUTSET_TYPE EulerTourTree<DefaultSketchColumn>
#define CUTSET_NAME "ett"
#elif defined(CUTSET_DS_LCT)
#define CUTSET_TYPE cutset_lct::CutsetLCT<DefaultSketchColumn>
#define CUTSET_NAME "lct"
#elif defined(CUTSET_DS_UFO)
#define CUTSET_TYPE ufo::CutsetUFOTree<DefaultSketchColumn>
#define CUTSET_NAME "ufo"
#else
#error "Correctness benchmarks require ETT, LCT, or UFO cutsets"
#endif

#if defined(CUPCAKE_ALGO_GRAPH_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
using BenchSystem = GraphTiers<CUTSET_TYPE>;
#define NEEDS_MPI 0
#define TIER_NAME "graph_tiers"
#define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_BATCH_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
using BenchSystem = BatchTiers<CUTSET_TYPE>;
#define NEEDS_MPI 0
#define TIER_NAME "batch_tiers"
#define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_MPI_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
using BenchInputNode = InputNode;
using BenchTierNode = TierNode<CUTSET_TYPE>;
#define NEEDS_MPI 1
#define TIER_NAME "mpi_tiers"
#define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_MPI_BATCH_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
using BenchInputNode = BatchInputNode;
using BenchTierNode = BatchTierNode<CUTSET_TYPE>;
#define NEEDS_MPI 1
#define TIER_NAME "mpi_batch_tiers"
#define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_GRAPH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
using BenchSystem = HybridConnManager<GraphTiers<CUTSET_TYPE>>;
#define NEEDS_MPI 0
#define TIER_NAME "graph_tiers"
#define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_BATCH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
using BenchSystem = HybridConnManager<BatchTiers<CUTSET_TYPE>>;
#define NEEDS_MPI 0
#define TIER_NAME "batch_tiers"
#define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_MPI_TIERS) && defined(USE_HYBRID) && USE_HYBRID
using BenchSystem = HybridConnManager<>;
using BenchTierNode = TierNode<CUTSET_TYPE>;
#define NEEDS_MPI 1
#define TIER_NAME "mpi_tiers"
#define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_MPI_BATCH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
using BenchSystem = HybridConnManager<BatchInputNode>;
using BenchTierNode = BatchTierNode<CUTSET_TYPE>;
#define NEEDS_MPI 1
#define TIER_NAME "mpi_batch_tiers"
#define IS_HYBRID 1
#else
#error "Unsupported correctness benchmark backend"
#endif

struct Config {
  std::string input_path;
  std::string output_path;
  int batch_size = 0;
  int num_tiers = 0;
  int hybrid_threshold = 0;
  int recovery_size = 0;
  int move_to_sketch = 0;
  long repeats = 1;
  long check_interval = 1000000;
  bool static_graph = false;
  bool do_deletions = false;
  uint64_t seed = 0;
  bool seed_set = false;
};

struct Result {
  long repeat = 0;
  uint64_t seed = 0;
  long updates = 0;
  long checks = 0;
  bool correct = true;
  long first_failure_update = -1;
  std::string first_failure_phase;
  std::string reason;
  long elapsed_ms = 0;
};

class ReferenceDSU {
 public:
  explicit ReferenceDSU(node_id_t n) : parent(n), size(n, 1) {
    for (node_id_t v = 0; v < n; ++v) parent[v] = v;
  }

  node_id_t find(node_id_t v) {
    while (parent[v] != v) {
      parent[v] = parent[parent[v]];
      v = parent[v];
    }
    return v;
  }

  void unite(node_id_t a, node_id_t b) {
    a = find(a);
    b = find(b);
    if (a == b) return;
    if (size[a] < size[b]) std::swap(a, b);
    parent[b] = a;
    size[a] += size[b];
  }

 private:
  std::vector<node_id_t> parent;
  std::vector<node_id_t> size;
};

static Config parse_args(int argc, char** argv) {
  if (argc < 2) {
    throw std::runtime_error(
        "Usage: bench_correctness_<config> <stream_or_graph> [--static-graph] "
        "[--do-deletions] [--correctness-repeats N] "
        "[--correctness-check-interval N] [--batch-size N] [--num-tiers N] "
        "[--hybrid-threshold N] [--recovery-size N] [--move-to-sketch N] "
        "[--seed N] [--output path.tsv]");
  }

  Config cfg;
  cfg.input_path = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&]() -> const char* {
      if (++i >= argc) throw std::runtime_error("Missing value for " + arg);
      return argv[i];
    };
    if (arg == "--static-graph" || arg == "--static") cfg.static_graph = true;
    else if (arg == "--do-deletions") cfg.do_deletions = true;
    else if (arg == "--correctness-repeats") cfg.repeats = std::stol(value());
    else if (arg == "--correctness-check-interval") cfg.check_interval = std::stol(value());
    else if (arg == "--batch-size") cfg.batch_size = std::stoi(value());
    else if (arg == "--num-tiers") cfg.num_tiers = std::stoi(value());
    else if (arg == "--hybrid-threshold") cfg.hybrid_threshold = std::stoi(value());
    else if (arg == "--recovery-size") cfg.recovery_size = std::stoi(value());
    else if (arg == "--move-to-sketch") cfg.move_to_sketch = std::stoi(value());
    else if (arg == "--seed") { cfg.seed = std::stoull(value()); cfg.seed_set = true; }
    else if (arg == "--output") cfg.output_path = value();
    else throw std::runtime_error("Unknown argument: " + arg);
  }
  if (cfg.repeats < 1 || cfg.check_interval < 1) {
    throw std::runtime_error("Repeat count and check interval must be positive");
  }
  return cfg;
}

static bool compare_partition(
    node_id_t num_nodes,
    const std::unordered_set<edge_id_t>& active_edges,
    const std::vector<std::set<node_id_t>>& reported_components,
    std::string& reason) {
  ReferenceDSU dsu(num_nodes);
  for (edge_id_t edge_id : active_edges) {
    Edge edge = inv_concat_pairing_fn(edge_id);
    dsu.unite(edge.src, edge.dst);
  }

  std::vector<long> component_of(num_nodes, -1);
  for (size_t component = 0; component < reported_components.size(); ++component) {
    for (node_id_t vertex : reported_components[component]) {
      if (vertex >= num_nodes) {
        reason = "reported an out-of-range vertex";
        return false;
      }
      if (component_of[vertex] != -1) {
        reason = "reported a vertex in multiple components";
        return false;
      }
      component_of[vertex] = static_cast<long>(component);
    }
  }

  std::unordered_map<node_id_t, long> reference_to_reported;
  std::unordered_map<long, node_id_t> reported_to_reference;
  for (node_id_t vertex = 0; vertex < num_nodes; ++vertex) {
    if (component_of[vertex] == -1) {
      reason = "did not report every vertex";
      return false;
    }
    const node_id_t reference_root = dsu.find(vertex);
    const long reported_component = component_of[vertex];
    auto [reference_it, new_reference] = reference_to_reported.emplace(reference_root, reported_component);
    if (!new_reference && reference_it->second != reported_component) {
      reason = "split a reference connected component";
      return false;
    }
    auto [reported_it, new_reported] = reported_to_reference.emplace(reported_component, reference_root);
    if (!new_reported && reported_it->second != reference_root) {
      reason = "merged distinct reference connected components";
      return false;
    }
  }
  return true;
}

template <typename System>
static std::vector<std::set<node_id_t>> get_components(System& system) {
 #if IS_HYBRID
  system.force_sync();
  return system.cc_query();
 #else
  system.process_all_updates();
  #if NEEDS_MPI
  return system.cc_query();
  #else
  return system.get_cc();
  #endif
#endif
}

template <typename System>
static Result run_repeat(System& system, const Config& cfg, node_id_t num_nodes,
                         const std::vector<std::pair<node_id_t, node_id_t>>& static_edges,
                         long repeat, uint64_t seed) {
  Result result;
  result.repeat = repeat;
  result.seed = seed;
  std::unordered_set<edge_id_t> active_edges;
  const auto started = std::chrono::steady_clock::now();

  auto check = [&](const std::string& phase) {
    ++result.checks;
    std::string reason;
    if (!compare_partition(num_nodes, active_edges, get_components(system), reason) && result.correct) {
      result.correct = false;
      result.first_failure_update = result.updates;
      result.first_failure_phase = phase;
      result.reason = reason;
    }
  };

  auto apply = [&](GraphUpdate update, const std::string& phase) {
    if (update.type == BREAKPOINT) return;
    if (update.edge.src > update.edge.dst) std::swap(update.edge.src, update.edge.dst);
    const edge_id_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
    bool valid = true;
    if (update.type == INSERT) valid = active_edges.insert(edge_id).second;
    else if (update.type == DELETE) valid = active_edges.erase(edge_id) == 1;
    if (!valid && result.correct) {
      result.correct = false;
      result.first_failure_update = result.updates + 1;
      result.first_failure_phase = phase;
      result.reason = update.type == INSERT ? "duplicate insert in input" : "delete of inactive edge in input";
    }
    system.update(update);
    ++result.updates;
    if (result.updates % cfg.check_interval == 0) check(phase);
  };

  if (cfg.static_graph) {
    std::vector<std::pair<node_id_t, node_id_t>> edges = static_edges;
    std::mt19937_64 random(seed);
    std::shuffle(edges.begin(), edges.end(), random);
    for (const auto& edge : edges) apply({Edge{edge.first, edge.second}, INSERT}, "insert");
    if (result.updates % cfg.check_interval != 0) check("post_insert");
    if (cfg.do_deletions) {
      std::shuffle(edges.begin(), edges.end(), random);
      for (const auto& edge : edges) apply({Edge{edge.first, edge.second}, DELETE}, "delete");
      if (result.updates % cfg.check_interval != 0) check("post_delete");
    }
  } else {
    BinaryGraphStream stream(cfg.input_path, 100000);
    for (long operation = 0; operation < stream.edges(); ++operation) {
      apply(stream.get_edge(), "stream");
    }
    if (result.updates % cfg.check_interval != 0) check("end_stream");
  }

  result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  return result;
}

static void write_results(std::ostream& out, const std::string& input, const std::string& config_name,
                          node_id_t nodes, const std::vector<Result>& results) {
  out << "input\tconfig\trepeat\tseed\tnum_nodes\tupdates\tchecks\tcorrect\t"
         "first_failure_update\tfirst_failure_phase\treason\telapsed_ms\n";
  long failures = 0;
  for (const Result& result : results) {
    failures += !result.correct;
    out << input << '\t' << config_name << '\t' << result.repeat << '\t' << result.seed << '\t'
        << nodes << '\t' << result.updates << '\t' << result.checks << '\t'
        << (result.correct ? "true" : "false") << '\t' << result.first_failure_update << '\t'
        << result.first_failure_phase << '\t' << result.reason << '\t' << result.elapsed_ms << '\n';
  }
  out << "summary\t" << config_name << "\t-\t-\t" << nodes << "\t-\t-\t"
      << (failures == 0 ? "true" : "false") << "\t-\t-\t-\t-\t"
      << "correct_repeats=" << (results.size() - failures) << "/" << results.size()
      << ";incorrect_repeats=" << failures << "/" << results.size() << '\n';
}

int main(int argc, char** argv) {
  try {
#if NEEDS_MPI
    MPI_Init(&argc, &argv);
    int world_rank = 0;
    int world_size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
#else
    const int world_rank = 0;
    const int world_size = 1;
#endif
    Config cfg = parse_args(argc, argv);
    node_id_t num_nodes = 0;
    std::vector<std::pair<node_id_t, node_id_t>> static_edges;
    if (world_rank == 0) {
      if (cfg.static_graph) {
        auto graph = ufo::graph_utils::read_static_graph_auto(cfg.input_path);
        auto edges = parlay::remove_duplicates_ordered(ufo::graph_utils::to_edges(graph),
            [] (ufo::graph_utils::edge a, ufo::graph_utils::edge b) { return a < b; });
        num_nodes = graph.size();
        static_edges.assign(edges.begin(), edges.end());
      } else {
        BinaryGraphStream stream(cfg.input_path, 100000);
        num_nodes = stream.nodes();
      }
    }
#if NEEDS_MPI
    bcast(&num_nodes, sizeof(num_nodes), 0);
#endif
    const int batch_size = cfg.batch_size > 0 ? cfg.batch_size :
#if defined(CUPCAKE_ALGO_MPI_BATCH_TIERS)
        16834;
#else
        100;
#endif
    const uint32_t num_tiers = cfg.num_tiers > 0 ? cfg.num_tiers :
#if NEEDS_MPI
        static_cast<uint32_t>(world_size - 1);
#else
        std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::log2(static_cast<double>(num_nodes)))));
#endif
#if NEEDS_MPI
    if (world_size != static_cast<int>(num_tiers + 1)) {
      if (world_rank == 0) std::cerr << "Error: MPI world size must equal num_tiers + 1\n";
      MPI_Finalize();
      return 2;
    }
#endif
    height_factor = 1.0 / std::log2(std::log2(static_cast<double>(num_nodes)));
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = 1;

    uint64_t base_seed = cfg.seed;
    if (world_rank == 0 && !cfg.seed_set) base_seed = std::random_device{}();
#if NEEDS_MPI
    bcast(&base_seed, sizeof(base_seed), 0);
#endif
    std::vector<Result> results;
    for (long repeat = 1; repeat <= cfg.repeats; ++repeat) {
      const uint64_t repeat_seed = base_seed + static_cast<uint64_t>(repeat - 1);
#if NEEDS_MPI
      std::mt19937 tier_rng(repeat_seed);
      for (int discard = 0; discard < world_rank; ++discard) tier_rng();
      const int tier_seed = tier_rng();
      if (world_rank == 0) {
  #if IS_HYBRID
        BenchSystem system(num_nodes, num_tiers, batch_size, static_cast<int>(repeat_seed));
        if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
        if (cfg.recovery_size > 0) system.set_recovery_size(cfg.recovery_size);
        if (cfg.move_to_sketch > 0) system.set_move_to_sketch(cfg.move_to_sketch);
  #else
        BenchInputNode system(num_nodes, num_tiers, batch_size, static_cast<int>(repeat_seed));
        system.initialize_all_nodes();
  #endif
        Result result = run_repeat(system, cfg, num_nodes, static_edges, repeat, repeat_seed);
        system.end();
        std::cout << "[correctness] repeat " << result.repeat << "/" << cfg.repeats
            << ": " << (result.correct ? "correct" : "incorrect")
            << " (checks=" << result.checks << ", updates=" << result.updates << ")"
            << std::endl;
        results.push_back(std::move(result));
      } else {
        BenchTierNode tier(num_nodes, world_rank - 1, num_tiers, batch_size, tier_seed);
        tier.main();
      }
      MPI_Barrier(MPI_COMM_WORLD);
#else
  #if IS_HYBRID
      BenchSystem system(num_nodes, num_tiers, batch_size, repeat_seed);
      if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
      if (cfg.recovery_size > 0) system.set_recovery_size(cfg.recovery_size);
      if (cfg.move_to_sketch > 0) system.set_move_to_sketch(cfg.move_to_sketch);
  #elif defined(CUPCAKE_ALGO_BATCH_TIERS)
      BenchSystem system(num_nodes, num_tiers, batch_size, repeat_seed);
      system.initialize_all_nodes();
  #else
      BenchSystem system(num_nodes, repeat_seed);
      system.initialize_all_nodes();
  #endif
      Result result = run_repeat(system, cfg, num_nodes, static_edges, repeat, repeat_seed);
      std::cout << "[correctness] repeat " << result.repeat << "/" << cfg.repeats
            << ": " << (result.correct ? "correct" : "incorrect")
            << " (checks=" << result.checks << ", updates=" << result.updates << ")"
            << std::endl;
      results.push_back(std::move(result));
#endif
    }
    if (world_rank == 0) {
      std::string config_name = std::string(TIER_NAME) + "_" + CUTSET_NAME;
      if (IS_HYBRID) config_name += "_hybrid";
      if (cfg.output_path.empty()) write_results(std::cout, cfg.input_path, config_name, num_nodes, results);
      else {
        std::ofstream out(cfg.output_path);
        if (!out) throw std::runtime_error("Cannot open output file: " + cfg.output_path);
        write_results(out, cfg.input_path, config_name, num_nodes, results);
        write_results(std::cout, cfg.input_path, config_name, num_nodes, results);
      }
    }
#if NEEDS_MPI
    MPI_Finalize();
#endif
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Correctness benchmark error: " << error.what() << std::endl;
#if NEEDS_MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
#endif
    return 1;
  }
}
