#pragma once

#include "driver.h"
#include "../column_generation/rmp.h"
#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

#include <fstream>
#include <string>
#include <vector>

struct JsonlLogger {
    std::ofstream stream;
    std::ofstream vertex_stream;
    std::string path;
    std::string vertex_path;

    bool open(MasterRunSummary& summary);
    void write(const std::string& record);
    void write_vertex(const std::string& record);
};

void log_run_start(
    JsonlLogger& logger,
    const MasterRunSummary& summary,
    int initial_columns
);

void log_pricing_iteration(
    JsonlLogger& logger,
    const std::string& run_id,
    int cg_iter,
    const std::string& pricing_id,
    const RMPSolution& sol,
    int column_count,
    double last_reduced_cost,
    double rmp_time,
    double pricing_time
);

void log_vertex_features(
    JsonlLogger& logger,
    int cg_iter,
    const Graph& G,
    const std::vector<double>& dual_value,
    const ColumnPool& pool,
    const StableColumn& selected_column
);

void write_run_summary(const MasterRunSummary& summary);
