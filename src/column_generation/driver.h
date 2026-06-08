#pragma once

#include <cstddef>
#include <string>

struct MasterRunConfig {
    std::string instance_path = "tests/queen5_5.col";
    int num_trials = 20;
    std::size_t seed = 40;
    int max_iter = 100000;
};

MasterRunConfig parse_master_args(int argc, char** argv);

int run_column_generation(const MasterRunConfig& config);
