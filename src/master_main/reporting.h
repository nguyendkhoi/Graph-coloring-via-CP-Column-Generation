#pragma once

#include "driver.h"
#include "../column_generation/rmp.h"
#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

#include <string>

void print_run_header(
    const MasterRunConfig& config,
    const Graph& G,
    const ColumnPool& pool,
    int proven_lb,
    int incumbent_ub
);

void print_iteration(
    int iter,
    const RMPSolution& sol,
    double reduced_cost,
    long long lower_bound,
    int upper_bound,
    const std::string& step,
    int column_count
);

int count_active_lambdas(const RMPSolution& sol);

void print_final_report(
    int iterations,
    const RMPSolution& sol,
    const RMPSolver& rmp,
    int proven_lb,
    int incumbent_ub,
    bool closed_gap,
    bool converged_by_pricing
);
