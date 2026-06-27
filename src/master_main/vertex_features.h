#pragma once

#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

#include <algorithm>
#include <vector>

namespace vf {

std::vector<int> column_frequencies(const ColumnPool& pool, int n);

double weighted_neighbor_dual(
    const Graph& G,
    const std::vector<double>& dual_value,
    int vertex
);

template<typename T>
std::vector<T> minmax_normalize(const std::vector<T>& values) {
    std::vector<T> result(values.size(), T(0));
    if (values.empty()) return result;
    auto [mn, mx] = std::minmax_element(values.begin(), values.end());
    T range = *mx - *mn;
    if (range <= T(1e-12)) return result;
    T base = *mn;
    for (std::size_t i = 0; i < values.size(); ++i)
        result[i] = (values[i] - base) / range;
    return result;
}

} // namespace vf
