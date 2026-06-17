#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct MasterRunConfig {
    std::string instance_path = "tests/queen5_5.col";
    int num_trials = 20;
    std::size_t seed = 40;
    int max_iter = 100000;
    double mwss_time_limit_seconds = 40.0;
    double augmented_time_limit_seconds = 40.0;
    std::string output_path;
    bool append_output = true;
    std::vector<std::string> output_columns;
};

MasterRunConfig parse_master_args(int argc, char** argv);

int run_column_generation(const MasterRunConfig& config);
