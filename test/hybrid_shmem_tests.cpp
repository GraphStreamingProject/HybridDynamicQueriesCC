#include <gtest/gtest.h>
#include <omp.h>
#include <signal.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>

#include "batch_tiers.h"
#include "binary_graph_stream.h"
#include "graph_tiers.h"
// #include "mat_graph_verifier.h"
#include "graph_verifier.h"
#include "serial_hybrid_conn.h"
#include "parallel_hybrid_conn.h"
#include "util.h"

const vec_t DEFAULT_SKETCH_ERR = 1;

size_t update_batch_size = 200;

static uint32_t compute_num_tiers(node_id_t node_count) {
    if (node_count <= 100) {
        return 5;
    }
    const double numerator = log2(static_cast<double>(node_count));
    // const double denominator = log2(3.0) - 1.0;
    //  const double denominator=0.6;
    const double denominator = 1.4;
    auto tiers = static_cast<uint32_t>(numerator / denominator);
    return std::max<uint32_t>(5, tiers);
}

// using GraphTierSystem = GraphTiers<EulerTourTree<DefaultSketchColumn>>;
// using GraphTierSystem = BatchTiers<EulerTourTree<DefaultSketchColumn>>;
using GraphTierSystem = BatchTiers<ufo::CutsetUFOTree<DefaultSketchColumn>>;

auto start = std::chrono::high_resolution_clock::now();
auto stop = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

static void print_metrics() {
    stop = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    std::cout << "\nTotal time for all updates performed (ms): " << duration.count() << std::endl;
    std::cout << "\tTotal time in Sketch update (ms): " << sketch_time / 1000 << std::endl;
    std::cout << "\tTotal time in Refresh function (ms): " << refresh_time / 1000 << std::endl;
    std::cout << "\t\tTime in Parallel isolated checking (ms): " << parallel_isolated_check / 1000
              << std::endl;
    std::cout << "\t\tTime in Sketch queries (ms): " << sketch_query / 1000 << std::endl;
    std::cout << "\t\tTime in LCT operations (ms): " << lct_time / 1000 << std::endl;
    std::cout << "\t\tTime in ETT operations (ms): " << (ett_time + ett_find_root + ett_get_agg) / 1000
              << std::endl;
    std::cout << "\t\t\tETT Split and Join (ms): " << ett_time / 1000 << std::endl;
    std::cout << "\t\t\tETT Find Tree Root (ms): " << ett_find_root / 1000 << std::endl;
    std::cout << "\t\t\tETT Get Aggregate (ms): " << ett_get_agg / 1000 << std::endl;
    std::cout << "Total number of tiers grown: " << tiers_grown << std::endl;
    std::cout << "Total number of normal refreshes: " << normal_refreshes << std::endl;
}

TEST(HybridGraphTiersSuite, gibbs_mixed_speed_test) {
    BinaryGraphStream stream(stream_file, 100000);
    long edgecount = stream.edges();
    // height_factor = 1;//1./log2(log2(stream.nodes()));
    height_factor = 1 / log2(log2(stream.nodes()));
    sketch_len = Sketch::calc_vector_length(stream.nodes());
    sketch_err = DEFAULT_SKETCH_ERR;
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
    uint64_t seed = dist(rng);
    // GraphTierSystem gt(stream.nodes(), seed);
    // SerialConnectivityManager<GraphTierSystem>
    uint32_t num_tiers = log2(stream.nodes()) / (log2(3) - 1);
    SerialConnectivityManager<GraphTierSystem> hybrid_driver(stream.nodes(), num_tiers, update_batch_size,
                                                             seed);

    long total_update_time = 0;
    long total_query_time = 0;
    auto update_timer = std::chrono::high_resolution_clock::now();
    auto query_timer = update_timer;
    bool doing_updates = true;
    for (long i = 0; i < edgecount; i++) {
        // Read an update from the stream and have the input node process it
        GraphUpdate operation = stream.get_edge();
        if (operation.type == 2) {  // 2 is the symbol for queries
            unlikely_if(doing_updates) {
                total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::high_resolution_clock::now() - update_timer)
                                         .count();
                doing_updates = false;
                query_timer = std::chrono::high_resolution_clock::now();
            }
            hybrid_driver.connectivity_query(operation.edge.src, operation.edge.dst);
        } else {
            unlikely_if(!doing_updates) {
                total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::high_resolution_clock::now() - query_timer)
                                        .count();
                doing_updates = true;
                update_timer = std::chrono::high_resolution_clock::now();
            }
            hybrid_driver.update(operation);
        }
        unlikely_if(i % 1000000 == 0 || i == edgecount - 1) {
            std::cout << "FINISHED OPERATION " << i << " OUT OF " << edgecount << " IN " << stream_file
                      << std::endl;
            std::cout << "Sketched nodes: " << hybrid_driver.sketched_node_count() << " out of "
                      << stream.nodes() << std::endl;
            // std::cout << "-  Space usage of CF: " << hybrid_driver.get_space_usage_cf()/(1024*1024) << "
            // MB" << std::endl; std::cout << "-  Space usage of Driver: " <<
            // hybrid_driver.get_space_usage_driver()/(1024*1024) << " MB" << std::endl; std::cout << "- Space
            // usage of Sketches: " << hybrid_driver.space_usage_conn_sketch()/(1024*1024) << " MB" <<
            // std::endl; std::cout << "-  Space usage of Recovery Sketches: " <<
            // hybrid_driver.space_usage_recovery_sketch()/(1024*1024) << " MB" << std::endl;
            std::cout << "-  Total edges: " << hybrid_driver.total_edges() << std::endl;
            std::cout << "-  Sketched edges: " << hybrid_driver.num_sketched_edges() << std::endl;
            double percent_sketched =
                100.0 * ((double)hybrid_driver.num_sketched_edges()) / ((double)hybrid_driver.total_edges());
            std::cout << "-  Percent sketched edges: " << percent_sketched << "%" << std::endl;
        }
    }
    if (doing_updates) {
        total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::high_resolution_clock::now() - update_timer)
                                 .count();
    } else {
        total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::high_resolution_clock::now() - query_timer)
                                .count();
    }

    std::cout << "Total update time(ms):   " << (total_update_time / 1000) << std::endl;
    std::cout << "Total query time(ms):    " << (total_query_time / 1000) << std::endl;

    std::ofstream file;
    std::string out_file =
        "./../results/gibbs_speed_results/" + stream_file.substr(stream_file.find("/") + 1) + ".txt";
    std::cout << "WRITING RESULTS TO " << out_file << std::endl;
    file.open(out_file, std::ios_base::app);
    file << " UPDATES/SECOND: " << ((long)(0.9 * edgecount)) / (1 + total_update_time / 1000) * 1000
         << std::endl;
    file << " QUERIES/SECOND: " << ((long)(0.1 * edgecount)) / (1 + total_query_time / 1000) * 1000
         << std::endl;
    file.close();
}
TEST(HybridGraphTiersSuite, sparse_only_speed_test) {
    BinaryGraphStream stream(stream_file, 100000);
    long edgecount = stream.edges();
    // height_factor = 1;//1./log2(log2(stream.nodes()));
    height_factor = 1 / log2(log2(stream.nodes()));
    sketch_len = Sketch::calc_vector_length(stream.nodes());
    sketch_err = DEFAULT_SKETCH_ERR;
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
    uint64_t seed = dist(rng);
    // GraphTierSystem gt(stream.nodes(), seed);
    // SerialConnectivityManager<GraphTierSystem>
    uint32_t num_tiers = log2(stream.nodes()) / (log2(3) - 1);
    SCCWN<> cf_algo(stream.nodes());

    long total_update_time = 0;
    long total_query_time = 0;
    auto update_timer = std::chrono::high_resolution_clock::now();
    auto query_timer = update_timer;
    bool doing_updates = true;
    for (long i = 0; i < edgecount; i++) {
        // Read an update from the stream and have the input node process it
        GraphUpdate operation = stream.get_edge();
        if (operation.type == 2) {  // 2 is the symbol for queries
            unlikely_if(doing_updates) {
                total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::high_resolution_clock::now() - update_timer)
                                         .count();
                doing_updates = false;
                query_timer = std::chrono::high_resolution_clock::now();
            }
            cf_algo.is_connected(operation.edge.src, operation.edge.dst);
        } else {
            unlikely_if(!doing_updates) {
                total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::high_resolution_clock::now() - query_timer)
                                        .count();
                doing_updates = true;
                update_timer = std::chrono::high_resolution_clock::now();
            }
            if (operation.type == INSERT) {
                cf_algo.insert(operation.edge.src, operation.edge.dst);
            } else {
                cf_algo.remove(operation.edge.src, operation.edge.dst);
            }
        }
        unlikely_if(i % 1000000 == 0 || i == edgecount - 1) {
            std::cout << "FINISHED OPERATION " << i << " OUT OF " << edgecount << " IN " << stream_file
                      << std::endl;
            if (i % 20000000 == 0 || i == edgecount - 1) {
                std::cout << "-  Space usage of CF: " << cf_algo.getMemUsage() / (1024 * 1024) << " MB"
                          << std::endl;
            }
        }
    }
    if (doing_updates) {
        total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::high_resolution_clock::now() - update_timer)
                                 .count();
    } else {
        total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::high_resolution_clock::now() - query_timer)
                                .count();
    }
    std::cout << "Total update time(ms):   " << (total_update_time / 1000) << std::endl;
    std::cout << "Total query time(ms):    " << (total_query_time / 1000) << std::endl;

    std::ofstream file;
    std::string out_file =
        "./../results/gibbs_speed_results/" + stream_file.substr(stream_file.find("/") + 1) + ".txt";
    std::cout << "WRITING RESULTS TO " << out_file << std::endl;
    file.open(out_file, std::ios_base::app);
    file << " UPDATES/SECOND: " << ((long)(0.9 * edgecount)) / (1 + total_update_time / 1000) * 1000
         << std::endl;
    file << " QUERIES/SECOND: " << ((long)(0.1 * edgecount)) / (1 + total_query_time / 1000) * 1000
         << std::endl;
    file.close();
}

TEST(HybridGraphTiersSuite, hybrid_memory_test) {
    BinaryGraphStream stream(stream_file, 100000);
    long edgecount = stream.edges();
    // height_factor = 1;//1./log2(log2(stream.nodes()));
    height_factor = 1 / log2(log2(stream.nodes()));
    sketch_len = Sketch::calc_vector_length(stream.nodes());
    sketch_err = DEFAULT_SKETCH_ERR;
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
    uint64_t seed = dist(rng);
    // GraphTierSystem gt(stream.nodes(), seed);
    // SerialConnectivityManager<GraphTierSystem>
    uint32_t num_tiers = log2(stream.nodes()) / (log2(3) - 1);
    SerialConnectivityManager<GraphTierSystem> hybrid_driver(stream.nodes(), num_tiers, update_batch_size,
                                                             seed);

    long total_update_time = 0;
    long total_query_time = 0;
    auto update_timer = std::chrono::high_resolution_clock::now();
    auto query_timer = update_timer;
    bool doing_updates = true;
    for (long i = 0; i < edgecount; i++) {
        // Read an update from the stream and have the input node process it
        GraphUpdate operation = stream.get_edge();
        if (operation.type == 2) {  // 2 is the symbol for queries
            unlikely_if(doing_updates) {
                total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::high_resolution_clock::now() - update_timer)
                                         .count();
                doing_updates = false;
                query_timer = std::chrono::high_resolution_clock::now();
            }
            hybrid_driver.connectivity_query(operation.edge.src, operation.edge.dst);
        } else {
            unlikely_if(!doing_updates) {
                total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::high_resolution_clock::now() - query_timer)
                                        .count();
                doing_updates = true;
                update_timer = std::chrono::high_resolution_clock::now();
            }
            hybrid_driver.update(operation);
        }
        unlikely_if(i % 1000000 == 0 || i == edgecount - 1) {
            std::cout << "FINISHED OPERATION " << i << " OUT OF " << edgecount << " IN " << stream_file
                      << std::endl;
            if (i % 20000000 == 0 || i == edgecount - 1) {
                std::cout << "Sketched nodes: " << hybrid_driver.sketched_node_count() << " out of "
                          << stream.nodes() << std::endl;
                std::cout << "-  Space usage of CF: " << hybrid_driver.get_space_usage_cf() / (1024 * 1024)
                          << " MB" << std::endl;
                std::cout << "-  Space usage of Driver: "
                          << hybrid_driver.get_space_usage_driver() / (1024 * 1024) << " MB" << std::endl;
                std::cout << "-  Space usage of Sketches: "
                          << hybrid_driver.space_usage_conn_sketch() / (1024 * 1024) << " MB" << std::endl;
                std::cout << "-  Space usage of Recovery Sketches: "
                          << hybrid_driver.space_usage_recovery_sketch() / (1024 * 1024) << " MB"
                          << std::endl;
                std::cout << "-  Total edges: " << hybrid_driver.total_edges() << std::endl;
                std::cout << "-  Sketched edges: " << hybrid_driver.num_sketched_edges() << std::endl;
                double percent_sketched = 100.0 * ((double)hybrid_driver.num_sketched_edges()) /
                                          ((double)hybrid_driver.total_edges());
                std::cout << "-  Percent sketched edges: " << percent_sketched << "%" << std::endl;
            }
        }
    }
    if (doing_updates) {
        total_update_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::high_resolution_clock::now() - update_timer)
                                 .count();
    } else {
        total_query_time += std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::high_resolution_clock::now() - query_timer)
                                .count();
    }

    std::cout << "Total update time(ms):   " << (total_update_time / 1000) << std::endl;
    std::cout << "Total query time(ms):    " << (total_query_time / 1000) << std::endl;

    std::ofstream file;
    std::string out_file =
        "./../results/gibbs_speed_results/" + stream_file.substr(stream_file.find("/") + 1) + ".txt";
    std::cout << "WRITING RESULTS TO " << out_file << std::endl;
    file.open(out_file, std::ios_base::app);
    file << " UPDATES/SECOND: " << ((long)(0.9 * edgecount)) / (1 + total_update_time / 1000) * 1000
         << std::endl;
    file << " QUERIES/SECOND: " << ((long)(0.1 * edgecount)) / (1 + total_query_time / 1000) * 1000
         << std::endl;
    file.close();
}

TEST(HybridGraphTiersSuite, mini_correctness_test) {
    node_id_t numnodes = 10;
    height_factor = 1 / log2(log2(numnodes));
    sketch_len = Sketch::calc_vector_length(numnodes);
    sketch_err = DEFAULT_SKETCH_ERR;

    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
    uint64_t seed = dist(rng);
    uint32_t num_tiers = compute_num_tiers(numnodes);
    SerialConnectivityManager<GraphTierSystem> hybrid_driver(numnodes, num_tiers, update_batch_size, seed);
    GraphVerifier gv(numnodes);

    // Link all of the nodes into 1 connected component
    for (node_id_t i = 0; i < numnodes - 1; i++) {
        hybrid_driver.update({{i, i + 1}, INSERT});
        gv.edge_update({i, i + 1});
        if (i % 3 == 0) {
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after linking nodes " << i << " and " << i + 1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << numnodes - i - 1
                          << " components" << std::endl;
                FAIL();
            }
        }
    }
    // One by one cut all of the nodes into singletons
    for (node_id_t i = 0; i < numnodes - 1; i++) {
        hybrid_driver.update({{i, i + 1}, DELETE});
        gv.edge_update({i, i + 1});
        if (i % 3 == 0) {
            std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
            try {
                // gv.reset_cc_state();
                gv.verify_cc_from_component_set(cc);
            } catch (IncorrectCCException& e) {
                std::cout << "Incorrect cc found after cutting nodes " << i << " and " << i + 1 << std::endl;
                std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << i + 2 << " components"
                          << std::endl;
                FAIL();
            }
        }
    }
}

TEST(HybridGraphTiersSuite, deletion_replace_correctness_test) {
    node_id_t numnodes = 50;
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
    uint64_t seed = dist(rng);
    uint32_t num_tiers = compute_num_tiers(numnodes);
    SerialConnectivityManager<GraphTierSystem> hybrid_driver(numnodes, num_tiers, update_batch_size, seed);
    GraphVerifier gv(numnodes);

    // Link all of the nodes into 1 connected component
    for (node_id_t i = 0; i < numnodes - 1; i++) {
        hybrid_driver.update({{i, i + 1}, INSERT});
        gv.edge_update({i, i + 1});
        std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
        try {
            // gv.reset_cc_state();
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after linking nodes " << i << " and " << i + 1 << std::endl;
            std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << numnodes - i - 1
                      << " components" << std::endl;
            FAIL();
        }
    }
    // Generate a random bridge
    node_id_t first = rand() % numnodes;
    node_id_t second = rand() % numnodes;
    while (first == second || second == first + 1 || first == second + 1) second = rand() % numnodes;

    hybrid_driver.update({{first, second}, INSERT});
    gv.edge_update({first, second});

    node_id_t distance = std::max(first, second) - std::min(first, second);
    // Cut a random edge
    first = std::min(first, second) + rand() % (distance - 1);

    hybrid_driver.update({{first, first + 1}, DELETE});
    gv.edge_update({first, first + 1});

    std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
    try {
        // gv.reset_cc_state();
        gv.verify_cc_from_component_set(cc);
    } catch (IncorrectCCException& e) {
        std::cout << "Incorrect cc found after cutting nodes " << first << " and " << first + 1 << std::endl;
        std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
        FAIL();
    }
}

TEST(HybridGraphTiersSuite, omp_correctness_test) {
    // omp_set_dynamic(1);
    try {
        BinaryGraphStream stream(stream_file, 100000);

        height_factor = 1 / log2(log2(stream.nodes()));
        sketch_len = Sketch::calc_vector_length(stream.nodes());
        sketch_err = DEFAULT_SKETCH_ERR;

        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
        uint64_t seed = dist(rng);
        uint32_t num_tiers = compute_num_tiers(stream.nodes());
        SerialConnectivityManager<GraphTierSystem> hybrid_driver(stream.nodes(), num_tiers, update_batch_size,
                                                                 seed);
        int edgecount = stream.edges();
        // edgecount = 1000000;
        GraphVerifier gv(stream.nodes());
        start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < edgecount; i++) {
            GraphUpdate update = stream.get_edge();
            hybrid_driver.update(update);
            gv.edge_update(update.edge);
            unlikely_if(i % 1000 == 0 || i == edgecount - 1) {
                std::vector<std::set<node_id_t>> cc = hybrid_driver.cc_query();
                try {
                    // gv.reset_cc_state();
                    gv.verify_cc_from_component_set(cc);
                    std::cout << "Update " << i << ", CCs correct." << std::endl;
                } catch (IncorrectCCException& e) {
                    std::cout << "Incorrect connected components found at update " << i << std::endl;
                    std::cout << "GOT: " << cc.size() << std::endl;
                    std::cout << "EXPECTED: " << gv.get_num_kruskal_ccs() << std::endl;
                    FAIL();
                }
            }
        }
        std::ofstream file;
        file.open("omp_kron_results.txt", std::ios_base::app);
        file << stream_file << " passed correctness test." << std::endl;
        file.close();

    } catch (BadStreamException& e) {
        std::cout << "ERROR: Stream binary file not found." << std::endl;
    }
}

TEST(HybridGraphTiersSuite, omp_speed_test) {
    // omp_set_dynamic(1);
    try {
        long time = 0;
        BinaryGraphStream stream(stream_file, 100000);

        // height_factor = 1;//1./log2(log2(stream.nodes()));
        height_factor = 1 / log2(log2(stream.nodes()));
        sketch_len = Sketch::calc_vector_length(stream.nodes());
        sketch_err = DEFAULT_SKETCH_ERR;

        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
        uint64_t seed = dist(rng);
        uint32_t num_tiers = compute_num_tiers(stream.nodes());
        SerialConnectivityManager<GraphTierSystem> hybrid_driver(stream.nodes(), num_tiers, update_batch_size,
                                                                 seed);
        
        const char* interval_env = std::getenv("MEMORY_REPORT_INTERVAL");
        long report_interval = interval_env ? std::atol(interval_env) : 1000000;
        const char* file_env = std::getenv("MEMORY_REPORT_FILE");
        std::string report_file = file_env ? std::string(file_env) : "memory_report.tsv";
        bool first_report = true;

        int edgecount = stream.edges();
        start = std::chrono::high_resolution_clock::now();

        START(timer);
        for (int i = 0; i < edgecount; i++) {
            GraphUpdate update = stream.get_edge();
            hybrid_driver.update(update);
            unlikely_if(i > 0 && (i % report_interval == 0 || i == edgecount-1)) {
                auto stop = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
                std::cout << "FINISHED UPDATE " << i << " OUT OF " << edgecount << " IN " << stream_file
                          << std::endl;
                          
                // Print TSV to file
                auto reports = hybrid_driver.report_space_usage();
                write_space_report_tsv(reports, report_file, !first_report, i);
                first_report = false;
            }
        }
        STOP(time, timer);
        print_metrics();
        std::ofstream file;
        file.open("omp_kron_results.txt", std::ios_base::app);
        file << stream_file << " time (ms): " << time / 1000 << std::endl;
        file.close();

    } catch (BadStreamException& e) {
        std::cout << "ERROR: Stream binary file not found." << std::endl;
    }
}

TEST(HybridGraphTiersSuite, query_speed_test) {
    // omp_set_dynamic(1);
    try {
        BinaryGraphStream stream(stream_file, 100000);

        height_factor = 1 / log2(log2(stream.nodes()));
        sketch_len = Sketch::calc_vector_length(stream.nodes());
        sketch_err = DEFAULT_SKETCH_ERR;

        int nodecount = stream.nodes();

        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist(0, MAX_INT);
        uint64_t sketch_seed = dist(rng);
        uint32_t num_tiers = compute_num_tiers(nodecount);
        SerialConnectivityManager<GraphTierSystem> hybrid_driver(nodecount, num_tiers, update_batch_size,
                                                                 sketch_seed);
        int edgecount = 150000;

        std::cout << "Building up graph..." << std::endl;
        for (int i = 0; i < edgecount; i++) {
            GraphUpdate update = stream.get_edge();
            hybrid_driver.update(update);
        }

        int querycount = 1000000;
        int seed = time(NULL);
        srand(seed);
        std::cout << "Performing queries..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < querycount; i++) {
            hybrid_driver.connectivity_query(rand() % nodecount, rand() % nodecount);
        }
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
        std::cout << querycount << " Connectivity Queries, Time:  " << duration.count() << std::endl;
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < querycount / 100; i++) {
            hybrid_driver.cc_query();
        }
        stop = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
        std::cout << querycount / 100 << " Connected Components Queries, Time:  " << duration.count()
                  << std::endl;

    } catch (BadStreamException& e) {
        std::cout << "ERROR: Stream binary file not found." << std::endl;
    }
}
