#include <gtest/gtest.h>
#include <chrono>
#include <signal.h>
// #include <omp.h>
#include <iostream>
#include <fstream>
#include "graph_tiers.h"
#include "binary_graph_stream.h"
// #include "mat_graph_verifier.h"
#include "graph_verifier.h"
#include "util.h"

const vec_t DEFAULT_SKETCH_ERR = 1;

auto start = std::chrono::high_resolution_clock::now();
auto stop = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

static void print_metrics() {
    stop = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    std::cout << "\nTotal time for all updates performed (ms): " << duration.count() << std::endl;
    std::cout << "\tTotal time in Sketch update (ms): " << sketch_time/1000 << std::endl;
    std::cout << "\tTotal time in Refresh function (ms): " << refresh_time/1000 << std::endl;
    std::cout << "\t\tTime in Parallel isolated checking (ms): " << parallel_isolated_check/1000 << std::endl;
    std::cout << "\t\tTime in Sketch queries (ms): " << sketch_query/1000 << std::endl;
    std::cout << "\t\tTime in LCT operations (ms): " << lct_time/1000 << std::endl;
    std::cout << "\t\tTime in ETT operations (ms): " << (ett_time+ett_find_root+ett_get_agg)/1000 << std::endl;
    std::cout << "\t\t\tETT Split and Join (ms): " << ett_time/1000 << std::endl;
    std::cout << "\t\t\tETT Find Tree Root (ms): " << ett_find_root/1000 << std::endl;
    std::cout << "\t\t\tETT Get Aggregate (ms): " << ett_get_agg/1000 << std::endl;
    std::cout << "Total number of tiers grown: " << tiers_grown << std::endl;
    std::cout << "Total number of normal refreshes: " << normal_refreshes << std::endl;
}

TEST(GraphTiersSuite, mini_correctness_test) {
    node_id_t numnodes = 10;
    GraphTiers gt(numnodes);
    GraphVerifier gv(numnodes);

    // Link all of the nodes into 1 connected component
    for (node_id_t i = 0; i < numnodes-1; i++) {
        gt.update({{i, i+1}, INSERT});
        gv.edge_update({i,i+1});
        std::vector<std::set<node_id_t>> cc = gt.get_cc();
        try {
            // gv.reset_cc_state();
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
            std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << numnodes-i-1 << " components" << std::endl;
            FAIL();
        }
    }
    // One by one cut all of the nodes into singletons
    for (node_id_t i = 0; i < numnodes-1; i++) {
        gt.update({{i, i+1}, DELETE});
        gv.edge_update({i,i+1});
        std::vector<std::set<node_id_t>> cc = gt.get_cc();
        try {
            // gv.reset_cc_state();
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after cutting nodes " << i << " and " << i+1 << std::endl;
            std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << i+2 << " components" << std::endl;
            FAIL();
        }
    }
}

TEST(GraphTiersSuite, deletion_replace_correctness_test) {
    node_id_t numnodes = 50;
    GraphTiers gt(numnodes);
    GraphVerifier gv(numnodes);

    // Link all of the nodes into 1 connected component
    for (node_id_t i = 0; i < numnodes-1; i++) {
        gt.update({{i, i+1}, INSERT});
        gv.edge_update({i,i+1});
        std::vector<std::set<node_id_t>> cc = gt.get_cc();
        try {
            // gv.reset_cc_state();
            gv.verify_cc_from_component_set(cc);
        } catch (IncorrectCCException& e) {
            std::cout << "Incorrect cc found after linking nodes " << i << " and " << i+1 << std::endl;
            std::cout << "GOT: " << cc.size() << " components, EXPECTED: " << numnodes-i-1 << " components" << std::endl;
            FAIL();
        }
    }
    // Generate a random bridge
    node_id_t first = rand() % numnodes;
    node_id_t second = rand() % numnodes;
    while(first == second || second == first+1 || first == second+1)
        second = rand() % numnodes;

    gt.update({{first, second}, INSERT});
    gv.edge_update({first, second});

    node_id_t distance = std::max(first, second) - std::min(first, second);
    // Cut a random edge
    first = std::min(first, second) + rand() % (distance-1);

    gt.update({{first, first+1}, DELETE});
    gv.edge_update({first, first+1});

    std::vector<std::set<node_id_t>> cc = gt.get_cc();
    try {
        // gv.reset_cc_state();
        gv.verify_cc_from_component_set(cc);
    } catch (IncorrectCCException& e) {
        std::cout << "Incorrect cc found after cutting nodes " << first << " and " << first+1 << std::endl;
        std::cout << "GOT: " << cc.size() << " components, EXPECTED: 1 components" << std::endl;
        FAIL();
    }

}

TEST(GraphTiersSuite, omp_correctness_test) {
    // omp_set_dynamic(1);
    try {
        BinaryGraphStream stream(stream_file, 100000);

        height_factor = 1/log2(log2(stream.nodes()));
        sketch_len = Sketch::calc_vector_length(stream.nodes());
        sketch_err = DEFAULT_SKETCH_ERR;

        GraphTiers gt(stream.nodes());
        int edgecount = stream.edges();
        edgecount = 1000000;
        GraphVerifier gv(stream.nodes());
        start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < edgecount; i++) {
            GraphUpdate update = stream.get_edge();
            gt.update(update);
            gv.edge_update(update.edge);
            unlikely_if(i%1000 == 0 || i == edgecount-1) {
                std::vector<std::set<node_id_t>> cc = gt.get_cc();
                try {
                    // gv.reset_cc_state();
                    gv.verify_cc_from_component_set(cc);
                    std::cout << "Update " << i << ", CCs correct." << std::endl;
                } catch (IncorrectCCException& e) {
                    std::cout << "Incorrect connected components found at update "  << i << std::endl;
		            std::cout << "GOT: " << cc.size() << std::endl;
                    FAIL();
                }
            }
        }
        std::ofstream file;
        file.open ("omp_kron_results.txt", std::ios_base::app);
        file << stream_file << " passed correctness test." << std::endl;
        file.close();

    } catch (BadStreamException& e) {
        std::cout << "ERROR: Stream binary file not found." << std::endl;
    }
}

TEST(GraphTiersSuite, omp_speed_test) {
    // omp_set_dynamic(1);
    try {
	    long time = 0;
        BinaryGraphStream stream(stream_file, 100000);

        height_factor = 1./log2(log2(stream.nodes()));
        sketch_len = Sketch::calc_vector_length(stream.nodes());
        sketch_err = DEFAULT_SKETCH_ERR;

        GraphTiers gt(stream.nodes());
        int edgecount = stream.edges();
        start = std::chrono::high_resolution_clock::now();

	    START(timer);
        for (int i = 0; i < edgecount; i++) {
            GraphUpdate update = stream.get_edge();
            gt.update(update);
            unlikely_if (i % 100000 == 0) {
                auto stop = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
                std::cout << "FINISHED UPDATE " << i << " OUT OF " << edgecount << " IN " << stream_file << std::endl;
            }
        }
	    STOP(time, timer);
        print_metrics();
        std::ofstream file;
        file.open ("omp_kron_results.txt", std::ios_base::app);
        file << stream_file << " time (ms): "<< time/1000 << std::endl;
        file.close();

    } catch (BadStreamException& e) {
        std::cout << "ERROR: Stream binary file not found." << std::endl;
    }
}

TEST(GraphTiersSuite, query_speed_test) {
    // omp_set_dynamic(1);
    try {

        BinaryGraphStream stream(stream_file, 100000);

        height_factor = 1/log2(log2(stream.nodes()));
        sketch_len = Sketch::calc_vector_length(stream.nodes());
        sketch_err = DEFAULT_SKETCH_ERR;
        
        int nodecount = stream.nodes();
        GraphTiers gt(nodecount);
        int edgecount = 150000;

        std::cout << "Building up graph..." <<  std::endl;
        for (int i = 0; i < edgecount; i++) {
            GraphUpdate update = stream.get_edge();
            gt.update(update);
        }

        int querycount = 1000000;
        int seed = time(NULL);
        srand(seed);
        std::cout << "Performing queries..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < querycount; i++) {
            gt.is_connected(rand()%nodecount, rand()%nodecount);
        }
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
        std::cout << querycount << " Connectivity Queries, Time:  " << duration.count() << std::endl;
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < querycount/100; i++) {
            gt.get_cc();
        }
        stop = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
        std::cout << querycount/100 << " Connected Components Queries, Time:  " << duration.count() << std::endl;


    } catch (BadStreamException& e) {
        std::cout << "ERROR: Stream binary file not found." << std::endl;
    }
}
