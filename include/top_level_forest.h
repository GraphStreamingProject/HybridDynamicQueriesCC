#pragma once

#include <algorithm>
#include <set>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "lct_v2.h"
#include "sketchless_euler_tour_tree.h"
#include "util.h"

#ifndef USE_SKETCHLESS_QUERY_ETT
#define USE_SKETCHLESS_QUERY_ETT 1
#endif

class TopLevelForest {
public:
    using TopLevelLct = LinkCutTreeMaxAgg<int8_t>;

    TopLevelForest(node_id_t num_nodes, uint64_t seed)
        : link_cut_tree(num_nodes), num_nodes(num_nodes)
#if USE_SKETCHLESS_QUERY_ETT
        , query_ett(num_nodes, 0, seed)
#endif
    {
        (void)seed;
    }

    bool is_initialized(node_id_t u) {
#if USE_SKETCHLESS_QUERY_ETT
        return query_ett.is_initialized(u);
#else
        (void)u;
        return true;
#endif
    }

    void initialize_node(node_id_t u) {
        link_cut_tree.initialize_node(u);
#if USE_SKETCHLESS_QUERY_ETT
        query_ett.initialize_node(u);
#else
        (void)u;
#endif
    }

    void uninitialize_node(node_id_t u) {
        link_cut_tree.uninitialize_node(u);
#if USE_SKETCHLESS_QUERY_ETT
        query_ett.uninitialize_node(u);
#else
        (void)u;
#endif
    }

    void initialize_all_nodes() {
        link_cut_tree.initialize_all_nodes();
#if USE_SKETCHLESS_QUERY_ETT
        query_ett.initialize_all_nodes();
#endif
    }

    void initialize_all_nodes(node_id_t until) {
        link_cut_tree.initialize_all_nodes(until);
#if USE_SKETCHLESS_QUERY_ETT
        query_ett.initialize_all_nodes(until);
#else
        (void)until;
#endif
    }

    bool has_edge(node_id_t u, node_id_t v) {
    #if USE_SKETCHLESS_QUERY_ETT
        return query_ett.has_edge(u, v);
    #else
        return link_cut_tree.has_edge(u, v);
    #endif
    }

    void link(node_id_t u, node_id_t v, int8_t weight = 0) {
        link_cut_tree.link(u, v, weight);
#if USE_SKETCHLESS_QUERY_ETT
        query_ett.link(u, v);
#endif
    }

    void cut(node_id_t u, node_id_t v) {
        link_cut_tree.cut(u, v);
#if USE_SKETCHLESS_QUERY_ETT
        query_ett.cut(u, v);
#endif
    }

    bool connected(node_id_t u, node_id_t v) {
        return link_cut_tree.connected(u, v);
    }

    std::pair<Edge, int8_t> path_query(node_id_t u, node_id_t v) {
        return link_cut_tree.path_query(u, v);
    }

    bool is_connected(node_id_t u, node_id_t v) {
#if USE_SKETCHLESS_QUERY_ETT
        return query_ett.is_connected(u, v);
#else
        return link_cut_tree.connected(u, v);
#endif
    }

    std::vector<std::set<node_id_t>> cc_query() {
#if USE_SKETCHLESS_QUERY_ETT
        return query_ett.cc_query();
#else
        std::vector<std::set<node_id_t>> result;
        absl::flat_hash_map<node_id_t, size_t> rep_to_idx;
        for (node_id_t i = 0; i < num_nodes; ++i) {
            if (!is_initialized(i)) continue;
            bool placed = false;
            for (const auto& pair : rep_to_idx) {
                if (link_cut_tree.connected(i, pair.first)) {
                    result[pair.second].insert(i);
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                size_t idx = result.size();
                rep_to_idx[i] = idx;
                result.push_back(std::set<node_id_t>{i});
            }
        }
        return result;
#endif
    }

    size_t space_usage_bytes() const {
#if USE_SKETCHLESS_QUERY_ETT
        return query_ett.space_usage_bytes();
#else
        return 0;
#endif
    }

    size_t lct_space_usage_bytes() const {
        return link_cut_tree.space_usage_bytes();
    }

private:
    TopLevelLct link_cut_tree;
    node_id_t num_nodes;
#if USE_SKETCHLESS_QUERY_ETT
    SketchlessEulerTourTree<> query_ett;
#endif
};
