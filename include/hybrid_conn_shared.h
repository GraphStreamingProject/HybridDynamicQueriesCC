#pragma once

#include "mpi_nodes.h"
#include "graph_tiers.h"
#include "types.h"
#ifndef DYCON_SCCWN_HPP_INCLUDED
#define DYCON_SCCWN_HPP_INCLUDED
#include <dycon/localTree/SCCWN.hpp>
#endif
#include "recovery.h"
#include <concepts>

// --- Shared Concepts ---
template <typename T>
concept DynamicSketchConcept = requires(T t) {
    { t.process_all_updates()} -> std::same_as<void>;
    { t.initialize_node( std::declval<node_id_t>() ) } -> std::same_as<void>;
    { t.uninitialize_node( std::declval<node_id_t>() ) } -> std::same_as<void>;
    { t.initialize_all_nodes() } -> std::same_as<void>;
    { t.drain_transaction_log(std::declval<std::vector<GraphUpdate>&>()) } -> std::same_as<void>;
    { t.update( std::declval<GraphUpdate>() ) } -> std::same_as<void>;
    { t.space_usage_bytes() } -> std::same_as<size_t>;
};

// --- Shared Types ---
struct SketchCommand {
    enum class Type : uint8_t {
        EDGE_UPDATE,
        ACTIVATE_VERTEX,
        DEACTIVATE_VERTEX,
        RECLAIM_VERTEX,
    };

    uint64_t seq_num;
    Type type = Type::EDGE_UPDATE;
    GraphUpdate update{};
    node_id_t node = 0;
};

struct RecoveryCommand {
    enum class Type : uint8_t {
        EDGE_UPDATE,
        ACTIVATE_VERTEX,
        DEACTIVATE_VERTEX,
        RECLAIM_VERTEX,
    };

    uint64_t seq_num;
    Type type = Type::EDGE_UPDATE;
    edge_id_t update = 0;
    node_id_t vertex = 0;
};
