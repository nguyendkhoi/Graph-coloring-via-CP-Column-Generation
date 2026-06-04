#include "augmented_pricing.h"
#include "../coloring/heuristic.h"
#include "../graph/graph.h"

using namespace std;
using namespace Gecode;

AP::AP(int num_stable_set, int num_vertices, StableColumn col, const Graph& G) 
    : k(num_stable_set), x(*this, k * num_vertices, 0, 1) {

    // Access X[i][v] means acces value of vertex v in stable set Zi
    auto X = [&](int i, int v) -> BoolVar {
        return x[i * num_vertices + v];
    };

    // 1st Constraint each Zi are a stable set
    for (int i = 0; i < k; i++) {
        for (const auto& [u, v] : G.edges()) {
            BoolVarArgs args;
            rel(*this, X(i,u), BOT_AND, X(i, v), 0);
        }
    }

    for (int v = 0; v < num_vertices; v++) {
        // 2nd constraint each vertex include in just one Zi
        BoolVarArgs args;
        for (int z = 0; z < k; z++) {
            args << X(z, v);
        }
        linear(*this, args, IRT_EQ, 1);

        // 3rd constraint Z1 = I(a_p)  (Z1 in paper corresponse to Z0)
        if (col.contains_vertex(v)) {
            rel(*this, X(0, v), IRT_EQ, 1);
        } else {
            rel(*this, X(0, v), IRT_EQ, 0);
        }
    }


    branch(*this, x, BOOL_VAR_DEGREE_MIN(), BOOL_VAL_MAX());
}

AP::AP(AP& other) : Space(other), k(other.k) {
    x.update(*this, other.x);
}

Space* AP::copy() {
    return new AP(*this);
}

//Helper
vector<StableColumn> AP::extract_columns(int num_vertices) const {
    vector<StableColumn> cols;
    cols.reserve(k);

    for (int i = 0; i < k; ++i) {
        vector<int> vertices;
        for (int v = 0; v < num_vertices; ++v) {
            if (x[i * num_vertices + v].val() == 1) { 
                vertices.push_back(v);
            }
        }
        if (!vertices.empty()) {
            cols.emplace_back(vertices, num_vertices);
        }
    }
    return cols;
}

vector<StableColumn> solveAugmentedPricing(const StableColumn& ap, int k, const Graph& G) {
    AP* model = new AP(k, G.num_vertices(), ap, G);
    DFS<AP> engine(model);
    delete model;
    if (AP* sol = engine.next()) {
        auto cols = sol->extract_columns(G.num_vertices());
        delete sol;
        return cols;
    }
    return {};
}