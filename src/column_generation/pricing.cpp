#include "pricing.h"
#include "gurobi_c++.h"
#include <graph/graph.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

bool is_stable_set(const Graph& G, const vector<int>& vertices) {
    for (int i = 0; i < (int)vertices.size(); i++) {
        for (int y = i + 1; y < (int)vertices.size(); y++) {
            if (G.has_edge(vertices[i], vertices[y])) return false;
        }
    }
    return true;
}

// Optimalize pricing
bool solve_mwss(GRBEnv& env, const Graph& G, const vector<double>& dual_value, MWSSResult& res) {
    GRBModel model(env);
    double eps = 1e-6;

    model.set(GRB_IntAttr_ModelSense, GRB_MAXIMIZE);

    int n = G.num_vertices();
    if (static_cast<int>(dual_value.size()) != n) {
        throw invalid_argument("dual_value size must match number of vertices");
    }

    vector<GRBVar> y(n);
    for (int v = 0; v < n; v++) {
        y[v] = model.addVar(0, 1, dual_value[v], GRB_BINARY, "y" + to_string(v));
    }

    for (int v = 0; v < n; v++) {
        for (int u : G.neighbors(v)) {
            if (v < u)
                model.addConstr(y[u] + y[v] <= 1);
        }
    }

    try {
        model.optimize();

        if (model.get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            vector<int> input_vertices;
            double weight = 0.0;
            for (int v = 0; v < n; v++) {
                if (y[v].get(GRB_DoubleAttr_X) > 0.8) {
                    input_vertices.push_back(v);
                    weight += dual_value[v];
                }
            }
            res.col = StableColumn(input_vertices, n);
            res.reduced_cost = 1.0 - weight;
            return res.reduced_cost < -eps;
        }
    } catch (const GRBException& e) {
        cerr << "Gurobi Error " << e.getErrorCode()
             << ": " << e.getMessage() << endl;
    }

    return false;
}

// Constraint Programming-based Column Generation
// Decision pricing
bool C