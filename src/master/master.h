#pragma once

#include "vector"
#include "unordered_set"

#include "../graph/graph.h"

class StableColumn {
public:
    std::vector<int> vertices;
    std::vector<uint64_t> bitset;
    double reduced_cost = 0.0; // < 0 good > 1 not good

    //Constructor
    StableColumn(const std::vector<int>& input_vertices, int num_vertices);
};

struct BitsetHash {
    std::size_t operator()(const std::vector<uint64_t>& bs) const noexcept {
        std::size_t seed = bs.size();
        for (uint64_t word : bs) {
            seed ^= word + 0x9e3779b97f4a7c15ULL + (seed << 12) + (seed >> 4);
        }
        return seed;
    }
};

class ColumnPool {
public:
    std::vector<StableColumn> columns;

private:
    std::unordered_set<std::vector<uint64_t>, BitsetHash> seen_bitsets;

public:

    ColumnPool initialize(const Graph& G, int num_trial = 20, size_t seed = 40);

    bool insert(StableColumn col);

    bool is_contains(const StableColumn& col);

    int size();

    const StableColumn& column(int j);
};

bool is_stable_set(const Graph& G, const std::vector<int>& vertices);

