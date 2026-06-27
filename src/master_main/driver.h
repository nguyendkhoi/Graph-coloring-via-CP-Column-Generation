#pragma once

#include <cstddef>
#include <string>

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

std::string load_master_configured_instance_path(const std::string& override_instance_path = "");

int run_column_generation(const std::string& instance_path);
