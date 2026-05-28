#ifndef HEURISTIC_H
#define HEURISTIC_H

#include "../graph/graph.h"
#include <vector>
#include <utility>

/**
 * @brief Greedy graph coloring with largest-degree-first ordering.
 *
 * Sorts vertices by degree in descending order, then assigns each vertex
 * the smallest color not used by its already-colored neighbors.
 *
 * @param G The input graph (undirected).
 * @return std::pair containing:
 *         - first: vector where coloring[v] is the color assigned to vertex v
 *         - second: total number of colors used (chromatic number upper bound)
 *
 * @note Time complexity: O(V^2). Memory: O(V).
 * @note Color values start from 0.
 *
 * @see DSATUR_coloring for a more sophisticated heuristic.
 */
std::pair<std::vector<int>, int> greedy_coloring(const Graph& G);

/**
 * @brief DSATUR (Degree of Saturation) graph coloring algorithm.
 *
 * At each step, selects the uncolored vertex with the highest saturation
 * degree (number of distinct colors among its neighbors). Ties are broken
 * by selecting the vertex with the highest original degree.
 *
 * Typically produces better results than simple greedy coloring.
 *
 * @param G The input graph (undirected).
 * @return std::pair of (color assignment vector, number of colors used).
 *
 * @note Time complexity: O(V^2) with current implementation.
 *       Can be optimized to O((V+E) log V) using priority queue.
 *
 * @see Brélaz, D. (1979). "New methods to color the vertices of a graph"
 */
std::pair<std::vector<int>, int> DSATUR_coloring(const Graph& G);

/**
 * @brief Assigns colors to vertices in a graph using a greedy approach based on a specific sequence.
 * 
 * @param G The input graph (undirected).
 * @param V The input array
 * @returns: color assignment vector
 */
std::vector<int> random_sequential_greedy(const Graph& G, const std::vector<int>& V);
#endif