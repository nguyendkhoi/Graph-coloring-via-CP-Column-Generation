#ifndef GRAPH_H
#define GRAPH_H

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Graph {
private:
    int V;
    std::vector<std::unordered_set<int>> adj;

public:
    // Constructors
    Graph();
    Graph(int vertices);

    // Methods
    void add_edge(int u, int v);
    bool has_edge(int u, int v) const;
    int num_vertices() const;
    Graph complement() const;
    int degree(int v) const;
    std::vector<int> nodes() const;
    std::vector<std::pair<int, int>> edges() const;

    // Return neighbors of vertex v
    const std::unordered_set<int>& neighbors(int v) const;
};

std::vector<int64_t> build_edge_index(const Graph& G);

// Parser file DIMACS
Graph parser_dimacs_col(const std::string& file_path, bool zero_based = true);

#endif