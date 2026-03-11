#include <gtest/gtest.h>
#include <chrono>
#include <signal.h>
#include <unordered_map>
#include <random>
#include <iostream>
#include <fstream>
#include <cstdlib>
// #include <omp.h>
#include "mpi_nodes.h"
#include "binary_graph_stream.h"
// #include "mat_graph_verifier.h"
#include "graph_verifier.h"
#include "serial_hybrid_conn.h"
#include "parallel_hybrid_conn.h"
#include "util.h"


const int DEFAULT_BATCH_SIZE = 100;
const int DEFAULT_HYBRID_THRESHOLD = 100;
const vec_t DEFAULT_SKETCH_ERR = 1;

using TierNodeSystem = TierNode<EulerTourTree<DefaultSketchColumn>>;
// using TierNodeSystem = TierNode<cutset_lct::CutsetLCT<DefaultSketchColumn>>;
// using TierNodeSystem = TierNode<ufo::CutsetUFOTree<DefaultSketchColumn>>;

using HybridManagerType = ParallelConnectivityManager<InputNode>;
// using HybridManagerType = SerialConnectivityManager<InputNode>;

static long force_sync_interval() {
    const char* interval_env = std::getenv("FORCE_SYNC_INTERVAL");
    return interval_env ? std::atol(interval_env) : 1000000;
}

// TEST(GraphTierSuite, hybrid_mixed_speed_test) {
//     int world_rank_buf;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
//     uint32_t world_rank = world_rank_buf;
//     int world_size_buf;
//     MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
//     uint32_t world_size = world_size_buf;

//     BinaryGraphStream stream(stream_file, 100000);
//     uint32_t num_nodes = stream.nodes();
//     uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);

//     // Parameters
//     int update_batch_size = (batch_size_arg==0) ? DEFAULT_BATCH_SIZE : batch_size_arg;
//     height_factor = (height_factor_arg==0) ? 1./log2(log2(num_nodes)) : height_factor_arg;
//     sketchless_height_factor = height_factor;
//     sketch_len = Sketch::calc_vector_length(num_nodes);
// 	sketch_err = DEFAULT_SKETCH_ERR;

//     std::cout << "BATCH SIZE: " << update_batch_size << " HEIGHT FACTOR " << height_factor << " SKETCH BUFFER: " << SKETCH_BUFFER_SIZE << std::endl;

//     // Seeds
//     std::random_device dev;
//     std::mt19937 rng(dev());
//     std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
//     int seed = dist(rng);
//     bcast(&seed, sizeof(int), 0);
//     std::cout << "SEED: " << seed << std::endl;
//     rng.seed(seed);
//     for (int i = 0; i < world_rank; i++)
//         dist(rng);
//     int tier_seed = dist(rng);

//     if (world_size != num_tiers+1)
//         FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;

//     if (world_rank == 0) {
//         int seed = time(NULL);
//         srand(seed);
//         std::cout << "InputNode seed: " << seed << std::endl;
//         InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
//         long edgecount = stream.edges();
//         // long count = 100000000;
//         // edgecount = std::min(edgecount, count);
//         long total_update_time = 0;
//         long total_query_time = 0;
//         auto update_timer = std::chrono::high_resolution_clock::now();
//         auto query_timer = update_timer;
//         bool doing_updates = true;
//         for (long i = 0; i < edgecount; i++) {
//             // Read an update from the stream and have the input node process it
//             GraphUpdate operation = stream.get_edge();
//             if (operation.type == 2) { // 2 is the symbol for queries
//                 unlikely_if (doing_updates) {
//                     total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - update_timer).count();
//                     doing_updates = false;
//                     query_timer = std::chrono::high_resolution_clock::now();
//                 }
//                 input_node.connectivity_query(operation.edge.src, operation.edge.dst);
//             } else {
//                 unlikely_if (!doing_updates) {
//                     total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - query_timer).count();
//                     doing_updates = true;
//                     update_timer = std::chrono::high_resolution_clock::now();
//                 }
//                 input_node.update(operation);
//             }
//             unlikely_if(i%1000000 == 0 || i == edgecount-1) {
//                 std::cout << "FINISHED OPERATION " << i << " OUT OF " << edgecount << " IN " << stream_file << std::endl;
//             }
//         }
//         if (doing_updates) {
//             total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - update_timer).count();
//         } else {
//             total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - query_timer).count();
//         }
//         // Communicate to all other nodes that the stream has ended
//         input_node.end();
//         std::cout << "Total update time(ms):   " << (total_update_time/1000) << std::endl;
//         std::cout << "Total query time(ms):    " << (total_query_time/1000) << std::endl;
//         std::cout << "Total time(ms):    " << (total_query_time + total_update_time)/1000 << std::endl;

//         std::ofstream file;
//         std::string out_file = "./../results/mpi_speed_results/" + stream_file.substr(stream_file.find("/") + 1) + ".txt";
//         std::cout << "WRITING RESULTS TO " << out_file << std::endl;
//         file.open (out_file, std::ios_base::app);
//         file << " UPDATES/SECOND: " << (0.9*edgecount)/(total_update_time) << std::endl;
//         file << " QUERIES/SECOND: " << (0.1*edgecount)/(total_query_time) << std::endl;
//         file.close();

//     } else if (world_rank < num_tiers+1) {
//         int tier_num = world_rank-1;
//         TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
//         tier_node.main();
//     }
// }

TEST(GraphTierSuite, hybrid_update_speed_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    BinaryGraphStream stream(stream_file, 100000);
    uint32_t num_nodes = stream.nodes();
    uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);

    // Parameters
    int update_batch_size = (batch_size_arg==0) ? DEFAULT_BATCH_SIZE : batch_size_arg;
    int threshold = (hybrid_threshold_arg==0) ? DEFAULT_HYBRID_THRESHOLD : hybrid_threshold_arg;
    height_factor = (height_factor_arg==0) ? 1./log2(log2(num_nodes)) : height_factor_arg;
    sketchless_height_factor = height_factor;
    sketch_len = Sketch::calc_vector_length(num_nodes);
	sketch_err = DEFAULT_SKETCH_ERR;

    std::cout << "BATCH SIZE: " << update_batch_size << " HEIGHT FACTOR " << height_factor << std::endl;

    // Seeds
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    int seed = dist(rng);
    bcast(&seed, sizeof(int), 0);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
    for (int i = 0; i < world_rank; i++)
        dist(rng);
    int tier_seed = dist(rng);

    if (world_size != num_tiers+1)
        FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "InputNode seed: " << seed << std::endl;
        // InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        HybridManagerType hybrid_manager(
            num_nodes, num_tiers, update_batch_size, seed
        );
        hybrid_manager.set_threshold(threshold);
        long edgecount = stream.edges();
        // long count = 100000000;
        // edgecount = std::min(edgecount, count);
        auto X = std::chrono::high_resolution_clock::now();
        for (long i = 0; i < edgecount; i++) {
            // Read an update from the stream and have the input node process it
            GraphUpdate update = stream.get_edge();
            hybrid_manager.update(update);
            unlikely_if(i%1000000 == 0 || i == edgecount-1) {
                std::cout << "FINISHED UPDATE " << i << " OUT OF " << edgecount << " IN " << stream_file << std::endl;
                std::cout << "Sketched nodes: " << hybrid_manager.num_sketched_vertices() << " out of " << num_nodes << std::endl;
                std::cout << "-  Total edges: " << hybrid_manager.total_edges() << std::endl;
                std::cout << "-  Sketched edges: " << hybrid_manager.num_sketched_edges() << std::endl;
                double percent_sketched = 100.0 * ((double)hybrid_manager.num_sketched_edges()) / ((double)hybrid_manager.total_edges());
                std::cout << "-  Percent sketched edges: " << percent_sketched << "%" << std::endl;
                // std::cout << "-  Direct sketched edges: " << hybrid_manager.num_direct_sketch_edges() << std::endl;
                double percent_direct = 100.0 * ((double)hybrid_manager.num_direct_sketch_edges()) / ((double)hybrid_manager.total_edges());
                std::cout << "-  Percent direct sketched edges: " << percent_direct << "%" << std::endl;
            }
        }
        // Communicate to all other nodes that the stream has ended
        hybrid_manager.end();
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - X).count();
        std::cout << "Total time(ms): " << (time/1000) << std::endl;

        std::ofstream file;
        file.open ("./../results/mpi_update_results.txt", std::ios_base::app);
        file << stream_file << " UPDATES/SECOND: " << edgecount/(time/1000)*1000 << std::endl;
        file.close();

    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(GraphTiersSuite, hybrid_query_speed_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    BinaryGraphStream stream(stream_file, 1000000);
    uint32_t num_nodes = stream.nodes();
    // uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
    uint32_t num_tiers = world_size-1;
    int nodecount = stream.nodes();
    int edgecount = stream.edges();
    if (edgecount > 100000000) edgecount = 100000000;

    // Parameters
    int update_batch_size = (batch_size_arg==0) ? DEFAULT_BATCH_SIZE : batch_size_arg;
    int threshold = (hybrid_threshold_arg==0) ? DEFAULT_HYBRID_THRESHOLD : hybrid_threshold_arg;
    height_factor = (height_factor_arg==0) ? 1./log2(log2(num_nodes)) : height_factor_arg;
	sketchless_height_factor = height_factor;
    sketch_len = Sketch::calc_vector_length(num_nodes);
	sketch_err = DEFAULT_SKETCH_ERR;

    // Seeds
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    int seed = dist(rng);
    bcast(&seed, sizeof(int), 0);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
    for (int i = 0; i < world_rank; i++)
        dist(rng);
    int tier_seed = dist(rng);

    // if (world_size != num_tiers+1)
    //     FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "InputNode seed: " << seed << std::endl;
        // InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        HybridManagerType hybrid_driver(
            num_nodes, num_tiers, update_batch_size, seed
        );
        hybrid_driver.set_threshold(threshold);

        long total_time = 0;
        for (int batch = 0; batch < 10; batch++) {
            std::cout << stream_file << " update batch " << batch <<  std::endl;
            for (int i = 0; i < edgecount/10; i++) {
                GraphUpdate update = stream.get_edge();
                hybrid_driver.update(update);
            }

            long querycount = 100000000;

            std::cout << "Performing queries..." << std::endl;
            auto X = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < querycount; i++) {
                hybrid_driver.connectivity_query(rand()%nodecount, rand()%nodecount);
            }
            auto time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - X).count();
            std::cout << querycount << " Connectivity Queries, Time (ms):  " << time/1000 << std::endl;
            total_time += time;
        }
        hybrid_driver.end();

        std::cout << "TOTAL TIME(ms): " << total_time/1000 << std::endl;
        std::cout << "QUERIES/SECOND: " << 1000000000/(total_time/1000)*1000 << std::endl;
        std::ofstream file;
        file.open ("./../results/mpi_query_results.txt", std::ios_base::app);
        file << stream_file << " QUERIES/SECOND: " << 1000000000/(total_time/1000)*1000 << std::endl;
        file.close();

    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        TierNodeSystem tier_node(num_nodes, world_rank-1, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(GraphTierSuite, hybrid_memory_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    BinaryGraphStream stream(stream_file, 100000);
    uint32_t num_nodes = stream.nodes();
    // uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
    std::cout << "Theory-informed number of tiers: " << log2(num_nodes)/(log2(3)-1) << std::endl;
    // TEMPORARY CHANGE - MAKE THE USER DECIDE WORLD SIZE
    uint32_t num_tiers = world_size-1;
    std::cout << "Using number of tiers: " << num_tiers << std::endl;

    // Parameters
    int update_batch_size = (batch_size_arg==0) ? DEFAULT_BATCH_SIZE : batch_size_arg;
    int threshold = (hybrid_threshold_arg==0) ? DEFAULT_HYBRID_THRESHOLD : hybrid_threshold_arg;
    height_factor = (height_factor_arg==0) ? 1./log2(log2(num_nodes)) : height_factor_arg;
    sketchless_height_factor = height_factor;
    sketch_len = Sketch::calc_vector_length(num_nodes);
	sketch_err = DEFAULT_SKETCH_ERR;

    std::cout << "BATCH SIZE: " << update_batch_size << " HEIGHT FACTOR " << height_factor << std::endl;

    // Seeds
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    int seed = dist(rng);
    bcast(&seed, sizeof(int), 0);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
    for (uint32_t i = 0; i < world_rank; i++)
        dist(rng);
    int tier_seed = dist(rng);

    if (world_size != num_tiers+1)
        FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "InputNode seed: " << seed << std::endl;
        // InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        HybridManagerType hybrid_manager(
            num_nodes, num_tiers, update_batch_size, seed
        );
        hybrid_manager.set_threshold(threshold);

        const char* interval_env = std::getenv("MEMORY_REPORT_INTERVAL");
        long report_interval = interval_env ? std::atol(interval_env) : 1000000;
        const char* file_env = std::getenv("MEMORY_REPORT_FILE");
        std::string report_file = file_env ? std::string(file_env) : "memory_report.tsv";
        bool first_report = true;

        long edgecount = stream.edges();
        // long count = 100000000;
        // edgecount = std::min(edgecount, count);
        auto X = std::chrono::high_resolution_clock::now();
        for (long i = 0; i < edgecount; i++) {
            // Read an update from the stream and have the input node process it
            GraphUpdate update = stream.get_edge();
            hybrid_manager.update(update);
            unlikely_if (i > 0 && (i % report_interval == 0 || i == edgecount-1)) {
                auto stop = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - X);
                std::cout << "FINISHED UPDATE " << i << " OUT OF " << edgecount << " IN " << stream_file << std::endl;
                
                // Print TSV to file
                auto reports = hybrid_manager.report_space_usage();
                write_space_report_tsv(reports, report_file, !first_report, i);
                first_report = false;
                
                std::cout << "--- SPACE_REPORT operation=" << i << " ---" << std::endl;
                // hybrid_manager.sketching_algo.report_space_usage_tsv(std::cout); // Original line, commented out
                std::cout << "--- END_SPACE_REPORT ---" << std::endl;
            if (i%20000000 == 0 || i == edgecount-1) {
                std::cout << "Sketched nodes: " << hybrid_manager.sketched_node_count() << " out of " << stream.nodes() << std::endl;
                std::cout << "-  Space usage of CF: " << hybrid_manager.get_space_usage_cf()/(1024*1024) << " MB" << std::endl;
                std::cout << "-  Space usage of Driver: " << hybrid_manager.get_space_usage_driver()/(1024*1024) << " MB" << std::endl;
                std::cout << "-  Space usage of Sketches: " << hybrid_manager.space_usage_conn_sketch()/(1024*1024) << " MB" << std::endl;
                std::cout << "-  Space usage of Recovery Sketches: " << hybrid_manager.space_usage_recovery_sketch()/(1024*1024) << " MB" << std::endl;
                std::cout << "-  Total edges: " << hybrid_manager.total_edges() << std::endl;
                std::cout << "-  Sketched edges: " << hybrid_manager.num_sketched_edges() << std::endl;
                double percent_sketched = 100.0 * ((double)hybrid_manager.num_sketched_edges()) / ((double)hybrid_manager.total_edges());
                std::cout << "-  Percent sketched edges: " << percent_sketched << "%" << std::endl;
                std::cout << "-  Direct sketched edges: " << hybrid_manager.num_direct_sketch_edges() << std::endl;
                double percent_direct = 100.0 * ((double)hybrid_manager.num_direct_sketch_edges()) / ((double)hybrid_manager.total_edges());
                std::cout << "-  Percent direct sketched edges: " << percent_direct << "%" << std::endl;
            }
            }
        }
        // Communicate to all other nodes that the stream has ended
        hybrid_manager.end();
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - X).count();
        std::cout << "Total time(ms): " << (time/1000) << std::endl;

        std::ofstream file;
        file.open ("./../results/mpi_update_results.txt", std::ios_base::app);
        file << stream_file << " UPDATES/SECOND: " << edgecount/(time/1000)*1000 << std::endl;
        file.close();

    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(GraphTiersSuite, hybrid_mini_correctness_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    uint32_t num_nodes = 100;
    uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
    if (world_size != num_tiers+1)
        FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;
    // Parameters
    int update_batch_size = 1;
    height_factor = 1;
    sketch_len = Sketch::calc_vector_length(num_nodes);
	sketch_err = DEFAULT_SKETCH_ERR;

    // Seeds
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    int seed = dist(rng);
    bcast(&seed, sizeof(int), 0);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
    for (int i = 0; i < world_rank; i++)
        dist(rng);
    int tier_seed = dist(rng);

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "InputNode seed: " << seed << std::endl;
        // InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        // 
        HybridManagerType hybrid_driver(
            num_nodes, num_tiers, update_batch_size, seed
        );
        GraphVerifier gv(num_nodes);
        // Link all of the nodes into 1 connected component
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            hybrid_driver.update({{i, i+1}, INSERT});
            gv.edge_update({i,i+1});
            std::cout << "Attempting query" << std::endl;
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << num_nodes-i-1 << " components" << std::endl;
                FAIL();
            }
        }
        // One by one cut all of the nodes into singletons
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            hybrid_driver.update({{i, i+1}, DELETE});
            gv.edge_update({i,i+1});
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after cutting nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << i+2 << " components" << std::endl;
                FAIL();
            }
        }
        // Communicate to all other nodes that the stream has ended
        hybrid_driver.end();
    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(GraphTiersSuite, hybrid_small_correctness_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    uint32_t num_nodes = 512;

    uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
    if (world_size != num_tiers+1)
        FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;
    // Parameters
    int update_batch_size = 1;
    height_factor = 1;
    sketch_len = Sketch::calc_vector_length(num_nodes);
	sketch_err = DEFAULT_SKETCH_ERR;

    // Seeds
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    int seed = dist(rng);
    bcast(&seed, sizeof(int), 0);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
    for (int i = 0; i < world_rank; i++)
        dist(rng);
    int tier_seed = dist(rng);

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "InputNode seed: " << seed << std::endl;
        // InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        // 
        HybridManagerType hybrid_driver(
            num_nodes, num_tiers, update_batch_size, seed
        );
        hybrid_driver.set_threshold(10);
        GraphVerifier gv(num_nodes);
        // Link all of the nodes into 1 connected component
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            hybrid_driver.update({{i, i+1}, INSERT});
            gv.edge_update({i,i+1});
            // std::cout << "Attempting query" << std::endl;
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << num_nodes-i-1 << " components" << std::endl;
                FAIL();
            }
        }
        // augment first few nodes so that they are hubs for the first half of the nodes.
        node_id_t hub_nodes = 25;
        for (node_id_t i=0; i < hub_nodes; i++) {
            // don't insert any edges that already exist: 
            for (node_id_t j = hub_nodes+2; j < num_nodes/2; j++) {
                hybrid_driver.update({{i, j}, INSERT});
                gv.edge_update({i,j});
            }
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after cutting nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << i+2 << " components" << std::endl;
                FAIL();
            }
        }
        std::cout << "Number of sketched nodes: " << hybrid_driver.num_sketched_vertices() << std::endl;
        for (node_id_t i=0; i < hub_nodes; i++) {
            for (node_id_t j = hub_nodes+2; j < num_nodes/2; j++) {
                hybrid_driver.update({{i, j}, DELETE});
                gv.edge_update({i,j});
            }
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after cutting nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << i+2 << " components" << std::endl;
                FAIL();
            }
        }
        std::cout << "Number of sketched nodes: " << hybrid_driver.num_sketched_vertices() << std::endl;
        
        // One by one cut all of the nodes into singletons
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            hybrid_driver.update({{i, i+1}, DELETE});
            gv.edge_update({i,i+1});
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after cutting nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << i+2 << " components" << std::endl;
                FAIL();
            }
        }
        // Communicate to all other nodes that the stream has ended
        hybrid_driver.end();
    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

// TEST(GraphTiersSuite, hybrid_mini_replacement_test) {
//     int world_rank_buf;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
//     uint32_t world_rank = world_rank_buf;
//     int world_size_buf;
//     MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
//     uint32_t world_size = world_size_buf;

//     uint32_t num_nodes = 100;
//     uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
//     if (world_size != num_tiers+1)
//         FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;
//     // Parameters
//     int update_batch_size = 1;
//     height_factor = 1;
//     sketch_len = Sketch::calc_vector_length(num_nodes);
// 	sketch_err = DEFAULT_SKETCH_ERR;

//     // Seeds
//     std::random_device dev;
//     std::mt19937 rng(dev());
//     std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
//     int seed = dist(rng);
//     bcast(&seed, sizeof(int), 0);
//     std::cout << "SEED: " << seed << std::endl;
//     rng.seed(seed);
//     for (int i = 0; i < world_rank; i++)
//         dist(rng);
//     int tier_seed = dist(rng);

//     if (world_rank == 0) {
//         int seed = time(NULL);
//         srand(seed);
//         std::cout << "InputNode seed: " << seed << std::endl;
//         InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
//         GraphVerifier gv(num_nodes);
//         // Link all of the nodes into 1 connected component
//         for (node_id_t i = 0; i < num_nodes-1; i++) {
//             input_node.update({{i, i+1}, INSERT});
//             gv.edge_update({i,i+1});
//             std::vector<std::set<node_id_t>> cc = input_node.cc_query();
//             try {
//                 // gv.reset_cc_state();
//                 gv.verify_cc_from_component_set(cc);
//             } catch (IncorrectCCException& e) {
//                 std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
//                 std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << num_nodes-i-1 << " components" << std::endl;
//                 FAIL();
//             }
//         }
//         // Generate a random bridge
//         node_id_t first = rand() % num_nodes;
//         node_id_t second = rand() % num_nodes;
//         while(first == second || second == first+1 || first == second+1)
//             second = rand() % num_nodes;
//         input_node.update({{first, second}, INSERT});
//         gv.edge_update({first, second});
//         node_id_t distance = std::max(first, second) - std::min(first, second);
//         // Cut a random edge that should be replaced by the bridge
//         first = std::min(first, second) + rand() % (distance-1);
//         input_node.update({{first, first+1}, DELETE});
//         gv.edge_update({first, first+1});
//         // Check the coonected components
//         std::vector<std::set<node_id_t>> cc = input_node.cc_query();
//         try {
//             // gv.reset_cc_state();
//             gv.verify_cc_from_component_set(cc);
//         } catch (IncorrectCCException& e) {
//             std::cout << "Incorrect cc found after cutting nodes " << first << " and " << first+1 << std::endl;
//             std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
//             FAIL();
//         }
//         // Communicate to all other nodes that the stream has ended
//         input_node.end();
//     } else if (world_rank < num_tiers+1) {
//         int tier_num = world_rank-1;
//         TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
//         tier_node.main();
//     }
// }

// TEST(GraphTiersSuite, hybrid_mini_batch_test) {
//     int world_rank_buf;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
//     uint32_t world_rank = world_rank_buf;
//     int world_size_buf;
//     MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
//     uint32_t world_size = world_size_buf;

//     uint32_t num_nodes = 100;
//     uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
//     if (world_size != num_tiers+1)
//         FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;
//     // Parameters
//     int update_batch_size = 10;
//     height_factor = 1;
//     sketch_len = Sketch::calc_vector_length(num_nodes);
// 	sketch_err = DEFAULT_SKETCH_ERR;

//     // Seeds
//     std::random_device dev;
//     std::mt19937 rng(dev());
//     std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
//     int seed = dist(rng);
//     bcast(&seed, sizeof(int), 0);
//     std::cout << "SEED: " << seed << std::endl;
//     rng.seed(seed);
//     for (int i = 0; i < world_rank; i++)
//         dist(rng);
//     int tier_seed = dist(rng);

//     if (world_rank == 0) {
//         int seed = time(NULL);
//         srand(seed);
//         std::cout << "InputNode seed: " << seed << std::endl;
//         InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
//         GraphVerifier gv(num_nodes);
//         // Link all of the nodes into 1 connected component
//         for (node_id_t i = 0; i < num_nodes-1; i++) {
//             input_node.update({{i, i+1}, INSERT});
//             gv.edge_update({i,i+1});
//             std::vector<std::set<node_id_t>> cc = input_node.cc_query();
//             try {
//                 // gv.reset_cc_state();
//                 gv.verify_cc_from_component_set(cc);
//             } catch (IncorrectCCException& e) {
//                 std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
//                 std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << num_nodes-i-1 << " components" << std::endl;
//                 FAIL();
//             }
//         }
//         // Add a batch that has no isolations
//         input_node.process_all_updates();
//         for (node_id_t i=0; i<(node_id_t)update_batch_size; i++) {
//             input_node.update({{i, i+2}, INSERT});
//             gv.edge_update({i,i+2});
//         }
//         // Check the coonected components
//         std::vector<std::set<node_id_t>> cc = input_node.cc_query();
//         try {
//             // gv.reset_cc_state();
//             gv.verify_cc_from_component_set(cc);
//         } catch (IncorrectCCException& e) {
//             std::cout << "Incorrect cc found after batch with no isolations" << std::endl;
//             std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
//             FAIL();
//         }
//         for (node_id_t i=0; i<(node_id_t)update_batch_size; i++) {
//             input_node.update({{i, i+2}, DELETE});
//             gv.edge_update({i,i+2});
//         }
//         input_node.process_all_updates();
//         // Add a batch that has one isolated deletion in the middle
//         for (node_id_t i=0; i<(node_id_t)update_batch_size/2-2; i++) {
//             input_node.update({{i, i+2}, INSERT});
//             gv.edge_update({i,i+2});
//         }
//         input_node.update({{(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1}, DELETE});
//         gv.edge_update({(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1});
//         for (node_id_t i=(node_id_t)update_batch_size/2+1; i<(node_id_t)update_batch_size+2; i++) {
//             input_node.update({{i, i+3}, INSERT});
//             gv.edge_update({i,i+3});
//         }
//         // Check the coonected components
//         cc = input_node.cc_query();
//         try {
//             // gv.reset_cc_state();
//             gv.verify_cc_from_component_set(cc);
//         } catch (IncorrectCCException& e) {
//             std::cout << "Incorrect cc found after batch with one isolated deletion" << std::endl;
//             std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
//             FAIL();
//         }
//         input_node.update({{(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1}, INSERT});
//         gv.edge_update({(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1});
//         input_node.process_all_updates();
//         // Add a batch with multiple forest edge deletions
//         for (node_id_t i=0; i<(node_id_t)update_batch_size/2-2; i++) {
//             input_node.update({{i, i+3}, INSERT});
//             gv.edge_update({i,i+3});
//         }
//         input_node.update({{2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+2}, INSERT}); // Add a replacement edge
//         gv.edge_update({2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+2});
//         input_node.update({{2*(node_id_t)update_batch_size+2, 2*(node_id_t)update_batch_size+3}, DELETE}); // First isolation
//         gv.edge_update({2*(node_id_t)update_batch_size+2, 2*(node_id_t)update_batch_size+3});
//         input_node.update({{2*(node_id_t)update_batch_size+4, 2*(node_id_t)update_batch_size+5}, DELETE}); // Non-replacing delete
//         gv.edge_update({2*(node_id_t)update_batch_size+4, 2*(node_id_t)update_batch_size+5});
//         input_node.update({{2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+1}, DELETE}); // Replacement delete
//         gv.edge_update({2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+1});
//         for (node_id_t i=(node_id_t)update_batch_size/2+1; i<(node_id_t)update_batch_size; i++) {
//             input_node.update({{i, i+3}, INSERT});
//             gv.edge_update({i,i+3});
//         }
//         // Check the coonected components
//         cc = input_node.cc_query();
//         try {
//             // gv.reset_cc_state();
//             gv.verify_cc_from_component_set(cc);
//         } catch (IncorrectCCException& e) {
//             std::cout << "Incorrect cc found after batch with one isolated deletion" << std::endl;
//             std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
//             FAIL();
//         }
//         // Communicate to all other nodes that the stream has ended
//         input_node.end();
//     } else if (world_rank < num_tiers+1) {
//         int tier_num = world_rank-1;
//         TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
//         tier_node.main();
//     }
// }

TEST(GraphTiersSuite, hybrid_correctness_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    BinaryGraphStream stream(stream_file, 100000);
    uint32_t num_nodes = stream.nodes();
    uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
    // Parameters
    int update_batch_size = DEFAULT_BATCH_SIZE;
    height_factor = 1./log2(log2(num_nodes));
    sketch_len = Sketch::calc_vector_length(num_nodes);
	sketch_err = DEFAULT_SKETCH_ERR;

    // Seeds
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    int seed = dist(rng);
    bcast(&seed, sizeof(int), 0);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
    for (int i = 0; i < world_rank; i++)
        dist(rng);
    int tier_seed = dist(rng);

    if (world_size != num_tiers+1)
        FAIL() << "MPI world size too small for graph with " << num_nodes << " vertices. Correct world size is: " << num_tiers+1;

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "InputNode seed: " << seed << std::endl;
        // initialize data structures
        // InputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        // SCCWN cluster_forest(num_nodes);
        HybridManagerType hybrid_driver(
            num_nodes, num_tiers, update_batch_size, seed
        );
        
        GraphVerifier gv(num_nodes);
        int edgecount = stream.edges();
	    int count = 20000000;
        edgecount = std::min(edgecount, count);
        for (int i = 0; i < edgecount; i++) {
            // Read an update from the stream and have the input node process it
            GraphUpdate update = stream.get_edge();
            hybrid_driver.update(update);
            // Correctness testing by performing a cc query
            gv.edge_update(update.edge);
            unlikely_if(i%100000 == 0 || i == edgecount-1) {
                std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
                try {
                    // gv.reset_cc_state();
                    gv.verify_cc_from_component_set(cc);
                    std::cout << "Update " << i << ", CCs correct." << std::endl;
                } catch (IncorrectCCException& e) {
                    std::cout << "Incorrect connected components found at update "  << i << std::endl;
                    std::cout << "GOT: " << cc.size() << std::endl;
                    hybrid_driver.end();
                    FAIL();
                }
            }
        }
        std::ofstream file;
        file.open ("mpi_kron_results.txt", std::ios_base::app);
        file << stream_file << " passed correctness test." << std::endl;
        file.close();
        // Communicate to all other nodes that the stream has ended
        hybrid_driver.end();

    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        TierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}
