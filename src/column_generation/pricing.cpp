#include "pricing.h"
#include "gurobi_c++.h"
#include <graph/graph.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
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

// Solve the Maximum Weight Stable Set pricing problem.
bool solve_maximum_weight_stable_set_pricing(
    GRBEnv& env,
    const Graph& G,
    const vector<double>& dual_value,
    StableSetPricingResult& result,
    double time_limit_seconds
) {
    result = StableSetPricingResult{};

    GRBModel model(env);
    double eps = 1e-6;

    model.set(GRB_IntAttr_ModelSense, GRB_MAXIMIZE);
    model.set(GRB_DoubleParam_TimeLimit, time_limit_seconds);

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
        result.status = model.get(GRB_IntAttr_Status);
        result.stopped = (result.status == GRB_TIME_LIMIT);
        result.proven_optimal = (result.status == GRB_OPTIMAL);

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
            result.column = StableColumn(input_vertices, n);
            result.reduced_cost = 1.0 - weight;
            return result.reduced_cost < -eps;
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
DecisionPricingModel::DecisionPricingModel(
    const Graph& graph,
    std::vector<int> max_clique
) : G(graph), x(*this, graph.num_vertices(), 0, 1) {

    // Stable-set constraints: adjacent vertices cannot both enter the
    // pricing column.
    for (auto& [u, v] : graph.edges()) {
        if (u < v) {
            rel(*this, x[u] + x[v] <= 1);
        }
    }
}

// (16-17-19)
CPSolveResult solve_decision_pricing_model(
    DecisionPricingModel& model,
    const vector<double>& dual_value,
    double weight_threshold,
    const vector<int>& static_order,
    double time_limit_seconds
) {
    CPSolveResult res;

    // Vertex-level bound filtering fixes x[v] = 0 when even the best
    // stable set containing v cannot exceed the decision threshold.
    model.add_weighted_maximum_clique_filtering(dual_value, weight_threshold);

    IntArgs c(dual_value.size());
    for (size_t i = 0; i < dual_value.size(); ++i) {
        c[i] = static_cast<int>(std::round(dual_value[i] * SCALE));
    }

    int int_threshold = static_cast<int>(std::round(weight_threshold * SCALE));
    
    // Decision cut: accept only columns with dual weight strictly greater
    // than the threshold, which implies negative reduced cost.
    linear(model, c, model.x, IRT_GR, int_threshold);

    // 4.2.1 Shuffled Static Order
    if (static_cast<int>(static_order.size()) != model.x.size()) {
        throw invalid_argument("static_order size must match number of vertices");
    }

    BoolVarArgs ordered_vars(model.x.size());
    for (int i = 0; i < model.x.size(); ++i) {
        int v = static_order[i];
        if (v < 0 || v >= model.x.size()) {
            throw invalid_argument("static_order contains an invalid vertex");
        }
        ordered_vars[i] = model.x[v];
    }
    branch(model, ordered_vars, BOOL_VAR_NONE(), BOOL_VAL_MAX());

    Search::Options opts;
    unique_ptr<Search::TimeStop> time_stop;
    if (time_limit_seconds > 0.0) {
        unsigned long millis = static_cast<unsigned long>(
            min(
                time_limit_seconds * 1000.0,
                static_cast<double>(numeric_limits<unsigned long>::max())
            )
        );
        time_stop = make_unique<Search::TimeStop>(
            millis
        );
        opts.stop = time_stop.get();
    }

    DFS<DecisionPricingModel> engine(&model, opts);

    if (DecisionPricingModel* sol = engine.next()) {
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

void DecisionPricingModel::add_weighted_maximum_clique_filtering(
    const vector<double>& dual_value,
    double weight_threshold
) {
    int n = G.num_vertices();

    for (int v = 0; v < n; ++v) {
        double bound = max(0.0, dual_value[v]);

        for (int u = 0; u < n; ++u) {
            if (u == v) {
                continue;
            }
            if (!G.has_edge(u, v)) {
                bound += max(0.0, dual_value[u]);
            }
        }

        // If this upper bound is not enough to pass the threshold, no
        // improving decision-pricing column can contain v.
        if (bound <= weight_threshold + 1e-9) {
            rel(*this, x[v], IRT_EQ, 0);
        }
    }
}



DecisionPricingModel::DecisionPricingModel(DecisionPricingModel& other)
    : Space(other), G(other.G) {
    x.update(*this, other.x);
}

Space* DecisionPricingModel::copy() {
    return new DecisionPricingModel(*this);
}
