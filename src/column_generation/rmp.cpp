#include "rmp.h"
#include "gurobi_c++.h"

#include <iostream>
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
    GRBVar var = model.addVar(0.0, 1.0, 1.0, GRB_CONTINUOUS, new_col,
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
        }
    } catch (const GRBException& e) {
        cerr << "Gurobi Error " << e.getErrorCode()
             << ": " << e.getMessage() << endl;
        sol.status = -1;
    }

    return sol;
}
