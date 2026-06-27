#include "output_writer.h"
#include "vertex_features.h"

#include "../config/config.h"
#include "util.h"

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <sstream>
#include <stdexcept>

using namespace std;
namespace fs = std::filesystem;

namespace {

string configured_log_dir() {
    ConfigLocation location = master_config_location();
    return resolve_config_path(
        location.root,
        config.get<string>("log_dir", "master_cp/runs")
    );
}

string safe_instance_folder_name(const MasterRunSummary& summary) {
    string name = fs::path(summary.instance).stem().string();
    if (name.empty()) name = "unknown_instance";
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

pair<double, double> mean_and_variance(const vector<double>& values) {
    if (values.empty()) return {0.0, 0.0};
    double mean = accumulate(values.begin(), values.end(), 0.0)
        / static_cast<double>(values.size());
    double variance = 0.0;
    for (double v : values) {
        double d = v - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(values.size());
    return {mean, variance};
}

} // namespace

bool JsonlLogger::open(MasterRunSummary& summary) {
    string log_dir = configured_log_dir();
    if (log_dir.empty()) return false;

    fs::path run_dir =
        fs::path(log_dir) / safe_instance_folder_name(summary) / summary.run_id;
    fs::create_directories(run_dir);

    summary.run_output_dir = run_dir.string();
    summary.run_log_path = (run_dir / "records.csv").string();
    summary.summary_csv_path = (run_dir / "summary.csv").string();
    summary.summary_json_path = (run_dir / "summary.json").string();

    ConfigLocation config_loc = master_config_location();
    fs::path comparing_dir =
        fs::path(resolve_config_path(config_loc.root, "master_cp/comparing"))
        / safe_instance_folder_name(summary) / summary.run_id;
    fs::create_directories(comparing_dir);

    fs::path data_dir =
        fs::path(resolve_config_path(config_loc.root, "data"))
        / safe_instance_folder_name(summary) / summary.run_id;
    fs::create_directories(data_dir);

    path = summary.run_log_path;
    vertex_path = (data_dir / "vertex_features.csv").string();
    pricing_comparison_path = (comparing_dir / "pricing_comparison.csv").string();

    stream.open(path, ios::out | ios::trunc);
    vertex_stream.open(vertex_path, ios::out | ios::trunc);
    pricing_comparison_stream.open(pricing_comparison_path, ios::out | ios::trunc);

    if (stream) {
        stream << "cg_iteration,rmp_value,number_of_columns,"
               << "last_reduced_cost,dual_mean,dual_variance,"
               << "rmp_time,pricing_time\n";
    }
    if (vertex_stream) {
        vertex_stream
            << "iteration,vertex_id,degree_normalized,normalized_degree,"
            << "dual_normalized,weighted_neighbor_dual_normalized,"
            << "complement_degree_normalized,"
            << "complement_dual_normalized,occurrence_rate,score_contribution\n";
    }
    if (pricing_comparison_stream) {
        pricing_comparison_stream
            << "cg_iteration,type,rmp_value,decision_threshold,"
            << "found,rc,cp_time_s,nodes,failures,stopped,"
            << "mwss_ran,mwss_found,mwss_rc,mwss_proven_optimal,mwss_time_s\n";
    }

    return static_cast<bool>(stream)
        && static_cast<bool>(vertex_stream)
        && static_cast<bool>(pricing_comparison_stream);
}

MasterRunOutput open_master_run_output(const string& instance_path) {
    MasterRunOutput output;
    output.summary.run_id = generate_uuid();
    output.summary.instance_path = instance_path;
    output.summary.instance = fs::path(instance_path).filename().string();
    output.summary.num_trials = config.get<int>("num_trials", 20);
    output.summary.seed = config.get<size_t>("seed", 40);
    output.summary.threads = config.get<int>("threads", 1);
    output.summary.time_limit_seconds =
        config.get<double>("time_limit_seconds", 3600.0);

    if (!output.logger.open(output.summary)) {
        throw runtime_error(
            "Cannot open run output files for instance: " + instance_path
        );
    }
    return output;
}

void JsonlLogger::write(const string& record) {
    if (stream) { stream << record << "\n"; stream.flush(); }
}

void JsonlLogger::write_vertex(const string& record) {
    if (vertex_stream) { vertex_stream << record << "\n"; vertex_stream.flush(); }
}

void JsonlLogger::write_pricing_comparison(const string& record) {
    if (pricing_comparison_stream) {
        pricing_comparison_stream << record << "\n";
        pricing_comparison_stream.flush();
    }
}

void log_run_start(
    JsonlLogger& logger,
    const MasterRunSummary& summary,
    int initial_columns
) {
    (void)logger; (void)summary; (void)initial_columns;
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
    (void)run_id; (void)pricing_id;

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
    if (!config.get<bool>("log_vertex_features", true)) return;

    int n = G.num_vertices();
    if (n == 0) return;

    vector<bool> in_selected(n, false);
    for (int v : selected_column.vertices) {
        if (v >= 0 && v < n) in_selected[v] = true;
    }

    vector<int> frequencies = vf::column_frequencies(pool, n);
    double pool_size_plus_one = static_cast<double>(pool.columns.size()) + 1.0;

    double total_dual_sum = 0.0;
    for (int u = 0; u < n; ++u) {
        if (u < static_cast<int>(dual_value.size()))
            total_dual_sum += dual_value[u];
    }

    vector<double> degrees(n), normalized_degrees(n), duals(n),
                   neighbor_duals(n), complement_degrees(n), complement_duals(n);

    for (int v = 0; v < n; ++v) {
        double deg = static_cast<double>(G.degree(v));
        degrees[v]           = deg;
        normalized_degrees[v]= n > 1 ? deg / static_cast<double>(n - 1) : 0.0;
        duals[v]             = v < static_cast<int>(dual_value.size()) ? dual_value[v] : 0.0;
        neighbor_duals[v]    = vf::weighted_neighbor_dual(G, dual_value, v);
        complement_degrees[v]= static_cast<double>(n - 1) - deg;
        complement_duals[v]  = total_dual_sum - duals[v] - neighbor_duals[v];
    }

    auto nd_mm  = vf::minmax_normalize(degrees);
    auto nd_d   = vf::minmax_normalize(duals);
    auto nd_nb  = vf::minmax_normalize(neighbor_duals);
    auto nd_cd  = vf::minmax_normalize(complement_degrees);
    auto nd_cdu = vf::minmax_normalize(complement_duals);

    double col_weight = 0.0;
    for (int u : selected_column.vertices)
        if (u >= 0 && u < n) col_weight += duals[u];

    double max_dual = duals.empty() ? 0.0
                    : *max_element(duals.begin(), duals.end());

    for (int v = 0; v < n; ++v) {
        double occurrence_rate =
            static_cast<double>(frequencies[v]) / pool_size_plus_one;
        double score_contribution;
        if (in_selected[v]) {
            score_contribution = (col_weight > 1e-9) ? duals[v] / col_weight : 1.0;
        } else {
            score_contribution = 0.2 * (max_dual > 1e-9 ? duals[v] / max_dual : 0.0);
        }

        ostringstream out;
        out << cg_iter << "," << v << ","
            << json_number(nd_mm[v])  << ","
            << json_number(normalized_degrees[v]) << ","
            << json_number(nd_d[v])   << ","
            << json_number(nd_nb[v])  << ","
            << json_number(nd_cd[v])  << ","
            << json_number(nd_cdu[v]) << ","
            << json_number(occurrence_rate) << ","
            << json_number(score_contribution);
        logger.write_vertex(out.str());
    }
}

void log_pricing_order_comparison(
    JsonlLogger& logger,
    int cg_iter,
    double rmp_value,
    double decision_threshold,
    const PricingOrderComparison& dual_comp,
    const PricingOrderComparison& ai_comp,
    bool mwss_ran,
    const StableSetPricingResult& mwss_result,
    double mwss_time_seconds
) {
    bool mwss_found = mwss_ran && mwss_result.reduced_cost < -1e-6;

    auto write_row = [&](const PricingOrderComparison& comp) {
        bool found = comp.pricing_result.reduced_cost < -1e-6;
        ostringstream out;
        out << cg_iter << ","
            << comp.method << ","
            << json_number(rmp_value) << ","
            << json_number(decision_threshold) << ","
            << (found ? "1" : "0") << ","
            << json_number(comp.pricing_result.reduced_cost) << ","
            << json_number(comp.cp_time_seconds) << ","
            << comp.cp_result.nodes << ","
            << comp.cp_result.failures << ","
            << (comp.cp_result.stopped ? "1" : "0") << ","
            << (mwss_ran   ? "1" : "0") << ","
            << (mwss_found ? "1" : "0") << ","
            << json_number(mwss_ran ? mwss_result.reduced_cost : 0.0) << ","
            << (mwss_result.proven_optimal ? "1" : "0") << ","
            << json_number(mwss_time_seconds);
        logger.write_pricing_comparison(out.str());
    };

    write_row(dual_comp);
    write_row(ai_comp);
}
