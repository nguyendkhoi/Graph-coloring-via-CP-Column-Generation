#include "vertex_features.h"

using namespace std;

namespace vf {

vector<int> column_frequencies(const ColumnPool& pool, int n) {
    vector<int> frequency(n, 0);
    for (const StableColumn& col : pool.columns) {
        for (int v : col.vertices) {
            if (v >= 0 && v < n)
                ++frequency[v];
        }
    }
    return frequency;
}

double weighted_neighbor_dual(
    const Graph& G,
    const vector<double>& dual_value,
    int vertex
) {
    double total = 0.0;
    for (int nb : G.neighbors(vertex)) {
        if (nb >= 0 && nb < static_cast<int>(dual_value.size()))
            total += dual_value[nb];
    }
    return total;
}

} // namespace vf
