#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "lct_v2.h"

using Clock = std::chrono::high_resolution_clock;

struct BenchConfig {
    node_id_t n = 100000;
    uint64_t ops = 1000000;
    uint64_t seed = 1;
    std::string which = "all"; // all | map | vector
};

struct BenchResult {
    std::string backend;
    std::string kernel;
    uint64_t ops = 0;
    double seconds = 0.0;
};

static BenchConfig parse_args(int argc, char** argv) {
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--n" && i + 1 < argc) cfg.n = static_cast<node_id_t>(std::stoul(argv[++i]));
        else if (arg == "--ops" && i + 1 < argc) cfg.ops = std::stoull(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) cfg.seed = std::stoull(argv[++i]);
        else if (arg == "--which" && i + 1 < argc) cfg.which = argv[++i];
        else if (arg == "--help") {
            std::cout
                << "Usage: lct_v2_bench [--n N] [--ops OPS] [--seed S] [--which all|map|vector]\n"
                << "Defaults: --n 100000 --ops 1000000 --seed 1 --which all\n";
            std::exit(0);
        }
    }
    return cfg;
}

static void print_results(const std::vector<BenchResult>& results) {
    std::cout << "backend\tkernel\tops\tseconds\tops_per_sec\tns_per_op\n";
    for (const auto& r : results) {
        const double ops_per_sec = (r.seconds > 0.0) ? (static_cast<double>(r.ops) / r.seconds) : 0.0;
        const double ns_per_op = (r.ops > 0) ? (r.seconds * 1e9 / static_cast<double>(r.ops)) : 0.0;
        std::cout << r.backend << "\t"
                  << r.kernel << "\t"
                  << r.ops << "\t"
                  << r.seconds << "\t"
                  << static_cast<uint64_t>(ops_per_sec) << "\t"
                  << ns_per_op << "\n";
    }
}

template <typename LctType>
static std::vector<BenchResult> run_backend(const std::string& backend, const BenchConfig& cfg) {
    std::vector<BenchResult> out;
    std::mt19937_64 rng(cfg.seed);
    std::uniform_int_distribution<node_id_t> dist(0, cfg.n - 1);

    // Kernel 1: link-build throughput (star graph)
    {
        LctType lct(static_cast<int>(cfg.n));
        lct.initialize_all_nodes(cfg.n);

        const uint64_t k = static_cast<uint64_t>(cfg.n > 0 ? cfg.n - 1 : 0);
        auto t0 = Clock::now();
        for (node_id_t v = 1; v < cfg.n; ++v) {
            lct.link(0, v, static_cast<int8_t>(1));
        }
        auto t1 = Clock::now();

        out.push_back({backend, "link_build_star", k,
                       std::chrono::duration<double>(t1 - t0).count()});
    }

    // Shared setup for query/churn kernels.
    LctType lct(static_cast<int>(cfg.n));
    lct.initialize_all_nodes(cfg.n);
    for (node_id_t v = 1; v < cfg.n; ++v) {
        lct.link(0, v, static_cast<int8_t>(1));
    }

    // Kernel 2: connected query throughput
    {
        volatile uint64_t checksum = 0;
        auto t0 = Clock::now();
        for (uint64_t i = 0; i < cfg.ops; ++i) {
            node_id_t a = dist(rng);
            node_id_t b = dist(rng);
            checksum ^= static_cast<uint64_t>(lct.connected(a, b));
        }
        auto t1 = Clock::now();
        (void)checksum;
        out.push_back({backend, "connected_query", cfg.ops,
                       std::chrono::duration<double>(t1 - t0).count()});
    }

    // Kernel 3: path-query throughput (all pairs connected in star)
    {
        volatile uint64_t checksum = 0;
        auto t0 = Clock::now();
        for (uint64_t i = 0; i < cfg.ops; ++i) {
            node_id_t a = dist(rng);
            node_id_t b = dist(rng);
            if (a == b) {
                b = static_cast<node_id_t>((b + 1) % cfg.n);
            }
            auto q = lct.path_query(a, b);
            checksum ^= static_cast<uint64_t>(q.first.src) ^ static_cast<uint64_t>(q.first.dst);
        }
        auto t1 = Clock::now();
        (void)checksum;
        out.push_back({backend, "path_query", cfg.ops,
                       std::chrono::duration<double>(t1 - t0).count()});
    }

    // Kernel 4: cut+relink throughput (churn on star edges)
    {
        if (cfg.n > 1) {
            auto t0 = Clock::now();
            for (uint64_t i = 0; i < cfg.ops; ++i) {
                node_id_t leaf = static_cast<node_id_t>(1 + (i % (cfg.n - 1)));
                lct.cut(0, leaf);
                lct.link(0, leaf, static_cast<int8_t>(1));
            }
            auto t1 = Clock::now();
            out.push_back({backend, "cut_relink_star", cfg.ops * 2,
                           std::chrono::duration<double>(t1 - t0).count()});
        }
    }

    return out;
}

int main(int argc, char** argv) {
    BenchConfig cfg = parse_args(argc, argv);
    std::vector<BenchResult> results;

    if (cfg.n < 2) {
        std::cerr << "--n must be at least 2\n";
        return 1;
    }

    if (cfg.which == "all" || cfg.which == "map") {
        using MapLct = LinkCutTreeMaxAgg<int8_t>;
        auto r = run_backend<MapLct>("map", cfg);
        results.insert(results.end(), r.begin(), r.end());
    }
    if (cfg.which == "all" || cfg.which == "vector") {
        using VectorLct = LinkCutTreeMaxAgg<int8_t, std::vector<NodeMaxLCT<int8_t>>>;
        auto r = run_backend<VectorLct>("vector", cfg);
        results.insert(results.end(), r.begin(), r.end());
    }

    if (results.empty()) {
        std::cerr << "Invalid --which value: " << cfg.which << " (expected all|map|vector)\n";
        return 1;
    }

    print_results(results);
    return 0;
}
