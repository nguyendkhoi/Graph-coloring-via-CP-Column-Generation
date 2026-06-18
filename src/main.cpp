#include "graph/graph.h"
#include "preprocessing/clique_processing.h"
#include "coloring/cp.h"
#include "coloring/heuristic.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <fstream>

using namespace std;
namespace fs = std::filesystem;

struct BenchRow {
    string instance;
    int n = 0;
    int m = 0;
    int clique_lb = -1;
    // DSATUR
    int dsatur_k = -1;
    double dsatur_time = 0.0;
    // CP-UB
    int cp_k = -1;
    double cp_time = 0.0;
    unsigned long long cp_nodes = 0;
    bool cp_stopped = false;
};

static void print_header() {
    cout << left
         << setw(22) << "Instance"
         << setw(8)  << "|V|"
         << setw(8)  << "|E|"
         << setw(8)  << "LB"
         << setw(10) << "DSATUR_k"
         << setw(12) << "DSATUR_t(s)"
         << setw(8)  << "CP_k"
         << setw(12) << "CP_t(s)"
         << setw(12) << "B&Bnodes"
         << setw(10) << "CP_stop"
         << setw(8)  << "Best"
         << endl;
    cout << string(22+8+8+8+10+12+8+12+12+10+8, '-') << endl;
}

static void print_row(const BenchRow& r) {
    string best;
    if (r.dsatur_k > 0 && r.cp_k > 0)       best = (r.cp_k < r.dsatur_k ? "CP" :
                                                   r.cp_k > r.dsatur_k ? "DSATUR" : "TIE");
    else if (r.cp_k > 0)                    best = "CP";
    else if (r.dsatur_k > 0)                best = "DSATUR";
    else                                    best = "-";

    cout << left
         << setw(22) << r.instance
         << setw(8)  << r.n
         << setw(8)  << r.m
         << setw(8)  << r.clique_lb
         << setw(10) << (r.dsatur_k > 0 ? to_string(r.dsatur_k) : "-")
         << setw(12) << fixed << setprecision(3) << r.dsatur_time
         << setw(8)  << (r.cp_k > 0 ? to_string(r.cp_k) : "-")
         << setw(12) << fixed << setprecision(3) << r.cp_time
         << setw(12) << r.cp_nodes
         << setw(10) << (r.cp_stopped ? "yes" : "no")
         << setw(8)  << best
         << endl;
}

static void write_csv(const string& path, const vector<BenchRow>& rows) {
    ofstream f(path);
    if (!f) return;
    f << "instance,n,m,clique_lb,dsatur_k,dsatur_time,"
         "cp_k,cp_time,cp_bnb_nodes,cp_stopped\n";
    for (const auto& r : rows) {
        f << r.instance << ","
          << r.n << "," << r.m << "," << r.clique_lb << ","
          << r.dsatur_k << "," << r.dsatur_time << ","
          << r.cp_k << "," << r.cp_time << "," << r.cp_nodes << ","
          << (r.cp_stopped ? 1 : 0) << "\n";
    }
}

static CPSolveResult run_cp_upper_bound(
    const Graph& G,
    const vector<vector<int>>& clique_info,
    int dsatur_ub,
    double time_limit,
    double& cp_time
) {
    int lb = clique_info.empty() ? 1 : (int)clique_info[0].size();

    CPSolveResult best;
    best.feasible = false;
    best.num_colors = dsatur_ub;
    best.stopped = false;
    best.color = {};
    best.nodes = 0;
    best.failures = 0;
    unsigned long long total_nodes = 0;
    unsigned long long total_failures = 0;

    auto t_start = chrono::high_resolution_clock::now();
    int k = dsatur_ub - 1;

    while (k >= lb) {
        double elapsed = chrono::duration<double>(
            chrono::high_resolution_clock::now() - t_start).count();
        double remain = time_limit - elapsed;
        if (remain <= 0.0) {
            best.stopped = true;
            break;
        }

        double budget = min(remain, max(5.0, remain / 3.0));

        CPSolveResult res = solve_coloring_cp_with_reduction(
            G,
            clique_info,
            k,
            budget
        );
        total_nodes += res.nodes;
        total_failures += res.failures;

        if (res.feasible) {
            best = res;
            best.nodes = total_nodes;
            best.failures = total_failures;
            k = res.num_colors - 1;
        } else if (res.stopped) {
            best.stopped = true;
            best.nodes = total_nodes;
            best.failures = total_failures;
            break;
        } else {
            best.nodes = total_nodes;
            best.failures = total_failures;
            break;
        }
    }

    auto t_end = chrono::high_resolution_clock::now();
    cp_time = chrono::duration<double>(t_end - t_start).count();

    return best;
}

static BenchRow run_one(const string& path, double time_limit) {
    BenchRow r;
    r.instance = fs::path(path).filename().string();

    Graph G = parser_dimacs_col(path, true);
    r.n = G.num_vertices();

    long long deg_sum = 0;
    for (int v = 0; v < r.n; ++v) deg_sum += (long long)G.neighbors(v).size();
    r.m = static_cast<int>(deg_sum / 2);

    auto cp_t0 = chrono::high_resolution_clock::now();
    vector<int> clique = find_maximal_clique_from_complement(G.complement());
    vector<vector<int>> clique_info;
    if (!clique.empty()) {
        clique_info.push_back(clique);
    }
    auto cp_t1 = chrono::high_resolution_clock::now();
    double clique_time = chrono::duration<double>(cp_t1 - cp_t0).count();
    r.cp_time = clique_time;
    r.clique_lb = clique_info.empty() ? 0 : (int)clique_info[0].size();

    // ---- DSATUR ----
    {
        auto t0 = chrono::high_resolution_clock::now();
        auto dsatur_result = DSATUR_coloring(G);
        auto t1 = chrono::high_resolution_clock::now();
        r.dsatur_time = chrono::duration<double>(t1 - t0).count();
        r.dsatur_k = dsatur_result.second;
    }

    // ---- CP-UB ----
    if (r.dsatur_k > 0) {
        if (r.clique_lb == r.dsatur_k) {
            r.cp_k = r.dsatur_k;
            r.cp_stopped = false;
        } else {
            double cp_solve_time = 0.0;
            CPSolveResult res = run_cp_upper_bound(
                G,
                clique_info,
                r.dsatur_k,
                time_limit,
                cp_solve_time
            );
            r.cp_time += cp_solve_time;
            r.cp_stopped = res.stopped;
            r.cp_nodes = res.nodes;
            if (res.feasible) {
                r.cp_k = res.num_colors;
            }
        }
    } else {
        r.cp_k = -1;
    }

    return r;
}    

int main(int argc, char** argv) {
    try {
        string tests_dir = "tests";
        double time_limit = 300.0;
        string csv_out = "benchmark.csv";

        if (argc >= 2) tests_dir  = argv[1];
        if (argc >= 3) time_limit = stod(argv[2]);
        if (argc >= 4) csv_out    = argv[3];

        if (!fs::exists(tests_dir) || !fs::is_directory(tests_dir)) {
            cerr << "Tests directory not found: " << tests_dir << endl;
            return 1;
        }

        vector<string> files;
        for (auto& entry : fs::directory_iterator(tests_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".col") {
                files.push_back(entry.path().string());
            }
        }
        sort(files.begin(), files.end());

        if (files.empty()) {
            cerr << "No .col files found in " << tests_dir << endl;
            return 1;
        }

        cout << "============================================" << endl;
        cout << " Auto Benchmark : DSATUR vs CP-UB" << endl;
        cout << " Tests dir   : " << tests_dir << endl;
        cout << " Time limit  : " << time_limit << " s (per instance, CP only)" << endl;
        cout << " Instances   : " << files.size() << endl;
        cout << "============================================" << endl;

        print_header();

        vector<BenchRow> rows;
        rows.reserve(files.size());

        int cp_wins = 0, ds_wins = 0, ties = 0;

        for (const auto& f : files) {
            BenchRow r;
            try {
                r = run_one(f, time_limit);
            } catch (const exception& e) {
                r.instance = fs::path(f).filename().string();
                cerr << "[ERROR] " << r.instance << " : " << e.what() << endl;
            }
            print_row(r);
            rows.push_back(r);

            if (r.cp_k > 0 && r.dsatur_k > 0) {
                if (r.cp_k < r.dsatur_k) ++cp_wins;
                else if (r.cp_k > r.dsatur_k) ++ds_wins;
                else ++ties;
            }
        }

        cout << string(22+8+8+8+10+12+8+12+12+10+8, '-') << endl;
        cout << "Summary: CP better = " << cp_wins
             << " | DSATUR better = " << ds_wins
             << " | Tie = " << ties << endl;

        write_csv(csv_out, rows);
        cout << "CSV exported -> " << csv_out << endl;

    } catch (const exception& e) {
        cerr << "Fatal: " << e.what() << endl;
        return 1;
    }
    return 0;
}
