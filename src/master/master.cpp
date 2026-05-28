#include "../graph/graph.h"
#include "../heuristic/heuristic.h"
#include "master.h"

#include "algorithm"
#include "vector"
#include <stdexcept>
#include <random>
#include <unordered_map>

using namespace std;


bool is_stable_set(const Graph& G, const std::vector<int>& vertices) {
    for (int i = 0; i < vertices.size(); i++) {
        for (int y = i + 1; y < vertices.size(); y++) {
            if (G.has_edge(vertices[i], vertices[y])) return false;
        }
    }
    return true;
}

// StableColumn
StableColumn::StableColumn(const vector<int>& input_vertices, int num_vertices) {
    this->vertices = input_vertices;

    this->bitset.assign((num_vertices + 63) / 64, 0ULL);

    this->vertices.erase(
        unique(this->vertices.begin(), this->vertices.end()),
        this->vertices.end()
    );

    for (int vertex : vertices) {
        if (vertex < 0 || vertex >= num_vertices) {
            throw std::out_of_range("vertex index out of range");
        }

        this->bitset[vertex / 64] |= (1ULL << (vertex % 64));
    }
}

// ColumnPool
bool ColumnPool::insert(StableColumn col) {
    auto[it, inserted] = seen_bitsets.insert(col.bitset);

    if (inserted) {
        columns.push_back(col);
        return true;
    }
    else return false;
};

bool ColumnPool::is_contains(const StableColumn& col) {
    return seen_bitsets.find(col.bitset) != seen_bitsets.end();
};

int ColumnPool::size() {return static_cast<int>(columns.size());};

const StableColumn& ColumnPool::column(int j) {return columns[j];};

// Helper
void maximalize_stable_set(StableColumn& col, const Graph& G) {
    int n = G.num_vertices();
    unordered_set<int> in_S(col.vertices.begin(), col.vertices.end());
    
    unordered_set<int> forbidden;
    for (int v : col.vertices) {
        for (int u : G.neighbors(v)) {
            forbidden.insert(u);
        }
    }
    
    for (int v = 0; v < n; v++) {
        if (in_S.count(v)) continue;
        if (forbidden.count(v)) continue;
        
        col.vertices.push_back(v);
        in_S.insert(v);
        
        col.bitset[v / 64] |= (1ULL << (v % 64));
        
        for (int u : G.neighbors(v)) {
            forbidden.insert(u);
        }
    }
}

ColumnPool ColumnPool::initialize(const Graph& G, int num_trial, size_t seed) {
    int n = G.num_vertices();
    vector<int> V = G.nodes();
    ColumnPool pool;
    mt19937 rng(seed);

    for (int i = 0; i < num_trial; i++) {
        shuffle(V.begin(), V.end(), rng);
        vector<int> coloring = random_sequential_greedy(G, V);

        unordered_map<int, vector<int>> color_classes;
        for (int v = 0; v < (int)coloring.size(); v++) {
            color_classes[coloring[v]].push_back(v);
        }

        for (auto& [color, vertices] : color_classes) {
            StableColumn col(vertices, n);
            maximalize_stable_set(col, G);
            pool.insert(col);
        }
    }

    return pool;
}