#include "driver.h"

#include "pricing.h"
#include "rmp.h"
#include "stable_set.h"
#include "../augmented_pricing/augmented_pricing.h"
#include "../graph/graph.h"
#include "../preprocessing/clique_processing.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace {

struct MasterRunSummary {
    string instance_path;
    string instance;
    int n = 0;
    long long m = 0;
    int num_trials = 0;
    size_t seed = 0;
    int iterations = 0;
    string rmp_status = "NOT_RUN";
    double lp_objective = 0.0;
    int proven_lb = 0;
    int incumbent_ub = 0;
    int column_count = 0;
    int active_lambdas = 0;
    int total_lambdas = 0;
    bool closed_gap = false;
    bool converged_by_pricing = false;
    bool reached_max_iter = false;
    int exit_code = 0;
};

string trim(const string& value) {
    size_t first = 0;
    while (first < value.size()
        && isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    size_t last = value.size();
    while (last > first
        && isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

map<string, string> read_key_value_file(const fs::path& path) {
    map<string, string> values;
    ifstream input(path);
    if (!input) {
        return values;
    }

    string line;
    while (getline(input, line)) {
        size_t comment = line.find('#');
        if (comment != string::npos) {
            line = line.substr(0, comment);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        size_t separator = line.find('=');
        if (separator == string::npos) {
            continue;
        }

        string key = trim(line.substr(0, separator));
        string value = trim(line.substr(separator + 1));
        if (!key.empty()) {
            values[key] = value;
        }
    }

    return values;
}

vector<string> split_csv_list(const string& value) {
    vector<string> items;
    string item;
    stringstream ss(value);
    while (getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

bool parse_bool(const string& value) {
    string lowered;
    lowered.reserve(value.size());
    for (char ch : value) {
        lowered.push_back(static_cast<char>(
            tolower(static_cast<unsigned char>(ch))
        ));
    }
    return lowered == "1" || lowered == "true"
        || lowered == "yes" || lowered == "on";
}

fs::path config_root_from_file(const fs::path& path) {
    fs::path parent = path.parent_path();
    if (parent.filename() == "master_cp") {
        return parent.parent_path();
    }
    return parent;
}

string resolve_config_path(const fs::path& root, const string& value) {
    fs::path path(value);
    if (path.is_relative()) {
        path = root / path;
    }
    return path.lexically_normal().string();
}

vector<fs::path> parent_chain(fs::path start) {
    vector<fs::path> paths;
    if (start.empty()) {
        return paths;
    }

    start = fs::absolute(start).lexically_normal();
    for (int depth = 0; depth < 6 && !start.empty(); ++depth) {
        paths.push_back(start);
        fs::path parent = start.parent_path();
        if (parent == start) {
            break;
        }
        start = parent;
    }
    return paths;
}

fs::path find_default_file(const string& argv0, const fs::path& relative_path) {
    vector<fs::path> bases = parent_chain(fs::current_path());

    if (!argv0.empty()) {
        fs::path exe_path(argv0);
        if (exe_path.has_parent_path()) {
            vector<fs::path> exe_bases = parent_chain(exe_path.parent_path());
            bases.insert(bases.end(), exe_bases.begin(), exe_bases.end());
        }
    }

    for (const fs::path& base : bases) {
        fs::path candidate = (base / relative_path).lexically_normal();
        if (fs::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

void apply_run_config_file(
    MasterRunConfig& config,
    const fs::path& path
) {
    map<string, string> values = read_key_value_file(path);
    if (values.empty()) {
        return;
    }

    fs::path root = config_root_from_file(path);

    if (values.count("instance_path")) {
        config.instance_path =
            resolve_config_path(root, values["instance_path"]);
    }
    if (values.count("num_trials")) {
        config.num_trials = stoi(values["num_trials"]);
    }
    if (values.count("seed")) {
        config.seed = static_cast<size_t>(stoull(values["seed"]));
    }
    if (values.count("max_iter")) {
        config.max_iter = stoi(values["max_iter"]);
    }
    if (values.count("mwss_time_limit_seconds")) {
        config.mwss_time_limit_seconds =
            stod(values["mwss_time_limit_seconds"]);
    }
    if (values.count("augmented_time_limit_seconds")) {
        config.augmented_time_limit_seconds =
            stod(values["augmented_time_limit_seconds"]);
    }
}

void apply_output_config_file(
    MasterRunConfig& config,
    const fs::path& path
) {
    map<string, string> values = read_key_value_file(path);
    if (values.empty()) {
        return;
    }

    fs::path root = config_root_from_file(path);

    if (values.count("result_path")) {
        config.output_path = resolve_config_path(root, values["result_path"]);
    } else if (values.count("output_path")) {
        config.output_path = resolve_config_path(root, values["output_path"]);
    }

    if (values.count("append")) {
        config.append_output = parse_bool(values["append"]);
    }
    if (values.count("columns")) {
        config.output_columns = split_csv_list(values["columns"]);
    }
}

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

string format_double(double value) {
    ostringstream out;
    out << fixed << setprecision(6) << value;
    return out.str();
}

string csv_escape(const string& value) {
    bool needs_quotes = value.find_first_of(",\"\n\r") != string::npos;
    if (!needs_quotes) {
        return value;
    }

    string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped += ch;
        }
    }
    escaped += "\"";
    return escaped;
}

vector<string> default_output_columns() {
    return {
        "instance_path",
        "instance",
        "n",
        "m",
        "num_trials",
        "seed",
        "iterations",
        "rmp_status",
        "lp_objective",
        "proven_lb",
        "incumbent_ub",
        "column_count",
        "active_lambdas",
        "total_lambdas",
        "closed_gap",
        "converged_by_pricing",
        "reached_max_iter",
        "exit_code"
    };
}

map<string, string> summary_values(const MasterRunSummary& summary) {
    return {
        {"instance_path", summary.instance_path},
        {"instance", summary.instance},
        {"n", to_string(summary.n)},
        {"m", to_string(summary.m)},
        {"num_trials", to_string(summary.num_trials)},
        {"seed", to_string(summary.seed)},
        {"iterations", to_string(summary.iterations)},
        {"rmp_status", summary.rmp_status},
        {"lp_objective", format_double(summary.lp_objective)},
        {"proven_lb", to_string(summary.proven_lb)},
        {"incumbent_ub", to_string(summary.incumbent_ub)},
        {"column_count", to_string(summary.column_count)},
        {"active_lambdas", to_string(summary.active_lambdas)},
        {"total_lambdas", to_string(summary.total_lambdas)},
        {"closed_gap", summary.closed_gap ? "1" : "0"},
        {"converged_by_pricing", summary.converged_by_pricing ? "1" : "0"},
        {"reached_max_iter", summary.reached_max_iter ? "1" : "0"},
        {"exit_code", to_string(summary.exit_code)}
    };
}

void write_master_output(
    const MasterRunConfig& config,
    const MasterRunSummary& summary
) {
    if (config.output_path.empty()) {
        return;
    }

    vector<string> columns = config.output_columns.empty()
        ? default_output_columns()
        : config.output_columns;

    fs::path output_path(config.output_path);
    if (output_path.has_parent_path()) {
        fs::create_directories(output_path.parent_path());
    }

    bool write_header = !config.append_output
        || !fs::exists(output_path)
        || fs::file_size(output_path) == 0;

    ios::openmode mode = ios::out;
    if (config.append_output) {
        mode |= ios::app;
    } else {
        mode |= ios::trunc;
    }

    ofstream output(output_path, mode);
    if (!output) {
        cerr << "Cannot open output file: " << config.output_path << endl;
        return;
    }

    if (write_header) {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) {
                output << ",";
            }
            output << csv_escape(columns[i]);
        }
        output << "\n";
    }

    map<string, string> values = summary_values(summary);
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        auto it = values.find(columns[i]);
        output << csv_escape(it == values.end() ? "" : it->second);
    }
    output << "\n";

    cout << "Summary exported -> " << config.output_path << endl;
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

    string argv0 = argc >= 1 ? argv[0] : "";
    fs::path default_config = find_default_file(
        argv0,
        fs::path("master_cp") / "config.txt"
    );
    if (!default_config.empty()) {
        apply_run_config_file(config, default_config);
    }

    fs::path default_output = find_default_file(
        argv0,
        fs::path("master_cp") / "output.txt"
    );
    if (!default_output.empty()) {
        apply_output_config_file(config, default_output);
    }

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
    if (argc >= 7) {
        config.output_path = argv[6];
    }

    return config;
}

int run_column_generation(const MasterRunConfig& config) {
    MasterRunSummary summary;
    summary.instance_path = config.instance_path;
    summary.instance = fs::path(config.instance_path).filename().string();
    summary.num_trials = config.num_trials;
    summary.seed = config.seed;

    if (!fs::exists(config.instance_path)) {
        cerr << "Instance not found: " << config.instance_path << endl;
        summary.rmp_status = "INSTANCE_NOT_FOUND";
        summary.exit_code = 1;
        write_master_output(config, summary);
        return 1;
    }

    // 1. Initialize Problem (Graph)
    Graph G = parser_dimacs_col(config.instance_path, true);
    summary.n = G.num_vertices();
    summary.m = count_edges(G);

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
        summary.rmp_status = gurobi_status_name(sol.status);
        summary.exit_code = 2;
        write_master_output(config, summary);
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
            summary.iterations = cg_iter;
            summary.rmp_status = gurobi_status_name(sol.status);
            summary.proven_lb = proven_lb;
            summary.incumbent_ub = incumbent_ub;
            summary.column_count = rmp.column_count();
            summary.exit_code = 2;
            write_master_output(config, summary);
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
    summary.exit_code = 0;
    write_master_output(config, summary);

    return 0;
}
