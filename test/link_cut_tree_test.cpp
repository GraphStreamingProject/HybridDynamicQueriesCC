#include <gtest/gtest.h>
#include <chrono>
#include <iostream>
#include <random>
#include <queue>
#include <unordered_set>
#include <string>
#include "link_cut_tree.h"
#include "lct_v2.h"

extern int command_line_n;
extern int command_line_k;
extern long command_line_seed;

static bool validate(LinkCutNode* v) {
    bool valid = true;
    if (!(v->get_parent() == nullptr || v->get_parent()->get_left() == v  || v->get_parent()->get_right() == v)) {
        valid = false;
        std::cout << "Node " << v << " invalid" << std::endl
        << "Parent's left: " << v->get_parent()->get_left()
        << " Parent's right: " << v->get_parent()->get_right() << std::endl;
    }
    if (!(v->get_left() == nullptr || v->get_left()->get_parent() == v)) {
        valid = false;
        std::cout << "Node " << v << " invalid" << std::endl
        << "Left's parent: " << v->get_left()->get_parent() << std::endl;
    }
    if (!(v->get_right() == nullptr || v->get_right()->get_parent() == v)) {
        valid = false;
        std::cout << "Node " << v << " invalid" << std::endl
        << "Rights's parent: " << v->get_right()->get_parent() << std::endl;
    }
    return valid;
}

// static void inorder(LinkCutNode* node, std::vector<LinkCutNode*>& nodes, bool reversal_state) {
//     if (node != nullptr) {
//         reversal_state = reversal_state != node->get_reversed();
//         if (!reversal_state) {
//             inorder(node->get_left(), nodes, reversal_state);
//             nodes.push_back(node);
//             inorder(node->get_right(), nodes, reversal_state);
//         } else {
//             inorder(node->get_right(), nodes, reversal_state);
//             nodes.push_back(node);
//             inorder(node->get_left(), nodes, reversal_state);
//         }
//     }
// }

// static std::vector<LinkCutNode*> get_inorder(LinkCutNode* node) {
//     LinkCutNode* curr = node;
//     LinkCutNode* root;
//     while (curr) {
//         if (curr->get_parent() == nullptr) { root = curr; }
//         curr = curr->get_parent();
//     }
//     std::vector<LinkCutNode*> nodes;
//     inorder(root, nodes, false);
//     return nodes;
// }

// static void print_paths(std::vector<LinkCutNode>* nodes) {
//     std::set<LinkCutNode*> paths;
//     std::cout << "Paths: " << std::endl;
//     for (uint32_t i = 0; i < (*nodes).size(); i++) {
//         LinkCutNode* curr = &(*nodes)[i];
//         while (curr) {
//             if (curr->get_parent() == nullptr) {
//                 if (paths.find(curr) == paths.end()) {
//                     paths.insert(curr);
//                     std::vector<LinkCutNode*> inorder = get_inorder(&(*nodes)[i]);
//                     for (uint32_t i = 0; i < inorder.size(); i++) {
//                         std::cout << inorder[i]-&(*nodes)[0] << " ";
//                     }
//                     std::cout << "dparent: " << (curr->get_head()->get_dparent()==nullptr ? "null" : std::to_string(curr->get_head()->get_dparent()-&(*nodes)[0]));
//                     std::cout << " agg: " << curr->get_max_edge().second << " edge: " << curr->get_max_edge().first << std::endl;
//                 }
//             }
//             curr = curr->get_parent();
//         }
//     }
//     std::cout << std::endl;
// }

TEST(LinkCutTreeSuite, join_split_test) {
    // power of 2 node count
    int nodecount = 1024;
    LinkCutTree lct(nodecount);
    lct.initialize_all_nodes();
    // Join every 2,4,8,16... nodes
    for (int i = 2; i <= nodecount; i*=2) {
        for (int j = 0; j < nodecount; j+=i) {
            lct.node(j).splay();
            lct.node(j+i/2).splay();
            //std::cout << "Join nodes: " << &nodes[j] << " and " << &nodes[j+i/2] << "\n";
            LinkCutNode* p = lct.join(&lct.node(j), &lct.node(j+i/2));
            EXPECT_EQ(p->get_head(), &lct.node(j));
            EXPECT_EQ(p->get_tail(), &lct.node(j+i-1));
        }
        // Validate all nodes
        for (int i = 0; i < nodecount; i++) {
            validate(&lct.node(i));
        }
    }
    // Split Every ...16,8,4,2 nodes
    for (int i = nodecount; i > 1; i/=2) {
        for (int j = 0; j < nodecount; j+=i) {
            //std::cout << "Split on node: " << &nodes[j+i/2-1] << "\n";
            std::pair<LinkCutNode*, LinkCutNode*> paths = lct.split(&lct.node(j+i/2-1));
            EXPECT_EQ(paths.first->get_head(), &lct.node(j));
            EXPECT_EQ(paths.first->get_tail(), &lct.node(j+i/2-1));
            EXPECT_EQ(paths.second->get_head(), &lct.node(j+i/2));
            EXPECT_EQ(paths.second->get_tail(), &lct.node(j+i-1));
        }
        // Validate all nodes
        for (int i = 0; i < nodecount; i++) {
            validate(&lct.node(i));
        }
    }
}

TEST(LinkCutTreeSuite, expose_simple_test) {
    int pathcount = 100;
    int nodesperpath = 100;
    LinkCutTree lct(nodesperpath*pathcount);
    lct.initialize_all_nodes();
    // Link all the nodes in each path together
    for (int path = 0; path < pathcount; path++) {
        for (int node = 0; node < nodesperpath-1; node++) {
            lct.node(path*nodesperpath+node).splay();
            lct.join(&lct.node(path*nodesperpath+node), &lct.node(path*nodesperpath+node+1));
        }
    }
    // Link all the paths together with dparent pointers half way up the previous path
    for (int path = 1; path < pathcount; path++) {
        lct.node(path*nodesperpath).set_dparent(&lct.node(path*nodesperpath-nodesperpath/2));
    }
    // Call expose on the node half way up the bottom path
    LinkCutNode* p = lct.expose(&lct.node(pathcount*nodesperpath-nodesperpath/2));

    // Validate all nodes
    for (int i = 0; i < pathcount*nodesperpath; i++) {
        validate(&lct.node(i));
    }
    // Validate head and tail of returned path
    EXPECT_EQ(p->get_head(), &lct.node(0));
    EXPECT_EQ(p->get_tail(), &lct.node(pathcount*nodesperpath-nodesperpath/2)) << "Exposed node not tail of path";
    // Validate all dparent pointers
    for (int path = 0; path < pathcount; path++) {
        EXPECT_EQ(lct.node((path+1)*nodesperpath-nodesperpath/2+1).get_dparent(), &lct.node((path+1)*nodesperpath-nodesperpath/2));
    }
}

TEST(LinkCutTreeSuite, random_links_and_cuts) {
    // TODO - restore the test cases.
    int nodecount = 1000;
    LinkCutTree lct(nodecount);
    lct.initialize_all_nodes();
    int seed = time(NULL);
    // Link all nodes
    for (int i = 0; i < nodecount-1; i++) {
        lct.link(i,i+1, rand()%100);
        // ASSERT_TRUE(std::all_of(lct.nodes.begin(), lct.nodes.end(), [](auto& node){return validate(&node);}))
        //   << "One or more invalid nodes found" << std::endl;
        for (int j = 0; j < nodecount; j++) {
           ASSERT_TRUE(validate(&lct.node(j)));
        }
    }
    // Cut every node
    for (int i = 0; i < nodecount-1; i+=1) {
        lct.cut(i,i+1);
        // ASSERT_TRUE(std::all_of(lct.nodes.begin(), lct.nodes.end(), [](auto& node){return validate(&node);}))
        //   << "One or more invalid nodes found" << std::endl;
        for (int j = 0; j < nodecount; j++) {
           ASSERT_TRUE(validate(&lct.node(j)));
        }
    }
    // Do random links and cuts
    int n = 5000;
    std::cout << "Seeding random links and cuts test with " << seed << std::endl;
    srand(seed);
    for (int i = 0; i < n; i++) {
        node_id_t a = rand() % nodecount, b = rand() % nodecount;
        if (a != b) {
            if (lct.find_root(a) != lct.find_root(b)) {
                uint32_t weight = rand()%100;
                //std::cout << i << ": Linking " << a << " and " << b << " weight " << weight << std::endl;
                lct.link(a, b, weight);
                //print_paths(&lct.nodes);
            } else if (lct.node(a).edges.find(&lct.node(b)-&lct.node(0)) != lct.node(a).edges.end()) {
                //std::cout << i << ": Cutting " << a << " and " << b << std::endl;
                lct.cut(a, b);
                //print_paths(&lct.nodes);
            }
            // ASSERT_TRUE(std::all_of(lct.nodes.begin(), lct.nodes.end(), [](auto& node){return validate(&node);}))
            //  << "One or more invalid nodes found" << std::endl;
            for (int j = 0; j < nodecount; j++) {
               ASSERT_TRUE(validate(&lct.node(j)));
            }
        }
    }
    // Manually compute the aggregates for each aux tree
    std::map<LinkCutNode*, uint32_t> path_aggregates;
    for (int i = 0; i < nodecount; i++) {
        uint32_t nodemax = std::max(lct.node(i).edges[lct.node(i).preferred_edges.first],
                lct.node(i).edges[lct.node(i).preferred_edges.second]);
        LinkCutNode* curr = &lct.node(i);
        while (curr) {
            if (curr->get_parent() == nullptr) {
                if (path_aggregates.find(curr) != path_aggregates.end()) {
                    path_aggregates[curr] = std::max(path_aggregates[curr], nodemax);
                } else {
                    path_aggregates.insert({curr, nodemax});
                }
            }
            curr = curr->get_parent();
        }
    }
    // Compare all root aggregates with manually computed ones
    for (auto agg : path_aggregates) {
        EXPECT_EQ(agg.second, agg.first->max) << "Aggregate incorrect" << std::endl;
    }
}

static void run_lct_v2_throughput_default(node_id_t n,
                                          uint64_t ops,
                                          uint64_t seed) {
    using Clock = std::chrono::high_resolution_clock;
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<node_id_t> dist(0, n - 1);

    LinkCutTreeMaxAgg<int8_t> lct(static_cast<int>(n));
    lct.initialize_all_nodes(n);

    auto t0 = Clock::now();
    for (node_id_t v = 1; v < n; ++v) {
        lct.link(0, v, static_cast<int8_t>(1));
    }
    auto t1 = Clock::now();

    volatile uint64_t connected_checksum = 0;
    auto t2 = Clock::now();
    for (uint64_t i = 0; i < ops; ++i) {
        node_id_t a = dist(rng);
        node_id_t b = dist(rng);
        connected_checksum ^= static_cast<uint64_t>(lct.connected(a, b));
    }
    auto t3 = Clock::now();

    volatile uint64_t path_checksum = 0;
    auto t4 = Clock::now();
    for (uint64_t i = 0; i < ops; ++i) {
        node_id_t a = dist(rng);
        node_id_t b = dist(rng);
        if (a == b) {
            b = static_cast<node_id_t>((b + 1) % n);
        }
        auto q = lct.path_query(a, b);
        path_checksum ^= static_cast<uint64_t>(q.first.src) ^ static_cast<uint64_t>(q.first.dst);
    }
    auto t5 = Clock::now();

    const double link_s = std::chrono::duration<double>(t1 - t0).count();
    const double conn_s = std::chrono::duration<double>(t3 - t2).count();
    const double path_s = std::chrono::duration<double>(t5 - t4).count();

    const uint64_t link_ops = (n > 0) ? static_cast<uint64_t>(n - 1) : 0;
    const uint64_t conn_ops_per_sec = (conn_s > 0.0) ? static_cast<uint64_t>(static_cast<double>(ops) / conn_s) : 0;
    const uint64_t path_ops_per_sec = (path_s > 0.0) ? static_cast<uint64_t>(static_cast<double>(ops) / path_s) : 0;
    const uint64_t link_ops_per_sec = (link_s > 0.0 && link_ops > 0) ? static_cast<uint64_t>(static_cast<double>(link_ops) / link_s) : 0;

    std::cout << "[lct_v2_bench] backend=default_int8"
              << " n=" << n
              << " ops=" << ops
              << " seed=" << seed << std::endl;
    std::cout << "  link_build_star:  " << link_ops_per_sec << " ops/sec" << std::endl;
    std::cout << "  connected_query:  " << conn_ops_per_sec << " ops/sec" << std::endl;
    std::cout << "  path_query:       " << path_ops_per_sec << " ops/sec" << std::endl;
    std::cout << "  checksums: connected=" << connected_checksum
              << " path=" << path_checksum << std::endl;
}

TEST(LCTv2Bench, ThroughputManual) {
    if (command_line_n <= 0 || command_line_k <= 0) {
        GTEST_SKIP() << "Pass positional n and k to test runner for throughput benchmark";
    }

    const node_id_t n = static_cast<node_id_t>(command_line_n);
    const uint64_t ops = static_cast<uint64_t>(command_line_k);
    const uint64_t seed = (command_line_seed >= 0) ? static_cast<uint64_t>(command_line_seed) : 1ULL;

    run_lct_v2_throughput_default(n, ops, seed);
}

TEST(LCTv2Correctness, HasEdgeBasic) {
    LinkCutTreeMaxAgg<int8_t> lct(8);
    lct.initialize_all_nodes(8);

    lct.link(0, 1, static_cast<int8_t>(1));
    lct.link(1, 2, static_cast<int8_t>(2));
    lct.link(2, 3, static_cast<int8_t>(3));

    EXPECT_TRUE(lct.has_edge(0, 1));
    EXPECT_TRUE(lct.has_edge(1, 2));
    EXPECT_TRUE(lct.has_edge(2, 3));

    EXPECT_FALSE(lct.has_edge(0, 2));
    EXPECT_FALSE(lct.has_edge(1, 3));
    EXPECT_FALSE(lct.has_edge(0, 0));
    EXPECT_FALSE(lct.has_edge(4, 5));

    lct.cut(1, 2);
    EXPECT_FALSE(lct.has_edge(1, 2));
    EXPECT_FALSE(lct.connected(0, 3));
}

TEST(LCTv2Correctness, SparseNodeCanBeReinitializedAfterDisconnect) {
    using MapLct = LinkCutTreeMaxAgg<
        int8_t,
        absl::flat_hash_map<node_id_t, NodeMaxLCT<int8_t>*>>;

    MapLct lct(3);
    lct.initialize_all_nodes(3);
    lct.link(0, 1, static_cast<int8_t>(1));
    lct.cut(0, 1);

    lct.uninitialize_node(0);
    lct.initialize_node(0);
    lct.link(0, 2, static_cast<int8_t>(2));

    EXPECT_TRUE(lct.connected(0, 2));
    EXPECT_TRUE(lct.has_edge(0, 2));
    EXPECT_FALSE(lct.connected(0, 1));
}

TEST(LCTv2Correctness, HasEdgeMatchesPathOnAdjacentPairs) {
    LinkCutTreeMaxAgg<int8_t> lct(10);
    lct.initialize_all_nodes(10);

    const std::pair<node_id_t, node_id_t> edges[] = {
        {0, 1}, {1, 2}, {1, 3}, {3, 4}, {4, 5}
    };

    for (const auto& e : edges) {
        lct.link(e.first, e.second, static_cast<int8_t>(1));
    }

    for (const auto& e : edges) {
        ASSERT_TRUE(lct.has_edge(e.first, e.second));
        const auto q = lct.path_query(e.first, e.second);
        ASSERT_EQ(VERTICES_TO_EDGE(q.first.src, q.first.dst), VERTICES_TO_EDGE(e.first, e.second));
    }

    EXPECT_FALSE(lct.has_edge(0, 5));
    EXPECT_FALSE(lct.has_edge(2, 5));
}

static bool ref_connected(const std::vector<std::unordered_set<node_id_t>>& adj,
                          node_id_t src,
                          node_id_t dst) {
    if (src == dst) return true;
    std::vector<bool> seen(adj.size(), false);
    std::queue<node_id_t> q;
    seen[src] = true;
    q.push(src);
    while (!q.empty()) {
        node_id_t u = q.front();
        q.pop();
        for (node_id_t v : adj[u]) {
            if (!seen[v]) {
                if (v == dst) return true;
                seen[v] = true;
                q.push(v);
            }
        }
    }
    return false;
}

TEST(LCTv2Correctness, RandomForestCrossCheck) {
    constexpr node_id_t n = 24;
    constexpr int rounds = 1200;
    constexpr uint64_t seed = 1337;

    LinkCutTreeMaxAgg<int8_t> lct(static_cast<int>(n));
    lct.initialize_all_nodes(n);

    std::vector<std::unordered_set<node_id_t>> adj(n);
    std::vector<std::pair<node_id_t, node_id_t>> ref_edges;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<node_id_t> dist(0, n - 1);
    std::uniform_int_distribution<int> coin(0, 99);

    auto add_ref_edge = [&](node_id_t a, node_id_t b) {
        adj[a].insert(b);
        adj[b].insert(a);
        ref_edges.push_back({a, b});
    };

    auto remove_ref_edge = [&](node_id_t a, node_id_t b) {
        adj[a].erase(b);
        adj[b].erase(a);
        const edge_id_t target = VERTICES_TO_EDGE(a, b);
        for (size_t i = 0; i < ref_edges.size(); ++i) {
            if (VERTICES_TO_EDGE(ref_edges[i].first, ref_edges[i].second) == target) {
                ref_edges[i] = ref_edges.back();
                ref_edges.pop_back();
                break;
            }
        }
    };

    for (int r = 0; r < rounds; ++r) {
        node_id_t a = dist(rng);
        node_id_t b = dist(rng);
        if (a == b) {
            continue;
        }

        bool connected_ref = ref_connected(adj, a, b);
        if (!connected_ref) {
            lct.link(a, b, static_cast<int8_t>(coin(rng) % 7));
            add_ref_edge(a, b);
        } else if (!ref_edges.empty() && coin(rng) < 35) {
            std::uniform_int_distribution<size_t> edge_pick(0, ref_edges.size() - 1);
            const auto [u, v] = ref_edges[edge_pick(rng)];
            lct.cut(u, v);
            remove_ref_edge(u, v);
        }

        for (node_id_t u = 0; u < n; ++u) {
            for (node_id_t v = static_cast<node_id_t>(u + 1); v < n; ++v) {
                const bool want_connected = ref_connected(adj, u, v);
                ASSERT_EQ(lct.connected(u, v), want_connected)
                    << "connected mismatch after round " << r << " on pair (" << u << "," << v << ")";

                const bool want_edge = adj[u].contains(v);
                ASSERT_EQ(lct.has_edge(u, v), want_edge)
                    << "has_edge mismatch after round " << r << " on pair (" << u << "," << v << ")";
            }
        }
    }
}

TEST(LCTv2Correctness, ManualSmallBug) {
    LinkCutTreeMaxAgg<int> lct(10);
    lct.initialize_all_nodes();
    lct.link(0, 1, 10);
    lct.link(1, 2, 20);
    lct.link(2, 3, 30);
    
    EXPECT_TRUE(lct.connected(0, 3));
    EXPECT_TRUE(lct.has_edge(0, 1));
    EXPECT_TRUE(lct.has_edge(1, 2));
    EXPECT_FALSE(lct.has_edge(0, 2)) << "Path query returned wrong edges for a multi-hop path";
}

TEST(LCTv2Correctness, ExplorePathQueryBug) {
    LinkCutTreeMaxAgg<int> lct(10);
    lct.initialize_all_nodes();
    lct.link(0, 1, 10);
    lct.link(1, 2, 20);
    lct.link(2, 3, 30);
    
    auto q = lct.path_query(0, 2);
    std::cout << "path_query(0, 2) edge = (" << q.first.src << ", " << q.first.dst << "), max_weight = " << q.second << "\n";
    
    auto q2 = lct.path_query(0, 3);
    std::cout << "path_query(0, 3) edge = (" << q2.first.src << ", " << q2.first.dst << "), max_weight = " << q2.second << "\n";
}
