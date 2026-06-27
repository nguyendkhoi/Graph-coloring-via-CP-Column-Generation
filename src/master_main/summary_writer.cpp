#include "summary_writer.h"
#include "util.h"

#include "../config/config.h"

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace std;

namespace {

vector<string> summary_columns() {
    return {
        "run_id", "run_output_dir", "run_log_path", "instance_path", "instance",
        "n", "m", "num_trials", "seed", "threads", "time_limit_seconds",
        "run_time_seconds", "iterations", "rmp_status", "lp_objective",
        "proven_lb", "incumbent_ub", "column_count", "active_lambdas",
        "total_lambdas", "closed_gap", "converged_by_pricing",
        "reached_max_iter", "reached_time_limit", "exit_code"
    };
}

map<string, string> summary_values(const MasterRunSummary& s) {
    return {
        {"run_id",              s.run_id},
        {"run_output_dir",      s.run_output_dir},
        {"run_log_path",        s.run_log_path},
        {"instance_path",       s.instance_path},
        {"instance",            s.instance},
        {"n",                   to_string(s.n)},
        {"m",                   to_string(s.m)},
        {"num_trials",          to_string(s.num_trials)},
        {"seed",                to_string(s.seed)},
        {"threads",             to_string(s.threads)},
        {"time_limit_seconds",  format_double(s.time_limit_seconds)},
        {"run_time_seconds",    format_double(s.run_time_seconds)},
        {"iterations",          to_string(s.iterations)},
        {"rmp_status",          s.rmp_status},
        {"lp_objective",        format_double(s.lp_objective)},
        {"proven_lb",           to_string(s.proven_lb)},
        {"incumbent_ub",        to_string(s.incumbent_ub)},
        {"column_count",        to_string(s.column_count)},
        {"active_lambdas",      to_string(s.active_lambdas)},
        {"total_lambdas",       to_string(s.total_lambdas)},
        {"closed_gap",          s.closed_gap ? "1" : "0"},
        {"converged_by_pricing",s.converged_by_pricing ? "1" : "0"},
        {"reached_max_iter",    s.reached_max_iter ? "1" : "0"},
        {"reached_time_limit",  s.reached_time_limit ? "1" : "0"},
        {"exit_code",           to_string(s.exit_code)}
    };
}

bool is_string_col(const string& col) {
    return col == "run_id" || col == "run_output_dir" || col == "run_log_path"
        || col == "instance_path" || col == "instance" || col == "rmp_status";
}

bool is_bool_col(const string& col) {
    return col == "closed_gap" || col == "converged_by_pricing"
        || col == "reached_max_iter" || col == "reached_time_limit";
}

string json_key(const string& col) {
    return col == "run_log_path" ? "records_path" : col;
}

string json_val(const string& col, const string& val) {
    if (is_string_col(col)) return json_string(val);
    if (is_bool_col(col))   return val == "1" ? "true" : "false";
    return val.empty() ? "null" : val;
}

void write_csv(const MasterRunSummary& s) {
    if (s.summary_csv_path.empty()) return;
    ofstream out(s.summary_csv_path, ios::out | ios::trunc);
    if (!out) return;

    auto cols = summary_columns();
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out << ",";
        out << csv_escape(cols[i]);
    }
    out << "\n";

    auto vals = summary_values(s);
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out << ",";
        auto it = vals.find(cols[i]);
        out << csv_escape(it == vals.end() ? "" : it->second);
    }
    out << "\n";
}

void write_json(const MasterRunSummary& s) {
    if (s.summary_json_path.empty()) return;
    ofstream out(s.summary_json_path, ios::out | ios::trunc);
    if (!out) return;

    out << "{\n"
        << "  \"record_type\": \"run_summary\",\n"
        << "  \"schema_version\": " << config.get<int>("schema_version", 1);

    auto cols = summary_columns();
    auto vals = summary_values(s);
    for (const string& col : cols) {
        auto it = vals.find(col);
        string val = it == vals.end() ? "" : it->second;
        out << ",\n  " << json_string(json_key(col)) << ": " << json_val(col, val);
    }
    out << "\n}\n";
}

} // namespace

void write_run_summary(const MasterRunSummary& summary) {
    write_csv(summary);
    write_json(summary);
    if (!summary.run_output_dir.empty())
        cout << "Run output -> " << summary.run_output_dir << "\n";
}
