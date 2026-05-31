#include "master.h"
#include "../graph/graph.h"
#include "../heuristic/heuristic.h"
#include "gurobi_c++.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <random>
#include <unordered_map>

using namespace std;

// Helper
static void maximalize_stable_set(StableColumn& col, const Graph& G) {
    int n = G.num_vertices();
    unordered_set<int> in_S(col.vertices.begin(), col.vertices.end());
    
    unordered_set<int> forbidden;
    for (int v : col.vertices) {
        for (int u : G.neighbors(v)) {
            forbidden.insert(u);
        }
    }
    
    for (int v = 0; v < n; v++) {
        if (in_S.count(v)) continue;
        if (forbidden.count(v)) continue;
        
        col.vertices.push_back(v);
        in_S.insert(v);
        
        col.bitset[v / 64] |= (1ULL << (v % 64));
        
        for (int u : G.neighbors(v)) {
            forbidden.insert(u);
        }
    }
}
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
bool is_stable_set(const Graph& G, const vector<int>& vertices) {
    for (int i = 0; i < vertices.size(); i++) {
        for (int y = i + 1; y < vertices.size(); y++) {
            if (G.has_edge(vertices[i], vertices[y])) return false;
        }
    }
    return true;
}

// ============================================
// StableColumn
// ============================================
StableColumn::StableColumn(const vector<int>& input_vertices, int num_vertices) {
    this->vertices = input_vertices;

    this->bitset.assign((num_vertices + 63) / 64, 0ULL);

    sort(this->vertices.begin(), this->vertices.end());
    this->vertices.erase(
        unique(this->vertices.begin(), this->vertices.end()),
        this->vertices.end()
    );

    for (int vertex : vertices) {
        if (vertex < 0 || vertex >= num_vertices) {
            throw out_of_range("vertex index out of range");
        }

        this->bitset[vertex / 64] |= (1ULL << (vertex % 64));
    }
}

// ============================================
// ColumnPool
// ============================================
bool ColumnPool::insert(StableColumn col) {
    auto[it, inserted] = seen_bitsets.insert(col.bitset);

    if (inserted) {
        columns.push_back(col);
        return true;
    }
    else return false;
};
bool ColumnPool::is_contains(const StableColumn& col) {
    return seen_bitsets.find(col.bitset) != seen_bitsets.end();
};
int ColumnPool::size() const { return static_cast<int>(columns.size()); }
const StableColumn& ColumnPool::column(int j) const { return columns[j]; }
void ColumnPool::initialize(const Graph& G, int num_trial, size_t seed) {
    int n = G.num_vertices();
    
    vector<int> V = G.nodes();
    
    mt19937 rng(seed);
    
    for (int trial = 0; trial < num_trial; trial++) {
        shuffle(V.begin(), V.end(), rng);
        vector<int> coloring = random_sequential_greedy(G, V);
        
        int num_colors = *max_element(coloring.begin(), coloring.end()) + 1;
        vector<vector<int>> color_classes(num_colors);
        
        for (int v = 0; v < n; v++) {
            color_classes[coloring[v]].push_back(v);
        }
        
        for (auto& vertices : color_classes) {
            StableColumn col(vertices, n);
            maximalize_stable_set(col, G);
            this->insert(move(col));
        }
    }
}

// ============================================
// RMPSolver
// ============================================
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
};
void RMPSolver::add_column_pool(const ColumnPool& pool) {
    for (const StableColumn& col : pool.columns) {
        add_column(col);
    }
};
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

