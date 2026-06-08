#pragma once

#include "stable_set.h"
#include "gurobi_c++.h"
#include <graph/graph.h>
#include "../coloring/cp.h"
#include <vector>

#include <gecode/int.hh>
#include <gecode/search.hh>

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

// 4 Constraint Programming-based Column Generation
class CP_CG : public Gecode::Space {
public:
    const Graph& G;
    Gecode::BoolVarArray x;

    CP_CG(const Graph& graph, std::vector<int>max_clique);

    Gecode::Space* copy() override;

    void symetrique_breaking(std::vector<int> clique);

private:
    CP_CG(CP_CG& other);
};

CPSolveResult solve_CP_CG(CP_CG& cp_cg, const std::vector<double>& dual_value, double threshhold);