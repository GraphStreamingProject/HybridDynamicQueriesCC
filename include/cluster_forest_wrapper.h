#pragma once

#ifndef DYCON_SCCWN_HPP_INCLUDED
#define DYCON_SCCWN_HPP_INCLUDED
#include <dycon/localTree/SCCWN.hpp>
#endif
#include "types.h"
#include "util.h"

template <typename... Args>
class ClusterForestWrapper : public SCCWN<Args...> {
public:
    ClusterForestWrapper(node_id_t num_nodes, uint64_t seed = 0) : SCCWN<Args...>(num_nodes) {
        // seed is ignored
    }

    void update(GraphUpdate op) {
        if (op.type == INSERT) {
            this->insert(op.edge.src, op.edge.dst);
        } else if (op.type == DELETE) {
            this->remove(op.edge.src, op.edge.dst);
        }
    }

    void initialize_all_nodes() {
        // no-op, SCCWN constructed with num_nodes handles this
    }

    SpaceReport report_space_usage() {
        SpaceReport report;
        
        // Report the single tier (CF)
        report.tier_reports.push_back({
            0, // tier_num dummy 
            this->getMemUsage(), // space_bytes
            this->CC_stat().size()  // num_components
        });
        
        report.query_tree_bytes = 0;
        report.top_level_lct_bytes = 0;
        return report;
    }
};

