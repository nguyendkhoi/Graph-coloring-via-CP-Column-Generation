#pragma once

#include "driver.h"
#include "onnx.h"
#include "output_writer.h"
#include "pricing_helpers.h"
#include "../column_generation/pricing.h"
#include "../column_generation/rmp.h"
#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

#include <gurobi_c++.h>
#include <functional>

struct CGLoopContext {
    GRBEnv& env;
    RMPSolver& rmp;
    ColumnPool& pool;
    const Graph& G;
    MasterRunSummary& summary;
    JsonlLogger& logger;
    OnnxSession* onnx;                        // nullable — null disables AI ordering
    int proven_lb;                            // updated in-place by loop
    int incumbent_ub;
    double adaptive_lower_bound;
    std::function<double()> elapsed_seconds;
};

struct CGLoopResult {
    int exit_code = 0;
    int iterations = 0;
    bool converged_by_pricing = false;
    bool reached_max_iter = false;
    RMPSolution sol;
};

// Run the column generation loop.
// Mutates ctx.proven_lb, ctx.incumbent_ub, ctx.adaptive_lower_bound,
// ctx.summary.reached_time_limit.
// Returns exit code: 0 = normal, 2 = RMP infeasible/error.
CGLoopResult run_cg_loop(CGLoopContext& ctx);
