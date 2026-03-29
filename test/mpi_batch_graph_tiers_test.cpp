#include <gtest/gtest.h>
#include <chrono>
#include <signal.h>
#include <unordered_map>
#include <random>
#include <iostream>
#include <fstream>
#include "mpi_batch_nodes.h"
#include "binary_graph_stream.h"
#include "cutsets/lct_cutset.h"
#include "graph_verifier.h"
#include "util.h"


const int DEFAULT_BATCH_SIZE = 16834;
const vec_t DEFAULT_SKETCH_ERR = 1;

using BatchTierNodeSystem = BatchTierNode<cutset_lct::CutsetLCT<DefaultSketchColumn>>;
// using BatchTierNodeSystem = BatchTierNode<EulerTourTree<DefaultSketchColumn>>;
// using BatchTierNodeSystem = BatchTierNode<ufo::CutsetUFOTree<

TEST(BatchGraphTierSuite, mpi_batch_mini_correctness_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    uint32_t num_nodes = 100;
    uint32_t num_tiers = world_size - 1;
    int update_batch_size = 1;
    height_factor = 1;
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = DEFAULT_SKETCH_ERR;

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

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "BatchInputNode seed: " << seed << std::endl;
        BatchInputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        input_node.initialize_all_nodes();
        GraphVerifier gv(num_nodes);
        // Link all of the nodes into 1 connected component
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            input_node.update({{i, i+1}, INSERT});
            gv.edge_update({i,i+1});
            std::vector<std::set<node_id_t>> cc = input_node.cc_query();
            try {
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << num_nodes-i-1 << " components" << std::endl;
                FAIL();
            }
        }
        // One by one cut all of the nodes into singletons
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            input_node.update({{i, i+1}, DELETE});
            gv.edge_update({i,i+1});
            std::vector<std::set<node_id_t>> cc = input_node.cc_query();
            try {
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after cutting nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << i+2 << " components" << std::endl;
                FAIL();
            }
        }
        input_node.end();
    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        BatchTierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(BatchGraphTierSuite, mpi_batch_mini_replacement_test) {
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
    int update_batch_size = 1;
    height_factor = 1;
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = DEFAULT_SKETCH_ERR;

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

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "BatchInputNode seed: " << seed << std::endl;
        BatchInputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        input_node.initialize_all_nodes();
        GraphVerifier gv(num_nodes);
        // Link all of the nodes into 1 connected component
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            input_node.update({{i, i+1}, INSERT});
            gv.edge_update({i,i+1});
            std::vector<std::set<node_id_t>> cc = input_node.cc_query();
            try {
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << num_nodes-i-1 << " components" << std::endl;
                FAIL();
            }
        }
        // Generate a random bridge
        node_id_t first = rand() % num_nodes;
        node_id_t second = rand() % num_nodes;
        while(first == second || second == first+1 || first == second+1)
            second = rand() % num_nodes;
        input_node.update({{first, second}, INSERT});
        gv.edge_update({first, second});
        node_id_t distance = std::max(first, second) - std::min(first, second);
        // Cut a random edge that should be replaced by the bridge
        first = std::min(first, second) + rand() % (distance-1);
        input_node.update({{first, first+1}, DELETE});
        gv.edge_update({first, first+1});
        // Check the connected components
        std::vector<std::set<node_id_t>> cc = input_node.cc_query();
        try {
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after cutting nodes " << first << " and " << first+1 << std::endl;
            std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
            FAIL();
        }
        input_node.end();
    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        BatchTierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(BatchGraphTierSuite, mpi_batch_mini_batch_test) {
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
    int update_batch_size = 10;
    height_factor = 1;
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = DEFAULT_SKETCH_ERR;

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

    if (world_rank == 0) {
        int seed = time(NULL);
        srand(seed);
        std::cout << "BatchInputNode seed: " << seed << std::endl;
        BatchInputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        input_node.initialize_all_nodes();
        GraphVerifier gv(num_nodes);
        // Link all of the nodes into 1 connected component
        for (node_id_t i = 0; i < num_nodes-1; i++) {
            input_node.update({{i, i+1}, INSERT});
            gv.edge_update({i,i+1});
            std::vector<std::set<node_id_t>> cc = input_node.cc_query();
            try {
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << num_nodes-i-1 << " components" << std::endl;
                FAIL();
            }
        }
        // Add a batch that has no isolations
        input_node.process_all_updates();
        for (node_id_t i=0; i<(node_id_t)update_batch_size; i++) {
            input_node.update({{i, i+2}, INSERT});
            gv.edge_update({i,i+2});
        }
        std::vector<std::set<node_id_t>> cc = input_node.cc_query();
        try {
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after batch with no isolations" << std::endl;
            std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
            FAIL();
        }
        for (node_id_t i=0; i<(node_id_t)update_batch_size; i++) {
            input_node.update({{i, i+2}, DELETE});
            gv.edge_update({i,i+2});
        }
        input_node.process_all_updates();
        // Add a batch that has one isolated deletion in the middle
        for (node_id_t i=0; i<(node_id_t)update_batch_size/2-2; i++) {
            input_node.update({{i, i+2}, INSERT});
            gv.edge_update({i,i+2});
        }
        input_node.update({{(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1}, DELETE});
        gv.edge_update({(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1});
        for (node_id_t i=(node_id_t)update_batch_size/2+1; i<(node_id_t)update_batch_size+2; i++) {
            input_node.update({{i, i+3}, INSERT});
            gv.edge_update({i,i+3});
        }
        cc = input_node.cc_query();
        try {
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after batch with one isolated deletion" << std::endl;
            std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
            FAIL();
        }
        input_node.update({{(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1}, INSERT});
        gv.edge_update({(node_id_t)update_batch_size/2, (node_id_t)update_batch_size/2+1});
        input_node.process_all_updates();
        // Add a batch with multiple forest edge deletions
        for (node_id_t i=0; i<(node_id_t)update_batch_size/2-2; i++) {
            input_node.update({{i, i+3}, INSERT});
            gv.edge_update({i,i+3});
        }
        input_node.update({{2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+2}, INSERT});
        gv.edge_update({2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+2});
        input_node.update({{2*(node_id_t)update_batch_size+2, 2*(node_id_t)update_batch_size+3}, DELETE});
        gv.edge_update({2*(node_id_t)update_batch_size+2, 2*(node_id_t)update_batch_size+3});
        input_node.update({{2*(node_id_t)update_batch_size+4, 2*(node_id_t)update_batch_size+5}, DELETE});
        gv.edge_update({2*(node_id_t)update_batch_size+4, 2*(node_id_t)update_batch_size+5});
        input_node.update({{2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+1}, DELETE});
        gv.edge_update({2*(node_id_t)update_batch_size, 2*(node_id_t)update_batch_size+1});
        for (node_id_t i=(node_id_t)update_batch_size/2+1; i<(node_id_t)update_batch_size; i++) {
            input_node.update({{i, i+3}, INSERT});
            gv.edge_update({i,i+3});
        }
        cc = input_node.cc_query();
        try {
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after batch with multiple forest edge deletions" << std::endl;
            std::cout << "GOT: " << cc.size() << " components" << std::endl;
            FAIL();
        }
        input_node.end();
    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        BatchTierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(BatchGraphTierSuite, mpi_batch_correctness_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    BinaryGraphStream stream(stream_file, 100000);
    uint32_t num_nodes = stream.nodes();
    uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);
    int update_batch_size = DEFAULT_BATCH_SIZE;
    height_factor = 1./log2(log2(num_nodes));
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = DEFAULT_SKETCH_ERR;

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
        std::cout << "BatchInputNode seed: " << seed << std::endl;
        BatchInputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        input_node.initialize_all_nodes();
        GraphVerifier gv(num_nodes);
        int edgecount = stream.edges();
        int count = 20000000;
        edgecount = std::min(edgecount, count);
        for (int i = 0; i < edgecount; i++) {
            GraphUpdate update = stream.get_edge();
            input_node.update(update);
            gv.edge_update(update.edge);
            unlikely_if(i%1000 == 0 || i == edgecount-1) {
                std::vector<std::set<node_id_t>> cc = input_node.cc_query();
                try {
                    gv.verify_cc_from_component_set(cc);
                    std::cout << "Update " << i << ", CCs correct." << std::endl;
                } catch (IncorrectCCException& e) {
                    std::cout << "Incorrect connected components found at update "  << i << std::endl;
                    std::cout << "GOT: " << cc.size() << std::endl;
                    input_node.end();
                    FAIL();
                }
            }
        }
        std::ofstream file;
        file.open ("mpi_batch_kron_results.txt", std::ios_base::app);
        file << stream_file << " passed batch correctness test." << std::endl;
        file.close();
        input_node.end();

    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        BatchTierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}

TEST(BatchGraphTierSuite, mpi_batch_mixed_speed_test) {
    int world_rank_buf;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank_buf);
    uint32_t world_rank = world_rank_buf;
    int world_size_buf;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size_buf);
    uint32_t world_size = world_size_buf;

    BinaryGraphStream stream(stream_file, 100000);
    uint32_t num_nodes = stream.nodes();
    uint32_t num_tiers = world_size - 1;
    std::cout << "NUM TIERS: " << num_tiers << std::endl;

    int update_batch_size = (batch_size_arg==0) ? DEFAULT_BATCH_SIZE : batch_size_arg;
    height_factor = (height_factor_arg==0) ? 1./log2(log2(num_nodes)) : height_factor_arg;
    sketchless_height_factor = height_factor;
    sketch_len = Sketch::calc_vector_length(num_nodes);
    sketch_err = DEFAULT_SKETCH_ERR;

    std::cout << "BATCH SIZE: " << update_batch_size << " HEIGHT FACTOR " << height_factor << " SKETCH BUFFER: " << SKETCH_BUFFER_SIZE << std::endl;

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
        std::cout << "BatchInputNode seed: " << seed << std::endl;
        BatchInputNode input_node(num_nodes, num_tiers, update_batch_size, seed);
        input_node.initialize_all_nodes();
        long edgecount = stream.edges();
        long total_update_time = 0;
        long total_query_time = 0;
        auto update_timer = std::chrono::high_resolution_clock::now();
        auto query_timer = update_timer;
        bool doing_updates = true;
        for (long i = 0; i < edgecount; i++) {
            GraphUpdate operation = stream.get_edge();
            if (operation.type == 2) {
                unlikely_if (doing_updates) {
                    total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - update_timer).count();
                    doing_updates = false;
                    query_timer = std::chrono::high_resolution_clock::now();
                }
                input_node.connectivity_query(operation.edge.src, operation.edge.dst);
            } else {
                unlikely_if (!doing_updates) {
                    total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - query_timer).count();
                    doing_updates = true;
                    update_timer = std::chrono::high_resolution_clock::now();
                }
                input_node.update(operation);
            }
            unlikely_if(i%1000000 == 0 || i == edgecount-1) {
                std::cout << "FINISHED OPERATION " << i << " OUT OF " << edgecount << " IN " << stream_file << std::endl;
            }
        }
        if (doing_updates) {
            total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - update_timer).count();
        } else {
            total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - query_timer).count();
        }
        input_node.end();
        std::cout << "Total update time(ms):   " << (total_update_time/1000) << std::endl;
        std::cout << "Total query time(ms):    " << (total_query_time/1000) << std::endl;
        std::cout << "Total time(ms):    " << (total_query_time + total_update_time)/1000 << std::endl;

        std::ofstream file;
        std::string out_file = "./../results/mpi_batch_speed_results/" + stream_file.substr(stream_file.find("/") + 1) + ".txt";
        std::cout << "WRITING RESULTS TO " << out_file << std::endl;
        file.open (out_file, std::ios_base::app);
        file << " UPDATES/SECOND: " << (0.9*edgecount)/(total_update_time) << std::endl;
        file << " QUERIES/SECOND: " << (0.1*edgecount)/(total_query_time) << std::endl;
        file.close();

    } else if (world_rank < num_tiers+1) {
        int tier_num = world_rank-1;
        BatchTierNodeSystem tier_node(num_nodes, tier_num, num_tiers, update_batch_size, tier_seed);
        tier_node.main();
    }
}
