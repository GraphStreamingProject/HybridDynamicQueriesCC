
#include "../include/graph_tiers.h"
#include "util.h"
#include <random>
#include <atomic>

// #define CANARY(X) do {if (update.edge.src == 1784 && update.edge.dst == 4420) { std::cout << __FILE__ << ":" << __LINE__ << " says " << X << std::endl;}} while (false)
#define CANARY(X) ;
// #define ENDPOINT_CANARY(X, src, dst) do {if ((src == 7781 || dst == 7781)) {std::cout << __FILE__ << ":" << __LINE__ << " says " << X << " " << src << " " << dst << std::endl;}} while (false)
#define ENDPOINT_CANARY(X, src, dst) ;

long lct_time = 0;
long ett_time = 0;
long ett_find_root = 0;
long ett_get_agg = 0;
long sketch_query = 0;
long sketch_time = 0;
long refresh_time = 0;
long parallel_isolated_check = 0;
long tiers_grown = 0;
long normal_refreshes = 0;


template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
GraphTiers<SketchClass>::GraphTiers(node_id_t num_nodes) : link_cut_tree(num_nodes) {
	// Algorithm parameters
	uint32_t num_tiers = log2(num_nodes)/(log2(3)-1);

	// Initialize all the ETTs
	std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(0,MAX_INT);
    int seed = dist(rng);
    std::cout << "SEED: " << seed << std::endl;
    rng.seed(seed);
	dist(rng); // To give 1:1 correspondence with MPI seeds
	for (uint32_t i = 0; i < num_tiers; i++) {
		int tier_seed = dist(rng);
		ett.emplace_back(num_nodes, i, tier_seed);
	}

	root_nodes.reserve(num_tiers*2);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
GraphTiers<SketchClass>::~GraphTiers() {}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void GraphTiers<SketchClass>::update(GraphUpdate update) {
	edge_id_t edge = VERTICES_TO_EDGE(update.edge.src, update.edge.dst);
	// Update the sketches of both endpoints of the edge in all tiers
	if (update.type == DELETE && link_cut_tree.has_edge(update.edge.src, update.edge.dst)) {
		link_cut_tree.cut(update.edge.src, update.edge.dst);
	}
	START(su);
	#pragma omp parallel for
	for (uint32_t i = 0; i < ett.size(); i++) {
		if (update.type == DELETE && ett[i].has_edge(update.edge.src, update.edge.dst)) {
			ett[i].cut(update.edge.src, update.edge.dst);
			ENDPOINT_CANARY("Cutting Tier " << i << " ETT With", update.edge.src, update.edge.dst);
		}
		root_nodes[2*i] = ett[i].update_sketch(update.edge.src, (vec_t)edge);
		root_nodes[2*i+1] = ett[i].update_sketch(update.edge.dst, (vec_t)edge);
		ENDPOINT_CANARY("Updating Sketch With", update.edge.src, update.edge.dst);
	}
	STOP(sketch_time, su);
	// Refresh the data structure
	START(ref);
	refresh(update);
	STOP(refresh_time, ref);
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
void GraphTiers<SketchClass>::refresh(GraphUpdate update) {
	// In parallel check if all tiers are not isolated
	START(iso);
	std::atomic<bool> isolated(false);
	//#pragma omp parallel for
	for (uint32_t tier = 0; tier < ett.size()-1; tier++) {
		// Check if the tree containing first endpoint is isolated
		uint32_t tier_size1 = root_nodes[2*tier]->size;
		uint32_t next_size1 = root_nodes[2*(tier+1)]->size;
		if (tier_size1 == next_size1) {
			root_nodes[2*tier]->process_updates();
			SketchClass* ett_agg1 = root_nodes[2*tier]->sketch_agg;
			ett_agg1->reset_sample_state();
			SketchSample<> query_result1 = ett_agg1->sample();
			if (query_result1.result == GOOD) {
				isolated = true;
				continue;
			}
		}
		// Check if the tree containing second endpoint is isolated
		uint32_t tier_size2 = root_nodes[2*tier+1]->size;
		uint32_t next_size2 = root_nodes[2*(tier+1)+1]->size;
		if (tier_size2 == next_size2) {
			root_nodes[2*tier+1]->process_updates();
			SketchClass* ett_agg2 = root_nodes[2*tier+1]->sketch_agg;
			ett_agg2->reset_sample_state();
			SketchSample query_result2 = ett_agg2->sample();
			if (query_result2.result == GOOD) {
				isolated = true;
				continue;
			}
		}
	}
	STOP(parallel_isolated_check, iso);
	if (!isolated)
		return;
	normal_refreshes++;
	// For each tier for each endpoint of the edge
	for (uint32_t tier = 0; tier < ett.size()-1; tier++) {
		for (node_id_t v : {update.edge.src, update.edge.dst}) {
			// Check if the tree containing this endpoint is isolated
			START(size);
			uint32_t tier_size = ett[tier].get_size(v);
			uint32_t next_size = ett[tier+1].get_size(v);
			STOP(ett_find_root, size);
			// Check for same size for isolated
			if (tier_size != next_size)
				continue;

			START(agg);
			SkipListNode<SketchClass>* root = ett[tier].get_root(v);
			root->process_updates();
			SketchClass* ett_agg = root->sketch_agg;
			STOP(ett_get_agg, agg);
			START(sq);
			ett_agg->reset_sample_state();
			SketchSample query_result = ett_agg->sample();
			STOP(sketch_query, sq);

			// Check for new edge to eliminate isolation
			if (query_result.result != GOOD)
				continue;

			tiers_grown++;
			edge_id_t edge = query_result.idx;
			node_id_t a = (node_id_t)edge;
			node_id_t b = (node_id_t)(edge>>32);

			// Check if a path exists between the edge's endpoints
			START(lct1);
			void* a_root = link_cut_tree.find_root(a);
			void* b_root = link_cut_tree.find_root(b);
			STOP(lct_time, lct1);
			if (a_root == b_root) {
				START(lct2);
				// Find the maximum tier edge on the path and what tier it first appeared on
				std::pair<edge_id_t, uint32_t> max = link_cut_tree.path_aggregate(a,b);
				node_id_t c = (node_id_t)max.first;
				node_id_t d = (node_id_t)(max.first>>32);
				STOP(lct_time, lct2);

				// Remove the maximum tier edge on all paths where it exists
				START(ett1);
				#pragma omp parallel for
				for (uint32_t i = max.second; i < ett.size(); i++) {
					ett[i].cut(c,d);
					ENDPOINT_CANARY("Cutting Tier " << i << " ETT With", c, d);
				}
				STOP(ett_time, ett1);
				START(lct3);
				link_cut_tree.cut(c,d);
				STOP(lct_time, lct3);
			}

			// Join the ETTs for the endpoints of the edge on all tiers above the current
			START(ett2);
			#pragma omp parallel for
			for (uint32_t i = tier+1; i < ett.size(); i++) {
				ett[i].link(a,b);
				ENDPOINT_CANARY("Linking Tier " << i << " ETT With", a, b);
			}
			STOP(ett_time, ett2);
			START(lct4);
			link_cut_tree.link(a,b, tier+1);
			STOP(lct_time, lct4);
		}
	}
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
std::vector<std::set<node_id_t>> GraphTiers<SketchClass>::get_cc() {
	std::vector<std::set<node_id_t>> cc;
	std::set<EulerTourNode<SketchClass>*> visited;
	int top = ett.size()-1;
	for (uint32_t i = 0; i < ett[top].ett_nodes.size(); i++) {
		if (visited.find(&ett[top].ett_nodes[i]) == visited.end()) {
			std::set<EulerTourNode<SketchClass>*> pointer_component = ett[top].ett_nodes[i].get_component();
			std::set<node_id_t> component;
			for (auto pointer : pointer_component) {
				component.insert(pointer->vertex);
				visited.insert(pointer);
			}
			cc.push_back(component);
		}
	}
	return cc;
}

template <typename SketchClass> requires(SketchColumnConcept<SketchClass, vec_t>)
bool GraphTiers<SketchClass>::is_connected(node_id_t a, node_id_t b) {
	return this->link_cut_tree.find_root(a) == this->link_cut_tree.find_root(b);
}

template class GraphTiers<DefaultSketchColumn>;