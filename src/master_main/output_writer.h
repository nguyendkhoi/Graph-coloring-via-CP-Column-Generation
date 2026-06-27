#pragma once

#include "driver.h"
#include "../column_generation/rmp.h"
#include "../column_generation/pricing.h"
#include "../coloring/cp.h"
#include "../column_generation/stable_set.h"
#include "../graph/graph.h"

#include <fstream>
#include <string>
#include <vector>

struct JsonlLogger {
    std::ofstream stream;
    std::ofstream vertex_stream;
    std::ofstream pricing_comparison_stream;
    std::string path;
    std::string vertex_path;
    std::string pricing_comparison_path;

    bool open(MasterRunSummary& summary);
    void write(const std::string& record);
    void write_vertex(const std::string& record);
    void write_pricing_comparison(const std::string& record);
};

struct PricingOrderComparison {
    std::string method;
    double cp_time_seconds = 0.0;
    std::vector<int> vertex_order;
    CPSolveResult cp_result;
    StableSetPricingResult pricing_result;
};

struct MasterRunOutput {
    MasterRunSummary summary;
    JsonlLogger logger;
};

MasterRunOutput open_master_run_output(const std::string& instance_path);

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

void log_pricing_order_comparison(
    JsonlLogger& logger,
    int cg_iter,
    double rmp_value,
    double decision_threshold,
    const PricingOrderComparison& dual_comp,
    const PricingOrderComparison& ai_comp,
    bool mwss_ran,
    const StableSetPricingResult& mwss_result,
    double mwss_time_seconds
);

