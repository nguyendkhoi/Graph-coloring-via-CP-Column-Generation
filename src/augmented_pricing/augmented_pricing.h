#pragma once

#include <gecode/int.hh>
#include <gecode/search.hh>

#include <vector>

#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

class AugmentedPricingModel : public Gecode::Space {
public:
    int num_stable_sets;
    Gecode::BoolVarArray x;

    AugmentedPricingModel(
        int num_stable_sets,
        int num_vertices,
        StableColumn forced_column,
        const Graph& G
    );

    std::vector<StableColumn> extract_columns(int num_vertices) const;

    Gecode::Space* copy() override;

private:
    AugmentedPricingModel(AugmentedPricingModel& other);
};

std::vector<StableColumn> solve_augmented_pricing(
    const StableColumn& forced_column,
    int num_stable_sets,
    const Graph& G,
    double time_limit_seconds = 5.0
);

