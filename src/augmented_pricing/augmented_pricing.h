#pragma once

#include <gecode/int.hh>
#include <gecode/search.hh>

#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

class AP : public Gecode::Space {
public:
    int k; //K stable sets
    Gecode::BoolVarArray x; //Decision variable

    AP(int num_stable_set, int num_vertices, StableColumn col, const Graph& G);

    std::vector<StableColumn> extract_columns(int num_vertices) const;
    // Override copy function of gecode
    Gecode::Space* copy() override;

private:
    // Constructor copy node
    AP(AP& other);

    

};

//Solver
std::vector<StableColumn> solveAugmentedPricing(const StableColumn& ap, int k, const Graph& G);

