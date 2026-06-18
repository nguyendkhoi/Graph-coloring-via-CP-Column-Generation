#include "config.h"

#include "util.h"

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

using namespace std;
namespace fs = std::filesystem;

namespace {

void apply_run_config_file(
    MasterRunConfig& config,
    const fs::path& path
) {
    map<string, string> values = read_config_object_file(path);
    if (values.empty()) {
        return;
    }

    fs::path root = config_root_from_file(path);

    if (values.count("instance")) {
        config.instance_path = resolve_instance_path(root, values["instance"]);
    }
    if (values.count("num_trials")) {
        config.num_trials = stoi(values["num_trials"]);
    }
    if (values.count("initial_columns")) {
        config.initial_columns_target = stoi(values["initial_columns"]);
    }
    if (values.count("seed")) {
        config.seed = static_cast<size_t>(stoull(values["seed"]));
    }
    if (values.count("max_iter")) {
        config.max_iter = stoi(values["max_iter"]);
    }
    if (values.count("threads")) {
        config.threads = stoi(values["threads"]);
    }
    if (values.count("time_limit_seconds")) {
        config.time_limit_seconds = stod(values["time_limit_seconds"]);
    }
    if (values.count("decision_pricing_limit")) {
        config.decision_pricing_limit_seconds =
            stod(values["decision_pricing_limit"]);
    }
    if (values.count("exact_pricing_limit")) {
        config.mwss_time_limit_seconds = stod(values["exact_pricing_limit"]);
    }
    if (values.count("augmented_time_limit_seconds")) {
        config.augmented_time_limit_seconds =
            stod(values["augmented_time_limit_seconds"]);
    }
    if (values.count("vertex_ordering")) {
        config.vertex_ordering = values["vertex_ordering"];
    }
    if (values.count("enable_ml")) {
        config.enable_ml = parse_bool(values["enable_ml"]);
    }
    if (values.count("log_vertex_features")) {
        config.log_vertex_features = parse_bool(values["log_vertex_features"]);
    }
    if (values.count("log_dir")) {
        config.log_dir = resolve_config_path(root, values["log_dir"]);
    }
    if (values.count("schema_version")) {
        config.schema_version = stoi(values["schema_version"]);
    }
}

} // namespace

MasterRunConfig parse_master_args() {
    MasterRunConfig config;
    fs::path config_path = fs::path("master_cp") / "solver_config.json";
    if (!fs::exists(config_path)) {
        throw runtime_error("Missing config file: " + config_path.string());
    }
    apply_run_config_file(config, config_path);

    return config;
}
