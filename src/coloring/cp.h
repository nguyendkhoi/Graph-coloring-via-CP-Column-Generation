#pragma once

#include <vector>
#include <iostream>
#include <algorithm>

#include <gecode/int.hh>
#include <gecode/search.hh>

#include <graph/graph.h>

class ColoringCP : public Gecode::Space {
public:
    const Graph& G;
    int k;
    Gecode::IntVarArray x;

    ColoringCP(
        const Graph& graph,
        int num_colors,
        const std::vector<std::vector<int>>& clique_info = {}
    );

    Gecode::Space* copy() override;

    void add_symmetry_breaking_constraints(std::vector<int> clique);
    void print_solution() const;

private:
    ColoringCP(ColoringCP& other);
    void add_edge_constraints();
    void add_all_different(const std::vector<std::vector<int>>& clique_info);
};

struct CPSolveResult {
    bool feasible = false;
    bool stopped = false;
    int num_colors = -1;
    double val = 0.0;
    unsigned long long nodes = 0;
    unsigned long long failures = 0;
    std::vector<int> color;
    std::vector<int> vertices;
};

CPSolveResult solve_coloring_cp(
    const Graph& G,
    int k,
    const std::vector<std::vector<int>>& clique_info,
    double time_limit
);

CPSolveResult solve_coloring_cp_with_reduction(
    const Graph& G,
    const std::vector<std::vector<int>>& clique_info,
    int k,
    double time_limit
);
