#include <mpi.h>
#include <gtest/gtest.h>
#include "util.h"


std::string stream_file;
int command_line_n = 0;
int command_line_k = 0;
int command_line_num_trials = 0;
long command_line_seed = -1;

int main(int argc, char** argv) {
    int arg_index = 1;
    struct {
        std::string* var;
        bool is_long;
    } args[] = {
        {reinterpret_cast<std::string*>(&stream_file), false},
        {nullptr, false}  // n
    };

    // Parse positional arguments
    int arg_count = 0;
    while (arg_index < argc && argv[arg_index][0] != '-') {
        std::string arg_str(argv[arg_index++]);
        
        if (arg_count == 0) stream_file = arg_str;
        else if (arg_count == 1) command_line_n = std::stoi(arg_str);
        else if (arg_count == 2) command_line_k = std::stoi(arg_str);
        else if (arg_count == 3) command_line_num_trials = std::stoi(arg_str);
        else if (arg_count == 4) command_line_seed = std::stol(arg_str);
        else break;
        
        arg_count++;
    }

    testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}