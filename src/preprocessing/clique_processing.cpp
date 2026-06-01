#include "clique_processing.h"
#include <set>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <random>
#include "../heuristic/heuristic.h"
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
vector<int> maximalize_stable_set(const vector<int>& stable_set, const Graph& G) {
    int n = G.num_vertices();
    vector<char> in_set(n, 0);
    vector<char> blocked(n, 0);

    vector<int> result = stable_set;
    for (int v : stable_set) {
        in_set[v] = 1;
        for (int u : G.neighbors(v))
            blocked[u] = 1;
    }

    for (int v = 0; v < n; v++) {
        if (in_set[v] || blocked[v]) continue;
        result.push_back(v);
        in_set[v] = 1;
        for (int u : G.neighbors(v))
            blocked[u] = 1;
    }

    return result;
}

// Find largest_clique
vector<int> largest_clique(const Graph& G_complement) {
    vector<int> coloring = DSATUR_coloring(G_complement).first;
    vector<int> s = find_stable_set(coloring);
    return maximalize_stable_set(s, G_complement);
}

// Remove vertices with degree < clique size - 1
GraphReduction reduce_by_degree(const Graph& G, int clique_size) {
    int n = G.num_vertices();
    int threshold = clique_size - 1;

    GraphReduction result;
    result.to_reduced.assign(n, -1);

    int idx = 0;
    for (int v = 0; v < n; v++) {
        if (G.degree(v) < threshold) {
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

vector<vector <int>> generate_clique(const Graph& G, int number_cliques) {
    vector<int> vertices = G.nodes();
    vector<vector <int>> clique_list;

    Graph G_complement = G.complement();
    clique_list.push_back(largest_clique(G_complement));

    // Create seed for shuffle
    random_device rd;
    mt19937 g(rd());

    for (int i = 0; i < number_cliques - 1; i++) {
        shuffle(vertices.begin(), vertices.end(), g);

        vector<int> coloring = random_sequential_greedy(G_complement, vertices);
        
        clique_list.push_back(find_stable_set(coloring));
    }

    sort(clique_list.begin(), clique_list.end(), 
        [](const auto& a, const auto& b) {
        return a.size() > b.size();
    });

    return clique_list;
}