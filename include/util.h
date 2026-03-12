#pragma once
#include <chrono>
#include "types.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <string>

typedef struct {
  uint32_t tier_num = 0;
  size_t space_bytes = 0;
  size_t num_components = 0;
} SpaceReportMessage;

struct SpaceReport {
  std::vector<SpaceReportMessage> tier_reports;
  size_t query_tree_bytes = 0;     // SketchlessETT (query structure)
  size_t top_level_lct_bytes = 0;  // LinkCutTree (connectivity tracking)
};

/**
 * Compute the "first maximal tier": the first tier i such that tier i+1
 * has the same number of components. Returns -1 if no such tier exists.
 */
inline int compute_first_maximal_tier(const std::vector<SpaceReportMessage>& reports) {
    for (size_t t = 0; t + 1 < reports.size(); t++) {
        if (reports[t].num_components == reports[t + 1].num_components) {
            return static_cast<int>(reports[t].tier_num);
        }
    }
    return -1;
}

inline int compute_first_maximal_tier(const SpaceReport& report) {
    return compute_first_maximal_tier(report.tier_reports);
}

inline void write_space_report_tsv(const SpaceReport& report, std::ostream& out, long update_idx = -1, bool write_header = true) {
    if (write_header) {
        out << "update_idx\ttier\tspace_bytes\tnum_components\tmaximal_tier\tquery_tree_bytes\ttop_level_lct_bytes" << std::endl;
    }
    int maximal_tier = compute_first_maximal_tier(report.tier_reports);
    for (const auto& r : report.tier_reports) {
        size_t qt = (r.tier_num == 0) ? report.query_tree_bytes : 0;
        size_t lct = (r.tier_num == 0) ? report.top_level_lct_bytes : 0;
        out << update_idx << "\t" << r.tier_num << "\t" << r.space_bytes << "\t" << r.num_components << "\t" << maximal_tier << "\t" << qt << "\t" << lct << std::endl;
    }
}

struct HybridSpaceReport {
    size_t cf_space_bytes = 0;
    size_t driver_space_bytes = 0;
    size_t recovery_sketch_space_bytes = 0;
    SpaceReport sketch_forest_report;

    // Scaling metrics
    size_t num_sketched_vertices = 0;
    size_t total_num_edges = 0;
    size_t num_sketched_edges = 0;
    size_t num_direct_sketch_edges = 0;
};

inline int compute_first_maximal_tier(const HybridSpaceReport& report) {
    return compute_first_maximal_tier(report.sketch_forest_report.tier_reports);
}

inline void write_space_report_tsv(const SpaceReport& report, const std::string& file_path, bool append = true, long update_idx = -1) {
    std::ofstream out(file_path, append ? std::ios_base::app : std::ios_base::out);
    write_space_report_tsv(report, out, update_idx, !append);

    // Write summary to a separate file
    std::string summary_path = file_path.substr(0, file_path.rfind('.')) + "_summary.tsv";
    std::ofstream summary(summary_path, append ? std::ios_base::app : std::ios_base::out);
    if (!append) {
        summary << "update_idx\ttotal_space_bytes\tquery_tree_bytes\ttop_level_lct_bytes\tmaximal_tier" << std::endl;
    }
    size_t total = 0;
    for (const auto& r : report.tier_reports) {
        total += r.space_bytes;
    }
    summary << update_idx << "\t" << total << "\t" << report.query_tree_bytes << "\t" << report.top_level_lct_bytes << "\t" << compute_first_maximal_tier(report.tier_reports) << std::endl;
}

inline void write_space_report_tsv(const HybridSpaceReport& hybrid_report, const std::string& file_path, bool append = true, long update_idx = -1) {
    // Write standard tiers TSV
    write_space_report_tsv(hybrid_report.sketch_forest_report, file_path, append, update_idx);
    
    // Write hybrid summary to a custom hybrid summary file
    std::string summary_path = file_path.substr(0, file_path.rfind('.')) + "_hybrid_summary.tsv";
    std::ofstream summary(summary_path, append ? std::ios_base::app : std::ios_base::out);
    if (!append) {
        summary << "update_idx\ttotal_cf_bytes\ttotal_driver_bytes\ttotal_recovery_bytes\ttotal_sketch_bytes\tmaximal_tier\ttotal_edges\tnum_sketched_vertices\tnum_sketched_edges\tnum_direct_sketch_edges" << std::endl;
    }
    size_t total_sketch_bytes = 0;
    for (const auto& r : hybrid_report.sketch_forest_report.tier_reports) {
        total_sketch_bytes += r.space_bytes;
    }
    summary << update_idx << "\t"
            << hybrid_report.cf_space_bytes << "\t"
            << hybrid_report.driver_space_bytes << "\t"
            << hybrid_report.recovery_sketch_space_bytes << "\t"
            << total_sketch_bytes << "\t"
            << compute_first_maximal_tier(hybrid_report.sketch_forest_report.tier_reports) << "\t"
            << hybrid_report.total_num_edges << "\t"
            << hybrid_report.num_sketched_vertices << "\t"
            << hybrid_report.num_sketched_edges << "\t"
            << hybrid_report.num_direct_sketch_edges << std::endl;
}

extern std::string stream_file;
extern int batch_size_arg;
extern double height_factor_arg;
extern int hybrid_threshold_arg;

//#define START(X) auto X = std::chrono::high_resolution_clock::now()
//#define STOP(C, X) C += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - X).count()

#define START(X) ;
#define STOP(C, X) ;

#define VERTICES_TO_EDGE(A, B) A<B ? (((edge_id_t)A)<<32) + ((edge_id_t)B) : (((edge_id_t)B)<<32) + ((edge_id_t)A)

#define MAX_INT (std::numeric_limits<int>::max())
