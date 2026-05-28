#pragma once

#include <vector>
#include <iostream>
#include <algorithm>

#include <gecode/int.hh>
#include <gecode/search.hh>

#include "../graph/graph.h"

struct CPSolveResult {
    bool feasible = false;
    bool stopped = false;
    int num_colors = -1;
    std::vector<int> color;
};

class ColoringCP : public Gecode::Space {
public:
    const Graph& G;
    int k; //K colors
    Gecode::IntVarArray x; //Decision variable

    // Constructor
    ColoringCP(
        const Graph& graph,
        int num_colors,
        const std::vector<std::vector<int>>& clique_info = {}
    );

    // Constructor Copy
    ColoringCP(ColoringCP& other);
    Gecode::Space* copy() override;

    //Symetrique Breaking
    void symetrique_breaking(std::vector<int> clique);

    // Print color of solution
    void print_solution() const;

private:
    //add edge constraints
    void add_edge_constraints();
    void add_all_different(const std::vector<std::vector<int>>& clique_info);
};

CPSolveResult solve_coloring_cp(
    const Graph& G,
    int k,
    const std::vector<std::vector<int>>& clique_info,
    double time_limit
);

CPSolveResult cp_upper_bound(
    const Graph& G,
    const std::vector<std::vector<int>>& clique_info,
    int dsatur_ub,
    double time_limit
);