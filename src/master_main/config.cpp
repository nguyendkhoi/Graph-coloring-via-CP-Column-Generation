#include "config.h"

#include "../config/config.h"
#include "util.h"

#include <filesystem>
#include <stdexcept>

using namespace std;
namespace fs = std::filesystem;

namespace {

void apply_run_config(
    MasterRunConfig& config,
    const Config& settings,
    const fs::path& path
) {
    fs::path root = config_root_from_file(path);

    if (settings.contains("instance")) {
        config.instance_path = resolve_instance_path(
            root,
            settings.get<string>("instance", config.instance_path)
        );
    }
    if (settings.contains("num_trials")) {
        config.num_trials = settings.get<int>("num_trials", config.num_trials);
    }
    if (settings.contains("initial_columns")) {
        config.initial_columns_target =
            settings.get<int>("initial_columns", config.initial_columns_target);
    }
    if (settings.contains("seed")) {
        config.seed = settings.get<size_t>("seed", config.seed);
    }
    if (settings.contains("max_iter")) {
        config.max_iter = settings.get<int>("max_iter", config.max_iter);
    }
    if (settings.contains("threads")) {
        config.threads = settings.get<int>("threads", config.threads);
    }
    if (settings.contains("time_limit_seconds")) {
        config.time_limit_seconds =
            settings.get<double>("time_limit_seconds", config.time_limit_seconds);
    }
    if (settings.contains("decision_pricing_limit")) {
        config.decision_pricing_limit_seconds =
            settings.get<double>(
                "decision_pricing_limit",
                config.decision_pricing_limit_seconds
            );
    }
    if (settings.contains("exact_pricing_limit")) {
        config.mwss_time_limit_seconds =
            settings.get<double>(
                "exact_pricing_limit",
                config.mwss_time_limit_seconds
            );
    }
    if (settings.contains("augmented_time_limit_seconds")) {
        config.augmented_time_limit_seconds =
            settings.get<double>(
                "augmented_time_limit_seconds",
                config.augmented_time_limit_seconds
            );
    }
    if (settings.contains("vertex_ordering")) {
        config.vertex_ordering =
            settings.get<string>("vertex_ordering", config.vertex_ordering);
    }
    if (settings.contains("enable_ml")) {
        config.enable_ml = settings.get<bool>("enable_ml", config.enable_ml);
    }
    if (settings.contains("log_vertex_features")) {
        config.log_vertex_features =
            settings.get<bool>("log_vertex_features", config.log_vertex_features);
    }
    if (settings.contains("log_dir")) {
        config.log_dir = resolve_config_path(
            root,
            settings.get<string>("log_dir", config.log_dir)
        );
    }
    if (settings.contains("schema_version")) {
        config.schema_version =
            settings.get<int>("schema_version", config.schema_version);
    }
}

} // namespace

MasterRunConfig parse_master_args() {
    MasterRunConfig config;
    fs::path config_path = fs::path("master_cp") / "solver_config.json";
    if (!fs::exists(config_path)) {
        throw runtime_error("Missing config file: " + config_path.string());
    }
    Config settings = Config::from_json_file(config_path);
    apply_run_config(config, settings, config_path);

    return config;
}
