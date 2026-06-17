#include "output_writer.h"

#include "util.h"

#include <filesystem>
#include <map>
#include <numeric>
#include <sstream>

using namespace std;
namespace fs = std::filesystem;

bool JsonlLogger::open(
    const MasterRunConfig& config,
    MasterRunSummary& summary
) {
    if (config.log_dir.empty()) {
        return false;
    }

    fs::path run_dir = fs::path(config.log_dir) / summary.run_id;
    fs::create_directories(run_dir);

    summary.run_output_dir = run_dir.string();
    summary.run_log_path = (run_dir / "records.jsonl").string();
    summary.summary_csv_path = (run_dir / "summary.csv").string();
    summary.summary_json_path = (run_dir / "summary.json").string();

    path = summary.run_log_path;
    vertex_path = (run_dir / "vertex_features.jsonl").string();
    stream.open(path, ios::out | ios::trunc);
    vertex_stream.open(vertex_path, ios::out | ios::trunc);
    return static_cast<bool>(stream) && static_cast<bool>(vertex_stream);
}

void JsonlLogger::write(const string& record) {
    if (stream) {
        stream << record << "\n";
    }
}

void JsonlLogger::write_vertex(const string& record) {
    if (vertex_stream) {
        vertex_stream << record << "\n";
    }
}

namespace {

pair<double, double> mean_and_variance(const vector<double>& values) {
    if (values.empty()) {
        return {0.0, 0.0};
    }

    double mean = accumulate(values.begin(), values.end(), 0.0)
        / static_cast<double>(values.size());
    double variance = 0.0;
    for (double value : values) {
        double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    return {mean, variance};
}

vector<int> column_frequencies(const ColumnPool& pool, int n) {
    vector<int> frequency(n, 0);
    for (const StableColumn& column : pool.columns) {
        for (int v : column.vertices) {
            if (v >= 0 && v < n) {
                ++frequency[v];
            }
        }
    }
    return frequency;
}

double weighted_neighbor_dual(
    const Graph& G,
    const vector<double>& dual_value,
    int vertex
) {
    double total = 0.0;
    for (int neighbor : G.neighbors(vertex)) {
        if (neighbor >= 0 && neighbor < static_cast<int>(dual_value.size())) {
            total += dual_value[neighbor];
        }
    }
    return total;
}

vector<string> summary_columns() {
    return {
        "run_id",
        "run_output_dir",
        "run_log_path",
        "instance_path",
        "instance",
        "n",
        "m",
        "num_trials",
        "seed",
        "threads",
        "time_limit_seconds",
        "run_time_seconds",
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
        "reached_time_limit",
        "exit_code"
    };
}

map<string, string> summary_values(const MasterRunSummary& summary) {
    return {
        {"run_id", summary.run_id},
        {"run_output_dir", summary.run_output_dir},
        {"run_log_path", summary.run_log_path},
        {"instance_path", summary.instance_path},
        {"instance", summary.instance},
        {"n", to_string(summary.n)},
        {"m", to_string(summary.m)},
        {"num_trials", to_string(summary.num_trials)},
        {"seed", to_string(summary.seed)},
        {"threads", to_string(summary.threads)},
        {"time_limit_seconds", format_double(summary.time_limit_seconds)},
        {"run_time_seconds", format_double(summary.run_time_seconds)},
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
        {"reached_time_limit", summary.reached_time_limit ? "1" : "0"},
        {"exit_code", to_string(summary.exit_code)}
    };
}

void write_summary_csv(const MasterRunSummary& summary) {
    if (summary.summary_csv_path.empty()) {
        return;
    }

    ofstream output(summary.summary_csv_path, ios::out | ios::trunc);
    if (!output) {
        return;
    }

    vector<string> columns = summary_columns();
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        output << csv_escape(columns[i]);
    }
    output << "\n";

    map<string, string> values = summary_values(summary);
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        auto it = values.find(columns[i]);
        output << csv_escape(it == values.end() ? "" : it->second);
    }
    output << "\n";
}

void write_summary_json(
    const MasterRunConfig& config,
    const MasterRunSummary& summary
) {
    if (summary.summary_json_path.empty()) {
        return;
    }

    ofstream output(summary.summary_json_path, ios::out | ios::trunc);
    if (!output) {
        return;
    }

    output << "{\n"
           << "  \"record_type\": \"run_summary\",\n"
           << "  \"schema_version\": " << config.schema_version << ",\n"
           << "  \"run_id\": " << json_string(summary.run_id) << ",\n"
           << "  \"run_output_dir\": " << json_string(summary.run_output_dir) << ",\n"
           << "  \"records_path\": " << json_string(summary.run_log_path) << ",\n"
           << "  \"instance\": " << json_string(summary.instance) << ",\n"
           << "  \"instance_path\": " << json_string(summary.instance_path) << ",\n"
           << "  \"n\": " << summary.n << ",\n"
           << "  \"m\": " << summary.m << ",\n"
           << "  \"seed\": " << summary.seed << ",\n"
           << "  \"iterations\": " << summary.iterations << ",\n"
           << "  \"rmp_status\": " << json_string(summary.rmp_status) << ",\n"
           << "  \"lp_objective\": " << json_number(summary.lp_objective) << ",\n"
           << "  \"proven_lb\": " << summary.proven_lb << ",\n"
           << "  \"incumbent_ub\": " << summary.incumbent_ub << ",\n"
           << "  \"column_count\": " << summary.column_count << ",\n"
           << "  \"closed_gap\": " << json_bool(summary.closed_gap) << ",\n"
           << "  \"converged_by_pricing\": "
           << json_bool(summary.converged_by_pricing) << ",\n"
           << "  \"reached_max_iter\": "
           << json_bool(summary.reached_max_iter) << ",\n"
           << "  \"reached_time_limit\": "
           << json_bool(summary.reached_time_limit) << ",\n"
           << "  \"run_time_seconds\": "
           << json_number(summary.run_time_seconds) << ",\n"
           << "  \"exit_code\": " << summary.exit_code << "\n"
           << "}\n";
}

} // namespace

void log_run_start(
    JsonlLogger& logger,
    const MasterRunConfig& config,
    const MasterRunSummary& summary,
    int initial_columns
) {
    ostringstream out;
    out << "{"
        << "\"instance\":" << json_string(summary.instance) << ","
        << "\"instance_path\":" << json_string(summary.instance_path) << ","
        << "\"seed\":" << summary.seed << ","
        << "\"git_commit\":" << json_string(current_git_commit_short()) << ","
        << "\"compiler\":" << json_string(compiler_name()) << ","
        << "\"gecode_version\":\"6.x\","
        << "\"threads\":" << config.threads << ","
        << "\"time_limit\":" << json_number(config.time_limit_seconds) << ","
        << "\"decision_pricing_limit\":"
        << json_number(config.decision_pricing_limit_seconds) << ","
        << "\"exact_pricing_limit\":"
        << json_number(config.mwss_time_limit_seconds) << ","
        << "\"augmented_pricing_limit\":"
        << json_number(config.augmented_time_limit_seconds) << ","
        << "\"requested_initial_columns\":"
        << config.initial_columns_target << ","
        << "\"initial_columns\":" << initial_columns << ","
        << "\"vertex_ordering\":" << json_string(config.vertex_ordering) << ","
        << "\"enable_ml\":" << json_bool(config.enable_ml)
        << "}";
    logger.write(out.str());
}

void log_pricing_iteration(
    JsonlLogger& logger,
    const MasterRunConfig& config,
    const string& run_id,
    int cg_iter,
    const string& pricing_id,
    const RMPSolution& sol,
    int column_count,
    double last_reduced_cost,
    double rmp_time,
    double pricing_time
) {
    auto [dual_mean, dual_variance] = mean_and_variance(sol.dual_value);

    ostringstream out;
    out << "{"
        << "\"record_type\":\"pricing_iteration\","
        << "\"schema_version\":" << config.schema_version << ","
        << "\"run_id\":" << json_string(run_id) << ","
        << "\"bp_node_id\":0,"
        << "\"cg_iteration\":" << cg_iter << ","
        << "\"pricing_id\":" << json_string(pricing_id) << ","
        << "\"rmp_value\":" << json_number(sol.objective) << ","
        << "\"number_of_columns\":" << column_count << ","
        << "\"last_reduced_cost\":" << json_number(last_reduced_cost) << ","
        << "\"dual_mean\":" << json_number(dual_mean) << ","
        << "\"dual_variance\":" << json_number(dual_variance) << ","
        << "\"rmp_time\":" << json_number(rmp_time) << ","
        << "\"pricing_time\":" << json_number(pricing_time)
        << "}";
    logger.write(out.str());
}

void log_vertex_features(
    JsonlLogger& logger,
    const MasterRunConfig& config,
    int cg_iter,
    const Graph& G,
    const vector<double>& dual_value,
    const ColumnPool& pool
) {
    if (!config.log_vertex_features) {
        return;
    }

    int n = G.num_vertices();
    vector<int> frequencies = column_frequencies(pool, n);

    for (int v = 0; v < n; ++v) {
        int degree = G.degree(v);
        double normalized_degree = n > 1
            ? static_cast<double>(degree) / static_cast<double>(n - 1)
            : 0.0;
        double dual = v < static_cast<int>(dual_value.size())
            ? dual_value[v]
            : 0.0;

        ostringstream out;
        out << "{"
            << "\"iteration\":" << cg_iter << ","
            << "\"vertex_id\":" << v << ","
            << "\"degree\":" << degree << ","
            << "\"normalized_degree\":"
            << json_number(normalized_degree) << ","
            << "\"dual\":" << json_number(dual) << ","
            << "\"weighted_neighbor_dual\":"
            << json_number(weighted_neighbor_dual(G, dual_value, v)) << ","
            << "\"complement_degree\":" << (n - 1 - degree) << ","
            << "\"stable_set_occurrences\":" << frequencies[v]
            << "}";
        logger.write_vertex(out.str());
    }
}

void write_run_summary(
    const MasterRunConfig& config,
    const MasterRunSummary& summary
) {
    write_summary_csv(summary);
    write_summary_json(config, summary);
    if (!summary.run_output_dir.empty()) {
        cout << "Run output -> " << summary.run_output_dir << endl;
    }
}
