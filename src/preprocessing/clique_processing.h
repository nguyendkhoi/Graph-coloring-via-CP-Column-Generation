#pragma once

#include <vector>
#include "../graph/graph.h"
#include "../cp_coloring/cp.h"

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

/**
 * @brief Finds a large clique in the original graph using the complement graph.
 *
 * A stable set in the complement graph corresponds to a clique
 * in the original graph.
 *
 * @param G_complement The complement graph of the original graph.
 * @return Vector of vertices forming a clique in the original graph.
 */
std::vector<int> largest_clique(const Graph& G_complement);

/**
 * @brief Generates a list of large cliques in the original graph using diverse colorings of its complement.
 *
 * This function finds multiple distinct cliques by computing different stable sets via
 * an initial DSATUR coloring and subsequent randomized greedy colorings on the complement graph.
 * The resulting list is sorted in descending order of clique size.
 *
 * @param G The original input graph.
 * @param number_cliques The total number of cliques to generate (including the largest one found by DSATUR).
 * @return A 2D vector where each inner vector represents a list of vertex IDs forming a clique, 
 * sorted from largest to smallest.
 */
std::vector<std::vector<int>> generate_clique(const Graph& G, int number_cliques);