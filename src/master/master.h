#pragma once

#include <vector>
#include <unordered_set>
#include <cstdint>
#include <cstddef>

#include "gurobi_c++.h"
#include "../graph/graph.h"

// ============================================================
// Stable Column
// ============================================================
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
};

// ============================================================
// Hash for bitset
// ============================================================
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

// ============================================================
// Column Pool
// ============================================================
class ColumnPool {
public:
    std::vector<StableColumn> columns;

private:
    std::unordered_set<std::vector<uint64_t>, BitsetHash> seen_bitsets;

public:
    void initialize(
        const Graph& G,
        int num_trial = 20,
        std::size_t seed = 40
    );

    bool insert(StableColumn col);

    bool is_contains(const StableColumn& col);

    int size() const;

    const StableColumn& column(int j) const;
};

// ============================================================
// RMP Solution
// ============================================================
struct RMPSolution {
public:
    double objective = 0.0;
    std::vector<double> lambda_value;
    std::vector<double> dual_value;
    int status = 0;
};

// ============================================================
// Restricted Master Problem Solver
// ============================================================
class RMPSolver {
private:
    GRBModel model;
    std::vector<GRBVar> lambda;
    std::vector<GRBConstr> constraints;
    int num_vertices;

public:
    RMPSolver(
        GRBEnv& env,
        int num_vertices
    );

    void add_column(
        const StableColumn& col
    );

    void add_column_pool(
        const ColumnPool& pool
    );

    int column_count() const { return static_cast<int>(lambda.size()); }

    RMPSolution solve();
};

// ============================================================
// MWSS Result
// ============================================================
struct MWSSResult {
    StableColumn col;
    double reduced_cost = 0.0;
};

// ============================================================
// Free functions
// ============================================================
bool is_stable_set(
    const Graph& G,
    const std::vector<int>& vertices
);

bool solve_mwss(
    GRBEnv& env,
    const Graph& G,
    const std::vector<double>& dual_value,
    MWSSResult& res
);