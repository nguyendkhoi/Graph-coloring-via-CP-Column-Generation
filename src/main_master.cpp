#include "graph/graph.h"
#include "column_generation/stable_set.h"
#include "column_generation/rmp.h"
#include "column_generation/pricing.h"
#include "augmented_pricing/augmented_pricing.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace std;
namespace fs = filesystem;

static long long count_edges(const Graph& G) {
    long long degree_sum = 0;
    for (int v = 0; v < G.num_vertices(); ++v) {
        degree_sum += static_cast<long long>(G.neighbors(v).size());
    }
    return degree_sum / 2;
}

static string gurobi_status_name(int status) {
    switch (status) {
        case GRB_OPTIMAL:    return "OPTIMAL";
        case GRB_INFEASIBLE: return "INFEASIBLE";
        case GRB_INF_OR_UNBD:return "INF_OR_UNBD";
        case GRB_UNBOUNDED:  return "UNBOUNDED";
        case GRB_TIME_LIMIT: return "TIME_LIMIT";
        default:             return "STATUS_" + to_string(status);
    }
}

int main(int argc, char** argv) {
    try {
        string instance_path = "tests/queen5_5.col";
        int num_trials = 20;
        size_t seed = 40;

        if (argc >= 2) instance_path = argv[1];
        if (argc >= 3) num_trials = stoi(argv[2]);
        if (argc >= 4) seed = static_cast<size_t>(stoull(argv[3]));

        if (!fs::exists(instance_path)) {
            cerr << "Instance not found: " << instance_path << endl;
            return 1;
        }

        Graph G = parser_dimacs_col(instance_path, true);

        // ------------------------------------------------------------
        // Preprocessing / initial column pool
        // ------------------------------------------------------------
        ColumnPool pool;
        pool.initialize(G, num_trials, seed);

        // UB1 from heuristic coloring
        // Khởi tạo incumbent bằng lời giải heuristic ban đầu
        vector<StableColumn> incumbent = dsatur_coloring_columns(G);
        int kbar = (int)incumbent.size();

        if (kbar <= 0) {
            cerr << "Invalid initial heuristic coloring." << endl;
            return 1;
        }

        cout << "============================================" << endl;
        cout << " Column Generation with Augmented Pricing" << endl;
        cout << " Instance : " << instance_path << endl;
        cout << " |V|      : " << G.num_vertices() << endl;
        cout << " |E|      : " << count_edges(G) << endl;
        cout << " Trials   : " << num_trials << endl;
        cout << " Init cols: " << pool.size() << endl;
        cout << " Init UB  : " << kbar << endl;
        cout << "============================================" << endl;

        // ------------------------------------------------------------
        // Gurobi environment
        // ------------------------------------------------------------
        GRBEnv env(true);
        env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        // ------------------------------------------------------------
        // Restricted Master Problem
        // ------------------------------------------------------------
        RMPSolver rmp(env, G.num_vertices());
        rmp.add_column_pool(pool);

        // ------------------------------------------------------------
        // Algorithm 2: Column generation with augmented pricing
        // ------------------------------------------------------------
        const int max_iter = 100000;
        int cg_iter = 0;

        bool condition = true;

        RMPSolution sol;
        MWSSResult pricing;

        bool converged_by_pricing = false;
        bool closed_gap = false;

        while (true) {
            // line 2: solve RMP
            sol = rmp.solve();

            if (sol.status != GRB_OPTIMAL) {
                cerr << "RMP not optimal at iter " << cg_iter
                     << ": " << gurobi_status_name(sol.status) << endl;
                return 2;
            }

            // line 3: a_p <- solve regular pricing problem
            bool has_negative_reduced_cost_column =
                solve_mwss(env, G, sol.dual_value, pricing);

            // line 14, condition 1:
            // No column with negative reduced cost.
            // Therefore column generation has converged and z_RMP = chi_f.
            if (!has_negative_reduced_cost_column) {
                converged_by_pricing = true;
                break;
            }

            // --------------------------------------------------------
            // Valid lower bound during column generation
            //
            // pricing.reduced_cost = 1 - sum(pi_v x_v) < 0
            //
            // Let c* be the minimum reduced cost.
            // Lübbecke–Desrosiers bound:
            //
            //     kappa = z_RMP / (1 - c*)
            //
            // Then ceil(kappa) is a valid integer lower bound.
            // --------------------------------------------------------
            double kappa = sol.objective / (1.0 - pricing.reduced_cost);
            long long LB = static_cast<long long>(ceil(kappa - 1e-9));

            // --------------------------------------------------------
            // line 5-7: augmented pricing
            // Try to find a kbar-partition containing a_p.
            // --------------------------------------------------------
            vector<StableColumn> augmented_columns;

            if (condition) {
                augmented_columns = solveAugmentedPricing(pricing.col, kbar - 1, G);
            }

            // --------------------------------------------------------
            // line 8-9:
            // If augmented pricing found a kbar-partition, update incumbent.
            // --------------------------------------------------------
            if (!augmented_columns.empty()) {
                incumbent = augmented_columns;
                kbar = static_cast<int>(augmented_columns.size());

                for (const StableColumn& col : augmented_columns) {
                    if (pool.insert(col)) {
                        rmp.add_column(col);
                    }
                }

                cout << "[iter " << setw(4) << cg_iter << "] "
                     << "obj = " << fixed << setprecision(6) << sol.objective
                     << " | rc = " << pricing.reduced_cost
                     << " | LB = " << LB
                     << " | UB = " << kbar
                     << " | AP = success"
                     << " | cols = " << rmp.column_count()
                     << endl;
            }

            // --------------------------------------------------------
            // line 10-12:
            // If augmented pricing failed, add only the regular pricing column.
            // Then condition becomes false.
            // --------------------------------------------------------
            else {
                if (!pool.insert(pricing.col)) {
                    cout << "[iter " << cg_iter
                         << "] duplicate regular pricing column -> stop"
                         << endl;
                    break;
                }

                rmp.add_column(pricing.col);
                condition = false;

                cout << "[iter " << setw(4) << cg_iter << "] "
                     << "obj = " << fixed << setprecision(6) << sol.objective
                     << " | rc = " << pricing.reduced_cost
                     << " | LB = " << LB
                     << " | UB = " << kbar
                     << " | AP = fail"
                     << " | cols = " << rmp.column_count()
                     << endl;
            }

            // line 14, condition 2:
            // If lower bound reaches upper bound, incumbent is optimal.
            if (LB >= kbar) {
                closed_gap = true;
                break;
            }

            ++cg_iter;

            if (cg_iter >= max_iter) {
                cerr << "Reached max CG iterations." << endl;
                break;
            }
        }

        // Final report
        cout << "============================================" << endl;
        cout << "Finished after " << cg_iter << " iterations" << endl;
        cout << "RMP status: " << gurobi_status_name(sol.status) << endl;
        cout << "LP objective chi_f: "
             << fixed << setprecision(6) << sol.objective << endl;

        long long LB_final = static_cast<long long>(ceil(sol.objective - 1e-9));

        cout << "Final lower bound ceil(chi_f): " << LB_final << endl;
        cout << "Final upper bound kbar       : " << kbar << endl;
        cout << "Final columns in RMP         : " << rmp.column_count() << endl;

        int active = 0;
        for (double value : sol.lambda_value) {
            if (value > 1e-6) {
                ++active;
            }
        }

        cout << "Active lambdas: "
             << active << " / " << sol.lambda_value.size() << endl;

        if (closed_gap || LB_final >= kbar) {
            cout << "OPTIMAL: chi(G) = " << kbar << endl;
            cout << "Reason : lower bound reached incumbent upper bound." << endl;
        } else if (converged_by_pricing) {
            cout << "Column generation converged: no negative reduced cost column." << endl;

            if (LB_final >= kbar) {
                cout << "OPTIMAL: chi(G) = " << kbar << endl;
            } else {
                cout << "Gap remains: ["
                     << LB_final << ", " << kbar
                     << "] -> Branch-and-Price is needed."
                     << endl;
            }
        } else {
            cout << "Stopped before full proof of optimality." << endl;
            cout << "Gap: ["
                 << LB_final << ", " << kbar
                 << "]"
                 << endl;
        }

        // Print incumbent coloring
        cout << "============================================" << endl;
        cout << "Incumbent coloring with " << incumbent.size()
             << " stable sets:" << endl;

        for (int i = 0; i < static_cast<int>(incumbent.size()); ++i) {
            cout << "  Z_" << i << " :";
            for (int v : incumbent[i].vertices) {
                cout << ' ' << v;
            }
            cout << endl;
        }

        // Print first active RMP columns
        cout << "============================================" << endl;
        cout << "First active RMP columns:" << endl;

        int printed = 0;
        for (int j = 0;
             j < static_cast<int>(sol.lambda_value.size()) && printed < 10;
             ++j) {
            if (sol.lambda_value[j] <= 1e-6) {
                continue;
            }

            cout << "  lambda_" << j << " = "
                 << fixed << setprecision(6) << sol.lambda_value[j]
                 << " | vertices:";

            for (int v : pool.column(j).vertices) {
                cout << ' ' << v;
            }

            cout << endl;
            ++printed;
        }

    } catch (const GRBException& e) {
        cerr << "Gurobi Error "
             << e.getErrorCode() << ": "
             << e.getMessage() << endl;
        return 3;
    } catch (const exception& e) {
        cerr << "Fatal: " << e.what() << endl;
        return 1;
    }

    return 0;
}