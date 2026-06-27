#include "cg_loop.h"
#include "output_writer.h"
#include "reporting.h"
#include "util.h"

#include "../config/config.h"
#include "../column_generation/pricing.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

using namespace std;

CGLoopResult run_cg_loop(CGLoopContext& ctx) {
    int cg_iter = 0;
    bool converged_by_pricing = false;
    bool reached_max_iter = false;
    RMPSolution sol;
    StableSetPricingResult pricing_result;
    double last_rmp_time_seconds = 0.0;

    auto gap_closed = [&]() {
        return ctx.proven_lb >= ctx.incumbent_ub;
    };

    auto solve_rmp = [&]() -> bool {
        cout << "Run RMP\n";
        auto t0 = chrono::high_resolution_clock::now();
        sol = ctx.rmp.solve();
        last_rmp_time_seconds = chrono::duration<double>(
            chrono::high_resolution_clock::now() - t0).count();
        if (sol.status == GRB_OPTIMAL) return true;
        cerr << "RMP not optimal at iter " << cg_iter
             << ": " << gurobi_status_name(sol.status) << "\n";
        return false;
    };

    auto iter_limit_hit = [&]() -> bool {
        if (cg_iter < config.get<int>("max_iter", 100000)) return false;
        cerr << "Reached max CG iterations.\n";
        reached_max_iter = true;
        return true;
    };

    auto add_column = [&](const string& step) -> bool {
        if (!ctx.pool.insert(pricing_result.column)) {
            cout << "[iter " << cg_iter << "] duplicate " << step << " column -> stop\n";
            return false;
        }
        ctx.rmp.add_column(pricing_result.column);
        print_iteration(cg_iter, sol, pricing_result.reduced_cost,
            ctx.proven_lb, ctx.incumbent_ub, step, ctx.rmp.column_count());
        ++cg_iter;
        return !iter_limit_hit();
    };

    auto make_result = [&](int code) -> CGLoopResult {
        return {code, cg_iter, converged_by_pricing, reached_max_iter, sol};
    };

    if (!solve_rmp())
        return make_result(2);

    while (!gap_closed() && !reached_max_iter) {
        if (ctx.summary.time_limit_seconds > 0.0
            && ctx.elapsed_seconds() >= ctx.summary.time_limit_seconds) {
            cerr << "Reached global time limit.\n";
            ctx.summary.reached_time_limit = true;
            break;
        }

        string pricing_id = ctx.summary.run_id + "-node0-cg" + to_string(cg_iter);
        auto pricing_t0 = chrono::high_resolution_clock::now();

        bool found = false;
        string step;

        // 1. Commencing Decision Pricing
        cout << "Run Decision Pricing\n";
        double decision_threshold = compute_adaptive_decision_threshold(
            sol.objective, ctx.adaptive_lower_bound);

        PricingOrderComparison dual_comp, ai_comp;
        dual_comp.method = "dual_desc";
        ai_comp.method   = "onnx_ai";
        dual_comp.vertex_order = build_dual_desc_pricing_order(ctx.G, sol.dual_value);
        bool dual_found;
        {
            auto t = chrono::high_resolution_clock::now();
            dual_found = solve_decision_pricing_column(
                ctx.G, sol.dual_value, decision_threshold,
                dual_comp.vertex_order, dual_comp.pricing_result,
                config.get<double>("decision_pricing_limit", 5.0),
                &dual_comp.cp_result);
            dual_comp.cp_time_seconds = chrono::duration<double>(
                chrono::high_resolution_clock::now() - t).count();
        }

        cout << "[iter " << cg_iter << "] top5 dual_desc:";
        for (int i = 0; i < 5 && i < (int)dual_comp.vertex_order.size(); ++i)
            cout << " " << dual_comp.vertex_order[i];
        cout << "\n";

        bool ai_found = false;
        if (ctx.onnx) {
            ai_comp.vertex_order = ordering_vertices(
                *ctx.onnx, ctx.G, sol.dual_value, ctx.pool);
            cout << "[iter " << cg_iter << "] top5 onnx_ai  :";
            for (int i = 0; i < 5 && i < (int)ai_comp.vertex_order.size(); ++i)
                cout << " " << ai_comp.vertex_order[i];
            cout << "\n";
            auto t = chrono::high_resolution_clock::now();
            ai_found = solve_decision_pricing_column(
                ctx.G, sol.dual_value, decision_threshold,
                ai_comp.vertex_order, ai_comp.pricing_result,
                config.get<double>("decision_pricing_limit", 5.0),
                &ai_comp.cp_result);
            ai_comp.cp_time_seconds = chrono::duration<double>(
                chrono::high_resolution_clock::now() - t).count();
        }

        if (dual_found) {
            pricing_result = dual_comp.pricing_result;
            step = "CP_dual_desc"; found = true;
        } else if (ai_found) {
            pricing_result = ai_comp.pricing_result;
            step = "CP_ai"; found = true;
        }
        // [end decision pricing]

        // MWSS fallback
        bool mwss_ran = !found;
        double mwss_time_seconds = 0.0;
        StableSetPricingResult mwss_result;
        if (mwss_ran) {
            cout << "CP pricing found no column, run MWSS\n";
            auto t = chrono::high_resolution_clock::now();
            bool mwss_found = solve_maximum_weight_stable_set_pricing(
                ctx.env, ctx.G, sol.dual_value, mwss_result,
                config.get<double>("exact_pricing_limit", 40.0));
            mwss_time_seconds = chrono::duration<double>(
                chrono::high_resolution_clock::now() - t).count();
            if (mwss_found) {
                pricing_result = mwss_result;
                step = "MWSS";
                found = true;
                cout << "MWSS found column | rc = "
                     << fixed << setprecision(6) << mwss_result.reduced_cost << "\n";
                log_vertex_features(ctx.logger, cg_iter, ctx.G,
                    sol.dual_value, ctx.pool, mwss_result.column);
            } else {
                cout << "MWSS found no column";
                if (mwss_result.stopped) cout << " (time limit)";
                cout << "\n";
            }
        }

        // Update proven lower bound — only MWSS can prove optimality
        if (mwss_ran && mwss_result.proven_optimal) {
            if (!found) {
                ctx.proven_lb = max(ctx.proven_lb, ceil_bound(sol.objective));
                ctx.adaptive_lower_bound = max(ctx.adaptive_lower_bound, sol.objective);
                converged_by_pricing = true;
            } else {
                double exact_bound = sol.objective / (1.0 - mwss_result.reduced_cost);
                ctx.adaptive_lower_bound = max(ctx.adaptive_lower_bound, exact_bound);
                ctx.proven_lb = max(ctx.proven_lb, ceil_bound(exact_bound));
            }
        }

        double pricing_time = chrono::duration<double>(
            chrono::high_resolution_clock::now() - pricing_t0).count();
        double last_rc = found ? pricing_result.reduced_cost
                               : (mwss_ran ? mwss_result.reduced_cost : 0.0);

        log_pricing_order_comparison(ctx.logger, cg_iter, sol.objective,
            decision_threshold, dual_comp, ai_comp,
            mwss_ran, mwss_result, mwss_time_seconds);

        // log_pricing_iteration(ctx.logger, ctx.summary.run_id, cg_iter, pricing_id,
        //     sol, ctx.rmp.column_count(), last_rc,
        //     last_rmp_time_seconds, pricing_time);

        if (gap_closed()) { cout << "Gap closed after pricing bounds update.\n"; break; }
        if (!found)        break;
        if (!add_column(step)) break;
        if (!solve_rmp())  return make_result(2);
    }

    return make_result(0);
}
