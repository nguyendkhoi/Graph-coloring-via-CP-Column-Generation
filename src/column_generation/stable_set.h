#pragma once

#include <vector>
#include <unordered_set>
#include <cstdint>
#include <cstddef>

#include <graph/graph.h>

// A stable set (independent set) represented as vertex list + bitset.
class StableColumn {
public:
    std::vector<int> vertices;
    std::vector<uint64_t> bitset;
    double reduced_cost = 0.0;

public:
    StableColumn() = default;

    StableColumn(
        const std::vector<int>& input_vertices,
        int num_vertices
    );

    bool contains_vertex(int v) const;
};

struct BitsetHash {
    std::size_t operator()(const std::vector<uint64_t>& bs) const noexcept {
        std::size_t seed = bs.size();
        for (uint64_t word : bs) {
            seed ^= word
                  + 0x9e3779b97f4a7c15ULL
                  + (seed << 12)
                  + (seed >> 4);
        }
        return seed;
    }
};

// Deduplicating pool of stable set columns.
class ColumnPool {
public:
    std::vector<StableColumn> columns;

private:
    std::unordered_set<std::vector<uint64_t>, BitsetHash> seen_bitsets;

public:
    // Populate pool with initial columns from randomized greedy colorings.
    void initialize(
        const Graph& G,
        int num_trial = 20,
        std::size_t seed = 40
    );

    // Insert column; returns false if duplicate.
    bool insert(StableColumn col);

    bool is_contains(const StableColumn& col);
    int size() const;
    const StableColumn& column(int j) const;
};

//Helper
std::vector<StableColumn> dsatur_coloring_columns(const Graph& G);