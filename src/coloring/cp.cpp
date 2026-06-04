#include "cp.h"
#include "heuristic.h"
#include <preprocessing/clique_processing.h>

#include <vector>
#include <algorithm>
#include <chrono>
#include <unordered_set>

using namespace std;
using namespace Gecode;

ColoringCP::ColoringCP(
    const Graph& graph,
    int num_colors,
    const vector<vector<int>>& clique_info
) : G(graph), k(num_colors), x(*this, graph.num_vertices(), 0, num_colors - 1) {

    int clique_size = 0;
    if (!clique_info.empty() && !clique_info[0].empty()) {
        vector<int> target_clique = clique_info[0];
        clique_size = static_cast<int>(target_clique.size());
        symetrique_breaking(target_clique);
    }

    add_edge_constraints();
    add_all_different(clique_info);

    if (num_colors > clique_size) {
        IntArgs remaining_colors(num_colors - clique_size);
        for (int i = clique_size; i < num_colors; ++i) {
            remaining_colors[i - clique_size] = i;
        }
        Symmetries syms;
        syms << ValueSymmetry(remaining_colors);
        branch(*this, x, INT_VAR_SIZE_MIN(), INT_VAL_MIN(), syms);
    } else {
        branch(*this, x, INT_VAR_SIZE_MIN(), INT_VAL_MIN());
    }
};

ColoringCP::ColoringCP(ColoringCP& other)
    : Space(other), G(other.G), k(other.k) {
    x.update(*this, other.x);
}

Space* ColoringCP::copy() {
    return new ColoringCP(*this);
}

void ColoringCP::print_solution() const {
    for (int v = 0; v < x.size(); v++) {
        cout << "vertex " << v << " -> color " << x[v].val() << "\n";
    }
}

void ColoringCP::add_edge_constraints() {
    int n = G.num_vertices();
    for (int u = 0; u < n; u++) {
        for (const int& v : G.neighbors(u)) {
            if (u < v) {
                rel(*this, x[u], IRT_NQ, x[v]);
            }
        }
    }
}

void ColoringCP::add_all_different(const vector<vector<int>>& clique_info) {
    if (clique_info.empty()) return;

    for (int i = 1; i < (int)clique_info.size(); i++) {
        const vector<int>& current_clique = clique_info[i];
        int clique_size = (int)current_clique.size();
        IntVarArgs clique_variables(clique_size);
        for (int a = 0; a < clique_size; a++) {
            clique_variables[a] = x[current_clique[a]];
        }
        distinct(*this, clique_variables, IPL_DOM);
    }
}

void ColoringCP::symetrique_breaking(vector<int> clique) {
    sort(clique.begin(), clique.end(),
        [this](int a, int b) {
            return G.degree(a) > G.degree(b);
        }
    );

    for (int i = 0; i < static_cast<int>(clique.size()); i++) {
        int vertice = clique[i];
        rel(*this, x[vertice], IRT_EQ, i);
    }
}

CPSolveResult solve_coloring_cp(
    const Graph& G,
    int k,
    const vector<vector<int>>& clique_info,
    double time_limit
) {
    if (!clique_info.empty() && static_cast<int>(clique_info[0].size()) > k) {
        return {false, false, -1, {}};
    }

    ColoringCP* problem = new ColoringCP(G, k, clique_info);

    Search::Options opts;
    Search::TimeStop stop(static_cast<unsigned long long>(time_limit * 1000));
    opts.stop = &stop;

    DFS<ColoringCP> engine(problem, opts);

    if (ColoringCP* sol = engine.next()) {
        vector<int> colors;
        int max_color = -1;
        for (int i = 0; i < sol->x.size(); i++) {
            int c = sol->x[i].val();
            colors.push_back(c);
            if (c > max_color) max_color = c;
        }
        delete sol;
        return {true, false, max_color + 1, colors};
    } else if (engine.stopped()) {
        return {false, true, -1, {}};
    } else {
        return {false, false, -1, {}};
    }
}

static vector<int> restore_coloring(
    const vector<int>& reduced_colors,
    const GraphReduction& reduction,
    const Graph& G
) {
    int n = G.num_vertices();
    vector<int> full_colors(n, -1);

    for (int i = 0; i < (int)reduction.to_origin.size(); i++)
        full_colors[reduction.to_origin[i]] = reduced_colors[i];

    for (int i = (int)reduction.removed_vertices.size() - 1; i >= 0; i--) {
        int v = reduction.removed_vertices[i];
        unordered_set<int> used;
        for (int u : G.neighbors(v))
            if (full_colors[u] >= 0) used.insert(full_colors[u]);
        int c = 0;
        while (used.count(c)) c++;
        full_colors[v] = c;
    }

    return full_colors;
}

CPSolveResult cp_upper_bound(
    const Graph& G,
    const vector<vector<int>>& clique_info,
    int dsatur_ub,
    double time_limit
) {
    int lb = clique_info.empty() ? 1 : (int)clique_info[0].size();

    GraphReduction reduction = reduce_by_degree(G, lb);
    bool is_reduced = !reduction.removed_vertices.empty();

    vector<vector<int>> reduced_clique_info;
    for (const auto& clique : clique_info) {
        vector<int> remapped;
        for (int v : clique) {
            int rv = reduction.to_reduced[v];
            if (rv >= 0) remapped.push_back(rv);
        }
        if (!remapped.empty())
            reduced_clique_info.push_back(remapped);
    }

    const Graph& solve_G = is_reduced ? reduction.reduced : G;
    const vector<vector<int>>& solve_clique = is_reduced ? reduced_clique_info : clique_info;

    CPSolveResult best;
    best.feasible = true;
    best.num_colors = dsatur_ub;
    best.stopped = false;
    best.color = {};

    auto t_start = chrono::high_resolution_clock::now();
    int k = dsatur_ub - 1;

    while (k >= lb) {
        double elapsed = chrono::duration<double>(
            chrono::high_resolution_clock::now() - t_start).count();
        double remain = time_limit - elapsed;
        if (remain <= 0.0) { best.stopped = true; break; }

        double budget = min(remain, max(5.0, remain / 3.0));

        CPSolveResult r = solve_coloring_cp(solve_G, k, solve_clique, budget);

        if (r.feasible) {
            if (is_reduced) {
                r.color = restore_coloring(r.color, reduction, G);
                r.num_colors = *max_element(r.color.begin(), r.color.end()) + 1;
            }
            best = r;
            k = r.num_colors - 1;
        } else if (r.stopped) {
            best.stopped = true;
            break;
        } else {
            break;
        }
    }
    return best;
}
