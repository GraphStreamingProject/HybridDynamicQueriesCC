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

inline void write_space_report_tsv(const std::vector<SpaceReportMessage>& reports, std::ostream& out, long update_idx = -1) {
    out << "update_idx\ttier\tspace_bytes\tnum_components" << std::endl;
    size_t total = 0;
    for (const auto& r : reports) {
        out << update_idx << "\t" << r.tier_num << "\t" << r.space_bytes << "\t" << r.num_components << std::endl;
        total += r.space_bytes;
    }
    out << update_idx << "\ttotal\t" << total << "\t-" << std::endl;
}

inline void write_space_report_tsv(const std::vector<SpaceReportMessage>& reports, const std::string& file_path, bool append = true, long update_idx = -1) {
    std::ofstream out(file_path, append ? std::ios_base::app : std::ios_base::out);
    write_space_report_tsv(reports, out, update_idx);
}extern std::string stream_file;
extern int batch_size_arg;
extern double height_factor_arg;
extern int hybrid_threshold_arg;

//#define START(X) auto X = std::chrono::high_resolution_clock::now()
//#define STOP(C, X) C += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - X).count()

#define START(X) ;
#define STOP(C, X) ;

#define VERTICES_TO_EDGE(A, B) A<B ? (((edge_id_t)A)<<32) + ((edge_id_t)B) : (((edge_id_t)B)<<32) + ((edge_id_t)A)

#define MAX_INT (std::numeric_limits<int>::max())
