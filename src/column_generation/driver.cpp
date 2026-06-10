#include "driver.h"

#include "pricing.h"
#include "rmp.h"
#include "stable_set.h"
#include "../augmented_pricing/augmented_pricing.h"
#include "../graph/graph.h"
#include "../preprocessing/clique_processing.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
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
    int proven_lb,
    int incumbent_ub
) {
    cout << "============================================" << endl;
    cout << " Column Generation with Augmented Pricing" << endl;
    cout << " Instance : " << config.instance_path << endl;
    cout << " |V|      : " << G.num_vertices() << endl;
    cout << " |E|      : " << count_edges(G) << endl;
    cout << " Trials   : " << config.num_trials << endl;
    cout << " MWSS TL  : " << config.mwss_time_limit_seconds << " s" << endl;
    cout << " AP TL    : " << config.augmented_time_limit_seconds << " s" << endl;
    cout << " Init cols: " << pool.size() << endl;
    cout << " Init LB  : " << proven_lb << endl;
    cout << " Init UB  : " << incumbent_ub << endl;
    cout << "============================================" << endl;
}

int ceil_bound(double value) {
    return static_cast<int>(ceil(value - 1e-9));
}

vector<int> weighted_shuffled_static_order(
    const Graph& G,
    const vector<double>& dual_value,
    size_t seed,
    int iteration
) {
    int n = G.num_vertices();
    vector<int> order(n);
    iota(order.begin(), order.end(), 0);

    mt19937 rng(static_cast<unsigned int>(
        seed ^ (0x9e3779b9U + static_cast<unsigned int>(iteration) * 2654435761U)
    ));
    shuffle(order.begin(), order.end(), rng);

    vector<double> score(n, 0.0);
    for (int v = 0; v < n; ++v) {
        for (int u = 0; u < n; ++u) {
            if (u != v && !G.has_edge(u, v)) {
                score[v] += max(0.0, dual_value[u]);
            }
        }
    }

    stable_sort(order.begin(), order.end(),
        [&](int a, int b) {
            return score[a] > score[b] + 1e-12;
        }
    );
    return order;
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
    int proven_lb,
    int incumbent_ub,
    bool closed_gap,
    bool converged_by_pricing
) {
    cout << "============================================" << endl;
    cout << "Finished after " << iterations << " iterations" << endl;
    cout << "RMP status: " << gurobi_status_name(sol.status) << endl;
    cout << "LP objective chi_f: "
         << fixed << setprecision(6) << sol.objective << endl;
    cout << "Final proven lower bound      : " << proven_lb << endl;
    cout << "Final upper bound kbar       : " << incumbent_ub << endl;
    cout << "Final columns in RMP         : " << rmp.column_count() << endl;
    cout << "Active lambdas: "
         << count_active_lambdas(sol) << " / " << sol.lambda_value.size()
         << endl;

    if (closed_gap || proven_lb >= incumbent_ub) {
        cout << "OPTIMAL: chi(G) = " << incumbent_ub << endl;
        cout << "Reason : lower bound reached incumbent upper bound." << endl;
        return;
    }

    if (converged_by_pricing) {
        cout << "Column generation converged: no negative reduced cost column."
             << endl;
        cout << "Gap remains: [" << proven_lb << ", " << incumbent_ub
             << "] -> Branch-and-Price is needed." << endl;
        return;
    }

    cout << "Stopped before full proof of optimality." << endl;
    cout << "Gap: [" << proven_lb << ", " << incumbent_ub << "]" << endl;
}

bool try_improve_upper_bound_with_augmented_pricing(
    const Graph& G,
    const StableColumn& forced_column,
    double time_limit_seconds,
    int& incumbent_ub,
    vector<StableColumn>& augmented_columns
) {
    int target_k = incumbent_ub - 1;
    cout << "Run CP coloring check with k = " << target_k << endl;

    augmented_columns =
        solve_augmented_pricing(forced_column, target_k, G, time_limit_seconds);

    if (augmented_columns.empty()) {
        cout << " -> Failed (no feasible coloring found within time limit)." << endl;
        return false;
    } else {
        cout << " -> Success! Found a better coloring with k = " << target_k << "." << endl;
        incumbent_ub = target_k;
        return true;
    }
}

bool solve_decision_pricing_column(
    const Graph& G,
    const vector<double>& dual_value,
    double weight_threshold,
    const vector<int>& static_order,
    StableSetPricingResult& pricing_result
) {
    pricing_result = StableSetPricingResult{};

    DecisionPricingModel model(G, {});
    CPSolveResult res =
        solve_decision_pricing_model(
            model,
            dual_value,
            weight_threshold,
            static_order
        );

    if (!res.feasible) {
        return false;
    }

    pricing_result.column = StableColumn(res.vertices, G.num_vertices());
    pricing_result.reduced_cost = 1.0 - res.val;

    return pricing_result.reduced_cost < -1e-6;
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

    // 1. Initialize Problem (Graph)
    Graph G = parser_dimacs_col(config.instance_path, true);

    // 2. Build initial columns and initial upper bound kbar
    ColumnPool pool;
    pool.initialize(G, config.num_trials, config.seed);

    vector<vector<int>> clique_info = generate_clique(G, 20);
    int proven_lb = clique_info.empty() ? (G.num_vertices() > 0 ? 1 : 0)
                                        : static_cast<int>(clique_info[0].size());
    // kappa_i(c*) for adaptive threshold formula (24); starts from clique LB.
    double adaptive_lower_bound = static_cast<double>(proven_lb);
    int incumbent_ub = static_cast<int>(dsatur_coloring_columns(G).size());

    print_run_header(config, G, pool, proven_lb, incumbent_ub);

    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();

    // 3. Create Restricted Master Problem (RMP)
    RMPSolver rmp(env, G.num_vertices());
    rmp.add_column_pool(pool);

    int cg_iter = 0;
    bool converged_by_pricing = false;
    bool closed_gap = false;
    bool reached_max_iter = false;

    RMPSolution sol;
    StableSetPricingResult pricing_result;

    // Solve the current RMP with LP relaxation and get dual values.
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

    // Add-column phase
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

    if (!solve_current_rmp()) {
        return 2;
    }

    // CP–CG algorithm with the augmented pricing.
    while (!closed_gap && !reached_max_iter) {

        // 4. Run Decision Pricing w/ r > 1.0
        double decision_threshold =
            compute_adaptive_decision_threshold(
                sol.objective,
                adaptive_lower_bound
            );
        vector<int> static_order =
            weighted_shuffled_static_order(G, sol.dual_value, config.seed, cg_iter);

        bool found_pricing_column = solve_decision_pricing_column(
            G,
            sol.dual_value,
            decision_threshold,
            static_order,
            pricing_result
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
                config.mwss_time_limit_seconds
            );

            step = "MWSS";

            // IF not found any pricing problem -> branch
            if (!found_pricing_column) {
                cout << "MWSS pricing failed";
                if (pricing_result.stopped) {
                    cout << " (time limit)";
                }
                cout << endl;

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
                    break;
                }
            }
        }
        
        // Run augmented pricing after found a new column
        vector<StableColumn> augmented_columns;

        if( try_improve_upper_bound_with_augmented_pricing(
            G,
            pricing_result.column,
            config.augmented_time_limit_seconds,
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
            if (!add_pricing_column(step)) {
                break;
            }
        }

        // Solve new RMP
        if (!solve_current_rmp()) {
            return 2;
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

    return 0;
}
