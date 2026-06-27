#include "master_main/driver.h"

#include "gurobi_c++.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace {

vector<fs::path> collect_col_instances(const fs::path& instance_dir) {
    vector<fs::path> instances;
    if (!fs::exists(instance_dir) || !fs::is_directory(instance_dir)) {
        return instances;
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(instance_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".col") {
            instances.push_back(entry.path());
        }
    }

    sort(instances.begin(), instances.end());
    return instances;
}
 
int run_all_instances(const fs::path& instance_dir) {
    vector<fs::path> instances = collect_col_instances(instance_dir);
    if (instances.empty()) {
        cerr << "No .col instances found in: " << instance_dir << endl;
        return 1;
    }

    cout << "Running " << instances.size()
         << " instances from " << instance_dir << endl;

    int failed_runs = 0;
    int last_error_code = 0;
    for (size_t i = 0; i < instances.size(); ++i) {
        cout << "\n============================================" << endl;
        cout << " Batch instance " << (i + 1) << " / " << instances.size()
             << ": " << instances[i].filename().string() << endl;
        cout << "============================================" << endl;

        int code = run_column_generation(instances[i].string());
        if (code != 0) {
            ++failed_runs;
            last_error_code = code;
            cerr << "Instance failed with exit code " << code
                 << ": " << instances[i] << endl;
        }
    }

    cout << "\nBatch finished: "
         << (instances.size() - static_cast<size_t>(failed_runs))
         << " succeeded, " << failed_runs << " failed." << endl;

    return failed_runs == 0 ? 0 : last_error_code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        string instance_path = load_master_configured_instance_path(
            argc > 1 ? argv[1] : ""
        );
        if (fs::exists(instance_path)
            && fs::is_directory(instance_path)) {
            return run_all_instances(instance_path);
        }
        return run_column_generation(instance_path);
    } catch (const GRBException& e) {
        cerr << "Gurobi Error "
             << e.getErrorCode() << ": "
             << e.getMessage() << endl;
        return 3;
    } catch (const exception& e) {
        cerr << "Fatal: " << e.what() << endl;
        return 1;
    }
}
