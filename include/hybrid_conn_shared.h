#pragma once

#include "mpi_nodes.h"
#include "graph_tiers.h"
#include "types.h"
#include <dycon/localTree/SCCWN.hpp>
#include "recovery.h"
#include <concepts>

// --- Shared Concepts ---
template <typename T>
concept DynamicSketchConcept = requires(T t) {
    { t.process_all_updates()} -> std::same_as<void>;
    { t.initialize_node( std::declval<node_id_t>() ) } -> std::same_as<void>;
    { t.uninitialize_node( std::declval<node_id_t>() ) } -> std::same_as<void>;
    { t.initialize_all_nodes() } -> std::same_as<void>;
    { t.get_transaction_log() } -> std::same_as<const std::vector<GraphUpdate>&>;
    { t.update( std::declval<GraphUpdate>() ) } -> std::same_as<void>;
    { t.space_usage_bytes() } -> std::same_as<size_t>;
};

// --- Shared Types ---
struct SketchCommand {
    uint64_t seq_num;
    GraphUpdate update;
};

struct RecoveryCommand {
    uint64_t seq_num;
    edge_id_t update;
};
