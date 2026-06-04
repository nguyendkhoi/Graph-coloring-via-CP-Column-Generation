#pragma once

#include "stable_set.h"
#include "gurobi_c++.h"
#include <graph/graph.h>
#include <vector>

struct MWSSResult {
    StableColumn col;
    double reduced_cost = 0.0;
};

bool is_stable_set(const Graph& G, const std::vector<int>& vertices);

// Solve Maximum Weight Stable Set via Gurobi.
// Returns true if a column with negative reduced cost was found.
bool solve_mwss(
    GRBEnv& env,
    const Graph& G,
    const std::vector<double>& dual_value,
    MWSSResult& res
);
