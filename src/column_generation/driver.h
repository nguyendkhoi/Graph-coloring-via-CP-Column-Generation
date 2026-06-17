#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct MasterRunConfig {
    std::string instance_path = "tests/queen5_5.col";
    int num_trials = 20;
    int initial_columns_target = 0;
    std::size_t seed = 40;
    int max_iter = 100000;
    int threads = 1;
    double time_limit_seconds = 3600.0;
    double decision_pricing_limit_seconds = 5.0;
    double mwss_time_limit_seconds = 40.0;
    double augmented_time_limit_seconds = 40.0;
    std::string vertex_ordering = "dual_desc";
    bool enable_ml = false;
    bool log_vertex_features = true;
    int schema_version = 1;
    std::string log_dir = "master_cp/runs";
    std::string output_path;
    bool append_output = true;
    std::vector<std::string> output_columns;
};

MasterRunConfig parse_master_args(int argc, char** argv);

int run_column_generation(const MasterRunConfig& config);
