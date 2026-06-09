#include "driver.h"

#include "pricing.h"
#include "rmp.h"
#include "stable_set.h"
#include "../augmented_pricing/augmented_pricing.h"
#include "../graph/graph.h"

#include "gurobi_c++.h"

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace {

long long count_edges(const Graph& G) {
    long long degree_sum = 0;
    for (int v = 0; v < G.num_vertices(); ++v) {
        degree_sum += static_cast<long long>(G.neighbors(v).size());
    }
    return degree_sum / 2;
}

string gurobi_status_name(int status) {
    switch (status) {
        case GRB_OPTIMAL: return "OPTIMAL";
        case GRB_INFEASIBLE: return "INFEASIBLE";
        case GRB_INF_OR_UNBD: return "INF_OR_UNBD";
        case GRB_UNBOUNDED: return "UNBOUNDED";
        case GRB_TIME_LIMIT: return "TIME_LIMIT";
        default: return "STATUS_" + to_string(status);
    }
}

void print_run_header(
    const MasterRunConfig& config,
    const Graph& G,
    const ColumnPool& pool,
    int incumbent_ub
) {
    cout << "============================================" << endl;
    cout << " Column Generation with Augmented Pricing" << endl;
    cout << " Instance : " << config.instance_path << endl;
    cout << " |V|      : " << G.num_vertices() << endl;
    cout << " |E|      : " << count_edges(G) << endl;
    cout << " Trials   : " << config.num_trials << endl;
    cout << " Seed     : " << config.seed << endl;
    cout << " MWSS TL  : " << config.mwss_time_limit_seconds << " s" << endl;
    cout << " AP TL    : " << config.augmented_time_limit_seconds << " s" << endl;
    cout << " Init cols: " << pool.size() << endl;
    cout << " Init UB  : " << incumbent_ub << endl;
    cout << "============================================" << endl;
}

long long lubbecke_desrosiers_bound(double rmp_objective, double reduced_cost) {
    double kappa = rmp_objective / (1.0 - reduced_cost);
    return static_cast<long long>(ceil(kappa - 1e-9));
}

void print_iteration(
    int iter,
    const RMPSolution& sol,
    double reduced_cost,
    long long lower_bound,
    int upper_bound,
    const string& step,
    int column_count
) {
    cout << "[iter " << setw(4) << iter << "] "
         << "obj = " << fixed << setprecision(6) << sol.objective
         << " | rc = " << reduced_cost
         << " | LB = " << lower_bound
         << " | UB = " << upper_bound
         << " | step = " << step
         << " | cols = " << column_count
         << endl;
}

int count_active_lambdas(const RMPSolution& sol) {
    int active = 0;
    for (double value : sol.lambda_value) {
        if (value > 1e-6) {
            ++active;
        }
    }
    return active;
}

void print_final_report(
    int iterations,
    const RMPSolution& sol,
    const RMPSolver& rmp,
    int incumbent_ub,
    bool closed_gap,
    bool converged_by_pricing
) {
    long long final_lb = static_cast<long long>(ceil(sol.objective - 1e-9));

    cout << "============================================" << endl;
    cout << "Finished after " << iterations << " iterations" << endl;
    cout << "RMP status: " << gurobi_status_name(sol.status) << endl;
    cout << "LP objective chi_f: "
         << fixed << setprecision(6) << sol.objective << endl;
    cout << "Final lower bound ceil(chi_f): " << final_lb << endl;
    cout << "Final upper bound kbar       : " << incumbent_ub << endl;
    cout << "Final columns in RMP         : " << rmp.column_count() << endl;
    cout << "Active lambdas: "
         << count_active_lambdas(sol) << " / " << sol.lambda_value.size()
         << endl;

    if (closed_gap || final_lb >= incumbent_ub) {
        cout << "OPTIMAL: chi(G) = " << incumbent_ub << endl;
        cout << "Reason : lower bound reached incumbent upper bound." << endl;
        return;
    }

    if (converged_by_pricing) {
        cout << "Column generation converged: no negative reduced cost column."
             << endl;
        cout << "Gap remains: [" << final_lb << ", " << incumbent_ub
             << "] -> Branch-and-Price is needed." << endl;
        return;
    }

    cout << "Stopped before full proof of optimality." << endl;
    cout << "Gap: [" << final_lb << ", " << incumbent_ub << "]" << endl;
}

bool solve_decision_pricing(
    const Graph& G,
    const vector<int>& max_clique,
    const vector<double>& dual_value,
    double threshold,
    MWSSResult& pricing
) {
    CP_CG cp_cg(G, max_clique);
    CPSolveResult res = solve_CP_CG(cp_cg, dual_value, threshold);

    if (!res.feasible) {
        return false;
    }

    pricing.col = StableColumn(res.vertices, G.num_vertices());
    pricing.reduced_cost = 1.0 - res.val;

    return pricing.reduced_cost < -1e-6;
}

} // namespace

MasterRunConfig parse_master_args(int argc, char** argv) {
    MasterRunConfig config;

    if (argc >= 2) {
        config.instance_path = argv[1];
    }
    if (argc >= 3) {
        config.num_trials = stoi(argv[2]);
    }
    if (argc >= 4) {
        config.seed = static_cast<size_t>(stoull(argv[3]));
    }
    if (argc >= 5) {
        config.mwss_time_limit_seconds = stod(argv[4]);
    }
    if (argc >= 6) {
        config.augmented_time_limit_seconds = stod(argv[5]);
    }

    return config;
}

int run_column_generation(const MasterRunConfig& config) {
    if (!fs::exists(config.instance_path)) {
        cerr << "Instance not found: " << config.instance_path << endl;
        return 1;
    }

    Graph G = parser_dimacs_col(config.instance_path, true);

    ColumnPool pool;
    pool.initialize(G, config.num_trials, config.seed);

    vector<StableColumn> incumbent = dsatur_coloring_columns(G);
    int incumbent_ub = static_cast<int>(incumbent.size());

    if (incumbent_ub <= 0) {
        cerr << "Invalid initial heuristic coloring." << endl;
        return 1;
    }

    print_run_header(config, G, pool, incumbent_ub);

    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();

    RMPSolver rmp(env, G.num_vertices());
    rmp.add_column_pool(pool);

    int cg_iter = 0;
    bool augmented_pricing_enabled = true;
    bool converged_by_pricing = false;
    bool closed_gap = false;
    bool reached_max_iter = false;

    RMPSolution sol;
    MWSSResult pricing;

    auto solve_current_rmp = [&]() -> bool {
        cout << "Run RMP" << endl;
        sol = rmp.solve();
        if (sol.status == GRB_OPTIMAL) {
            return true;
        }

        cerr << "RMP not optimal at iter " << cg_iter
             << ": " << gurobi_status_name(sol.status) << endl;
        return false;
    };

    auto stop_if_iteration_limit_reached = [&]() -> bool {
        if (cg_iter < config.max_iter) {
            return false;
        }

        cerr << "Reached max CG iterations." << endl;
        reached_max_iter = true;
        return true;
    };

    auto add_pricing_column = [&](const string& step) -> bool {
        long long lower_bound =
            lubbecke_desrosiers_bound(sol.objective, pricing.reduced_cost);

        if (lower_bound >= incumbent_ub) {
            closed_gap = true;
            print_iteration(
                cg_iter,
                sol,
                pricing.reduced_cost,
                lower_bound,
                incumbent_ub,
                step + " skipped",
                rmp.column_count()
            );
            return false;
        }

        if (!pool.insert(pricing.col)) {
            cout << "[iter " << cg_iter << "] duplicate " << step
                 << " column -> stop" << endl;
            return false;
        }

        rmp.add_column(pricing.col);
        print_iteration(
            cg_iter,
            sol,
            pricing.reduced_cost,
            lower_bound,
            incumbent_ub,
            step,
            rmp.column_count()
        );

        ++cg_iter;
        return !stop_if_iteration_limit_reached();
    };

    if (!solve_current_rmp()) {
        return 2;
    }

    while (!closed_gap && !reached_max_iter) {
        double decision_threshold = 1.0;
        cout << "Run decision pricing with threshold = "
             << fixed << setprecision(3) << decision_threshold << endl;

        bool found_decision_column =
            solve_decision_pricing(
                G,
                pool.column(0).vertices,
                sol.dual_value,
                decision_threshold,
                pricing
            );

        if (!found_decision_column) {
            break;
        }

        if (!add_pricing_column("decision")) {
            break;
        }

        if (!solve_current_rmp()) {
            return 2;
        }
    }

    while (!closed_gap && !reached_max_iter) {
        cout << "Run MWSS pricing" << endl;
        bool found_mwss_column = solve_mwss(
            env,
            G,
            sol.dual_value,
            pricing,
            config.mwss_time_limit_seconds
        );

        if (!found_mwss_column) {
            if (!pricing.stopped) {
                converged_by_pricing = true;
            }
            break;
        }

        vector<StableColumn> augmented_columns;
        bool attempted_augmented = false;
        bool augmented_success = false;

        if (augmented_pricing_enabled) {
            attempted_augmented = true;
            augmented_columns =
                solveAugmentedPricing(
                    pricing.col,
                    incumbent_ub - 1,
                    G,
                    config.augmented_time_limit_seconds
                );
            augmented_success = !augmented_columns.empty();

            if (!augmented_success) {
                augmented_pricing_enabled = false;
            }
        }

        if (augmented_success) {
            long long lower_bound =
                lubbecke_desrosiers_bound(sol.objective, pricing.reduced_cost);

            for (const StableColumn& col : augmented_columns) {
                if (pool.insert(col)) {
                    rmp.add_column(col);
                }
            }

            string step = "MWSS + augmented columns";
            int augmented_ub = static_cast<int>(augmented_columns.size());
            if (augmented_ub < incumbent_ub) {
                incumbent = augmented_columns;
                incumbent_ub = augmented_ub;
                step = "MWSS + augmented improve";
            }

            print_iteration(
                cg_iter,
                sol,
                pricing.reduced_cost,
                lower_bound,
                incumbent_ub,
                step,
                rmp.column_count()
            );

            if (lower_bound >= incumbent_ub) {
                closed_gap = true;
                break;
            }

            ++cg_iter;
            if (stop_if_iteration_limit_reached()) {
                break;
            }
        } else {
            string step = "MWSS";
            if (attempted_augmented) {
                step = "MWSS (AP failed)";
            } else if (!augmented_pricing_enabled) {
                step = "MWSS (AP disabled)";
            }

            if (!add_pricing_column(step)) {
                break;
            }
        }

        if (!solve_current_rmp()) {
            return 2;
        }
    }

    print_final_report(
        cg_iter,
        sol,
        rmp,
        incumbent_ub,
        closed_gap,
        converged_by_pricing
    );

    return 0;
}
