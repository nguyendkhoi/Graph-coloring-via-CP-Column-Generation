#pragma once

#include <graph/graph.h>
#include <vector>
#include <utility>

// Greedy coloring sorted by degree descending.
std::pair<std::vector<int>, int> greedy_coloring(const Graph& G);

// DSATUR coloring: pick vertex with highest saturation at each step.
std::pair<std::vector<int>, int> DSATUR_coloring(const Graph& G);

// Greedy coloring in a given vertex order.
std::vector<int> random_sequential_greedy(const Graph& G, const std::vector<int>& V);
