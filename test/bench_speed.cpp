/**
 * bench_speed.cpp — Standalone speed benchmark harness.
 *
 * Compile-time configuration via CMake -D flags:
 *   CUPCAKE_ALGO:   graph_tiers | batch_tiers | mpi_tiers
 *   CUTSET_DS:   ett | lct | ufo
 *   USE_HYBRID:  0 | 1
 *
 * Runtime parameters (CLI):
 *   <stream_path> [--batch-size N] [--height-factor F] [--num-tiers N] [--output path.tsv]
 *
 * For MPI configs, launch via mpirun.
 */

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

#include "binary_graph_stream.h"
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

#if defined(USE_HYBRID) && USE_HYBRID
  #include "mpi_hybrid_conn.h"
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
  // MPI: InputNode + TierNode<CUTSET_TYPE>
  using BenchTierNode = TierNode<CUTSET_TYPE>;
  #define TIER_NAME "mpi_tiers"
  #define NEEDS_MPI 1
  #define IS_HYBRID 0
#elif defined(CUPCAKE_ALGO_BATCH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
  using BenchSystem = HybridConnectivityManager<BatchTiers<CUTSET_TYPE>>;
  #define TIER_NAME "batch_tiers"
  #define NEEDS_MPI 0
  #define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_GRAPH_TIERS) && defined(USE_HYBRID) && USE_HYBRID
  using BenchSystem = HybridConnectivityManager<GraphTiers<CUTSET_TYPE>>;
  #define TIER_NAME "graph_tiers"
  #define NEEDS_MPI 0
  #define IS_HYBRID 1
#elif defined(CUPCAKE_ALGO_MPI_TIERS) && defined(USE_HYBRID) && USE_HYBRID
  // Hybrid MPI: HybridConnectivityManager<InputNode>
  using BenchSystem = HybridConnectivityManager<>;
  #define TIER_NAME "mpi_tiers"
  #define NEEDS_MPI 1
  #define IS_HYBRID 1
#else
  #error "Must define one of CUPCAKE_ALGO_GRAPH_TIERS, CUPCAKE_ALGO_BATCH_TIERS, CUPCAKE_ALGO_MPI_TIERS"
#endif

#ifdef USE_RESIZEABLE_SKETCH
  #define SKETCH_NAME "resizeable"
#else
  #define SKETCH_NAME "fixed"
#endif

// ========== Globals required by library (extern in headers) ==========
// NOTE: sketch_len, sketch_err, height_factor are defined in skiplist.cpp
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
    int batch_size = 0;       // 0 = use default
    double height_factor = 0; // 0 = auto-compute
    int num_tiers = 0;        // 0 = auto-compute
    int hybrid_threshold = 0; // 0 = use default
    std::string output_path;  // empty = stdout
};

static BenchConfig parse_args(int argc, char** argv) {
    BenchConfig cfg;
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <stream_path> [--batch-size N] [--height-factor F] "
                     "[--num-tiers N] [--hybrid-threshold N] [--output path.tsv]"
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
    }
    return cfg;
}

static void write_speed_tsv(std::ostream& out, const BenchConfig& cfg,
                            const std::string& config_name, node_id_t num_nodes,
                            long total_ops, long update_time_us, long query_time_us,
                            int actual_batch_size, double actual_height_factor,
                            int actual_num_tiers) {
    // Header
    out << "stream\tconfig\tnum_nodes\ttotal_ops\tupdate_time_ms\tquery_time_ms\t"
           "updates_per_sec\tqueries_per_sec\tbatch_size\theight_factor\tnum_tiers"
        << std::endl;
    long update_ms = update_time_us / 1000;
    long query_ms = query_time_us / 1000;
    // Estimate 90% updates, 10% queries (matches stream format)
    long est_updates = static_cast<long>(0.9 * total_ops);
    long est_queries = total_ops - est_updates;
    double ups = (update_ms > 0) ? (static_cast<double>(est_updates) / update_ms * 1000.0) : 0;
    double qps = (query_ms > 0) ? (static_cast<double>(est_queries) / query_ms * 1000.0) : 0;

    out << basename_of(cfg.stream_path) << "\t"
        << config_name << "\t"
        << num_nodes << "\t"
        << total_ops << "\t"
        << update_ms << "\t"
        << query_ms << "\t"
        << static_cast<long>(ups) << "\t"
        << static_cast<long>(qps) << "\t"
        << actual_batch_size << "\t"
        << actual_height_factor << "\t"
        << actual_num_tiers
        << std::endl;
}

// ========== Main ==========
int main(int argc, char** argv) {
#if NEEDS_MPI
    MPI_Init(&argc, &argv);
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
#endif

    BenchConfig cfg = parse_args(argc, argv);
    std::string config_name = std::string(TIER_NAME) + "_" + CUTSET_NAME + "_" + SKETCH_NAME;
    if (IS_HYBRID) config_name += "_hybrid";

    BinaryGraphStream stream(cfg.stream_path, 100000);
    node_id_t num_nodes = stream.nodes();
    long edgecount = stream.edges();

    // Resolve defaults
    double hf = (cfg.height_factor > 0) ? cfg.height_factor : 1.0 / log2(log2(num_nodes));
    height_factor = hf;
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = 1;

    int actual_batch_size = (cfg.batch_size > 0) ? cfg.batch_size : 100;

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
    // Broadcast seed
    int seed_int = static_cast<int>(seed);
    bcast(&seed_int, sizeof(int), 0);
    seed = seed_int;
    rng.seed(seed);
    for (int i = 0; i < world_rank; i++) dist(rng);
#if NEEDS_MPI
    int tier_seed = dist(rng);
#else
    int tier_seed = 0; // Unused for non-MPI but required by compiler scope for other unused statements
#endif
#endif

    // ==================== Shmem / Hybrid-Shmem path ====================
#if !NEEDS_MPI
    (void)actual_batch_size; // may be unused for GraphTiers

  #if IS_HYBRID
    BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
    if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
  #elif defined(CUPCAKE_ALGO_BATCH_TIERS)
    BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
  #else
    BenchSystem system(num_nodes, seed);
    system.initialize_all_nodes();
  #endif

    long total_update_time = 0;
    long total_query_time = 0;
    auto update_timer = std::chrono::high_resolution_clock::now();
    auto query_timer = update_timer;
    bool doing_updates = true;

    for (long i = 0; i < edgecount; i++) {
        GraphUpdate operation = stream.get_edge();
        if (operation.type == 2) {  // query
            if (doing_updates) {
                total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - update_timer).count();
                doing_updates = false;
                query_timer = std::chrono::high_resolution_clock::now();
            }
          #if IS_HYBRID
            system.connectivity_query(operation.edge.src, operation.edge.dst);
          #else
            system.is_connected(operation.edge.src, operation.edge.dst);
          #endif
        } else {
            if (!doing_updates) {
                total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - query_timer).count();
                doing_updates = true;
                update_timer = std::chrono::high_resolution_clock::now();
            }
          #if IS_HYBRID
            system.update(operation);
          #else
            system.update(operation);
          #endif
        }
    }
    // Flush remaining timer
    if (doing_updates) {
        total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - update_timer).count();
    } else {
        total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - query_timer).count();
    }

    // Output
    if (cfg.output_path.empty()) {
        write_speed_tsv(std::cout, cfg, config_name, num_nodes, edgecount,
                        total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers);
    } else {
        std::ofstream out(cfg.output_path);
        write_speed_tsv(out, cfg, config_name, num_nodes, edgecount,
                        total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers);
    }

#endif  // !NEEDS_MPI

    // ==================== MPI path ====================
#if NEEDS_MPI
    if (world_rank == 0) {
      #if IS_HYBRID
        BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
      #else
        InputNode system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        system.initialize_all_nodes();
      #endif

        long total_update_time = 0;
        long total_query_time = 0;
        auto update_timer = std::chrono::high_resolution_clock::now();
        auto query_timer = update_timer;
        bool doing_updates = true;

        for (long i = 0; i < edgecount; i++) {
            GraphUpdate operation = stream.get_edge();
            if (operation.type == 2) {
                if (doing_updates) {
                    total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::high_resolution_clock::now() - update_timer).count();
                    doing_updates = false;
                    query_timer = std::chrono::high_resolution_clock::now();
                }
                system.connectivity_query(operation.edge.src, operation.edge.dst);
            } else {
                if (!doing_updates) {
                    total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::high_resolution_clock::now() - query_timer).count();
                    doing_updates = true;
                    update_timer = std::chrono::high_resolution_clock::now();
                }
                system.update(operation);
            }
        }
        if (doing_updates) {
            total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - update_timer).count();
        } else {
            total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - query_timer).count();
        }

      #if IS_HYBRID
        system.sketching_algo.end();
      #else
        system.end();
      #endif

        if (cfg.output_path.empty()) {
            write_speed_tsv(std::cout, cfg, config_name, num_nodes, edgecount,
                            total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers);
        } else {
            std::ofstream out(cfg.output_path);
            write_speed_tsv(out, cfg, config_name, num_nodes, edgecount,
                            total_update_time, total_query_time, actual_batch_size, hf, actual_num_tiers);
        }
    } else if (world_rank <= (int)actual_num_tiers) {
        TierNode<CUTSET_TYPE> tier_node(num_nodes, world_rank - 1, actual_num_tiers, actual_batch_size, tier_seed);
        tier_node.main();
    }

    MPI_Finalize();
#endif  // NEEDS_MPI

    return 0;
}
