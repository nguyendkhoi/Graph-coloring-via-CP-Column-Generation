#include "pricing.h"
#include "gurobi_c++.h"
#include <graph/graph.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gecode/minimodel.hh>

using namespace std;
using namespace Gecode;

const double SCALE = 10000.0;

bool is_stable_set(const Graph& G, const vector<int>& vertices) {
    for (int i = 0; i < (int)vertices.size(); i++) {
        for (int y = i + 1; y < (int)vertices.size(); y++) {
            if (G.has_edge(vertices[i], vertices[y])) return false;
        }
    }
    return true;
}

// Optimalize pricing
bool solve_mwss(
    GRBEnv& env,
    const Graph& G,
    const vector<double>& dual_value,
    MWSSResult& res,
    double time_limit_seconds
) {
    res = MWSSResult{};

    GRBModel model(env);
    double eps = 1e-6;

    model.set(GRB_IntAttr_ModelSense, GRB_MAXIMIZE);
    model.set(GRB_DoubleParam_TimeLimit, time_limit_seconds);
    model.set(GRB_DoubleParam_BestObjStop, 1.0 + eps);

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
        res.status = model.get(GRB_IntAttr_Status);
        res.stopped = (res.status == GRB_TIME_LIMIT);

        int sol_count = model.get(GRB_IntAttr_SolCount);
        if (sol_count > 0) {
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

        return false;
    } catch (const GRBException& e) {
        cerr << "Gurobi Error " << e.getErrorCode()
             << ": " << e.getMessage() << endl;
    }

    return false;
}

// Constraint Programming-based Column Generation
// Decision pricing
// (14-15)
CP_CG::CP_CG(const Graph& graph, std::vector<int> max_clique) : G(graph), x(*this, graph.num_vertices(), 0, 1) {
    // (18)
    for (auto& [u, v] : graph.edges()) {
        if (u < v) {
            rel(*this, x[u] + x[v] <= 1);
        }
    }

    // 4.1 Weighted Maximum Clique Constraint
    symetrique_breaking(max_clique);

}

CP_CG::CP_CG(CP_CG& other)
    : Space(other), G(other.G) {
    x.update(*this, other.x);
}

Space* CP_CG::copy() {
    return new CP_CG(*this);
}

// (16-17-19)
CPSolveResult solve_CP_CG(CP_CG& cp_cg, const vector<double>& dual_value, double threshold) {
    CPSolveResult res;

    IntArgs c(dual_value.size());
    for (size_t i = 0; i < dual_value.size(); ++i) {
        c[i] = static_cast<int>(std::round(dual_value[i] * SCALE));
    }

    int int_threshold = static_cast<int>(std::round(threshold * SCALE));
    
    linear(cp_cg, c, cp_cg.x, IRT_GR, int_threshold);

    // 4.2.1 Shuffled Static Order
    Gecode::Rnd r(25);
    branch(cp_cg, cp_cg.x, BOOL_VAR_RND(r), BOOL_VAL_MAX());

    Search::Options opts;
    DFS<CP_CG> engine(&cp_cg, opts);

    if (CP_CG* sol = engine.next()) {
        res.feasible = true;
        
        for (int i = 0; i < sol->x.size(); i++) {
            if (sol->x[i].val() == 1) {
                res.vertices.push_back(i);
                //Take c value
                res.val += dual_value[i];
            }
        }

        delete sol;
        
    } else if (engine.stopped()) {
        res.stopped = true;
    }

    return res;
}

void CP_CG::symetrique_breaking(vector<int> clique) {
    sort(clique.begin(), clique.end(),
        [this](int a, int b) {
            return G.degree(a) > G.degree(b);
        }
    );

    BoolVarArgs clique_vars;
    for (int i = 0; i < static_cast<int>(clique.size()); i++) {
        rel(*this, x[clique[i]], IRT_EQ, 1);
    }
}
