/**
 * bench_profile.cpp — Standalone profile benchmark harness.
 *
 * Periodically reports per-tier space usage and tracks maximum tier used.
 * Same compile-time configuration as bench_speed.cpp.
 *
 * Runtime parameters (CLI):
 *   <stream_path> [--batch-size N] [--height-factor F] [--num-tiers N]
 *                 [--output-dir dir] [--report-interval N]
 */

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

struct ProfileConfig {
    std::string stream_path;
    int batch_size = 0;
    double height_factor = 0;
    int num_tiers = 0;
    int hybrid_threshold = 0;
  int recovery_size = 0;
    int move_to_sketch = 0;
    std::string output_dir = "results/profile";
    long report_interval = 1000000;
};

static ProfileConfig parse_args(int argc, char** argv) {
    ProfileConfig cfg;
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <stream_path> [--batch-size N] [--height-factor F] "
                  "[--num-tiers N] [--hybrid-threshold N] [--recovery-size N] [--move-to-sketch N] "
                     "[--output-dir dir] [--report-interval N]"
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
        else if (arg == "--recovery-size" && i + 1 < argc) cfg.recovery_size = std::atoi(argv[++i]);
        else if (arg == "--move-to-sketch" && i + 1 < argc) cfg.move_to_sketch = std::atoi(argv[++i]);
        else if (arg == "--output-dir" && i + 1 < argc) cfg.output_dir = argv[++i];
        else if (arg == "--report-interval" && i + 1 < argc) cfg.report_interval = std::atol(argv[++i]);
    }
    return cfg;
}


/**
 * Access report_space_usage depending on system type.
 * For hybrid systems, it's on the sketching_algo member.
 * For bare systems, it's directly on the system.
 */
template<typename T>
static auto get_space_reports(T& system) {
    return system.report_space_usage();
}

// ========== Main ==========
int main(int argc, char** argv) {
#if NEEDS_MPI
    MPI_Init(&argc, &argv);
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
#endif

    ProfileConfig cfg = parse_args(argc, argv);
    std::string config_name = std::string(TIER_NAME) + "_" + CUTSET_NAME + "_" + SKETCH_NAME;
    if (IS_HYBRID) config_name += "_hybrid";

    // Create output directory
    std::string stream_basename = basename_of(cfg.stream_path);
    std::string out_dir = cfg.output_dir;

#if NEEDS_MPI
    int do_mkdir = 0;
  #if NEEDS_MPI
    do_mkdir = (world_rank == 0) ? 1 : 0;
  #endif
    if (do_mkdir)
#endif
    {
        std::filesystem::create_directories(out_dir);
    }

    std::string space_file = out_dir + "/" + stream_basename + "_space.tsv";
    std::string summary_file = out_dir + "/" + stream_basename + "_summary.tsv";

    BinaryGraphStream stream(cfg.stream_path, 100000);
    node_id_t num_nodes = stream.nodes();
    long edgecount = stream.edges();

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
#if NEEDS_MPI
    int tier_seed = dist(rng);
#else
    int tier_seed = 0; // Unused for non-MPI but required by compiler scope for other unused statements
#endif
#endif

    // Lambda for the profiling loop (shared between shmem and MPI rank 0)
    auto run_profile = [&](auto& system) {
        int max_maximal_tier = -1;
        bool first_report = true;
        auto wall_start = std::chrono::high_resolution_clock::now();

        for (long i = 0; i < edgecount; i++) {
            GraphUpdate operation = stream.get_edge();
            if (operation.type == 2) {
                // Skip queries in profile mode — we only care about updates
                continue;
            }
#if IS_HYBRID
            system.update(operation);
#else
            system.update(operation);
#endif

            if (i > 0 && (i % cfg.report_interval == 0 || i == edgecount - 1)) {
                auto reports = get_space_reports(system);
                write_space_report_tsv(reports, space_file, !first_report, i);
                first_report = false;

                int maximal = compute_first_maximal_tier(reports);
                if (maximal > max_maximal_tier) {
                    max_maximal_tier = maximal;
                }

                std::cout << "Profile update " << i << "/" << edgecount
                          << " max_tier=" << max_maximal_tier
                          << " tree_ops=" << system.get_num_tree_ops() << std::endl;
            }
        }

        auto wall_end = std::chrono::high_resolution_clock::now();
        long wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wall_end - wall_start).count();
        long final_tree_ops = system.get_num_tree_ops();

        // Write summary
        std::ofstream summary(summary_file);
        summary << "stream\tconfig\tmax_tier\twall_time_ms\tbatch_size\theight_factor\tnum_tiers\ttree_ops" << std::endl;
        summary << stream_basename << "\t"
                << config_name << "\t"
                << max_maximal_tier << "\t"
                << wall_ms << "\t"
                << actual_batch_size << "\t"
                << hf << "\t"
                << actual_num_tiers << "\t"
                << final_tree_ops << std::endl;

        std::cout << "Profile complete. max_tier=" << max_maximal_tier
                  << " wall_time=" << wall_ms << "ms"
                  << " tree_ops=" << final_tree_ops << std::endl;
        std::cout << "Space file: " << space_file << std::endl;
        std::cout << "Summary file: " << summary_file << std::endl;
    };

    // ==================== Shmem / Hybrid-Shmem path ====================
#if !NEEDS_MPI
  #if IS_HYBRID
    BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
    if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
    if (cfg.recovery_size > 0) system.set_recovery_size(cfg.recovery_size);
    if (cfg.move_to_sketch > 0) system.set_move_to_sketch(cfg.move_to_sketch);
  #elif defined(CUPCAKE_ALGO_BATCH_TIERS)
    BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
  #else
    BenchSystem system(num_nodes, seed);
    system.initialize_all_nodes();
  #endif

    run_profile(system);
#endif

    // ==================== MPI path ====================
#if NEEDS_MPI
    if (world_rank == 0) {
      #if IS_HYBRID
        BenchSystem system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        if (cfg.hybrid_threshold > 0) system.set_threshold(cfg.hybrid_threshold);
        if (cfg.recovery_size > 0) system.set_recovery_size(cfg.recovery_size);
        if (cfg.move_to_sketch > 0) system.set_move_to_sketch(cfg.move_to_sketch);
      #else
        #if defined(USE_BATCH_INPUT_NODE) && USE_BATCH_INPUT_NODE
        BatchInputNode system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        #else
        InputNode system(num_nodes, actual_num_tiers, actual_batch_size, seed);
        #endif
        system.initialize_all_nodes();
      #endif

        run_profile(system);

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
#endif

    return 0;
}
