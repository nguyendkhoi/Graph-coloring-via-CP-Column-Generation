#include "graph/graph.h"
#include "preprocessing/clique_processing.h"
#include "cp_coloring/cp.h"
#include "heuristic/heuristic.h"

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

// ====== Helpers ======
static bool is_valid_coloring(const vector<int>& colors, const Graph& G) {
    for (int v = 0; v < static_cast<int>(colors.size()); ++v) {
        for (int u : G.neighbors(v)) {
            if (colors[u] == colors[v]) return false;
        }
    }
    return true;
}

static int count_colors(const vector<int>& colors) {
    int mx = -1;
    for (int c : colors) mx = max(mx, c);
    return mx + 1; // colors thường đánh từ 0
}

struct BenchRow {
    string instance;
    int n = 0;
    int m = 0;
    int clique_lb = -1;
    // DSATUR
    int dsatur_k = -1;
    double dsatur_time = 0.0;
    bool dsatur_valid = false;
    // CP-UB
    int cp_k = -1;
    double cp_time = 0.0;
    bool cp_stopped = false;
    bool cp_valid = false;
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
         << setw(10) << "CP_stop"
         << setw(8)  << "Best"
         << endl;
    cout << string(22+8+8+8+10+12+8+12+10+8, '-') << endl;
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
         << setw(10) << (r.cp_stopped ? "yes" : "no")
         << setw(8)  << best
         << endl;
}

static void write_csv(const string& path, const vector<BenchRow>& rows) {
    ofstream f(path);
    if (!f) return;
    f << "instance,n,m,clique_lb,dsatur_k,dsatur_time,dsatur_valid,"
         "cp_k,cp_time,cp_stopped,cp_valid\n";
    for (const auto& r : rows) {
        f << r.instance << ","
          << r.n << "," << r.m << "," << r.clique_lb << ","
          << r.dsatur_k << "," << r.dsatur_time << "," << (r.dsatur_valid ? 1 : 0) << ","
          << r.cp_k << "," << r.cp_time << "," << (r.cp_stopped ? 1 : 0) << ","
          << (r.cp_valid ? 1 : 0) << "\n";
    }
}

// ====== Chạy 1 instance ======
static BenchRow run_one(const string& path, double time_limit) {
    BenchRow r;
    r.instance = fs::path(path).filename().string();

    Graph G = parser_dimacs_col(path, true);
    r.n = G.num_vertices();

    // đếm cạnh (mỗi cạnh xuất hiện 2 lần trong adj)
    long long deg_sum = 0;
    for (int v = 0; v < r.n; ++v) deg_sum += (long long)G.neighbors(v).size();
    r.m = static_cast<int>(deg_sum / 2);

    // clique LB (dùng đúng API hiện có của bạn)
    vector<vector<int>> clique_info = generate_clique(G, 20);
    r.clique_lb = clique_info.empty() ? 0 : (int)clique_info[0].size();

    // ---- DSATUR ----
    {
        auto t0 = chrono::high_resolution_clock::now();
        auto [colors, kused] = DSATUR_coloring(G);
        auto t1 = chrono::high_resolution_clock::now();
        r.dsatur_time = chrono::duration<double>(t1 - t0).count();
        r.dsatur_k = kused;
        r.dsatur_valid = is_valid_coloring(colors, G);
        if (!r.dsatur_valid) r.dsatur_k = -1;
    }

    // ---- CP-UB ----
    if (r.dsatur_k > 0) {
        auto t0 = chrono::high_resolution_clock::now();
        CPSolveResult res = cp_upper_bound(G, clique_info, r.dsatur_k, time_limit);
        auto t1 = chrono::high_resolution_clock::now();
        r.cp_time = chrono::duration<double>(t1 - t0).count();
        r.cp_stopped = res.stopped;
        if (res.feasible) {
            r.cp_k = res.num_colors;
            r.cp_valid = is_valid_coloring(res.color, G);
            if (!r.cp_valid) r.cp_k = -1;
        }
    } else {
        r.cp_k = -1;
        r.cp_time = 0.0;
    }

    return r;
}    

// ====== Auto-test toàn bộ thư mục ======
int main(int argc, char** argv) {
    try {
        string tests_dir = "tests";
        double time_limit = 100.0;
        string csv_out = "benchmark.csv";

        if (argc >= 2) tests_dir  = argv[1];
        if (argc >= 3) time_limit = stod(argv[2]);
        if (argc >= 4) csv_out    = argv[3];

        if (!fs::exists(tests_dir) || !fs::is_directory(tests_dir)) {
            cerr << "Tests directory not found: " << tests_dir << endl;
            return 1;
        }

        // Lấy danh sách *.col
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

        cout << string(22+8+8+8+10+12+8+12+10+8, '-') << endl;
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