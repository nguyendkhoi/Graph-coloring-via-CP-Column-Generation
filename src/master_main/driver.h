#pragma once

#include <cstddef>
#include <string>

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
};

struct MasterRunSummary {
    std::string run_id;
    std::string run_output_dir;
    std::string run_log_path;
    std::string summary_csv_path;
    std::string summary_json_path;
    std::string instance_path;
    std::string instance;
    int n = 0;
    long long m = 0;
    int num_trials = 0;
    std::size_t seed = 0;
    int threads = 1;
    double time_limit_seconds = 0.0;
    double run_time_seconds = 0.0;
    int iterations = 0;
    std::string rmp_status = "NOT_RUN";
    double lp_objective = 0.0;
    int proven_lb = 0;
    int incumbent_ub = 0;
    int column_count = 0;
    int active_lambdas = 0;
    int total_lambdas = 0;
    bool closed_gap = false;
    bool converged_by_pricing = false;
    bool reached_max_iter = false;
    bool reached_time_limit = false;
    int exit_code = 0;
};

MasterRunConfig parse_master_args(int argc, char** argv);

int run_column_generation(const MasterRunConfig& config);
