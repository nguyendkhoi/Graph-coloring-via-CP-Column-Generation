#include "rmp.h"
#include "gurobi_c++.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace std;

RMPSolver::RMPSolver(GRBEnv& env, int num_vertices)
    : model(env), num_vertices(num_vertices) {
    constraints.resize(num_vertices);
    for (int v = 0; v < num_vertices; v++) {
        GRBLinExpr expr = 0.0;
        constraints[v] = model.addConstr(expr >= 1.0,
            "constraint_vertice_" + to_string(v));
    }
    model.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);
    model.update();
}

void RMPSolver::add_column(const StableColumn& col) {
    GRBColumn new_col;
    for (int v : col.vertices) {
        new_col.addTerm(1.0, constraints[v]);
    }
    GRBVar var = model.addVar(0.0, GRB_INFINITY, 1.0, GRB_CONTINUOUS, new_col,
        "lambda_" + to_string(int(lambda.size())));
    lambda.push_back(var);
}

void RMPSolver::add_column_pool(const ColumnPool& pool) {
    for (const StableColumn& col : pool.columns) {
        add_column(col);
    }
}

RMPSolution RMPSolver::solve() {
    RMPSolution sol;

    try {
        model.optimize();
        sol.status = model.get(GRB_IntAttr_Status);

        if (sol.status == GRB_OPTIMAL) {
            sol.lambda_value.resize(lambda.size());
            for (int i = 0; i < (int)lambda.size(); i++) {
                sol.lambda_value[i] = lambda[i].get(GRB_DoubleAttr_X);
            }
            sol.dual_value.resize((int)constraints.size());
            for (int i = 0; i < (int)constraints.size(); i++) {
                sol.dual_value[i] = constraints[i].get(GRB_DoubleAttr_Pi);
            }
            sol.objective = model.get(GRB_DoubleAttr_ObjVal);
            sol.dual_objective = 0.0;
            sol.min_dual_value = numeric_limits<double>::infinity();
            for (double dual : sol.dual_value) {
                sol.dual_objective += dual;
                sol.min_dual_value = min(sol.min_dual_value, dual);
            }
            if (sol.dual_value.empty()) {
                sol.min_dual_value = 0.0;
            }

            sol.duality_gap = fabs(sol.objective - sol.dual_objective);
            sol.max_reduced_cost_error = 0.0;
            for (size_t j = 0; j < lambda.size(); ++j) {
                double manual_reduced_cost = 1.0;
                for (size_t v = 0; v < constraints.size(); ++v) {
                    double coefficient = model.getCoeff(constraints[v], lambda[j]);
                    manual_reduced_cost -= coefficient * sol.dual_value[v];
                }

                double gurobi_reduced_cost =
                    lambda[j].get(GRB_DoubleAttr_RC);
                sol.max_reduced_cost_error = max(
                    sol.max_reduced_cost_error,
                    fabs(manual_reduced_cost - gurobi_reduced_cost)
                );
            }

            constexpr double tolerance = 1e-6;
            sol.dual_values_valid =
                sol.min_dual_value >= -tolerance
                && sol.duality_gap <= tolerance
                && sol.max_reduced_cost_error <= tolerance;
        }
    } catch (const GRBException& e) {
        cerr << "Gurobi Error " << e.getErrorCode()
             << ": " << e.getMessage() << endl;
        sol.status = -1;
    }

    return sol;
}
