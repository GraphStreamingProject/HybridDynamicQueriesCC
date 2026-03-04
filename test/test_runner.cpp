#include <mpi.h>
#include <gtest/gtest.h>
#include "util.h"


std::string stream_file;
int command_line_n = 0;
int command_line_k = 0;
int command_line_num_trials = 0;
long command_line_seed = -1;

int main(int argc, char** argv) {
    if (argc > 1) stream_file = argv[1];
    if (argc > 2) command_line_n = std::stoi(argv[2]);
    if (argc > 3) command_line_k = std::stoi(argv[3]);
    if (argc > 4) command_line_num_trials = std::stoi(argv[4]);
    if (argc > 5) command_line_seed = std::stol(argv[5]);

    testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}