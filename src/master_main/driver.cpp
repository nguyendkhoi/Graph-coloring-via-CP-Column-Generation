#include "driver.h"
#include "cg_loop.h"
#include "output_writer.h"
#include "pricing_helpers.h"
#include "reporting.h"
#include "summary_writer.h"
#include "util.h"
#include "onnx.h"

#include "../config/config.h"
#include "../column_generation/rmp.h"
#include "../column_generation/stable_set.h"
#include "../graph/graph.h"
#include "../preprocessing/clique_processing.h"

#include "gurobi_c++.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

string load_master_configured_instance_path(const string& override_instance_path) {
    ConfigLocation location = master_config_location();
    if (!fs::exists(location.path))
        throw runtime_error("Missing config file: " + location.path.string());
    load_config_json(location.path);
    if (!override_instance_path.empty())
        return override_instance_path;
    return resolve_instance_path(
        location.root,
        config.get<string>("instance", "tests/queen5_5.col")
    );
}

int run_column_generation(const string& instance_path) {
    auto run_start = chrono::high_resolution_clock::now();
    auto elapsed   = [&]() -> double {
        return chrono::duration<double>(
            chrono::high_resolution_clock::now() - run_start).count();
    };

    MasterRunOutput run_output = open_master_run_output(instance_path);
    MasterRunSummary& summary  = run_output.summary;
    JsonlLogger& logger        = run_output.logger;

    if (!fs::exists(instance_path)) {
        cerr << "Instance not found: " << instance_path << "\n";
        summary.rmp_status     = "INSTANCE_NOT_FOUND";
        summary.run_time_seconds = elapsed();
        summary.exit_code      = 1;
        write_run_summary(summary);
        return 1;
    }

    // Graph + initial bounds
    Graph G = parser_dimacs_col(instance_path, true);
    summary.n = G.num_vertices();
    summary.m = count_edges(G);

    ColumnPool pool;
    initialize_column_pool(pool, G);

    int proven_lb   = static_cast<int>(
        find_maximal_clique_from_complement(G.complement()).size());
    int incumbent_ub = static_cast<int>(dsatur_coloring_columns(G).size());

    print_run_header(instance_path, G, pool, proven_lb, incumbent_ub);
    log_run_start(logger, summary, pool.size());

    // Gurobi env + RMP
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    if (summary.threads > 0)
        env.set(GRB_IntParam_Threads, summary.threads);
    env.start();

    RMPSolver rmp(env, G.num_vertices());
    rmp.add_column_pool(pool);

    // ONNX session (nullptr = AI ordering disabled)
    string onnx_model_path_raw = config.get<string>("onnx_model_path", "");
    string onnx_model_path;
    if (!onnx_model_path_raw.empty()) {
        onnx_model_path = resolve_config_path(
            master_config_location().root, onnx_model_path_raw);
    }
    auto onnx = load_onnx_session(onnx_model_path, G);

    // Run column generation loop
    CGLoopContext ctx{
        env, rmp, pool, G, summary, logger,
        onnx.get(),
        proven_lb, incumbent_ub,
        static_cast<double>(proven_lb),
        elapsed
    };
    CGLoopResult result = run_cg_loop(ctx);

    // Aggregate final summary
    summary.iterations          = result.iterations;
    summary.rmp_status          = gurobi_status_name(result.sol.status);
    summary.lp_objective        = result.sol.objective;
    summary.proven_lb           = ctx.proven_lb;
    summary.incumbent_ub        = ctx.incumbent_ub;
    summary.column_count        = rmp.column_count();
    summary.active_lambdas      = count_active_lambdas(result.sol);
    summary.total_lambdas       = static_cast<int>(result.sol.lambda_value.size());
    summary.closed_gap          = (ctx.proven_lb >= ctx.incumbent_ub);
    summary.converged_by_pricing= result.converged_by_pricing;
    summary.reached_max_iter    = result.reached_max_iter;
    summary.run_time_seconds    = elapsed();
    summary.exit_code           = result.exit_code;

    print_final_report(result.iterations, result.sol, rmp,
        ctx.proven_lb, ctx.incumbent_ub,
        summary.closed_gap, result.converged_by_pricing);
    write_run_summary(summary);
    return result.exit_code;
}
