#include <set>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <random>
#include "../heuristic/heuristic.h"
#include "../graph/graph.h"

using namespace std;

// Helper find largest stable set in Graph complement (or clique in Graph) from coloring
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

vector<int> largest_clique(const Graph& G_complement) {
    vector<int> coloring = DSATUR_coloring(G_complement).first;

    return find_stable_set(coloring);
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