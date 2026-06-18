#include "clique_processing.h"
#include <vector>
#include <algorithm>
#include <coloring/heuristic.h>
#include "../graph/graph.h"

using namespace std;

// ----Helper----
//  find largest stable set in Graph complement (or clique in Graph) from coloring
vector<int> find_stable_set(const vector<int>& coloring) {
    vector<vector<int>> frequent_color(coloring.size()); 
    
    for (int i = 0; i < (int)coloring.size(); i++) {
        frequent_color[coloring[i]].push_back(i);
    }

    const vector<int>* best_set = &frequent_color[0];
    for (const auto& set : frequent_color) {
        if (set.size() > best_set->size()) {
            best_set = &set;
        }
    }
    return *best_set;
}

// Maximal stable set
void maximalize_stable_set(vector<int>& stable_set, const Graph& G) {
    int n = G.num_vertices();

    vector<char> available(n, 1);

    for (int v : stable_set) {
        available[v] = 0;
        for (int u : G.neighbors(v)) {
            available[u] = 0;
        }
    }

    vector<int> vertices_available;
    vertices_available.reserve(n);

    for (int v = 0; v < n; ++v) {
        if (available[v]) {
            vertices_available.push_back(v);
        }
    }

    sort(vertices_available.begin(), vertices_available.end(),
        [&G](int a, int b) {
            if (G.degree(a) != G.degree(b)) {
                return G.degree(a) < G.degree(b);
            }
            return a < b;
        }
    );

    for (int v : vertices_available) {
        if (!available[v]) continue;

        stable_set.push_back(v);
        available[v] = 0;

        for (int u : G.neighbors(v)) {
            available[u] = 0;
        }
    }
}

vector<int> find_maximal_clique_from_complement(const Graph& G_complement) {
    vector<int> coloring = DSATUR_coloring(G_complement).first;
    vector<int> s = find_stable_set(coloring);
    maximalize_stable_set(s, G_complement);
    return s;
}

// Remove vertices with degree < clique size
GraphReduction reduce_by_degree(const Graph& G, int clique_size) {
    int n = G.num_vertices();

    GraphReduction result;
    result.to_reduced.assign(n, -1);

    int idx = 0;
    for (int v = 0; v < n; v++) {
        if (G.degree(v) < clique_size) {
            result.removed_vertices.push_back(v);
        } else {
            result.to_reduced[v] = idx++;
            result.to_origin.push_back(v);
        }
    }

    result.reduced = Graph(idx);
    for (int v = 0; v < n; v++) {
        int rv = result.to_reduced[v];
        if (rv < 0) continue;
        for (int u : G.neighbors(v)) {
            int ru = result.to_reduced[u];
            if (ru >= 0 && v < u)
                result.reduced.add_edge(rv, ru);
        }
    }

    return result;
}
