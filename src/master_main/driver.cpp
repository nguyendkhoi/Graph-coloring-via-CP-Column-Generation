#include "driver.h"

#include "output_writer.h"
#include "pricing_helpers.h"
#include "reporting.h"
#include "util.h"

#include "../config/config.h"
#include "../column_generation/pricing.h"
#include "../column_generation/rmp.h"
#include "../column_generation/stable_set.h"
#include "../graph/graph.h"
#include "../preprocessing/clique_processing.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace {

fs::path master_config_path() {
    return fs::path("master_cp") / "solver_config.json";
}

} // namespace

void load_master_config() {
    fs::path config_path = master_config_path();
    if (!fs::exists(config_path)) {
        throw runtime_error("Missing config file: " + config_path.string());
    }

    load_config_json(config_path);
}

string configured_instance_path() {
    fs::path config_path = master_config_path();
    fs::path root = config_root_from_file(config_path);
    return resolve_instance_path(
        root,
        config.get<string>("instance", "tests/queen5_5.col")
    );
}

int run_column_generation(const string& instance_path) {
    auto run_start_time = chrono::high_resolution_clock::now();
    auto elapsed_run_seconds = [&]() -> double {
        return chrono::duration<double>(
            chrono::high_resolution_clock::now() - run_start_time
        ).count();
    };

    // Summary JSON format
    MasterRunSummary summary;
    summary.run_id = generate_uuid();
    summary.instance_path = instance_path;
    summary.instance = fs::path(instance_path).filename().string();
    summary.num_trials = config.get<int>("num_trials", 20);
    summary.seed = config.get<size_t>("seed", 40);
    summary.threads = config.get<int>("threads", 1);
    summary.time_limit_seconds =
        config.get<double>("time_limit_seconds", 3600.0);

    JsonlLogger logger;
    logger.open(summary);

    if (!fs::exists(instance_path)) {
        cerr << "Instance not found: " << instance_path << endl;
        summary.rmp_status = "INSTANCE_NOT_FOUND";
        summary.run_time_seconds = elapsed_run_seconds();
        summary.exit_code = 1;
        write_run_summary(summary);
        return 1;
    }

    // Initialize Graph
    Graph G = parser_dimacs_col(instance_path, true);
    summary.n = G.num_vertices();
    summary.m = count_edges(G);

    ColumnPool pool;
    initialize_column_pool(pool, G);

    
    vector<vector<int>> clique_info = generate_clique(G, 20);
    int proven_lb = clique_info.empty() ? (G.num_vertices() > 0 ? 1 : 0)
                                        : static_cast<int>(clique_info[0].size());
    double adaptive_lower_bound = static_cast<double>(proven_lb);
    int incumbent_ub = static_cast<int>(dsatur_coloring_columns(G).size());

    print_run_header(instance_path, G, pool, proven_lb, incumbent_ub);
    log_run_start(logger, summary, pool.size());

    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    if (summary.threads > 0) {
        env.set(GRB_IntParam_Threads, summary.threads);
    }
    env.start();

    RMPSolver rmp(env, G.num_vertices());
    rmp.add_column_pool(pool);

    int cg_iter = 0;
    bool converged_by_pricing = false;
    bool closed_gap = false;
    bool reached_max_iter = false;
    bool use_augmented_pricing = true;

    RMPSolution sol;
    StableSetPricingResult pricing_result;
    double last_rmp_time_seconds = 0.0;

    auto solve_current_rmp = [&]() -> bool {
        cout << "Run RMP" << endl;
        auto t0 = chrono::high_resolution_clock::now();
        sol = rmp.solve();
        last_rmp_time_seconds = chrono::duration<double>(
            chrono::high_resolution_clock::now() - t0
        ).count();
        if (sol.status == GRB_OPTIMAL) {
            return true;
        }

        cerr << "RMP not optimal at iter " << cg_iter
             << ": " << gurobi_status_name(sol.status) << endl;
        return false;
    };

    auto stop_if_iteration_limit_reached = [&]() -> bool {
        if (cg_iter < config.get<int>("max_iter", 100000)) {
            return false;
        }

        cerr << "Reached max CG iterations." << endl;
        reached_max_iter = true;
        return true;
    };

    auto add_pricing_column = [&](const string& step) -> bool {
        if (proven_lb >= incumbent_ub) {
            closed_gap = true;
            print_iteration(
                cg_iter,
                sol,
                pricing_result.reduced_cost,
                proven_lb,
                incumbent_ub,
                step + " skipped",
                rmp.column_count()
            );
            return false;
        }

        if (!pool.insert(pricing_result.column)) {
            cout << "[iter " << cg_iter << "] duplicate " << step
                 << " column -> stop" << endl;
            return false;
        }

        rmp.add_column(pricing_result.column);
        print_iteration(
            cg_iter,
            sol,
            pricing_result.reduced_cost,
            proven_lb,
            incumbent_ub,
            step,
            rmp.column_count()
        );

        ++cg_iter;
        return !stop_if_iteration_limit_reached();
    };

    auto finish_with_code = [&](int code) -> int {
        summary.iterations = cg_iter;
        summary.rmp_status = gurobi_status_name(sol.status);
        summary.lp_objective = sol.objective;
        summary.proven_lb = proven_lb;
        summary.incumbent_ub = incumbent_ub;
        summary.column_count = rmp.column_count();
        summary.active_lambdas = count_active_lambdas(sol);
        summary.total_lambdas = static_cast<int>(sol.lambda_value.size());
        summary.closed_gap = closed_gap;
        summary.converged_by_pricing = converged_by_pricing;
        summary.reached_max_iter = reached_max_iter;
        summary.run_time_seconds = elapsed_run_seconds();
        summary.exit_code = code;
        write_run_summary(summary);
        return code;
    };

    if (!solve_current_rmp()) {
        return finish_with_code(2);
    }

    while (!closed_gap && !reached_max_iter) {
        if (summary.time_limit_seconds > 0.0
            && elapsed_run_seconds() >= summary.time_limit_seconds) {
            cerr << "Reached global time limit." << endl;
            summary.reached_time_limit = true;
            break;
        }

        double decision_threshold =
            compute_adaptive_decision_threshold(
                sol.objective,
                adaptive_lower_bound
            );
        vector<int> static_order =
            build_pricing_order(G, sol.dual_value, cg_iter);

        string pricing_id = summary.run_id
            + "-node0-cg" + to_string(cg_iter);
        auto pricing_t0 = chrono::high_resolution_clock::now();

        bool found_pricing_column = solve_decision_pricing_column(
            G,
            sol.dual_value,
            decision_threshold,
            static_order,
            pricing_result,
            config.get<double>("decision_pricing_limit", 5.0)
        );

        string step = "Decision pricing";

        if (found_pricing_column) {
            cout << "Decision pricing found a column"
                 << " | rc = " << fixed << setprecision(6)
                 << pricing_result.reduced_cost << endl;
        } else {
            cout << "Decision pricing failed; switch to MWSS pricing" << endl;
            cout << "Run MWSS pricing" << endl;

            found_pricing_column = solve_maximum_weight_stable_set_pricing(
                env,
                G,
                sol.dual_value,
                pricing_result,
                config.get<double>("exact_pricing_limit", 40.0)
            );

            step = "MWSS";

            if (!found_pricing_column) {
                cout << "MWSS pricing failed";
                if (pricing_result.stopped) {
                    cout << " (time limit)";
                }
                cout << endl;

                double pricing_time_seconds = chrono::duration<double>(
                    chrono::high_resolution_clock::now() - pricing_t0
                ).count();
                log_pricing_iteration(
                    logger,
                    summary.run_id,
                    cg_iter,
                    pricing_id,
                    sol,
                    rmp.column_count(),
                    pricing_result.reduced_cost,
                    last_rmp_time_seconds,
                    pricing_time_seconds
                );
                log_vertex_features(
                    logger,
                    cg_iter,
                    G,
                    sol.dual_value,
                    pool,
                    pricing_result.column
                );

                if (pricing_result.proven_optimal) {
                    proven_lb = max(proven_lb, ceil_bound(sol.objective));
                    adaptive_lower_bound = max(adaptive_lower_bound, sol.objective);
                    converged_by_pricing = true;
                    closed_gap = proven_lb >= incumbent_ub;
                }
                break;
            }

            cout << "MWSS pricing found a column"
                 << " | rc = " << fixed << setprecision(6)
                 << pricing_result.reduced_cost << endl;

            if (pricing_result.proven_optimal) {
                double exact_pricing_bound =
                    sol.objective / (1.0 - pricing_result.reduced_cost);
                adaptive_lower_bound =
                    max(adaptive_lower_bound, exact_pricing_bound);

                int exact_pricing_lb = ceil_bound(exact_pricing_bound);
                proven_lb = max(proven_lb, exact_pricing_lb);

                if (proven_lb >= incumbent_ub) {
                    closed_gap = true;
                    print_iteration(
                        cg_iter,
                        sol,
                        pricing_result.reduced_cost,
                        proven_lb,
                        incumbent_ub,
                        step + " bound",
                        rmp.column_count()
                    );
                    double pricing_time_seconds = chrono::duration<double>(
                        chrono::high_resolution_clock::now() - pricing_t0
                    ).count();
                    log_pricing_iteration(
                        logger,
                        summary.run_id,
                        cg_iter,
                        pricing_id,
                        sol,
                        rmp.column_count(),
                        pricing_result.reduced_cost,
                        last_rmp_time_seconds,
                        pricing_time_seconds
                    );
                    log_vertex_features(
                        logger,
                        cg_iter,
                        G,
                        sol.dual_value,
                        pool,
                        pricing_result.column
                    );
                    break;
                }
            }
        }

        double pricing_time_seconds = chrono::duration<double>(
            chrono::high_resolution_clock::now() - pricing_t0
        ).count();
        log_pricing_iteration(
            logger,
            summary.run_id,
            cg_iter,
            pricing_id,
            sol,
            rmp.column_count(),
            pricing_result.reduced_cost,
            last_rmp_time_seconds,
            pricing_time_seconds
        );
        log_vertex_features(
            logger,
            cg_iter,
            G,
            sol.dual_value,
            pool,
            pricing_result.column
        );

        vector<StableColumn> augmented_columns;
        if (use_augmented_pricing && try_improve_upper_bound_with_augmented_pricing(
            G,
            pricing_result.column,
            config.get<double>("augmented_time_limit_seconds", 40.0),
            incumbent_ub,
            augmented_columns
        )) {
            for (const StableColumn& column : augmented_columns) {
                if (pool.insert(column)) {
                    rmp.add_column(column);
                }
            }
            step += " + CP improve";

            print_iteration(
                cg_iter,
                sol,
                pricing_result.reduced_cost,
                proven_lb,
                incumbent_ub,
                step,
                rmp.column_count()
            );

            ++cg_iter;
            if (proven_lb >= incumbent_ub) {
                closed_gap = true;
                break;
            }
            if (stop_if_iteration_limit_reached()) {
                break;
            }
        } else {
            if (use_augmented_pricing) {
                use_augmented_pricing = false;
                cout << "Disable augmented pricing after first failed attempt."
                     << endl;
            }
            if (!add_pricing_column(step)) {
                break;
            }
        }

        if (!solve_current_rmp()) {
            return finish_with_code(2);
        }
    }

    print_final_report(
        cg_iter,
        sol,
        rmp,
        proven_lb,
        incumbent_ub,
        closed_gap,
        converged_by_pricing
    );

    return finish_with_code(0);
}
