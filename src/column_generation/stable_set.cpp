#include "stable_set.h"
#include <coloring/heuristic.h>
#include <graph/graph.h>

#include <algorithm>
#include <stdexcept>
#include <random>
#include <unordered_set>
#include <vector>

using namespace std;

// Greedily extend a stable set to a maximal stable set.
static void maximalize_stable_set(StableColumn& col, const Graph& G) {
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

StableColumn::StableColumn(const vector<int>& input_vertices, int num_vertices) {
    this->vertices = input_vertices;
    this->bitset.assign((num_vertices + 63) / 64, 0ULL);

    sort(this->vertices.begin(), this->vertices.end());
    this->vertices.erase(
        unique(this->vertices.begin(), this->vertices.end()),
        this->vertices.end()
    );

    for (int vertex : vertices) {
        if (vertex < 0 || vertex >= num_vertices) {
            throw out_of_range("vertex index out of range");
        }
        this->bitset[vertex / 64] |= (1ULL << (vertex % 64));
    }
}

bool StableColumn::contains_vertex(int v) const {
    int word_index = v / 64;
    int bit_index = v & 64;

    if (word_index >= bitset.size()) return false;

    return (bitset[word_index] & (1ULL << bit_index)) != 0;
}

bool ColumnPool::insert(StableColumn col) {
    auto [it, inserted] = seen_bitsets.insert(col.bitset);
    if (inserted) {
        columns.push_back(col);
        return true;
    }
    return false;
}

bool ColumnPool::is_contains(const StableColumn& col) {
    return seen_bitsets.find(col.bitset) != seen_bitsets.end();
}

int ColumnPool::size() const { return static_cast<int>(columns.size()); }

const StableColumn& ColumnPool::column(int j) const { return columns[j]; }

vector<StableColumn> dsatur_coloring_columns(const Graph& G) {
    int n = G.num_vertices();

    auto [coloring, num_colors] = DSATUR_coloring(G);

    vector<vector<int>> color_classes(num_colors);

    for (int v = 0; v < n; v++) {
        int c = coloring[v];

        if (c < 0 || c >= num_colors) {
            throw runtime_error("DSATUR_coloring returned invalid color index");
        }

        color_classes[c].push_back(v);
    }

    vector<StableColumn> columns;
    columns.reserve(num_colors);

    for (auto& vertices : color_classes) {
        if (vertices.empty()) {
            continue;
        }
        StableColumn col(vertices, n);

        columns.push_back(move(col));
    }

    return columns;
}

void ColumnPool::initialize(const Graph& G, int num_trial, size_t seed) {
    int n = G.num_vertices();
    vector<int> V = G.nodes();
    mt19937 rng(seed);

    vector<StableColumn> columns = dsatur_coloring_columns(G);
    for (auto& col : columns) {
        maximalize_stable_set(col, G);
        this->insert(move(col));
    }

    for (int trial = 0; trial < num_trial; trial++) {
        shuffle(V.begin(), V.end(), rng);
        vector<int> coloring = random_sequential_greedy(G, V);

        int num_colors = *max_element(coloring.begin(), coloring.end()) + 1;
        vector<vector<int>> color_classes(num_colors);

        for (int v = 0; v < n; v++) {
            color_classes[coloring[v]].push_back(v);
        }

        for (auto& vertices : color_classes) {
            StableColumn col(vertices, n);
            maximalize_stable_set(col, G);
            this->insert(move(col));
        }
    }
}
