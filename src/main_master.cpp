#include "graph/graph.h"
#include "master/master.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

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
        case GRB_OPTIMAL: return "OPTIMAL";
        case GRB_INFEASIBLE: return "INFEASIBLE";
        case GRB_INF_OR_UNBD: return "INF_OR_UNBD";
        case GRB_UNBOUNDED: return "UNBOUNDED";
        case GRB_TIME_LIMIT: return "TIME_LIMIT";
        default: return "STATUS_" + to_string(status);
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

        ColumnPool pool;
        pool.initialize(G, num_trials, seed);

        cout << "============================================" << endl;
        cout << " Column Generation" << endl;
        cout << " Instance : " << instance_path << endl;
        cout << " |V|      : " << G.num_vertices() << endl;
        cout << " |E|      : " << count_edges(G) << endl;
        cout << " Trials   : " << num_trials << endl;
        cout << " Init cols: " << pool.size() << endl;
        cout << "============================================" << endl;

        GRBEnv env(true);
        env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        RMPSolver rmp(env, G.num_vertices());
        rmp.add_column_pool(pool);

        // ---------- Column generation loop ----------
        const int max_iter = 100000;
        int cg_iter = 0;

        RMPSolution sol;
        MWSSResult pricing;

        while (true) {
            sol = rmp.solve();
            if (sol.status != GRB_OPTIMAL) {
                cerr << "RMP not optimal at iter " << cg_iter
                     << ": " << gurobi_status_name(sol.status) << endl;
                return 2;
            }

            if (!solve_mwss(env, G, sol.dual_value, pricing)) {
                break;
            }

            if (!pool.insert(pricing.col)) {
                cout << "[iter " << cg_iter
                     << "] duplicate column generated -> stop" << endl;
                break;
            }

            rmp.add_column(pricing.col);

            cout << "[iter " << setw(4) << cg_iter << "] "
                 << "obj = " << fixed << setprecision(6) << sol.objective
                 << " | reduced_cost = " << pricing.reduced_cost
                 << " | cols = " << rmp.column_count() << endl;

            if (++cg_iter >= max_iter) {
                cerr << "Reached max CG iterations" << endl;
                break;
            }
        }
        // --------------------------------------------

        cout << "============================================" << endl;
        cout << "Converged after " << cg_iter << " iterations" << endl;
        cout << "RMP status: " << gurobi_status_name(sol.status) << endl;
        cout << "LP objective : " << fixed << setprecision(6) << sol.objective << endl;
        cout << "Lower bound  : " << (long long)ceil(sol.objective - 1e-9) << endl;

        int active = 0;
        for (double value : sol.lambda_value) {
            if (value > 1e-6) ++active;
        }
        cout << "Active lambdas: " << active << " / " << sol.lambda_value.size() << endl;

        cout << "First active columns:" << endl;
        int printed = 0;
        for (int j = 0; j < static_cast<int>(sol.lambda_value.size()) && printed < 10; ++j) {
            if (sol.lambda_value[j] <= 1e-6) continue;
            cout << "  lambda_" << j << " = " << fixed << setprecision(6)
                 << sol.lambda_value[j] << " | vertices:";
            for (int v : pool.column(j).vertices) {
                cout << ' ' << v;
            }
            cout << endl;
            ++printed;
        }
    } catch (const GRBException& e) {
        cerr << "Gurobi Error " << e.getErrorCode() << ": " << e.getMessage() << endl;
        return 3;
    } catch (const exception& e) {
        cerr << "Fatal: " << e.what() << endl;
        return 1;
    }

    return 0;
}
