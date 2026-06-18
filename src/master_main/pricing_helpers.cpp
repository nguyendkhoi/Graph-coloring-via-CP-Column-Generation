#include "pricing_helpers.h"

#include "../augmented_pricing/augmented_pricing.h"
#include "../config/config.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>

using namespace std;

namespace {

vector<int> weighted_shuffled_static_order(
    const Graph& G,
    const vector<double>& dual_value,
    size_t seed,
    int iteration
) {
    int n = G.num_vertices();
    vector<int> order(n);
    iota(order.begin(), order.end(), 0);

    mt19937 rng(static_cast<unsigned int>(
        seed ^ (0x9e3779b9U + static_cast<unsigned int>(iteration) * 2654435761U)
    ));
    shuffle(order.begin(), order.end(), rng);

    vector<double> score(n, 0.0);
    for (int v = 0; v < n; ++v) {
        for (int u = 0; u < n; ++u) {
            if (u != v && !G.has_edge(u, v)) {
                score[v] += max(0.0, dual_value[u]);
            }
        }
    }

    stable_sort(order.begin(), order.end(),
        [&](int a, int b) {
            return score[a] > score[b] + 1e-12;
        }
    );
    return order;
}

} // namespace

vector<int> build_pricing_order(
    const Graph& G,
    const vector<double>& dual_value,
    int iteration
) {
    int n = G.num_vertices();
    vector<int> order(n);
    iota(order.begin(), order.end(), 0);

    string vertex_ordering = config.get<string>("vertex_ordering", "dual_desc");

    if (vertex_ordering == "dual_desc") {
        stable_sort(order.begin(), order.end(),
            [&](int a, int b) {
                double da = a < static_cast<int>(dual_value.size())
                    ? dual_value[a]
                    : 0.0;
                double db = b < static_cast<int>(dual_value.size())
                    ? dual_value[b]
                    : 0.0;
                if (fabs(da - db) > 1e-12) {
                    return da > db;
                }
                return a < b;
            }
        );
        return order;
    }

    if (vertex_ordering == "degree_desc") {
        stable_sort(order.begin(), order.end(),
            [&](int a, int b) {
                if (G.degree(a) != G.degree(b)) {
                    return G.degree(a) > G.degree(b);
                }
                return a < b;
            }
        );
        return order;
    }

    return weighted_shuffled_static_order(
        G,
        dual_value,
        config.get<size_t>("seed", 40),
        iteration
    );
}

void grow_initial_column_pool_to_target(
    ColumnPool& pool,
    const Graph& G
) {
    int initial_columns_target = config.get<int>("initial_columns", 0);
    if (initial_columns_target <= 0 || pool.size() >= initial_columns_target) {
        return;
    }

    int attempts = 0;
    int max_attempts = max(
        initial_columns_target * 20,
        initial_columns_target + 100
    );

    size_t seed = config.get<size_t>("seed", 40);
    while (pool.size() < initial_columns_target
        && attempts < max_attempts) {
        pool.initialize(
            G,
            1,
            seed + static_cast<size_t>(1000003 + attempts)
        );
        ++attempts;
    }

    if (pool.size() < initial_columns_target) {
        cerr << "Warning: requested " << initial_columns_target
             << " initial columns but generated " << pool.size()
             << " unique columns." << endl;
    }
}

bool try_improve_upper_bound_with_augmented_pricing(
    const Graph& G,
    const StableColumn& forced_column,
    double time_limit_seconds,
    int& incumbent_ub,
    vector<StableColumn>& augmented_columns
) {
    int target_k = incumbent_ub - 1;
    cout << "Run CP coloring check with k = " << target_k << endl;

    augmented_columns =
        solve_augmented_pricing(forced_column, target_k, G, time_limit_seconds);

    if (augmented_columns.empty()) {
        cout << " -> Failed (no feasible coloring found within time limit)." << endl;
        return false;
    } else {
        cout << " -> Success! Found a better coloring with k = " << target_k << "." << endl;
        incumbent_ub = target_k;
        return true;
    }
}

bool solve_decision_pricing_column(
    const Graph& G,
    const vector<double>& dual_value,
    double weight_threshold,
    const vector<int>& static_order,
    StableSetPricingResult& pricing_result,
    double time_limit_seconds
) {
    pricing_result = StableSetPricingResult{};

    DecisionPricingModel model(G, {});
    CPSolveResult res =
        solve_decision_pricing_model(
            model,
            dual_value,
            weight_threshold,
            static_order,
            time_limit_seconds
        );

    if (!res.feasible) {
        return false;
    }

    pricing_result.column = StableColumn(res.vertices, G.num_vertices());
    pricing_result.reduced_cost = 1.0 - res.val;

    return pricing_result.reduced_cost < -1e-6;
}
