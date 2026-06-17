#include "reporting.h"

#include "util.h"

#include <iomanip>
#include <iostream>

using namespace std;

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
    cout << " Threads  : " << config.threads << endl;
    cout << " Run TL   : " << config.time_limit_seconds << " s" << endl;
    cout << " DP TL    : " << config.decision_pricing_limit_seconds << " s" << endl;
    cout << " MWSS TL  : " << config.mwss_time_limit_seconds << " s" << endl;
    cout << " AP TL    : " << config.augmented_time_limit_seconds << " s" << endl;
    cout << " Init cols: " << pool.size() << endl;
    cout << " Init LB  : " << proven_lb << endl;
    cout << " Init UB  : " << incumbent_ub << endl;
    cout << "============================================" << endl;
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
