#pragma once

#include "stable_set.h"
#include "gurobi_c++.h"
#include <graph/graph.h>
#include "../coloring/cp.h"
#include <algorithm>
#include <vector>

#include <gecode/int.hh>
#include <gecode/search.hh>

struct StableSetPricingResult {
    StableColumn column;
    double reduced_cost = 0.0;
    bool stopped = false;
    bool proven_optimal = false;
    int status = 0;
};

bool is_stable_set(const Graph& G, const std::vector<int>& vertices);

// Solve Maximum Weight Stable Set via Gurobi.
// Returns true if a column with negative reduced cost was found.
bool solve_maximum_weight_stable_set_pricing(
    GRBEnv& env,
    const Graph& G,
    const std::vector<double>& dual_value,
    StableSetPricingResult& result,
    double time_limit_seconds = 30.0
);

// 4 Constraint Programming-based Column Generation
class DecisionPricingModel : public Gecode::Space {
public:
    const Graph& G;
    Gecode::BoolVarArray x;

    DecisionPricingModel(const Graph& graph, std::vector<int> max_clique);

    Gecode::Space* copy() override;

    void add_weighted_maximum_clique_filtering(
        const std::vector<double>& dual_value,
        double weight_threshold
    );

private:
    DecisionPricingModel(DecisionPricingModel& other);
};

CPSolveResult solve_decision_pricing_model(
    DecisionPricingModel& model,
    const std::vector<double>& dual_value,
    double weight_threshold,
    const std::vector<int>& static_order
);

// 4.2.2 Adaptive Thresholds for the Negative Reduced Cost Constraint
// Second paper formula: tau(i,j) = z_RMP^j / kappa_i(c*).
inline double compute_adaptive_decision_threshold(
    double rmp_objective,
    double adaptive_lower_bound
) {
    constexpr double eps = 1e-6;
    if (adaptive_lower_bound <= 0.0) {
        return 1.0 + eps;
    }
    return std::max(1.0 + eps, rmp_objective / adaptive_lower_bound);
}
