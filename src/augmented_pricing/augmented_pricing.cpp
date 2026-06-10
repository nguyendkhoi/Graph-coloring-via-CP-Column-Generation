#include "augmented_pricing.h"
#include "../coloring/heuristic.h"
#include "../graph/graph.h"

#include <gecode/minimodel.hh>

using namespace std;
using namespace Gecode;

AugmentedPricingModel::AugmentedPricingModel(
    int num_stable_sets,
    int num_vertices,
    StableColumn forced_column,
    const Graph& G
) : num_stable_sets(num_stable_sets),
    x(*this, num_stable_sets * num_vertices, 0, 1) {

    // X(i, v) is 1 iff vertex v belongs to stable set Z_i.
    auto X = [&](int i, int v) -> BoolVar {
        return x[i * num_vertices + v];
    };

    // Each Z_i must be a stable set.
    for (int i = 0; i < num_stable_sets; i++) {
        for (const auto& [u, v] : G.edges()) {
            rel(*this, X(i,u), BOT_AND, X(i, v), 0);
        }
    }

    for (int v = 0; v < num_vertices; v++) {
        // Each vertex must be assigned to exactly one stable set.
        BoolVarArgs args;
        for (int z = 0; z < num_stable_sets; z++) {
            args << X(z, v);
        }
        linear(*this, args, IRT_EQ, 1);

        // Force Z_0 to be the pricing column.
        if (forced_column.contains_vertex(v)) {
            rel(*this, X(0, v), IRT_EQ, 1);
        } else {
            rel(*this, X(0, v), IRT_EQ, 0);
        }
    }

    // Break symmetry between interchangeable color classes Z_1..Z_k.
    for (int z = 1; z + 1 < num_stable_sets; ++z) {
        BoolVarArgs current;
        BoolVarArgs next;
        for (int v = 0; v < num_vertices; ++v) {
            current << X(z, v);
            next << X(z + 1, v);
        }
        lex(*this, current, IRT_GQ, next);
    }

    branch(*this, x, BOOL_VAR_DEGREE_MIN(), BOOL_VAL_MAX());
}

AugmentedPricingModel::AugmentedPricingModel(AugmentedPricingModel& other)
    : Space(other), num_stable_sets(other.num_stable_sets) {
    x.update(*this, other.x);
}

Space* AugmentedPricingModel::copy() {
    return new AugmentedPricingModel(*this);
}

vector<StableColumn> AugmentedPricingModel::extract_columns(
    int num_vertices
) const {
    vector<StableColumn> cols;
    cols.reserve(num_stable_sets);

    for (int i = 0; i < num_stable_sets; ++i) {
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

vector<StableColumn> solve_augmented_pricing(
    const StableColumn& forced_column,
    int num_stable_sets,
    const Graph& G,
    double time_limit_seconds
) {
    AugmentedPricingModel* model = new AugmentedPricingModel(
        num_stable_sets,
        G.num_vertices(),
        forced_column,
        G
    );
    Search::Options opts;
    Search::TimeStop stop(
        static_cast<unsigned long long>(time_limit_seconds * 1000.0)
    );
    opts.stop = &stop;

    DFS<AugmentedPricingModel> engine(model, opts);
    delete model;

    if (AugmentedPricingModel* sol = engine.next()) {
        auto cols = sol->extract_columns(G.num_vertices());
        delete sol;
        return cols;
    }

    return {};
}
