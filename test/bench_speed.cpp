/**
 * bench_speed.cpp — Standalone speed benchmark harness.
 *
 * Compile-time configuration via CMake -D flags:
 * CUPCAKE_ALGO:   graph_tiers | batch_tiers | mpi_tiers | mpi_batch_tiers
 * CUTSET_DS:   ett | lct | ufo
 * USE_HYBRID:   0 | 1
 *
 * Runtime parameters (CLI):
 * <stream_path> [--batch-size N] [--height-factor F] [--num-tiers N] [--output path.tsv]
 * [--static-graph] [--do-deletions] [--num-queries N] [--speed-interval N]
 *
 * For MPI configs, launch via mpirun.
 */

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

#include "binary_graph_stream.h"
#include "utils/graph_util.h"
#include "util.h"

// Include all possible backends
#include "../src/euler_tour_tree.cpp"
#include "cutsets/lct_cutset.h"
#include "../src/link_cut_tree.cpp"
#include "cutsets/ufo_cutset.h"

#if defined(CUPCAKE_ALGO_GRAPH_TIERS) || defined(CUPCAKE_ALGO_BATCH_TIERS)
  #include "../src/graph_tiers.cpp"
  #include "../src/batch_tiers.cpp"
#endif

#if defined(CUPCAKE_ALGO_MPI_TIERS)
  #include "mpi_nodes.h"
  #include "../src/input_node.cpp"
  #include "../src/tier_node.cpp"
#endif

#if defined(CUPCAKE_ALGO_MPI_BATCH_TIERS)
  #include "mpi_batch_nodes.h"
  #include "../src/batch_input_node.cpp"
  #include "../src/batch_tier_node.cpp"
#endif

#if defined(USE_HYBRID) && USE_HYBRID
  #if defined(USE_PARALLEL_HYBRID) && USE_PARALLEL_HYBRID
    #include "parallel_hybrid_conn.h"
    template<typename T = InputNode>
    using HybridConnManager = ParallelConnectivityManager<T>;
  #else
    #include "serial_hybrid_conn.h"
    template<typename T = InputNode>
    using HybridConnManager = SerialConnectivityManager<T>;
  #endif
#endif

// ========== Cutset DS selection ==========
#if defined(CUTSET_DS_ETT)
  #define CUTSET_TYPE EulerTourTree<DefaultSketchColumn>
  #define CUTSET_NAME "ett"
#elif defined(CUTSET_DS_LCT)
  #define CUTSET_TYPE cutset_lct::CutsetLCT<DefaultSketchColumn>
  #define CUTSET_NAME "lct"
#elif defined(CUTSET_DS_UFO)
  #define CUTSET_TYPE ufo::CutsetUFOTree<DefaultSketchColumn>
  #define CUTSET_NAME "ufo"
#elif defined(CUPCAKE_ALGO_CF)
  #define CUTSET_NAME "cf"
#else
  #error "Must define one of CUTSET_DS_ETT, CUTSET_DS_LCT, CUTSET_DS_UFO"
#endif

// ========== System type selection ==========
#if defined(CUPCAKE_ALGO_GRAPH_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
  using BenchSystem = GraphTiers<CUTSET_TYPE>;
  #define TIER_NAME "graph_tiers"
  #define NEEDS_MPI 0
  #define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_BATCH_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
  using BenchSystem = BatchTiers<CUTSET_TYPE>;
  #define TIER_NAME "batch_tiers"
  #define NEEDS_MPI 0
  #define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_MPI_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
  using BenchTierNode = TierNode<CUTSET_TYPE>;
  #define TIER_NAME "mpi_tiers"
  #define NEEDS_MPI 1
  #define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_MPI_BATCH_TIERS) && !(defined(USE_HYBRID) && USE_HYBRID)
  using BenchTierNode = BatchTierNode<CUTSET_TYPE>;
  using BenchInputNode = BatchInputNode;
  #define TIER_NAME "mpi_batch_tiers"
  #define NEEDS_MPI 1
  #define IS_HYBRID 0
  #define USE_BATCH_INPUT_NODE 1
#elif defined(CUPCAKE_ALGO_BATCH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
  using BenchSystem = HybridConnManager<BatchTiers<CUTSET_TYPE>>;
  #define TIER_NAME "batch_tiers"
  #define NEEDS_MPI 0
  #define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_GRAPH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
  using BenchSystem = HybridConnManager<GraphTiers<CUTSET_TYPE>>;
  #define TIER_NAME "graph_tiers"
  #define NEEDS_MPI 0
  #define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_MPI_TIERS) && defined(USE_HYBRID) && USE_HYBRID
  using BenchSystem = HybridConnManager<>;
  #define TIER_NAME "mpi_tiers"
  #define NEEDS_MPI 1
  #define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_MPI_BATCH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
  using BenchSystem = HybridConnManager<BatchInputNode>;
  #define TIER_NAME "mpi_batch_tiers"
  #define NEEDS_MPI 1
  #define IS_HYBRID 1
  #define USE_BATCH_INPUT_NODE 1
#elif defined(CUPCAKE_ALGO_CF)
  #include "cluster_forest_wrapper.h"
  using BenchSystem = ClusterForestWrapper<>;
  #define TIER_NAME "cluster_forest"
  #define NEEDS_MPI 0
  #define IS_HYBRID 0
#else
  #error "Must define one of CUPCAKE_ALGO_GRAPH_TIERS, CUPCAKE_ALGO_BATCH_TIERS, CUPCAKE_ALGO_MPI_TIERS, CUPCAKE_ALGO_MPI_BATCH_TIERS, CUPCAKE_ALGO_CF"
#endif

#ifdef USE_RESIZEABLE_SKETCH
  #define SKETCH_NAME "resizeable"
#else
  #define SKETCH_NAME "fixed"
#endif

std::string stream_file;
int batch_size_arg = 0;
double height_factor_arg = 0;
int hybrid_threshold_arg = 0;

static uint32_t compute_default_num_tiers(node_id_t n) {
    double numerator = log2(static_cast<double>(n));
    double denominator = log2(3.0) - 1.0;
    return std::max<uint32_t>(5, static_cast<uint32_t>(numerator / denominator));
}

static std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

struct BenchConfig {
    std::string stream_path;
    int batch_size = 0;
    double height_factor = 0;
    int num_tiers = 0;
    int hybrid_threshold = 0;
    std::string output_path;

    // Static Graph Flags
    bool static_graph = false;
    bool do_deletions = false;
    size_t num_queries = 0;

    // Periodic speed reporting (0 = disabled)
    long speed_interval = 0;
};

static BenchConfig parse_args(int argc, char** argv) {
    BenchConfig cfg;
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <stream_path> [--batch-size N] [--height-factor F] "
                     "[--num-tiers N] [--hybrid-threshold N] [--output path.tsv]\n"
                  << "        [--static-graph] [--do-deletions] [--num-queries Q]\n"
                  << "        [--speed-interval N]  (report time every N updates, default: disabled)"
                  << std::endl;
        exit(1);
    }
    cfg.stream_path = argv[1];
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--batch-size" && i + 1 < argc) cfg.batch_size = std::atoi(argv[++i]);
        else if (arg == "--height-factor" && i + 1 < argc) cfg.height_factor = std::atof(argv[++i]);
        else if (arg == "--num-tiers" && i + 1 < argc) cfg.num_tiers = std::atoi(argv[++i]);
        else if (arg == "--hybrid-threshold" && i + 1 < argc) cfg.hybrid_threshold = std::atoi(argv[++i]);
        else if (arg == "--output" && i + 1 < argc) cfg.output_path = argv[++i];
        else if (arg == "--static-graph") cfg.static_graph = true;
        else if (arg == "--do-deletions") cfg.do_deletions = true;
        else if (arg == "--num-queries" && i + 1 < argc) cfg.num_queries = std::stoull(argv[++i]);
        else if (arg == "--speed-interval" && i + 1 < argc) cfg.speed_interval = std::atol(argv[++i]);
    }
    return cfg;
}

// ======================= Stream Output Writers =======================

struct IntervalRecord {
    long op_index;
    long num_updates;    // updates in this interval
    long interval_ms;    // wall-clock ms for this interval
    long num_edges = 0;  
    size_t sketched_edges = 0;
    size_t direct_sketch_inserts = 0;
};

static std::string intervals_path_from(const std::string& output_path) {
    // foo_speed.tsv -> foo_speed_intervals.tsv
    auto dot = output_path.rfind('.');
    if (dot == std::string::npos) return output_path + "_intervals";
    return output_path.substr(0, dot) + "_intervals" + output_path.substr(dot);
}

static void write_intervals_tsv(const std::string& path, const std::string& stream_path,
                                const std::string& config_name,
                                const std::vector<IntervalRecord>& records) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path);
  out << "stream\tconfig\ttop_index\tnum_updates\tinterval_ms\tupdates_per_sec\tnum_edges";
#if IS_HYBRID
    out << "\tsketched_edges\tdirect_sketch_inserts";
#endif
  out << "\n";
    for (const auto& r : records) {
        double ups = (r.interval_ms > 0)
            ? (static_cast<double>(r.num_updates) / r.interval_ms * 1000.0) : 0;
        out << basename_of(stream_path) << "\t" << config_name << "\t"
            << r.op_index << "\t" << r.num_updates << "\t"
            << r.interval_ms << "\t" << static_cast<long>(ups) << "\t" << r.num_edges;
#if IS_HYBRID
  out << "\t" << r.sketched_edges << "\t" << r.direct_sketch_inserts;
#endif
        out << "\n";
    }
}

static void write_speed_tsv(std::ostream& out, const BenchConfig& cfg, const std::string& config_name, node_id_t num_nodes,
                            long total_ops, long update_time_us, long query_time_us, int actual_batch_size, double actual_height_factor, int actual_num_tiers,
                            long num_edges = 0, size_t sketched_edges = 0, size_t direct_sketch_inserts = 0) {
    out << "stream\tconfig\tnum_nodes\ttotal_ops\tupdate_time_ms\tquery_time_ms\t"
           "updates_per_sec\tqueries_per_sec\tbatch_size\theight_factor\tnum_tiers\tnum_edges"
#if IS_HYBRID
           "\tsketched_edges\tdirect_sketch_inserts"
#endif
           "\n";
    long update_ms = update_time_us / 1000;
    long query_ms = query_time_us / 1000;
    long est_updates = static_cast<long>(0.9 * total_ops);
    long est_queries = total_ops - est_updates;
    double ups = (update_ms > 0) ? (static_cast<double>(est_updates) / update_ms * 1000.0) : 0;
    double qps = (query_ms > 0) ? (static_cast<double>(est_queries) / query_ms * 1000.0) : 0;

    out << basename_of(cfg.stream_path) << "\t" << config_name << "\t" << num_nodes << "\t" << total_ops << "\t"
        << update_ms << "\t" << query_ms << "\t" << static_cast<long>(ups) << "\t" << static_cast<long>(qps) << "\t"
        << actual_batch_size << "\t" << actual_height_factor << "\t" << actual_num_tiers << "\t" << num_edges;
#if IS_HYBRID
  out << "\t" << sketched_edges << "\t" << direct_sketch_inserts;
#endif
    out << "\n";
}

static void write_speed_report(std::ostream& out, const BenchConfig& cfg, const std::string& config_name, node_id_t num_nodes,
                               long total_ops, long update_time_us, long query_time_us, int actual_batch_size, double actual_height_factor, int actual_num_tiers,
                               long num_edges = 0, size_t sketched_edges = 0, size_t direct_sketch_inserts = 0) {
    long update_ms = update_time_us / 1000;
    long query_ms = query_time_us / 1000;
    long est_updates = static_cast<long>(0.9 * total_ops);
    long est_queries = total_ops - est_updates;
    double ups = (update_ms > 0) ? (static_cast<double>(est_updates) / update_ms * 1000.0) : 0;
    double qps = (query_ms > 0) ? (static_cast<double>(est_queries) / query_ms * 1000.0) : 0;

    int w = 24;
    out << "\n" << std::string(50, '=') << "\n Benchmark Stream Results\n" << std::string(50, '=') << "\n"
        << std::left << std::setw(w) << "Stream" << ": " << basename_of(cfg.stream_path) << "\n"
        << std::setw(w) << "Configuration" << ": " << config_name << "\n"
        << std::setw(w) << "Num Nodes" << ": " << num_nodes << "\n"
        << std::setw(w) << "Total Ops" << ": " << total_ops << "\n"
        << std::setw(w) << "Update Time (ms)"<< ": " << update_ms << "\n"
        << std::setw(w) << "Query Time (ms)" << ": " << query_ms << "\n"
        << std::setw(w) << "Updates/sec" << ": " << static_cast<long>(ups) << "\n"
        << std::setw(w) << "Queries/sec" << ": " << static_cast<long>(qps) << "\n"
        << std::setw(w) << "Batch Size" << ": " << actual_batch_size << "\n"
        << std::setw(w) << "Height Factor" << ": " << actual_height_factor << "\n"
        << std::setw(w) << "Num Tiers" << ": " << actual_num_tiers << "\n"
        << std::setw(w) << "Num Edges" << ": " << num_edges << "\n";
#if IS_HYBRID
    out << std::setw(w) << "Sketched Edges" << ": " << sketched_edges << "\n"
      << std::setw(w) << "Direct Sketch Inserts" << ": " << direct_sketch_inserts << "\n";
#endif
    out << std::string(50, '=') << std::endl;
}

// ======================= Static Output Writers =======================

static void write_static_speed_tsv(std::ostream& out, const BenchConfig& cfg, const std::string& config_name, node_id_t num_nodes,
                                   long edgecount, long ins_time_us, long q_time_us, long del_time_us,
                                   int actual_batch_size, double actual_height_factor, int actual_num_tiers) {
    out << "graph\tconfig\tnum_nodes\tedges\tnum_queries\tinserts_ms\tqueries_ms\tdeletes_ms\tbatch_size\theight_factor\tnum_tiers\n";
    out << basename_of(cfg.stream_path) << "\t" << config_name << "\t" << num_nodes << "\t" << edgecount << "\t" << cfg.num_queries << "\t"
        << (ins_time_us/1000) << "\t" << (q_time_us/1000) << "\t" << (del_time_us/1000) << "\t"
        << actual_batch_size << "\t" << actual_height_factor << "\t" << actual_num_tiers << "\n";
}

static void write_static_speed_report(std::ostream& out, const BenchConfig& cfg, const std::string& config_name, node_id_t num_nodes,
                                      long edgecount, long ins_time_us, long q_time_us, long del_time_us,
                                      int actual_batch_size, double actual_height_factor, int actual_num_tiers) {
    int w = 24;
    out << "\n" << std::string(50, '=') << "\n Benchmark Static Graph Results\n" << std::string(50, '=') << "\n"
        << std::left << std::setw(w) << "Graph File" << ": " << basename_of(cfg.stream_path) << "\n"
        << std::setw(w) << "Configuration" << ": " << config_name << "\n"
        << std::setw(w) << "Num Nodes" << ": " << num_nodes << "\n"
        << std::setw(w) << "Total Edges" << ": " << edgecount << "\n"
        << std::setw(w) << "Num Queries" << ": " << cfg.num_queries << "\n"
        << std::setw(w) << "Insert Phase (ms)" << ": " << (ins_time_us/1000) << "\n";
    if (cfg.num_queries > 0) out << std::setw(w) << "Query Phase (ms)" << ": " << (q_time_us/1000) << "\n";
    if (cfg.do_deletions) {
        out << std::setw(w) << "Delete Phase (ms)" << ": " << (del_time_us/1000) << "\n";
    }
    out << std::setw(w) << "Batch Size" << ": " << actual_batch_size << "\n"
        << std::setw(w) << "Height Factor" << ": " << actual_height_factor << "\n"
        << std::setw(w) << "Num Tiers" << ": " << actual_num_tiers << "\n"
        << std::string(50, '=') << std::endl;
}

// ========== Main ==========
int main(int argc, char** argv) {
#if NEEDS_MPI
    MPI_Init(&argc, &argv);
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    bool is_rank_0 = (world_rank == 0);
#else
    bool is_rank_0 = true;
#endif

    BenchConfig cfg = parse_args(argc, argv);
    std::string config_name = std::string(TIER_NAME) + "_" + CUTSET_NAME + "_" + SKETCH_NAME;
    if (IS_HYBRID) config_name += "_hybrid";

    node_id_t num_nodes = 0;
    long edgecount = 0;
    
    // We will hold variables specifically for the stream or static graph
    BinaryGraphStream* stream_ptr = nullptr;
    std::vector<std::pair<node_id_t, node_id_t>> static_edges;

    if (is_rank_0) {
        if (!cfg.static_graph) {
            stream_ptr = new BinaryGraphStream(cfg.stream_path, 100000);
            num_nodes = stream_ptr->nodes();
            edgecount = stream_ptr->edges();
        } else {
            auto G = ufo::graph_utils::break_sym_graph_from_bin(cfg.stream_path);
            auto E = parlay::remove_duplicates_ordered(ufo::graph_utils::to_edges(G), [&] (ufo::graph_utils::edge a, ufo::graph_utils::edge b) {
                if (a.first == b.first) return a.second < b.second;
                return a.first < b.first;
            });
            num_nodes = G.size();
            edgecount = E.size();
            static_edges.reserve(E.size());
            for (size_t i = 0; i < E.size(); i++) {
                static_edges.push_back({E[i].first, E[i].second});
            }
        }
    }

#if NEEDS_MPI
    // Broadcast metadata to all ranks
    bcast(&num_nodes, sizeof(node_id_t), 0);
#endif

    // Resolve defaults
    double hf = (cfg.height_factor > 0) ? cfg.height_factor : 1.0 / log2(log2(num_nodes));
    height_factor = hf;
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = 1;

#if defined(CUPCAKE_ALGO_MPI_BATCH_TIERS)
    int actual_batch_size = (cfg.batch_size > 0) ? cfg.batch_size : 16834;
#else
    int actual_batch_size = (cfg.batch_size > 0) ? cfg.batch_size : 100;
#endif

#if NEEDS_MPI
    uint32_t actual_num_tiers = (cfg.num_tiers > 0) ? cfg.num_tiers : (world_size - 1);
#else
    uint32_t actual_num_tiers = (cfg.num_tiers > 0) ? cfg.num_tiers : compute_default_num_tiers(num_nodes);
#endif

    // Seed
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
    uint64_t seed = dist(rng);

#if NEEDS_MPI
    int seed_int = static_cast<int>(seed);
    bcast(&seed_int, sizeof(int), 0);
    seed = seed_int;
    rng.seed(seed);
    for (int i = 0; i < world_rank; i++) dist(rng);
    int tier_seed = dist(rng);
#else
    int tier_seed = 0;
#endif

    // ==================== Shmem / Hybrid-Shmem path ====================
#if !NEEDS_MPI
    (void)actual_batch_size;

  #if IS_HYBRID
    BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
    if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
  #elif defined(CUPCAKE_ALGO_BATCH_TIERS)
    BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
  #else
    BenchSystem system(num_nodes, seed);
    system.initialize_all_nodes();
  #endif

    if (!cfg.static_graph) {
        long total_update_time = 0;
        long total_query_time = 0;
        auto update_timer = std::chrono::high_resolution_clock::now();
        auto query_timer = update_timer;
        bool doing_updates = true;
        bool query_checksum = 0;

        // Periodic speed reporting state
        long updates_since_report = 0;
        long current_num_edges = 0;
        auto interval_timer = std::chrono::high_resolution_clock::now();
        std::vector<IntervalRecord> interval_records;

        for (long i = 0; i < edgecount; i++) {
            GraphUpdate operation = stream_ptr->get_edge();
            if (operation.type == 2) { 
                if (doing_updates) {
                    total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::high_resolution_clock::now() - update_timer).count();
                    doing_updates = false;
                    query_timer = std::chrono::high_resolution_clock::now();
                }
              #if IS_HYBRID
                query_checksum ^= system.connectivity_query(operation.edge.src, operation.edge.dst);
              #else
                query_checksum ^= system.is_connected(operation.edge.src, operation.edge.dst);
              #endif
            } else {
                if (!doing_updates) {
                    total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::high_resolution_clock::now() - query_timer).count();
                    doing_updates = true;
                    update_timer = std::chrono::high_resolution_clock::now();
                }
                system.update(operation);
                current_num_edges += (operation.type == INSERT) ? 1 : -1;
                ++updates_since_report;

                if (cfg.speed_interval > 0 && updates_since_report >= cfg.speed_interval) {
                    auto now = std::chrono::high_resolution_clock::now();
                    long interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - interval_timer).count();
                    double ups = (interval_ms > 0) ? (static_cast<double>(updates_since_report) / interval_ms * 1000.0) : 0;
                    std::cout << "[speed] op " << i << "/" << edgecount
                              << "  last " << updates_since_report << " updates in " << interval_ms << " ms"
                              << "  (" << static_cast<long>(ups) << " updates/sec)"
                              << "  edges=" << current_num_edges;
                  #if IS_HYBRID
                    size_t ise = system.num_sketched_edges();
                    size_t idse = system.num_direct_sketch_edges();
                    std::cout << "  sketched=" << ise << " direct=" << idse;
                  #else
                    size_t ise = 0, idse = 0;
                  #endif
                    std::cout << std::endl;
                    interval_records.push_back({i, updates_since_report, interval_ms, current_num_edges, ise, idse});
                    updates_since_report = 0;
                    interval_timer = now;
                }
            }
        }
      #if IS_HYBRID
        system.force_sync();
      #endif
        if (doing_updates) total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - update_timer).count();
        else total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - query_timer).count();
        volatile uint64_t escape = query_checksum;

      #if IS_HYBRID
        size_t se = system.num_sketched_edges();
        size_t dse = system.num_direct_sketch_edges();
      #else
        size_t se = 0, dse = 0;
      #endif

        if (cfg.output_path.empty()) {
            write_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers, current_num_edges, se, dse);
        } else {
            std::ofstream out(cfg.output_path);
            write_speed_tsv(out, cfg, config_name, num_nodes, edgecount, total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers, current_num_edges, se, dse);
            write_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers, current_num_edges, se, dse);
            if (!interval_records.empty()) {
                write_intervals_tsv(intervals_path_from(cfg.output_path), cfg.stream_path, config_name, interval_records);
            }
        }

    } else {
        // === STATIC GRAPH MODE ===
        std::mt19937 gen(seed);
        
        // Shuffle for insertion
        std::shuffle(static_edges.begin(), static_edges.end(), gen);

        // Phase 1: Inserts
        auto ins_timer = std::chrono::high_resolution_clock::now();
        for (const auto& e : static_edges) {
            GraphUpdate op;
            op.type = INSERT;
            op.edge.src = e.first;
            op.edge.dst = e.second;
            system.update(op);
        }
      #if IS_HYBRID
        system.force_sync();
      #endif
        long ins_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - ins_timer).count();

        // Phase 2: Queries after insert
        long q_ins_time_us = 0;
        if (cfg.num_queries > 0) {
            std::vector<std::pair<node_id_t, node_id_t>> queries(cfg.num_queries);
            std::uniform_int_distribution<node_id_t> node_dist(0, num_nodes - 1);
            for (size_t i = 0; i < cfg.num_queries; ++i) queries[i] = {node_dist(gen), node_dist(gen)};
            bool query_checksum = 0;

            auto q_timer = std::chrono::high_resolution_clock::now();
            for (const auto& q : queries) {
              #if IS_HYBRID
                query_checksum ^= system.connectivity_query(q.first, q.second);
              #else
                query_checksum ^= system.is_connected(q.first, q.second);
              #endif
            }
            q_ins_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - q_timer).count();
            volatile uint64_t escape = query_checksum;
        }

        // Phase 3: Deletes
        long del_time_us = 0;
        if (cfg.do_deletions) {
            std::shuffle(static_edges.begin(), static_edges.end(), gen);
            auto del_timer = std::chrono::high_resolution_clock::now();
            for (const auto& e : static_edges) {
                GraphUpdate op;
                op.type = DELETE;
                op.edge.src = e.first;
                op.edge.dst = e.second;
                system.update(op);
            }
          #if IS_HYBRID
            system.force_sync();
          #endif
            del_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - del_timer).count();
        }

        if (cfg.output_path.empty()) {
            write_static_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, ins_time_us, q_ins_time_us, del_time_us, actual_batch_size, hf, actual_num_tiers);
        } else {
            std::ofstream out(cfg.output_path);
            write_static_speed_tsv(out, cfg, config_name, num_nodes, edgecount, ins_time_us, q_ins_time_us, del_time_us, actual_batch_size, hf, actual_num_tiers);
            write_static_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, ins_time_us, q_ins_time_us, del_time_us, actual_batch_size, hf, actual_num_tiers);
        }
    }

#endif  // !NEEDS_MPI

    // ==================== MPI path ====================
#if NEEDS_MPI
    if (world_rank == 0) {
      #if IS_HYBRID
        BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
      #else
        #if defined(USE_BATCH_INPUT_NODE) && USE_BATCH_INPUT_NODE
        BatchInputNode system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        #else
        InputNode system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        #endif
        system.initialize_all_nodes();
      #endif

        if (!cfg.static_graph) {
            long total_update_time = 0;
            long total_query_time = 0;
            auto update_timer = std::chrono::high_resolution_clock::now();
            auto query_timer = update_timer;
            bool doing_updates = true;
            bool query_checksum = 0;

            // Periodic speed reporting state
            long updates_since_report = 0;
            long current_num_edges = 0;
            auto interval_timer = std::chrono::high_resolution_clock::now();
            std::vector<IntervalRecord> interval_records;

            for (long i = 0; i < edgecount; i++) {
                GraphUpdate operation = stream_ptr->get_edge();
                if (operation.type == 2) {
                    if (doing_updates) {
                        total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - update_timer).count();
                        doing_updates = false;
                        query_timer = std::chrono::high_resolution_clock::now();
                    }
                    query_checksum ^= system.connectivity_query(operation.edge.src, operation.edge.dst);
                } else {
                    if (!doing_updates) {
                        total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - query_timer).count();
                        doing_updates = true;
                        update_timer = std::chrono::high_resolution_clock::now();
                    }
                    system.update(operation);
                    current_num_edges += (operation.type == INSERT) ? 1 : -1;
                    ++updates_since_report;

                    if (cfg.speed_interval > 0 && updates_since_report >= cfg.speed_interval) {
                        auto now = std::chrono::high_resolution_clock::now();
                        long interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - interval_timer).count();
                        double ups = (interval_ms > 0) ? (static_cast<double>(updates_since_report) / interval_ms * 1000.0) : 0;
                        std::cout << "[speed] op " << i << "/" << edgecount
                                  << "  last " << updates_since_report << " updates in " << interval_ms << " ms"
                                  << "  (" << static_cast<long>(ups) << " updates/sec)"
                                  << "  edges=" << current_num_edges;
                      #if IS_HYBRID
                        size_t ise = system.num_sketched_edges();
                        size_t idse = system.num_direct_sketch_edges();
                        std::cout << "  sketched=" << ise << " direct=" << idse;
                      #else
                        size_t ise = 0, idse = 0;
                      #endif
                        std::cout << std::endl;
                        interval_records.push_back({i, updates_since_report, interval_ms, current_num_edges, ise, idse});
                        updates_since_report = 0;
                        interval_timer = now;
                    }
                }
            }
          #if IS_HYBRID
            system.force_sync();
          #endif
            if (doing_updates) total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - update_timer).count();
            else total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - query_timer).count();
            volatile uint64_t escape = query_checksum;

          #if IS_HYBRID
            size_t se = system.num_sketched_edges();
            size_t dse = system.num_direct_sketch_edges();
          #else
            size_t se = 0, dse = 0;
          #endif

            if (cfg.output_path.empty()) {
                write_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers, current_num_edges, se, dse);
            } else {
                std::ofstream out(cfg.output_path);
                write_speed_tsv(out, cfg, config_name, num_nodes, edgecount, total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers, current_num_edges, se, dse);
                write_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers, current_num_edges, se, dse);
                if (!interval_records.empty()) {
                    write_intervals_tsv(intervals_path_from(cfg.output_path), cfg.stream_path, config_name, interval_records);
                }
            }

        } else {
            // === STATIC GRAPH MODE (MPI) ===
            std::mt19937 gen(seed);
            
            std::shuffle(static_edges.begin(), static_edges.end(), gen);

            auto ins_timer = std::chrono::high_resolution_clock::now();
            for (const auto& e : static_edges) {
                GraphUpdate op;
                op.type = INSERT;
                op.edge.src = e.first;
                op.edge.dst = e.second;
                system.update(op);
            }
          #if IS_HYBRID
            system.force_sync();
          #endif
            long ins_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - ins_timer).count();
            bool query_checksum = 0;

            long q_ins_time_us = 0;
            if (cfg.num_queries > 0) {
                std::vector<std::pair<node_id_t, node_id_t>> queries(cfg.num_queries);
                std::uniform_int_distribution<node_id_t> node_dist(0, num_nodes - 1);
                for (size_t i = 0; i < cfg.num_queries; ++i) queries[i] = {node_dist(gen), node_dist(gen)};
                
                auto q_timer = std::chrono::high_resolution_clock::now();
                for (const auto& q : queries) query_checksum ^= system.connectivity_query(q.first, q.second);
                q_ins_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - q_timer).count();
            }
            volatile uint64_t escape = query_checksum;

            long del_time_us = 0;
            if (cfg.do_deletions) {
                std::shuffle(static_edges.begin(), static_edges.end(), gen);
                auto del_timer = std::chrono::high_resolution_clock::now();
                for (const auto& e : static_edges) {
                    GraphUpdate op;
                    op.type = DELETE;
                    op.edge.src = e.first;
                    op.edge.dst = e.second;
                    system.update(op);
                }
              #if IS_HYBRID
                system.force_sync();
              #endif
                del_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - del_timer).count();
            }

            if (cfg.output_path.empty()) {
                write_static_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, ins_time_us, q_ins_time_us, del_time_us, actual_batch_size, hf, actual_num_tiers);
            } else {
                std::ofstream out(cfg.output_path);
                write_static_speed_tsv(out, cfg, config_name, num_nodes, edgecount, ins_time_us, q_ins_time_us, del_time_us, actual_batch_size, hf, actual_num_tiers);
                write_static_speed_report(std::cout, cfg, config_name, num_nodes, edgecount, ins_time_us, q_ins_time_us, del_time_us, actual_batch_size, hf, actual_num_tiers);
            }
        }

      #if IS_HYBRID
        system.end();
      #else
        system.end();
      #endif

    } else if (world_rank <= (int)actual_num_tiers) {
        #if defined(USE_BATCH_INPUT_NODE) && USE_BATCH_INPUT_NODE
        BatchTierNode<CUTSET_TYPE> tier_node(num_nodes, world_rank - 1, actual_num_tiers, actual_batch_size, tier_seed);
        #else
        TierNode<CUTSET_TYPE> tier_node(num_nodes, world_rank - 1, actual_num_tiers, actual_batch_size, tier_seed);
        #endif
        tier_node.main();
    }

    MPI_Finalize();
#endif  // NEEDS_MPI

    if (stream_ptr) delete stream_ptr;
    return 0;
}
