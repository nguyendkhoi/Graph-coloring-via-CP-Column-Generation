#include "output_writer.h"

#include "../config/config.h"
#include "util.h"

#include <filesystem>
#include <map>
#include <numeric>
#include <sstream>

using namespace std;
namespace fs = std::filesystem;

namespace {

fs::path master_config_path() {
    return fs::path("master_cp") / "solver_config.json";
}

fs::path master_config_root() {
    return config_root_from_file(master_config_path());
}

string configured_log_dir() {
    return resolve_config_path(
        master_config_root(),
        config.get<string>("log_dir", "master_cp/runs")
    );
}

// Output folder naming
string safe_instance_folder_name(const MasterRunSummary& summary) {
    string name = fs::path(summary.instance).stem().string();
    if (name.empty()) {
        name = "unknown_instance";
    }

    for (char& ch : name) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 32 || ch == '<' || ch == '>' || ch == ':'
            || ch == '"' || ch == '/' || ch == '\\' || ch == '|'
            || ch == '?' || ch == '*') {
            ch = '_';
        }
    }

    return name.empty() ? "unknown_instance" : name;
}

} // namespace

// Run folder setup and raw stream helpers
bool JsonlLogger::open(MasterRunSummary& summary) {
    string log_dir = configured_log_dir();
    if (log_dir.empty()) {
        return false;
    }

    fs::path run_dir =
        fs::path(log_dir) / safe_instance_folder_name(summary) / summary.run_id;
    fs::create_directories(run_dir);

    summary.run_output_dir = run_dir.string();
    summary.run_log_path = (run_dir / "records.csv").string();
    summary.summary_csv_path = (run_dir / "summary.csv").string();
    summary.summary_json_path = (run_dir / "summary.json").string();

    path = summary.run_log_path;
    vertex_path = (run_dir / "vertex_features.csv").string();
    stream.open(path, ios::out | ios::trunc);
    vertex_stream.open(vertex_path, ios::out | ios::trunc);

    if (stream) {
        stream << "cg_iteration,rmp_value,number_of_columns,"
               << "last_reduced_cost,dual_mean,dual_variance,"
               << "rmp_time,pricing_time\n";
    }
    if (vertex_stream) {
        vertex_stream << "iteration,vertex_id,degree,normalized_degree,"
                      << "dual,weighted_neighbor_dual,complement_degree,"
                      << "stable_set_occurrences,is_selected\n";
    }

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

// Shared feature helpers
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

// Run summary output
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
           << "  \"schema_version\": "
           << config.get<int>("schema_version", 1) << ",\n"
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

// Records and feature output
void log_run_start(
    JsonlLogger& logger,
    const MasterRunSummary& summary,
    int initial_columns
) {
    (void)logger;
    (void)summary;
    (void)initial_columns;
}

void log_pricing_iteration(
    JsonlLogger& logger,
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

    (void)run_id;
    (void)pricing_id;

    ostringstream out;
    out << cg_iter << ","
        << json_number(sol.objective) << ","
        << column_count << ","
        << json_number(last_reduced_cost) << ","
        << json_number(dual_mean) << ","
        << json_number(dual_variance) << ","
        << json_number(rmp_time) << ","
        << json_number(pricing_time);
    logger.write(out.str());
}

void log_vertex_features(
    JsonlLogger& logger,
    int cg_iter,
    const Graph& G,
    const vector<double>& dual_value,
    const ColumnPool& pool,
    const StableColumn& selected_column
) {
    if (!config.get<bool>("log_vertex_features", true)) {
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
        out << cg_iter << ","
            << v << ","
            << degree << ","
            << json_number(normalized_degree) << ","
            << json_number(dual) << ","
            << json_number(weighted_neighbor_dual(G, dual_value, v)) << ","
            << (n - 1 - degree) << ","
            << frequencies[v] << ","
            << (selected_column.contains_vertex(v) ? 1 : 0);
        logger.write_vertex(out.str());
    }
}

void write_run_summary(const MasterRunSummary& summary) {
    write_summary_csv(summary);
    write_summary_json(summary);
    if (!summary.run_output_dir.empty()) {
        cout << "Run output -> " << summary.run_output_dir << endl;
    }
}
