#pragma once

#include <vector>
#include "../graph/graph.h"

struct GraphReduction {
    Graph reduced;
    std::vector<int> removed_vertices;
    std::vector<int> to_origin;
    std::vector<int> to_reduced;
};

// Iteratively remove vertices with active-degree < clique_size - 1.
// Removed vertices can always be colored greedily after solving the reduced graph.
GraphReduction reduce_by_degree(const Graph& G, int clique_size);

/**
 * @brief Finds the largest stable set from a coloring.
 *
 * Given a coloring vector, groups vertices by color and returns
 * the largest color class. In a valid graph coloring, each color
 * class is a stable set.
 *
 * @param coloring Vector where coloring[v] is the color of vertex v.
 * @return Vector of vertices forming the largest stable set.
 */
std::vector<int> find_stable_set(const std::vector<int>& coloring);

// A maximal stable set in the complement graph is a clique in the original graph.
std::vector<int> find_maximal_clique_from_complement(
    const Graph& G_complement
);
