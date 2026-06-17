#pragma once

#include "driver.h"
#include "../column_generation/pricing.h"
#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

#include <vector>

std::vector<int> build_pricing_order(
    const Graph& G,
    const std::vector<double>& dual_value,
    const MasterRunConfig& config,
    int iteration
);

void grow_initial_column_pool_to_target(
    ColumnPool& pool,
    const Graph& G,
    const MasterRunConfig& config
);

bool try_improve_upper_bound_with_augmented_pricing(
    const Graph& G,
    const StableColumn& forced_column,
    double time_limit_seconds,
    int& incumbent_ub,
    std::vector<StableColumn>& augmented_columns
);

bool solve_decision_pricing_column(
    const Graph& G,
    const std::vector<double>& dual_value,
    double weight_threshold,
    const std::vector<int>& static_order,
    StableSetPricingResult& pricing_result,
    double time_limit_seconds
);
