#pragma once

#include "hybrid_conn_shared.h"
#include <tbb/concurrent_queue.h>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>

template <typename SketchAlgoClass = InputNode, typename RecoverySketchType = NodeRecoveryIBLTCascade> requires(DynamicSketchConcept<SketchAlgoClass>)
class ParallelConnectivityManager {
public:
    SCCWN<> cf_algo;

    void set_threshold(size_t threshold) {
        DENSE_THRESHOLD = threshold;
        recovery_subsystem.set_threshold(threshold);
    }

    void set_recovery_size(size_t recovery_size) {
        recovery_subsystem.set_recovery_size(recovery_size);
    }

    void set_move_to_sketch(size_t val) {
        MOVE_TO_SKETCH = val;
    }

    node_id_t sketched_node_count() const {
        return recovery_subsystem.active_vertex_count();
    }

private:
    class SketchSubsystem {
    public:
        SketchSubsystem(node_id_t num_nodes, uint32_t num_tiers, int batch_size, size_t seed)
            : sketching_algo(num_nodes, num_tiers, batch_size, seed) {}

        ~SketchSubsystem() {
            stop();
        }

        void start() {
            running.store(true);
            worker = std::thread([this]() {
                SketchCommand cmd;
                std::vector<GraphUpdate> log_buffer;
                while (running.load()) {
                    if (!command_queue.try_pop(cmd)) {
                        std::this_thread::yield();
                        continue;
                    }

                    // Batch commands to amortize sketch processing and log draining.
                    uint64_t last_seq = cmd.seq_num;
                    size_t batch_count = 0;
                    apply_command(cmd);
                    ++batch_count;

                    while (batch_count < kSketchCommandBatchSize && command_queue.try_pop(cmd)) {
                        apply_command(cmd);
                        last_seq = cmd.seq_num;
                        ++batch_count;
                    }

                    process_and_publish(last_seq, log_buffer);
                }

                // Drain any residual work after stop.
                while (command_queue.try_pop(cmd)) {
                    uint64_t last_seq = cmd.seq_num;
                    size_t batch_count = 0;
                    apply_command(cmd);
                    ++batch_count;

                    while (batch_count < kSketchCommandBatchSize && command_queue.try_pop(cmd)) {
                        apply_command(cmd);
                        last_seq = cmd.seq_num;
                        ++batch_count;
                    }

                    process_and_publish(last_seq, log_buffer);
                }
            });
        }

    #ifdef CORRECTNESS_DIAGNOSTICS
        TierMaximalityCheck take_tier_maximality_check() {
            return sketching_algo.take_tier_maximality_check();
        }
    #endif

        void stop() {
            running.store(false);
            if (worker.joinable()) {
                worker.join();
            }
        }

        void enqueue(const SketchCommand& cmd) {
            command_queue.push(cmd);
        }

        void sync_to(uint64_t target_seq) const {
            while (processed_seq_num.load() < target_seq) {
                std::this_thread::yield();
            }
        }

        bool try_pop_commit(GraphUpdate& update) {
            return committed_updates.try_pop(update);
        }

        uint64_t processed_seq() const {
            return processed_seq_num.load();
        }

        void end_algo() {
            sketching_algo.end();
        }

        size_t space_usage_bytes() {
            return sketching_algo.space_usage_bytes();
        }

        SpaceReport report_space_usage() {
            return sketching_algo.report_space_usage();
        }

        long get_num_tree_ops() const { return sketching_algo.get_num_tree_ops(); }
        int get_max_link_tier() const { return sketching_algo.get_max_link_tier(); }
        void reset_max_link_tier() { sketching_algo.reset_max_link_tier(); }

    private:
        void apply_command(const SketchCommand& cmd) {
            switch (cmd.type) {
                case SketchCommand::Type::EDGE_UPDATE:
                    sketching_algo.update(cmd.update);
                    break;
                case SketchCommand::Type::ACTIVATE_VERTEX:
                    sketching_algo.initialize_node(cmd.node);
                    break;
                case SketchCommand::Type::DEACTIVATE_VERTEX:
                    // Flush any buffered edge updates that may reference this
                    // vertex before removing it from the query structures.
                    // TODO - there should be a better way
                    sketching_algo.process_all_updates();
                    sketching_algo.uninitialize_node(cmd.node);
                    // std::cout << "SKETCH DEACTIVATE_VERTEX for vertex " << cmd.node << std::endl;
                    break;
                case SketchCommand::Type::RECLAIM_VERTEX:
                    // Sketch-side reclaim is currently equivalent to deactivation.
                    break;
            }
        }

        void process_and_publish(uint64_t last_seq, std::vector<GraphUpdate>& log_buffer) {
            sketching_algo.process_all_updates();
            log_buffer.clear();
            sketching_algo.drain_transaction_log(log_buffer);
            for (const auto& update : log_buffer) {
                committed_updates.push(update);
            }
            processed_seq_num.store(last_seq);
        }

        // static constexpr size_t kSketchCommandBatchSize = 65536; 
        static constexpr size_t kSketchCommandBatchSize = 1024;

        SketchAlgoClass sketching_algo;
        tbb::concurrent_queue<SketchCommand> command_queue;
        tbb::concurrent_queue<GraphUpdate> committed_updates;
        std::thread worker;
        std::atomic<bool> running{false};
        std::atomic<uint64_t> processed_seq_num{0};
    };

    class RecoverySubsystem {
    public:
        RecoverySubsystem(node_id_t num_nodes, size_t dense_threshold, size_t seed)
            : num_nodes(num_nodes), seed(seed), dense_threshold(dense_threshold) {}

        ~RecoverySubsystem() {
            stop();
            cleanup_all();
        }

        void start() {
            running.store(true);
            worker = std::thread([this]() {
                RecoveryCommand cmd;
                while (running.load()) {
                    if (!command_queue.try_pop(cmd)) {
                        std::this_thread::yield();
                        continue;
                    }
                    handle_command(cmd);
                }
                while (command_queue.try_pop(cmd)) {
                    handle_command(cmd);
                }
            });
        }

        void stop() {
            running.store(false);
            if (worker.joinable()) {
                worker.join();
            }
        }

        void enqueue(const RecoveryCommand& cmd) {
            command_queue.push(cmd);
        }

        void sync_to(uint64_t target_seq) const {
            while (processed_seq_num.load() < target_seq) {
                std::this_thread::yield();
            }
        }

        uint64_t processed_seq() const {
            return processed_seq_num.load();
        }

        node_id_t active_vertex_count() const {
            return active_vertices.load();
        }

        size_t space_usage_bytes() const {
            return approx_space_usage_bytes.load();
        }

        void set_threshold(size_t threshold) {
            dense_threshold = threshold;
        }

        void set_recovery_size(size_t recovery_size) {
            recovery_size_override = recovery_size;
        }

        bool recover_and_unsketch_vertex(node_id_t vertex, std::vector<node_id_t>& recovered_neighbors) {
            std::lock_guard<std::mutex> lock(mu);
            auto it = recovery_sketches.find(vertex);
            if (it == recovery_sketches.end()) {
                return false;
            }

            auto recovery_attempt = it->second->recover(true);
            if (recovery_attempt.result == FAILURE || recovery_attempt.result == PARTIAL_RECOVERY) {
                return false;
            }

            recovered_neighbors.clear();
            recovered_neighbors.reserve(recovery_attempt.recovered_indices.size());

            for (vec_t &vec : recovery_attempt.recovered_indices) {
                node_id_t other_vertex = (node_id_t)vec;
                recovered_neighbors.push_back(other_vertex);

                // When unsketching this vertex, also remove incidence from each
                // other endpoint's recovery sketch if it is currently active.
                auto other_it = recovery_sketches.find(other_vertex);
                if (other_it != recovery_sketches.end()) {
                    other_it->second->update(vertex);
                }
            }

            cancel_pending_retire(vertex);
            // std::cout << "RECOVERY UNINITIALIZE for vertex " << vertex << std::endl;
            approx_space_usage_bytes.fetch_sub(it->second->space_usage_bytes());
            delete it->second;
            recovery_sketches.erase(it);
            active_vertices.fetch_sub(1);
            return true;
        }

    private:
        void reclaim_retired(uint64_t watermark) {
            while (!retired_vertices.empty() && retired_vertices.front().first <= watermark) {
                node_id_t vertex = retired_vertices.front().second;
                retired_vertices.pop_front();

                auto it = recovery_sketches.find(vertex);
                if (it == recovery_sketches.end()) {
                    continue;
                }

                approx_space_usage_bytes.fetch_sub(it->second->space_usage_bytes());
                delete it->second;
                recovery_sketches.erase(it);
                active_vertices.fetch_sub(1);
            }
        }

        void cancel_pending_retire(node_id_t vertex) {
            if (retired_vertices.empty()) {
                return;
            }
            std::deque<std::pair<uint64_t, node_id_t>> filtered;
            filtered.clear();
            for (const auto& entry : retired_vertices) {
                if (entry.second != vertex) {
                    filtered.push_back(entry);
                }
            }
            retired_vertices.swap(filtered);
        }

        void handle_command(const RecoveryCommand& cmd) {
            std::lock_guard<std::mutex> lock(mu);
            switch (cmd.type) {
                case RecoveryCommand::Type::EDGE_UPDATE: {
                    Edge edge = inv_concat_pairing_fn(cmd.update);
                    auto it1 = recovery_sketches.find(edge.src);
                    if (it1 != recovery_sketches.end()) {
                        it1->second->update(edge.dst);
                    }
                    auto it2 = recovery_sketches.find(edge.dst);
                    if (it2 != recovery_sketches.end()) {
                        it2->second->update(edge.src);
                    }
                    break;
                }
                case RecoveryCommand::Type::ACTIVATE_VERTEX: {
                    cancel_pending_retire(cmd.vertex);
                    if (recovery_sketches.find(cmd.vertex) != recovery_sketches.end()) {
                        break;
                    }
                    double cleanup_adjustment_factor = 5.0 / (log2(num_nodes));
                    size_t recovery_size = (recovery_size_override > 0)
                        ? recovery_size_override
                        : std::max<size_t>(1, dense_threshold / 8);
                    auto* sketch = new RecoverySketchType((size_t)num_nodes, recovery_size, cleanup_adjustment_factor, (uint64_t)seed, false);
                    recovery_sketches[cmd.vertex] = sketch;
                    approx_space_usage_bytes.fetch_add(sketch->space_usage_bytes());
                    active_vertices.fetch_add(1);
                    break;
                }
                case RecoveryCommand::Type::DEACTIVATE_VERTEX: {
                    retired_vertices.push_back({cmd.seq_num, cmd.vertex});
                    break;
                }
                case RecoveryCommand::Type::RECLAIM_VERTEX:
                    reclaim_retired(cmd.seq_num);
                    break;
                case RecoveryCommand::Type::NOOP:
                    break;
            }
            processed_seq_num.store(cmd.seq_num);
        }

        void cleanup_all() {
            for (auto& pair : recovery_sketches) {
                delete pair.second;
            }
            recovery_sketches.clear();
            retired_vertices.clear();
            active_vertices.store(0);
            approx_space_usage_bytes.store(0);
        }

        node_id_t num_nodes;
        size_t seed;
        size_t dense_threshold;
        size_t recovery_size_override = 0;
        tbb::concurrent_queue<RecoveryCommand> command_queue;
        std::thread worker;
        std::atomic<bool> running{false};
        std::atomic<uint64_t> processed_seq_num{0};
        mutable std::mutex mu;

        absl::flat_hash_map<node_id_t, RecoverySketchType*> recovery_sketches;
        std::deque<std::pair<uint64_t, node_id_t>> retired_vertices;
        std::atomic<node_id_t> active_vertices{0};
        std::atomic<size_t> approx_space_usage_bytes{0};
    };

    std::atomic<uint64_t> current_seq_num{0};

    SketchSubsystem sketch_subsystem;
    RecoverySubsystem recovery_subsystem;

    // TODO - this aint a great way
    size_t MOVE_TO_SKETCH = 100;
    size_t DENSE_THRESHOLD = 2000;

    size_t seed;
    node_id_t num_nodes;

    // tracks which of our CF edges are from the sketching algo
    absl::flat_hash_set<edge_id_t> edges_from_sketch;

    std::vector<uint16_t> num_pending_dense_edges;
    std::vector<uint32_t> num_edges;
    std::vector<uint32_t> num_cf_edges;

    size_t total_num_edges = 0;
    size_t total_sketch_insertions = 0;
    size_t total_sketch_deletions = 0;
    size_t total_direct_sketch_inserts = 0;
    bool pending_connectivity_work = false;

    static constexpr uint64_t PERIODIC_SYNC_UPDATE_PERIOD = 50000;
    static constexpr uint64_t SKETCH_BACKLOG_WATERMARK = 1048576;
    uint64_t updates_since_checkpoint = 0;
    uint64_t last_flushed_sketch_seq = 0;

    std::vector<node_id_t> _neighbors_buffer;
    absl::flat_hash_set<node_id_t> _is_vertex_sketched;

    uint64_t next_seq_num() {
        return ++current_seq_num;
    }

    size_t count_explicit_neighbors(node_id_t vertex) {
        (void)vertex;
        return num_cf_edges[vertex];
    }

    inline void enqueue_activate_vertex(node_id_t vertex) {
        uint64_t seq = next_seq_num();
        sketch_subsystem.enqueue(SketchCommand{seq, SketchCommand::Type::ACTIVATE_VERTEX, GraphUpdate{}, vertex});
        recovery_subsystem.enqueue(RecoveryCommand{seq, RecoveryCommand::Type::ACTIVATE_VERTEX, 0, vertex});
    }

    inline void enqueue_deactivate_and_reclaim_vertex(node_id_t vertex) {
        uint64_t deactivate_seq = next_seq_num();
        sketch_subsystem.enqueue(SketchCommand{deactivate_seq, SketchCommand::Type::DEACTIVATE_VERTEX, GraphUpdate{}, vertex});
        recovery_subsystem.enqueue(RecoveryCommand{deactivate_seq, RecoveryCommand::Type::DEACTIVATE_VERTEX, 0, vertex});

        uint64_t reclaim_seq = next_seq_num();
        sketch_subsystem.enqueue(SketchCommand{reclaim_seq, SketchCommand::Type::RECLAIM_VERTEX, GraphUpdate{}, vertex});
        recovery_subsystem.enqueue(RecoveryCommand{reclaim_seq, RecoveryCommand::Type::RECLAIM_VERTEX, 0, vertex});
    }

    inline void enqueue_insert_to_sketch(node_id_t u, node_id_t v) {
        node_id_t src = std::min(u, v);
        node_id_t dst = std::max(u, v);
        uint64_t seq = next_seq_num();

        sketch_subsystem.enqueue(SketchCommand{seq, SketchCommand::Type::EDGE_UPDATE, GraphUpdate{Edge{src, dst}, INSERT}, 0});
        recovery_subsystem.enqueue(RecoveryCommand{seq, RecoveryCommand::Type::EDGE_UPDATE, concat_pairing_fn(src, dst), 0});
        total_sketch_insertions++;
    }

    inline void enqueue_delete_from_sketch(node_id_t u, node_id_t v) {
        node_id_t src = std::min(u, v);
        node_id_t dst = std::max(u, v);
        uint64_t seq = next_seq_num();

        sketch_subsystem.enqueue(SketchCommand{seq, SketchCommand::Type::EDGE_UPDATE, GraphUpdate{Edge{src, dst}, DELETE}, 0});
        recovery_subsystem.enqueue(RecoveryCommand{seq, RecoveryCommand::Type::EDGE_UPDATE, concat_pairing_fn(src, dst), 0});
        total_sketch_deletions++;
    }

    inline void enqueue_delete_from_sketch_only(node_id_t u, node_id_t v) {
        node_id_t src = std::min(u, v);
        node_id_t dst = std::max(u, v);
        uint64_t seq = next_seq_num();

        sketch_subsystem.enqueue(SketchCommand{seq, SketchCommand::Type::EDGE_UPDATE, GraphUpdate{Edge{src, dst}, DELETE}, 0});
        recovery_subsystem.enqueue(RecoveryCommand{seq, RecoveryCommand::Type::NOOP, 0, 0});
        total_sketch_deletions++;
    }

    void sync_queues() {
        uint64_t target_seq = current_seq_num.load();
        sketch_subsystem.sync_to(target_seq);
        recovery_subsystem.sync_to(target_seq);
    }

    inline void apply_connectivity_sync_barrier() {
        uint64_t target_seq = current_seq_num.load();
        sketch_subsystem.sync_to(target_seq);
        flush_transaction_log();
        last_flushed_sketch_seq = sketch_subsystem.processed_seq();
        pending_connectivity_work = false;
    }

    void sync_sketch_connectivity_if_needed() {
        if (!pending_connectivity_work) {
            return;
        }
        apply_connectivity_sync_barrier();
    }

    inline uint64_t sketch_backlog_size() const {
        uint64_t issued = current_seq_num.load();
        uint64_t processed = sketch_subsystem.processed_seq();
        return (issued >= processed) ? (issued - processed) : 0;
    }

    inline bool has_unflushed_sketch_commits() const {
        return sketch_subsystem.processed_seq() != last_flushed_sketch_seq;
    }

    void drain_committed_sketch_updates_if_any() {
        if (!has_unflushed_sketch_commits()) {
            return;
        }
        uint64_t processed_seq = sketch_subsystem.processed_seq();
        flush_transaction_log();
        last_flushed_sketch_seq = processed_seq;
    }

    void run_periodic_sync_checkpoint() {
        updates_since_checkpoint++;
        if (updates_since_checkpoint < PERIODIC_SYNC_UPDATE_PERIOD) {
            return;
        }
        updates_since_checkpoint = 0;

        if (pending_connectivity_work || sketch_backlog_size() >= SKETCH_BACKLOG_WATERMARK) {
            apply_connectivity_sync_barrier();
            return;
        }

        // Otherwise just opportunistically apply any already-committed sketch updates.
        drain_committed_sketch_updates_if_any();
    }

    bool is_forest_edge_from_sketch(Edge edge) {
        return edges_from_sketch.find(concat_pairing_fn(edge.src, edge.dst)) != edges_from_sketch.end();
    }

    bool is_edge_in_cf(Edge edge) {
        return cf_algo.leaves[edge.src]->getEdgeLevel(edge.dst) <= MAX_LEVEL;
    }

    bool is_vertex_sketched(node_id_t vertex) {
        return _is_vertex_sketched.find(vertex) != _is_vertex_sketched.end();
    }

    void initialize_vertex_sketch(node_id_t vertex) {
        if (is_vertex_sketched(vertex)) {
            return;
        }

        enqueue_activate_vertex(vertex);
        _is_vertex_sketched.insert(vertex);

        for (auto& level_edges : cf_algo.leaves[vertex]->vertex->E) {
            for (node_id_t neighbor : *level_edges.second) {
                if (is_vertex_sketched(neighbor)) {
                    num_pending_dense_edges[neighbor]++;
                }
            }
        }
    }

    void uninitialize_vertex_sketch(node_id_t vertex) {
        unlikely_if (!is_vertex_sketched(vertex)) {
            return;
        }

        _is_vertex_sketched.erase(vertex);
        for (auto& level_edges : cf_algo.leaves[vertex]->vertex->E) {
            for (node_id_t neighbor : *level_edges.second) {
                if (is_vertex_sketched(neighbor) && !is_forest_edge_from_sketch(Edge{vertex, neighbor})) {
                    num_pending_dense_edges[neighbor]--;
                }
            }
        }
        enqueue_deactivate_and_reclaim_vertex(vertex);
    }

    void flush_transaction_log() {
        GraphUpdate update;
        while (sketch_subsystem.try_pop_commit(update)) {
            edge_id_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
            if (update.type == DELETE) {
                remove_from_cf(update.edge.src, update.edge.dst);
                edges_from_sketch.erase(edge_id);
            } else {
                insert_to_cf(update.edge.src, update.edge.dst);
                edges_from_sketch.insert(edge_id);
            }
        }
    }

public:
    ParallelConnectivityManager(node_id_t num_nodes, uint32_t num_tiers, int batch_size, size_t seed)
        : cf_algo(num_nodes),
          sketch_subsystem(num_nodes, num_tiers, batch_size, seed),
          recovery_subsystem(num_nodes, DENSE_THRESHOLD, seed),
          seed(seed),
          num_nodes(num_nodes) {
        num_pending_dense_edges.resize(num_nodes, 0);
        num_cf_edges.resize(num_nodes, 0);
        num_edges.resize(num_nodes, 0);
        sketch_subsystem.start();
        recovery_subsystem.start();
    }

    ~ParallelConnectivityManager() {
        sketch_subsystem.stop();
        recovery_subsystem.stop();
    }

    void flush_edges_to_sketch(node_id_t vertex_to_flush) {
        _neighbors_buffer.clear();
        for (auto& level_edges : cf_algo.leaves[vertex_to_flush]->vertex->E) {
            for (node_id_t neighbor : *level_edges.second) {
                if (is_vertex_sketched(neighbor) && !is_forest_edge_from_sketch(Edge{vertex_to_flush, neighbor})) {
                    _neighbors_buffer.push_back(neighbor);
                }
            }
        }

        for (node_id_t neighbor : _neighbors_buffer) {
            remove_from_cf(vertex_to_flush, neighbor);
        }

        for (node_id_t neighbor : _neighbors_buffer) {
            if (neighbor != vertex_to_flush) {
                enqueue_insert_to_sketch(vertex_to_flush, neighbor);
            }
        }

        if (!_neighbors_buffer.empty()) {
            pending_connectivity_work = true;
        }

        num_pending_dense_edges[vertex_to_flush] = 0;
    }

    bool check_and_perform_recovery(node_id_t vertex) {
        if (!is_vertex_sketched(vertex)) {
            return false;
        }

        if (num_edges[vertex] > DENSE_THRESHOLD / 8) {
            return false;
        }

        // Ensure both subsystems are fully caught up before attempting recovery.
        sync_queues();

        _neighbors_buffer.clear();
        if (!recovery_subsystem.recover_and_unsketch_vertex(vertex, _neighbors_buffer)) {
            return false;
        }

        // Disconnect the vertex before destroying its sparse top-level forest node.
        for (node_id_t other_vertex : _neighbors_buffer) {
            if (other_vertex != vertex) {
                enqueue_delete_from_sketch_only(vertex, other_vertex);
            }
        }

        pending_connectivity_work = true;
        sync_queues();
        flush_transaction_log();
        last_flushed_sketch_seq = sketch_subsystem.processed_seq();

        uninitialize_vertex_sketch(vertex);

        // Complete sketch + recovery command processing and apply sketch commits.
        sync_queues();
        flush_transaction_log();
        last_flushed_sketch_seq = sketch_subsystem.processed_seq();
        pending_connectivity_work = false;

        for (node_id_t other_vertex : _neighbors_buffer) {
            if (other_vertex != vertex) {
                insert_to_cf(vertex, other_vertex);
            }
        }

        return true;
    }

    inline void insert_to_cf(node_id_t src, node_id_t dst) {
        cf_algo.insert(src, dst);
        num_cf_edges[src]++;
        num_cf_edges[dst]++;
    }

    inline void remove_from_cf(node_id_t src, node_id_t dst) {
        cf_algo.remove(src, dst);
        num_cf_edges[src]--;
        num_cf_edges[dst]--;
    }

    void update(GraphUpdate update) {
        run_periodic_sync_checkpoint();

        if (update.edge.src == update.edge.dst) {
            std::cout << "WARNING: self-loop detected on vertex " << update.edge.src << std::endl;
            return;
        }
        if (update.edge.src > update.edge.dst) {
            std::swap(update.edge.src, update.edge.dst);
        }

        if (update.type == INSERT) {
            num_edges[update.edge.src]++;
            num_edges[update.edge.dst]++;
            total_num_edges++;

#if defined(ENABLE_DIRECT_SKETCH) && ENABLE_DIRECT_SKETCH
            if (is_vertex_sketched(update.edge.src) && is_vertex_sketched(update.edge.dst)) {
                if (cf_algo.is_connected(update.edge.src, update.edge.dst)) {
                    total_direct_sketch_inserts++;
                    enqueue_insert_to_sketch(update.edge.src, update.edge.dst);
                    return;
                }
            }
#endif

            insert_to_cf(update.edge.src, update.edge.dst);

            if (is_vertex_sketched(update.edge.src)) {
                num_pending_dense_edges[update.edge.dst]++;
            }
            if (is_vertex_sketched(update.edge.dst)) {
                num_pending_dense_edges[update.edge.src]++;
            }

            unlikely_if (!is_vertex_sketched(update.edge.src) && num_edges[update.edge.src] >= DENSE_THRESHOLD) {
                initialize_vertex_sketch(update.edge.src);
                flush_edges_to_sketch(update.edge.src);
            }
            unlikely_if (!is_vertex_sketched(update.edge.dst) && num_edges[update.edge.dst] >= DENSE_THRESHOLD) {
                initialize_vertex_sketch(update.edge.dst);
                flush_edges_to_sketch(update.edge.dst);
            }

            if (is_vertex_sketched(update.edge.dst) && num_pending_dense_edges[update.edge.dst] >= MOVE_TO_SKETCH) {
                flush_edges_to_sketch(update.edge.dst);
            }
            if (is_vertex_sketched(update.edge.src) && num_pending_dense_edges[update.edge.src] >= MOVE_TO_SKETCH) {
                flush_edges_to_sketch(update.edge.src);
            }
        } else if (update.type == DELETE) {
            num_edges[update.edge.src]--;
            num_edges[update.edge.dst]--;
            total_num_edges--;

            if (this->is_edge_in_cf(update.edge)) {
                edge_id_t edge_id = concat_pairing_fn(update.edge.src, update.edge.dst);
                if (edges_from_sketch.find(edge_id) != edges_from_sketch.end()) {
                    enqueue_delete_from_sketch(update.edge.src, update.edge.dst);
                    pending_connectivity_work = true;
                    check_and_perform_recovery(update.edge.src);
                    check_and_perform_recovery(update.edge.dst);
                } else {
                    remove_from_cf(update.edge.src, update.edge.dst);
                    if (is_vertex_sketched(update.edge.dst)) {
                        num_pending_dense_edges[update.edge.src]--;
                    }
                    if (is_vertex_sketched(update.edge.src)) {
                        num_pending_dense_edges[update.edge.dst]--;
                    }
                }
            } else {
                // Non-tree edges live only in the sketch path, so delete them immediately.
                enqueue_delete_from_sketch(update.edge.src, update.edge.dst);
                check_and_perform_recovery(update.edge.src);
                check_and_perform_recovery(update.edge.dst);
            }

            // TODO - implement check_and_perform_recovery
            // needs somehow be communicated between the two threads, so
            // isn't entirely ready quite yet.
        }
    }

    bool connectivity_query(node_id_t a, node_id_t b) {
        // Connectivity only depends on sketch commits when flagged.
        sync_sketch_connectivity_if_needed();
        return cf_algo.is_connected(a, b);
    }

    void force_sync() {
        sync_queues();
        flush_transaction_log();
        last_flushed_sketch_seq = sketch_subsystem.processed_seq();
        pending_connectivity_work = false;
        updates_since_checkpoint = 0;
    }

#ifdef CORRECTNESS_DIAGNOSTICS
    TierMaximalityCheck take_tier_maximality_check() {
        sync_queues();
        return sketch_subsystem.take_tier_maximality_check();
    }
#endif

    std::vector<std::set<node_id_t>> cc_query() {
        sync_sketch_connectivity_if_needed();
        std::vector<std::set<node_id_t>> ret;
        std::unordered_map<uint64_t, std::set<node_id_t>> component_map;
        for (node_id_t i = 0; i < num_nodes; i++) {
            localTree* root = localTree::getRoot(cf_algo.leaves[i]);
            uint64_t root_id = (uint64_t)root;
            auto it = component_map.find(root_id);
            if (it == component_map.end()) {
                component_map[root_id] = std::set<node_id_t>();
            }
            component_map[root_id].insert(i);
        }
        for (auto& pair : component_map) {
            ret.push_back(pair.second);
        }
        return ret;
    }

    size_t num_sketched_vertices() const {
        return _is_vertex_sketched.size();
    }

    size_t total_edges() const {
        return total_num_edges;
    }

    size_t num_sketch_insertions() const {
        return total_sketch_insertions;
    }

    size_t num_sketch_deletions() const {
        return total_sketch_deletions;
    }

    size_t num_sketched_edges() const {
        return (total_sketch_insertions >= total_sketch_deletions)
            ? (total_sketch_insertions - total_sketch_deletions)
            : 0;
    }

    size_t num_direct_sketch_inserts() const {
        return total_direct_sketch_inserts;
    }

    size_t num_direct_sketch_edges() const {
        // Backward-compatible alias; this is cumulative direct sketch inserts.
        return num_direct_sketch_inserts();
    }

    // TODO - this isnt locked rn properly
    // it should roughly be fine though?
    long get_num_tree_ops() const { return sketch_subsystem.get_num_tree_ops(); }
    int get_max_link_tier() const { return sketch_subsystem.get_max_link_tier(); }
    void reset_max_link_tier() { sketch_subsystem.reset_max_link_tier(); }

    void end() {
        sync_queues();
        flush_transaction_log();
        sketch_subsystem.stop();
        recovery_subsystem.stop();
        sketch_subsystem.end_algo();
    }

    size_t get_space_usage_cf() {
        return cf_algo.getMemUsage();
    }

    HybridSpaceReport report_space_usage() {
        sync_queues();
        flush_transaction_log();

        HybridSpaceReport report;
        report.cf_space_bytes = get_space_usage_cf();
        report.driver_space_bytes = get_space_usage_driver();
        report.recovery_sketch_space_bytes = space_usage_recovery_sketch();
        report.sketch_forest_report = sketch_subsystem.report_space_usage();

        report.total_num_edges = total_edges();
        report.num_sketched_vertices = num_sketched_vertices();
        report.num_sketch_insertions = num_sketch_insertions();
        report.num_sketch_deletions = num_sketch_deletions();
        report.num_sketched_edges = num_sketched_edges();
        report.num_direct_sketch_inserts = num_direct_sketch_inserts();
        return report;
    }

    size_t get_space_usage_driver() {
        size_t total = sizeof(*this);

        total += num_pending_dense_edges.capacity() * sizeof(uint16_t);
        total += num_edges.capacity() * sizeof(uint32_t);
        total += num_cf_edges.capacity() * sizeof(uint32_t);

        total += _neighbors_buffer.capacity() * sizeof(node_id_t);

        total += _is_vertex_sketched.bucket_count() * sizeof(node_id_t);
        total += edges_from_sketch.bucket_count() * sizeof(edge_id_t);

        return total;
    }

    size_t space_usage_conn_sketch() {
        return sketch_subsystem.space_usage_bytes();
    }

    size_t space_usage_recovery_sketch() {
        return recovery_subsystem.space_usage_bytes();
    }
};
