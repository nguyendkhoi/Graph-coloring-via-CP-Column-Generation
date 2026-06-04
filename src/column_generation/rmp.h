#pragma once

#include "stable_set.h"
#include "gurobi_c++.h"

#include <vector>

struct RMPSolution {
    double objective = 0.0;
    std::vector<double> lambda_value;
    std::vector<double> dual_value;
    int status = 0;
};

// Restricted Master Problem: LP relaxation of the set-cover formulation.
class RMPSolver {
private:
    GRBModel model;
    std::vector<GRBVar> lambda;
    std::vector<GRBConstr> constraints;
    int num_vertices;

public:
    RMPSolver(GRBEnv& env, int num_vertices);

    void add_column(const StableColumn& col);
    void add_column_pool(const ColumnPool& pool);

    int column_count() const { return static_cast<int>(lambda.size()); }

    RMPSolution solve();
};
