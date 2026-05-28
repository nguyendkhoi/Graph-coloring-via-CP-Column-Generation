#include <utility>
#include <graph/graph.h>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>
#include <unordered_set>
#include <unordered_map>

using namespace std;

pair<vector<int>, int> greedy_coloring(const Graph& G) {
    vector<int> coloring(G.num_vertices(), -1);

    // Sort the nodes by the number of edges
    vector<int> nodes_most_degree(G.num_vertices());
    iota(nodes_most_degree.begin(), nodes_most_degree.end(), 0);
    sort(nodes_most_degree.begin(), nodes_most_degree.end(), 
    [&G](int a, int b) {
        return G.degree(a) > G.degree(b);
    });

    // Coloring each nodes
    for (int node : nodes_most_degree) {
        unordered_set<int> neighbor_colors;
        for (int node_neighbor : G.neighbors(node)) {
            if (coloring[node_neighbor] != -1) {
                neighbor_colors.insert(coloring[node_neighbor]);
            }
        }
        
        // Coloring node
        int i = 0;
        while(neighbor_colors.count(i) > 0) i++;
        coloring[node] = i;
    }

    return {coloring, *max_element(coloring.begin(), coloring.end()) + 1};
}

pair<vector<int>, int> DSATUR_coloring(const Graph& G) {
    int v = G.num_vertices();

    vector<int> coloring(v, -1); //Set all vertices to -1 (uncolored)
    vector<unordered_set<int>> sat_colors   (v);

    // Cache degree
    vector<int> degree(v, 0);
    for (int i = 0; i < v; i++) degree[i] = G.degree(i);

    for (int step = 0; step < v; step++) {
        int chosen = -1; int best_sat = -1, best_deg = -1;
        //Find vertice with greatest saturations
        for (int u = 0; u < v; u++) {
            //If vertice is already colored
            if (coloring[u] != -1 ) continue;

            // If not have chosed u (preprocessing once when start)
            int sat_u = (int)sat_colors[u].size();

            //If new saturation is greater than the chosen one
            if (sat_u > best_sat || 
                (sat_u == best_sat && degree[u] > best_deg)) {
                chosen = u;
                best_sat = sat_u;
                best_deg = degree[u];
            }
        }

        // Coloring node
        int c = 0;
        while (sat_colors[chosen].count(c)) c++;
        coloring[chosen] = c;

        //Update saturation of neighbors node:
        for (int nb : G.neighbors(chosen)) {
            if (coloring[nb] == -1) {
                sat_colors[nb].insert(c);
            }
        }
    }

    return {coloring, *max_element(coloring.begin(), coloring.end()) + 1};
}

vector<int> random_sequential_greedy(const Graph& G, const vector<int>& V) {
    vector<int> coloring(G.num_vertices(), -1);
    // neighbor_color[u][c] = number neighbors u are colored c
    unordered_map<int, unordered_map<int, int>> neighbor_color;
    
    for (int v : V) {

        int color = 0;
        auto& v_neighbors_colors = neighbor_color[v];
        while (v_neighbors_colors.count(color)) {
            color++;
        }
        
        coloring[v] = color;
        
        // Update
        for (int neighbor : G.neighbors(v)) {
            neighbor_color[neighbor][color]++;
        }
    }
    
    return coloring;
}